module;

module epix.core;

import std;

namespace core {
void CommandQueue::append(CommandQueue& other) {
    assure_size(size_ + other.size_);
    std::size_t old_size = size_;
    size_ += other.size_;
    // move commands
    std::size_t offset = 0;
    for (const CommandMeta* meta : other.metas_) {
        meta->move(static_cast<std::byte*>(commands_) + old_size + offset,
                   static_cast<std::byte*>(other.commands_) + offset);
        offset += meta->size;
    }
    // destruct other commands
    offset = 0;
    for (const CommandMeta* meta : other.metas_) {
        meta->destructor(static_cast<std::byte*>(other.commands_) + offset);
        offset += meta->size;
    }

    // move metas
    metas_.insert_range(metas_.end(), std::move(other.metas_));

    // reset other
    other.size_ = 0;
    other.metas_.clear();
}

void CommandQueue::apply(World& world) {
    std::size_t offset = 0;
    for (const CommandMeta* meta : metas_) {
        meta->apply(static_cast<std::byte*>(commands_) + offset, world);
        meta->destructor(static_cast<std::byte*>(commands_) + offset);
        offset += meta->size;
    }
    size_ = 0;
    metas_.clear();
}

void CommandQueue::reallocate(std::size_t new_capacity) {
    void* new_commands = operator new(new_capacity);
    if (commands_) {
        std::size_t offset = 0;
        for (const CommandMeta* meta : metas_) {
            meta->move(static_cast<char*>(new_commands) + offset, static_cast<char*>(commands_) + offset);
            offset += meta->size;
        }
        // destruct old commands
        offset = 0;
        for (const CommandMeta* meta : metas_) {
            meta->destructor(static_cast<char*>(commands_) + offset);
            offset += meta->size;
        }
        operator delete(commands_);
    }
    commands_ = new_commands;
    capacity_ = new_capacity;
}

void CommandQueue::assure_size(std::size_t new_size) {
    if (new_size > capacity_) {
        std::size_t new_capacity = capacity_ == 0 ? 64 : capacity_;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }
        reallocate(new_capacity);
    }
}
}  // namespace core
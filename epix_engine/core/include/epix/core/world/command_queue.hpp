#pragma once

#include <vector>

#include "../fwd.hpp"
#include "command.hpp"

namespace epix::core {
struct CommandQueue {
   public:
    CommandQueue()                    = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue(CommandQueue&& other) {
        commands_ = other.commands_;
        capacity_ = other.capacity_;
        size_     = other.size_;
        metas_    = std::move(other.metas_);

        other.commands_ = nullptr;
        other.capacity_ = 0;
        other.size_     = 0;
    }
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue& operator=(CommandQueue&& other) {
        if (this != &other) {
            this->~CommandQueue();
            new (this) CommandQueue(std::move(other));
        }
        return *this;
    }
    ~CommandQueue() {
        if (commands_) {
            size_t offset = 0;
            for (const CommandMeta* meta : metas_) {
                meta->destructor(static_cast<char*>(commands_) + offset);
                offset += meta->size;
            }
            operator delete(commands_);
        }
    }

    template <typename T>
    void push(T&& command)
        requires(valid_command<Command<std::decay_t<T>>>)
    {
        static CommandMeta meta = {
            .size       = sizeof(std::decay_t<T>),
            .destructor = [](void* ptr) { reinterpret_cast<std::decay_t<T>*>(ptr)->~decay_t<T>(); },
            .move       = [](void* dest,
                       void* src) { new (dest) std::decay_t<T>(std::move(*reinterpret_cast<std::decay_t<T>*>(src))); },
            .apply =
                [](void* ptr, World& world) {
                    Command<std::decay_t<T>>::apply(*reinterpret_cast<std::decay_t<T>*>(ptr), world);
                },
        };
        size_t old_size = size_;
        size_ += meta.size;
        assure_size(size_);
        new (static_cast<std::byte*>(commands_) + old_size) std::decay_t<T>(std::forward<T>(command));
        metas_.push_back(&meta);
    }
    void append(CommandQueue& other) {
        assure_size(size_ + other.size_);
        size_t old_size = size_;
        size_ += other.size_;
        // move commands
        size_t offset = 0;
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
    void apply(World& world) {
        size_t offset = 0;
        for (const CommandMeta* meta : metas_) {
            meta->apply(static_cast<std::byte*>(commands_) + offset, world);
            meta->destructor(static_cast<std::byte*>(commands_) + offset);
            offset += meta->size;
        }
        size_ = 0;
        metas_.clear();
    }

   private:
    struct CommandMeta {
        size_t size;
        void (*destructor)(void*);
        void (*move)(void*, void*);
        void (*apply)(void*, World&);
    };

    void reallocate(size_t new_capacity) {
        void* new_commands = operator new(new_capacity);
        if (commands_) {
            size_t offset = 0;
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
    void assure_size(size_t new_size) {
        if (new_size > capacity_) {
            size_t new_capacity = capacity_ == 0 ? 64 : capacity_;
            while (new_capacity < new_size) {
                new_capacity *= 2;
            }
            reallocate(new_capacity);
        }
    }

    void* commands_  = nullptr;
    size_t capacity_ = 0;
    size_t size_     = 0;
    std::vector<const CommandMeta*> metas_;
};
}  // namespace epix::core
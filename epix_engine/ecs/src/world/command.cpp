#include <spdlog/spdlog.h>

#include <algorithm>
#include <epix/ecs/world/commands.hpp>
#include <epix/ecs/world.hpp>

namespace epix::ecs::internal {
void CommandQueue::swap_pending(CommandQueue& other) noexcept {
    using std::swap;
    swap(commands_, other.commands_);
    swap(capacity_, other.capacity_);
    swap(storage_alignment_, other.storage_alignment_);
    swap(size_, other.size_);
    swap(front_offset_, other.front_offset_);
    metas_.swap(other.metas_);
}

void CommandQueue::append(CommandQueue& other) {
    if (this == &other) throw std::invalid_argument("cannot append a command queue to itself");
    if (other.metas_.empty()) return;
    spdlog::trace("[world] Appending {} commands from another queue.", other.metas_.size());
    metas_.reserve(metas_.size() + other.metas_.size());
    std::size_t required_size = size_;
    std::size_t required_alignment = storage_alignment_;
    for (const auto* meta : other.metas_) {
        required_size = checked_end(aligned_offset(required_size, meta->alignment), meta->size);
        required_alignment = std::max(required_alignment, meta->alignment);
    }
    assure_size(required_size, required_alignment);
    std::size_t old_size = size_;
    // move commands
    std::size_t offset = other.front_offset_;
    std::size_t destination_offset = old_size;
    std::size_t moved = 0;
    try {
        for (const CommandMeta* meta : other.metas_) {
            offset = aligned_offset(offset, meta->alignment);
            destination_offset = aligned_offset(destination_offset, meta->alignment);
            meta->move(static_cast<std::byte*>(commands_) + destination_offset,
                       static_cast<std::byte*>(other.commands_) + offset);
            offset += meta->size;
            destination_offset += meta->size;
            ++moved;
        }
    } catch (...) {
        std::size_t cleanup = old_size;
        for (std::size_t index = 0; index < moved; ++index) {
            const auto* meta = other.metas_[index];
            cleanup = aligned_offset(cleanup, meta->alignment);
            meta->destructor(static_cast<std::byte*>(commands_) + cleanup);
            cleanup += meta->size;
        }
        throw;
    }
    // destruct other commands
    offset = other.front_offset_;
    for (const CommandMeta* meta : other.metas_) {
        offset = aligned_offset(offset, meta->alignment);
        meta->destructor(static_cast<std::byte*>(other.commands_) + offset);
        offset += meta->size;
    }

    // move metas
    size_ = destination_offset;
    metas_.insert_range(metas_.end(), std::move(other.metas_));

    // reset other
    other.size_ = 0;
    other.front_offset_ = 0;
    other.metas_.clear();
}

void CommandQueue::apply(World& world) {
    // Bevy applies the world's commands before a detached system queue. The
    // world's own queue must not recursively flush itself before detaching.
    if (this != &world.command_queue()) world.flush_commands();
    if (metas_.empty()) return;
    spdlog::trace("[world] Applying {} commands to world.", metas_.size());

    // A command can append more world commands, flush them, or spawn (which
    // flushes internally). Keep this batch's payloads stable and invisible to
    // reentrant application, like Bevy's reserved stop cursor.
    ++apply_depth_;
    struct DepthGuard {
        std::size_t& depth;
        ~DepthGuard() { --depth; }
    } depth_guard{apply_depth_};
    CommandQueue batch;
    swap_pending(batch);
    std::size_t index = 0;
    std::size_t offset = batch.front_offset_;
    try {
        while (index < batch.metas_.size()) {
            const CommandMeta* meta = batch.metas_[index];
            offset = aligned_offset(offset, meta->alignment);
            void* command = static_cast<std::byte*>(batch.commands_) + offset;
            auto consume = [&] {
                ++index;
                offset += meta->size;
                meta->destructor(command);
            };
            try {
                meta->apply(command, world);
            } catch (...) {
                consume();
                throw;
            }
            consume();
            // Newly queued world commands run before this batch's old tail.
            world.flush();
        }
    } catch (...) {
        // Bevy keeps panic recovery separate from runnable commands. An inner
        // failure can be caught by a command; its tail must remain in recovery
        // until a root unwind, not be flushed ahead of that command's new work.
        batch.metas_.erase(batch.metas_.begin(), batch.metas_.begin() + index);
        batch.front_offset_ = offset;
        batch.append(*this);
        recovery_.emplace_back(std::move(batch));
        if (apply_depth_ == 1) {
            for (auto& pending : recovery_) {
                if (pending.metas_.empty()) continue;
                if (metas_.empty()) swap_pending(pending);
                else append(pending);
            }
            recovery_.clear();
        }
        throw;
    }
    batch.metas_.clear();
    batch.size_ = 0;
    batch.front_offset_ = 0;
    // Keep reusable storage for the usual non-nested system queue instead of
    // allocating a new packed buffer on every frame.
    if (metas_.empty() && batch.capacity_ > capacity_) swap_pending(batch);
}

void CommandQueue::reallocate(std::size_t new_capacity, std::size_t new_alignment) {
    void* new_commands = operator new(new_capacity, std::align_val_t{new_alignment});
    if (commands_) {
        std::size_t offset = front_offset_;
        std::size_t destination_offset = 0;
        std::size_t moved = 0;
        try {
            for (const CommandMeta* meta : metas_) {
                offset = aligned_offset(offset, meta->alignment);
                destination_offset = aligned_offset(destination_offset, meta->alignment);
                meta->move(static_cast<char*>(new_commands) + destination_offset,
                           static_cast<char*>(commands_) + offset);
                offset += meta->size;
                destination_offset += meta->size;
                ++moved;
            }
        } catch (...) {
            std::size_t cleanup = 0;
            for (std::size_t index = 0; index < moved; ++index) {
                const auto* meta = metas_[index];
                cleanup = aligned_offset(cleanup, meta->alignment);
                meta->destructor(static_cast<char*>(new_commands) + cleanup);
                cleanup += meta->size;
            }
            operator delete(new_commands, std::align_val_t{new_alignment});
            throw;
        }
        // destruct old commands
        offset = front_offset_;
        for (const CommandMeta* meta : metas_) {
            offset = aligned_offset(offset, meta->alignment);
            meta->destructor(static_cast<char*>(commands_) + offset);
            offset += meta->size;
        }
        operator delete(commands_, std::align_val_t{storage_alignment_});
        size_ = destination_offset;
    }
    commands_ = new_commands;
    capacity_ = new_capacity;
    storage_alignment_ = new_alignment;
    front_offset_ = 0;
}

void CommandQueue::assure_size(std::size_t new_size, std::size_t required_alignment) {
    if (new_size > capacity_ || required_alignment > storage_alignment_) {
        std::size_t new_capacity = capacity_ == 0 ? 64 : capacity_;
        while (new_capacity < new_size) {
            if (new_capacity > std::numeric_limits<std::size_t>::max() / 2) {
                new_capacity = new_size;
                break;
            }
            new_capacity *= 2;
        }
        reallocate(new_capacity, std::max(storage_alignment_, required_alignment));
    }
}
}  // namespace epix::ecs::internal

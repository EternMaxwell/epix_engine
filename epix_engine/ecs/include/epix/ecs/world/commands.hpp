#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/world/detail/access.hpp>

namespace epix::ecs::internal {
template <typename T>
struct Command {
    static_assert("Not a valid command type");
};
template <typename T>
concept is_command = requires(T t) {
    { t.apply(std::declval<World&>()) } -> std::same_as<void>;
};
template <typename T>
concept valid_command = requires {
    typename T::Type;
    T::apply(std::declval<typename T::Type&>(), std::declval<World&>());
};
template <std::invocable<World&> F>
struct Command<F> {
    using Type = F;
    static void apply(F& f, World& world) noexcept(noexcept(std::invoke(f, world))) { std::invoke(f, world); }
};
template <is_command T>
struct Command<T> {
    using Type = T;
    static void apply(T& cmd, World& world) noexcept(noexcept(cmd.apply(world))) { cmd.apply(world); }
};

struct EPIX_API CommandQueue {
   public:
    CommandQueue()                    = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue(CommandQueue&& other) noexcept {
        commands_ = other.commands_;
        capacity_ = other.capacity_;
        storage_alignment_ = other.storage_alignment_;
        size_     = other.size_;
        front_offset_ = other.front_offset_;
        metas_    = std::move(other.metas_);
        apply_depth_ = other.apply_depth_;
        recovery_ = std::move(other.recovery_);

        other.commands_ = nullptr;
        other.capacity_ = 0;
        other.storage_alignment_ = alignof(std::max_align_t);
        other.size_     = 0;
        other.front_offset_ = 0;
        other.metas_.clear();
        other.apply_depth_ = 0;
        other.recovery_.clear();
    }
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue& operator=(CommandQueue&& other) noexcept {
        if (this != &other) {
            this->~CommandQueue();
            new (this) CommandQueue(std::move(other));
        }
        return *this;
    }
    ~CommandQueue() {
        if (commands_) {
            std::size_t offset = front_offset_;
            for (const CommandMeta* meta : metas_) {
                offset = aligned_offset(offset, meta->alignment);
                meta->destructor(static_cast<char*>(commands_) + offset);
                offset += meta->size;
            }
            operator delete(commands_, std::align_val_t{storage_alignment_});
        }
    }

    template <typename T>
    void push(T&& command)
        requires(valid_command<Command<std::decay_t<T>>>)
    {
        using type              = std::decay_t<T>;
        static CommandMeta meta = {
            .size       = sizeof(type),
            .alignment  = alignof(type),
            .destructor = [](void* ptr) { reinterpret_cast<type*>(ptr)->~type(); },
            .move       = [](void* dest, void* src) { new (dest) type(std::move(*reinterpret_cast<type*>(src))); },
            .apply      = [](void* ptr, World& world) { Command<type>::apply(*reinterpret_cast<type*>(ptr), world); },
        };
        assure_size(checked_end(aligned_offset(size_, meta.alignment), meta.size), meta.alignment);
        const std::size_t old_size = aligned_offset(size_, meta.alignment);
        void* destination = static_cast<std::byte*>(commands_) + old_size;
        new (destination) type(std::forward<T>(command));
        try {
            metas_.push_back(&meta);
        } catch (...) {
            meta.destructor(destination);
            throw;
        }
        size_ = checked_end(old_size, meta.size);
    }
    void append(CommandQueue& other);
    void apply(World& world);

   private:
    struct CommandMeta {
        std::size_t size;
        std::size_t alignment;
        void (*destructor)(void*);
        void (*move)(void*, void*);
        void (*apply)(void*, World&);
    };

    static std::size_t aligned_offset(std::size_t offset, std::size_t alignment) {
        const auto mask = alignment - 1;
        if (offset > std::numeric_limits<std::size_t>::max() - mask)
            throw std::length_error("command queue alignment overflows size_t");
        return (offset + mask) & ~mask;
    }
    static std::size_t checked_end(std::size_t offset, std::size_t size) {
        if (size > std::numeric_limits<std::size_t>::max() - offset)
            throw std::length_error("command queue size overflows size_t");
        return offset + size;
    }
    void reallocate(std::size_t new_capacity, std::size_t new_alignment);
    void assure_size(std::size_t new_size, std::size_t required_alignment);
    void swap_pending(CommandQueue& other) noexcept;

    void* commands_       = nullptr;
    std::size_t capacity_ = 0;
    std::size_t storage_alignment_ = alignof(std::max_align_t);
    std::size_t size_     = 0;
    // A failed apply may leave a consumed prefix before the live commands.
    std::size_t front_offset_ = 0;
    std::vector<const CommandMeta*> metas_;
    // Active application owns its own stop batch. Recovery stays separate until
    // a root unwind, including when a command catches an inner failure.
    std::size_t apply_depth_ = 0;
    std::vector<CommandQueue> recovery_;
};
}  // namespace epix::ecs::internal

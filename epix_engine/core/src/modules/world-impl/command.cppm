module;

#include <concepts>
#include <cstddef>
#include <functional>
#include <vector>

export module epix.core:world.commands;

import :world.decl;

namespace core {
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
    static void apply(F& f, World& world) { std::invoke(f, world); }
};
template <is_command T>
struct Command<T> {
    using Type = T;
    static void apply(T& cmd, World& world) { cmd.apply(world); }
};

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
            std::size_t offset = 0;
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
        using type              = std::decay_t<T>;
        static CommandMeta meta = {
            .size       = sizeof(type),
            .destructor = [](void* ptr) { reinterpret_cast<type*>(ptr)->~type(); },
            .move       = [](void* dest, void* src) { new (dest) type(std::move(*reinterpret_cast<type*>(src))); },
            .apply      = [](void* ptr, World& world) { Command<type>::apply(*reinterpret_cast<type*>(ptr), world); },
        };
        std::size_t old_size = size_;
        size_ += meta.size;
        assure_size(size_);
        new (static_cast<std::byte*>(commands_) + old_size) type(std::forward<T>(command));
        metas_.push_back(&meta);
    }
    void append(CommandQueue& other);
    void apply(World& world);

   private:
    struct CommandMeta {
        std::size_t size;
        void (*destructor)(void*);
        void (*move)(void*, void*);
        void (*apply)(void*, World&);
    };

    void reallocate(std::size_t new_capacity);
    void assure_size(std::size_t new_size);

    void* commands_       = nullptr;
    std::size_t capacity_ = 0;
    std::size_t size_     = 0;
    std::vector<const CommandMeta*> metas_;
};
}  // namespace core
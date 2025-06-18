#pragma once

#include <concepts>
#include <entt/container/dense_map.hpp>
#include <expected>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace epix::utils {
template <typename T, typename... Args>
concept AppliableCommand = requires(T t, Args... args) {
    { t.apply(std::declval<Args>()...) } -> std::same_as<void>;
};
struct CommandQueueError {
    enum class Type {
        TooManyCommandTypes,
    } type;
};
template <typename... Args>
struct CommandQueue {
   private:
    using index_t = uint16_t;

    struct CommandInfo {
        std::type_index type;
        size_t size;
        void (*apply)(void*, Args...);
        void (*destroy)(void*);
    };
    std::vector<CommandInfo> m_command_infos;
    std::vector<uint8_t> m_commands;
    entt::dense_map<std::type_index, index_t> m_command_map;
    std::mutex m_mutex;

    void clear_internal() {
        // Clear the command queue
        size_t ptr = 0;
        while (ptr < m_commands.size()) {
            index_t index =
                *reinterpret_cast<index_t*>(m_commands.data() + ptr);
            ptr += sizeof(index_t);
            auto& info = m_command_infos[index];
            info.destroy(reinterpret_cast<void*>(m_commands.data() + ptr));
            ptr += info.size;
        }
        m_commands.clear();
    }
    template <AppliableCommand<Args...> T, typename... CommandArgs>
    std::expected<void, CommandQueueError> enqueue_internal(
        CommandArgs&&... args
    ) {
        using type    = T;
        index_t index = 0;
        if (auto it = m_command_map.find(typeid(type));
            it == m_command_map.end()) {
            if (m_command_infos.size() >= std::numeric_limits<index_t>::max()) {
                return std::unexpected(CommandQueueError{
                    CommandQueueError::Type::TooManyCommandTypes
                });
            }
            m_command_map.emplace(
                typeid(type), static_cast<index_t>(m_command_infos.size())
            );
            index = m_command_infos.size();
            m_command_infos.emplace_back(
                typeid(type), sizeof(type),
                [](void* command, Args... args) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->apply(std::forward<Args>(args)...);
                },
                [](void* command) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->~type();
                }
            );
        } else {
            index = it->second;
        }
        m_commands.resize(m_commands.size() + sizeof(type) + sizeof(index_t));
        auto* pindex = reinterpret_cast<index_t*>(
            m_commands.data() + m_commands.size() - sizeof(type) -
            sizeof(index_t)
        );
        *pindex        = index;
        auto* pcommand = reinterpret_cast<type*>(
            m_commands.data() + m_commands.size() - sizeof(type)
        );
        new (pcommand) type(std::forward<CommandArgs>(args)...);
        return {};
    }

   public:
    CommandQueue()                               = default;
    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue(CommandQueue&&)                 = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue& operator=(CommandQueue&&)      = delete;
    ~CommandQueue() {}

    void clear() {
        std::lock_guard lock(m_mutex);
        clear_internal();
    }
    void clear_cache() {
        std::lock_guard lock(m_mutex);
        clear_internal();
        m_command_map.clear();
        m_command_infos.clear();
    }
    template <AppliableCommand<Args...> T, typename... CommandArgs>
    std::expected<void, CommandQueueError> enqueue(CommandArgs&&... args) {
        std::lock_guard lock(m_mutex);
        return enqueue_internal<T>(std::forward<CommandArgs>(args)...);
    }
    template <typename T>
        requires AppliableCommand<std::decay_t<T>, Args...>
    std::expected<void, CommandQueueError> enqueue(T&& command) {
        std::lock_guard lock(m_mutex);
        return enqueue_internal<std::decay_t<T>>(std::forward<T>(command));
    }

    void apply(Args... args) {
        std::lock_guard lock(m_mutex);
        size_t ptr = 0;
        while (ptr < m_commands.size()) {
            index_t index = *reinterpret_cast<index_t*>(&m_commands[ptr]);
            ptr += sizeof(index_t);
            auto& info = m_command_infos[index];
            info.apply(&m_commands[ptr], std::forward<Args>(args)...);
            info.destroy(&m_commands[ptr]);
            ptr += info.size;
        }
        m_commands.clear();
    }
};
}  // namespace epix::utils
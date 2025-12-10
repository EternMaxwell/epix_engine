/**
 * @file epix.core-component.cppm
 * @brief Component partition for component lifecycle and hooks
 */

export module epix.core:component;

import :fwd;
import :entities;
import :type_system;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

export namespace epix::core {
    struct HookContext {
        Entity entity;
        TypeId component_id;
    };
    
    struct ComponentHooks {
        std::function<void(World&, HookContext)> on_add;
        std::function<void(World&, HookContext)> on_insert;
        std::function<void(World&, HookContext)> on_replace;
        std::function<void(World&, HookContext)> on_remove;
        std::function<void(World&, HookContext)> on_despawn;

        template <typename T>
        ComponentHooks& update_from_component();
        
        template <std::invocable<World&, HookContext> F>
        bool try_on_add(F&& f);
        template <std::invocable<World&, HookContext> F>
        bool try_on_insert(F&& f);
        template <std::invocable<World&, HookContext> F>
        bool try_on_replace(F&& f);
        template <std::invocable<World&, HookContext> F>
        bool try_on_remove(F&& f);
        template <std::invocable<World&, HookContext> F>
        bool try_on_despawn(F&& f);
    };
    
    struct ComponentInfo {
       private:
        TypeId _id;
        ComponentHooks _hooks;

       public:
        ComponentInfo(TypeId id) : _id(id) {}
        
        TypeId id() const { return _id; }
        const ComponentHooks& hooks() const { return _hooks; }
        ComponentHooks& hooks_mut() { return _hooks; }
    };
    
    struct Components {
       private:
        std::unordered_map<TypeId, ComponentInfo, std::hash<uint64_t>> components;
        std::shared_ptr<type_system::TypeRegistry> type_registry;

       public:
        Components(std::shared_ptr<type_system::TypeRegistry> type_registry);
        
        template <typename T>
        ComponentInfo& register_component();
        
        std::optional<std::reference_wrapper<const ComponentInfo>> get(TypeId id) const;
        std::optional<std::reference_wrapper<ComponentInfo>> get_mut(TypeId id);
    };
}  // namespace epix::core

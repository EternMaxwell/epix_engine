#pragma once

#include <algorithm>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <unordered_map>

#include "../fwd.hpp"
#include "../meta/typeindex.hpp"

namespace epix::core::app {
template <typename T>
    requires requires(T t, App& app) {
        { t.build(app) } -> std::same_as<void>;
    } || std::invocable<T, App&>
struct PluginTraits {
    static constexpr bool has_finish = requires(T t, App& app) {
        { t.finish(app) } -> std::same_as<void>;
    };
    static constexpr bool has_finalize = requires(T t, App& app) {
        { t.finalize(app) } -> std::same_as<void>;
    };
    static constexpr bool callable = std::invocable<T, App&>;

    void build(T& instance, App& app) {
        if constexpr (callable) {
            instance(app);
        } else {
            instance.build(app);
        }
    }
    void finish(T& instance, App& app) {
        if constexpr (has_finish) {
            instance.finish(app);
        }
    }
    void finalize(T& instance, App& app) {
        if constexpr (has_finalize) {
            instance.finalize(app);
        }
    }
};
struct PluginBase {
    virtual ~PluginBase()           = default;
    virtual void build(App& app)    = 0;
    virtual void finish(App& app)   = 0;
    virtual void finalize(App& app) = 0;
};
template <typename T>
    requires requires(T t, App& app) {
        { t.build(app) } -> std::same_as<void>;
    } || std::invocable<T, App&>
struct PluginWrapper : PluginBase {
   public:
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    PluginWrapper(Args&&... args) : instance(std::forward<Args>(args)...) {}
    PluginWrapper(const PluginWrapper&)            = delete;
    PluginWrapper(PluginWrapper&&)                 = delete;
    PluginWrapper& operator=(const PluginWrapper&) = delete;
    PluginWrapper& operator=(PluginWrapper&&)      = delete;

    void build(App& app) override { PluginTraits<T>().build(instance, app); }
    void finish(App& app) override { PluginTraits<T>().finish(instance, app); }
    void finalize(App& app) override { PluginTraits<T>().finalize(instance, app); }

    T& get() { return instance; }
    const T& get() const { return instance; }

   private:
    T instance;
};
struct Plugins {
   public:
    Plugins()                          = default;
    Plugins(const Plugins&)            = delete;
    Plugins(Plugins&&)                 = default;
    Plugins& operator=(const Plugins&) = delete;
    Plugins& operator=(Plugins&&)      = default;

    template <typename T, typename... Args>
        requires std::constructible_from<T, Args...>
    void add_plugin(App& app, Args&&... args) {
        if (built) {
            throw std::runtime_error("Cannot add new plugins after build.");
        }
        // add if not exists.
        epix::meta::type_index type_id = epix::core::meta::type_id<T>();
        if (_plugin_index.contains(type_id)) return;
        size_t index              = _plugins.size();
        PluginWrapper<T>* wrapper = new PluginWrapper<T>(std::forward<Args>(args)...);
        _plugins.push_back(std::unique_ptr<PluginBase>(wrapper));
        _plugin_index[type_id] = index;
        wrapper->build(app);
    }
    template <typename T>
    void add_plugin(App& app, T&& plugin)
        requires std::constructible_from<std::decay_t<T>, T>
    {
        add_plugin<std::decay_t<T>>(app, std::forward<T>(plugin));
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_plugin_mut() {
        epix::meta::type_index type_id = epix::core::meta::type_id<T>();
        if (auto it = _plugin_index.find(type_id); it != _plugin_index.end()) {
            size_t index = it->second;
            auto ptr     = dynamic_cast<PluginWrapper<T>*>(_plugins[index].get());
            if (ptr) {
                return std::ref(ptr->get());
            }
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_plugin() const {
        epix::meta::type_index type_id = epix::core::meta::type_id<T>();
        if (auto it = _plugin_index.find(type_id); it != _plugin_index.end()) {
            size_t index = it->second;
            auto ptr     = dynamic_cast<const PluginWrapper<T>*>(_plugins[index].get());
            if (ptr) {
                return std::cref(ptr->get());
            }
        }
        return std::nullopt;
    }

    // No build all since build will be called when added to app.

    /// Finish building.
    void finish_all(App& app) {
        built = true;
        std::ranges::for_each(_plugins, [&](auto& plugin) { plugin->finish(app); });
    }
    /// Called at the end of the app's lifetime.
    void finalize_all(App& app) {
        std::ranges::for_each(_plugins, [&](auto& plugin) { plugin->finalize(app); });
    }

   private:
    bool built = false;
    std::vector<std::unique_ptr<PluginBase>> _plugins;
    std::unordered_map<epix::meta::type_index, size_t> _plugin_index;
};
}  // namespace epix::core::app
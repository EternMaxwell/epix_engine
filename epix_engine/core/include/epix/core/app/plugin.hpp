#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>

#include <epix/meta.hpp>

#include <epix/core/app/decl.hpp>

namespace epix::core {
template <typename T>
concept is_plugin = requires(T t, App& app) {
    { t.attach(app) } -> std::same_as<void>;
} || requires(T t, App& app) {
    { t.ready(app) } -> std::same_as<void>;
} || std::invocable<T, App&>;
template <is_plugin T>
struct PluginTraits {
    static constexpr bool has_attach = requires(T t, App& app) {
        { t.attach(app) } -> std::same_as<void>;
    };
    static constexpr bool has_ready = requires(T t, App& app) {
        { t.ready(app) } -> std::same_as<void>;
    };
    static constexpr bool has_detach = requires(T t, App& app) {
        { t.detach(app) } -> std::same_as<void>;
    };
    static constexpr bool callable = std::invocable<T, App&>;

    void attach(T& instance, App& app) {
        if constexpr (callable) {
            instance(app);
        } else if constexpr (has_attach) {
            instance.attach(app);
        }
    }
    void ready(T& instance, App& app) {
        if constexpr (has_ready) {
            instance.ready(app);
        }
    }
    void detach(T& instance, App& app) {
        if constexpr (has_detach) {
            instance.detach(app);
        }
    }
};
struct PluginBase {
    virtual ~PluginBase()         = default;
    virtual void attach(App& app) = 0;
    virtual void ready(App& app)  = 0;
    virtual void detach(App& app) = 0;
};
template <is_plugin T>
struct PluginWrapper : PluginBase {
   public:
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    PluginWrapper(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : instance(std::forward<Args>(args)...) {}
    PluginWrapper(const PluginWrapper&)            = delete;
    PluginWrapper(PluginWrapper&&)                 = delete;
    PluginWrapper& operator=(const PluginWrapper&) = delete;
    PluginWrapper& operator=(PluginWrapper&&)      = delete;

    void attach(App& app) override { PluginTraits<T>().attach(instance, app); }
    void ready(App& app) override { PluginTraits<T>().ready(instance, app); }
    void detach(App& app) override { PluginTraits<T>().detach(instance, app); }

    T& get() noexcept { return instance; }
    const T& get() const noexcept { return instance; }

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
        requires std::constructible_from<T, Args...> && is_plugin<T>
    void add_plugin(App& app, Args&&... args) {
        add_plugin_internal<T>(app, std::forward<Args>(args)...);
    }
    template <typename T>
    void add_plugin(App& app, T&& plugin)
        requires std::constructible_from<std::decay_t<T>, T> && is_plugin<std::decay_t<T>>
    {
        add_plugin_internal<std::decay_t<T>>(app, std::forward<T>(plugin));
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_plugin_mut() noexcept {
        meta::type_index type_id = meta::type_id<T>();
        if (auto it = _plugin_index.find(type_id); it != _plugin_index.end()) {
            std::size_t index = it->second;
            auto ptr          = dynamic_cast<PluginWrapper<T>*>(_plugins[index].get());
            if (ptr) {
                return std::ref(ptr->get());
            }
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_plugin() const noexcept {
        meta::type_index type_id = meta::type_id<T>();
        if (auto it = _plugin_index.find(type_id); it != _plugin_index.end()) {
            std::size_t index = it->second;
            auto ptr          = dynamic_cast<const PluginWrapper<T>*>(_plugins[index].get());
            if (ptr) {
                return std::cref(ptr->get());
            }
        }
        return std::nullopt;
    }

    // No attach all since attach is called when added to app.

    /// Mark plugins ready after all plugins have attached.
    void ready_all(App& app) {
        readied = true;
        spdlog::debug("[app] Readying {} plugins.", _plugins.size());
        std::ranges::for_each(_plugins, [&](auto& plugin) { plugin->ready(app); });
    }
    /// Called at the end of the app's lifetime.
    void detach_all(App& app) {
        spdlog::debug("[app] Detaching {} plugins.", _plugins.size());
        std::ranges::for_each(std::ranges::reverse_view(_plugins), [&](auto& plugin) { plugin->detach(app); });
    }

   private:
    template <typename T, typename... Args>
        requires std::constructible_from<T, Args...> && is_plugin<T>
    void add_plugin_internal(App& app, Args&&... args) {
        if (readied) {
            spdlog::error("Cannot add plugin after in/ready phase. Plugin[type = {}] will be ignored.",
                          meta::type_id<T>::name());
            return;
        }
        // add if not exists.
        meta::type_index type_id = meta::type_id<T>();
        if (_plugin_index.contains(type_id)) return;
        std::size_t index         = _plugins.size();
        PluginWrapper<T>* wrapper = new PluginWrapper<T>(std::forward<Args>(args)...);
        _plugins.push_back(std::unique_ptr<PluginBase>(wrapper));
        _plugin_index[type_id] = index;
        try {
            spdlog::debug("[app] Attaching plugin [type = {}].", meta::type_id<T>::name());
            wrapper->attach(app);
        } catch (const std::exception& e) {
            spdlog::error("Error attaching plugin[type = {}]: {}", meta::type_id<T>::name(), e.what());
        } catch (...) {
            spdlog::error("Unknown error attaching plugin[type = {}]", meta::type_id<T>::name());
        }
    }

    bool readied = false;
    std::vector<std::unique_ptr<PluginBase>> _plugins;
    std::unordered_map<meta::type_index, std::size_t> _plugin_index;
};
}  // namespace epix::core
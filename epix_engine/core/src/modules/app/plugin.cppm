module;

#include <spdlog/spdlog.h>

export module epix.core:app.plugin;

import std;
import epix.meta;

import :app.decl;

namespace core {
template <typename T>
concept is_plugin = requires(T t, App& app) {
    { t.build(app) } -> std::same_as<void>;
} || requires(T t, App& app) {
    { t.finish(app) } -> std::same_as<void>;
} || std::invocable<T, App&>;
template <is_plugin T>
struct PluginTraits {
    static constexpr bool has_build = requires(T t, App& app) {
        { t.build(app) } -> std::same_as<void>;
    };
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
        } else if constexpr (has_build) {
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
template <is_plugin T>
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
    std::optional<std::reference_wrapper<T>> get_plugin_mut() {
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
    std::optional<std::reference_wrapper<const T>> get_plugin() const {
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
    template <typename T, typename... Args>
        requires std::constructible_from<T, Args...> && is_plugin<T>
    void add_plugin_internal(App& app, Args&&... args) {
        if (built) {
            spdlog::error("Cannot add plugin after build phase. Plugin[type = {}] will be ignored.",
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
            wrapper->build(app);
        } catch (const std::exception& e) {
            spdlog::error("Error building plugin[type = {}]: {}", meta::type_id<T>::name(), e.what());
        } catch (...) {
            spdlog::error("Unknown error building plugin[type = {}]", meta::type_id<T>::name());
        }
    }

    bool built = false;
    std::vector<std::unique_ptr<PluginBase>> _plugins;
    std::unordered_map<meta::type_index, std::size_t> _plugin_index;
};
}  // namespace core
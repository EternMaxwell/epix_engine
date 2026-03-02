module;

#include <spdlog/spdlog.h>

export module epix.render:render_phase;

import epix.core;
import epix.meta;
import epix.utils;
import epix.traits;

import std;

import :graph;
import :pipeline;
import :pipeline_server;

namespace render::phase {
export struct DrawFunctionId : core::int_base<uint32_t> {
    using core::int_base<uint32_t>::int_base;
    auto operator<=>(const DrawFunctionId&) const = default;
    bool operator==(const DrawFunctionId&) const  = default;
};

export template <typename T>
concept PhaseItem = requires(const T item) {
    // the entity associated with this item
    { item.entity() } -> std::same_as<Entity>;
    // the sort key for this item, the smaller the key, the earlier it is rendered
    { item.sort_key() } -> std::three_way_comparable;
    // the draw function index for this item
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
};

export template <typename P>
concept BatchedPhaseItem = PhaseItem<P> && requires(const P item) {
    // batch size/count for this item
    { item.batch_size() } -> std::convertible_to<size_t>;
};

export template <typename P>
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(const P item) {
    // the pipeline cache key for this item
    { item.pipeline() } -> std::convertible_to<CachedPipelineId>;
};

export struct DrawError {
    enum class ErrorType {
        RenderCommandFailure,
        InvalidViewQuery,
        InvalidEntityQuery,
        ViewEntityMissing,
    } type;
    std::string message;

    static DrawError render_command_failure(std::string message = {}) {
        return DrawError{.type = ErrorType::RenderCommandFailure, .message = std::move(message)};
    }
    static DrawError invalid_view_query(std::string message = {}) {
        return DrawError{.type = ErrorType::InvalidViewQuery, .message = std::move(message)};
    }
    static DrawError invalid_entity_query(std::string message = {}) {
        return DrawError{.type = ErrorType::InvalidEntityQuery, .message = std::move(message)};
    }
    static DrawError view_entity_missing(std::string message = {}) {
        return DrawError{.type = ErrorType::ViewEntityMissing, .message = std::move(message)};
    }
};

std::string_view to_str(DrawError error) {
    switch (error.type) {
        case DrawError::ErrorType::RenderCommandFailure:
            return "Render command execution failed";
        case DrawError::ErrorType::InvalidViewQuery:
            return "Invalid view query";
        case DrawError::ErrorType::ViewEntityMissing:
            return "View entity missing in view query";
        case DrawError::ErrorType::InvalidEntityQuery:
            return "Invalid entity query";
        default:
            return "Unknown error";
    }
}

export template <typename FuncT, typename P>
concept Draw =
    PhaseItem<P> &&
    requires(FuncT func, const World& world, const wgpu::RenderPassEncoder& ctx, Entity view, const P& item) {
        { func.prepare(world) };
        { func.draw(world, ctx, view, item) } -> std::same_as<std::expected<void, DrawError>>;
    };

export template <PhaseItem P>
struct DrawFunction {
    virtual void prepare(const World& world) {}
    virtual std::expected<void, DrawError> draw(const World& world,
                                                const wgpu::RenderPassEncoder& ctx,
                                                Entity view,
                                                const P& item) = 0;

    virtual ~DrawFunction() = default;
};

template <PhaseItem P, Draw<P> Func>
struct DrawFunctionImpl : DrawFunction<P> {
   public:
    template <typename... Args>
    DrawFunctionImpl(Args&&... args) : m_func(std::forward<Args>(args)...) {}

    void prepare(const World& world) override { m_func.prepare(world); }

    std::expected<void, DrawError> draw(const World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        Entity view,
                                        const P& item) override {
        return m_func.draw(world, ctx, view, item);
    }

   private:
    Func m_func;
};

export template <PhaseItem P>
struct EmptyDrawFunction : DrawFunction<P> {
    std::expected<void, DrawError> draw(const World&, const wgpu::RenderPassEncoder&, Entity, const P&) override {
        return {};
    }
};

template <PhaseItem P>
struct DrawFunctionsInternal {
    std::vector<std::unique_ptr<DrawFunction<P>>> m_functions;
    std::unordered_map<meta::type_index, uint32_t> m_indices;

    void prepare(const World& world) {
        for (auto&& func : m_functions) {
            func->prepare(world);
        }
    }
    template <Draw<P> T, typename... Args>
        requires std::constructible_from<T, Args...>
    DrawFunctionId add(Args&&... args) {
        meta::type_index type = meta::type_id<T>();
        auto index            = static_cast<uint32_t>(m_functions.size());
        auto res              = m_indices.emplace(type, index);
        if (!res.second) return DrawFunctionId(res.first->second);
        // only construct and add if the type is not already present
        if constexpr (std::derived_from<T, DrawFunction<P>>) {
            m_functions.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        } else {
            m_functions.emplace_back(std::make_unique<DrawFunctionImpl<P, T>>(std::forward<Args>(args)...));
        }
        return DrawFunctionId(index);
    }
    template <typename Func>
        requires Draw<P, std::decay_t<Func>>
    DrawFunctionId add(Func&& func) {
        using T = std::decay_t<Func>;
        return add<T>(std::forward<Func>(func));
    }
    std::optional<DrawFunctionId> get_id(const meta::type_index& type) const {
        if (auto it = m_indices.find(type); it != m_indices.end()) {
            return DrawFunctionId(it->second);
        }
        return std::nullopt;
    }
    template <Draw<P> T>
    std::optional<DrawFunctionId> get_id() const {
        return get_id(meta::type_id<T>());
    }
    std::optional<std::reference_wrapper<DrawFunction<P>>> get(DrawFunctionId id) const {
        if (id.get() >= m_functions.size()) {
            return std::nullopt;
        }
        return std::ref(static_cast<DrawFunction<P>&>(*m_functions[id.get()]));
    }
};
/**
 * @brief An thread-safe DrawFunction registry for a specific phase item type. It allows adding and retrieving
 * DrawFunctions by type or by id.
 *
 * TODO: change the backend to cpp26 <rcu> after switching to cpp26, for better read performance.
 */
export template <PhaseItem P>
struct DrawFunctions {
    void prepare(const World& world) const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::unique_lock lock(m_mutex);
        for (auto&& func : m_functions) {
            func->prepare(world);
        }
    }
    template <Draw<P> T, typename... Args>
        requires std::constructible_from<T, Args...>
    DrawFunctionId add(Args&&... args) const {
        auto&& [m_mutex, m_functions] = *m_data;
        {
            std::shared_lock lock(m_mutex);
            auto id = m_functions.get_id<T>();
            if (id) return *id;
        }
        std::unique_lock lock(m_mutex);
        return m_functions.add<T>(std::forward<Args>(args)...);
    }
    std::optional<DrawFunctionId> get_id(const meta::type_index& type) const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::shared_lock lock(m_mutex);
        return m_functions.get_id(type);
    }
    template <Draw<P> T>
    std::optional<DrawFunctionId> get_id() const {
        return get_id(meta::type_id<T>());
    }
    std::optional<std::reference_wrapper<DrawFunction<P>>> get(DrawFunctionId id) const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::shared_lock lock(m_mutex);
        if (id.get() >= m_functions.size()) {
            return std::nullopt;
        }
        return std::ref(static_cast<DrawFunction<P>&>(*m_functions[id.get()]));
    }

   private:
    std::shared_ptr<std::pair<std::shared_mutex, DrawFunctionsInternal<P>>> m_data =
        std::make_shared<std::pair<std::shared_mutex, DrawFunctionsInternal<P>>>();
};

export template <PhaseItem T>
struct RenderPhase {
   public:
    using SortKey = decltype(std::declval<const T>().sort_key());

    std::vector<T> items;

   public:
    void add(const T& item) { items.push_back(item); }
    void add(T&& item) { items.push_back(std::move(item)); }
    void sort() {
        if constexpr (requires { T::sort(items); }) {
            T::sort(items);
        } else {
            std::ranges::sort(items, [](const T& a, const T& b) { return a.sort_key() < b.sort_key(); });
        }
    }
    auto iter_entities() const { return items | std::views::transform(T::entity); }
    void render(const wgpu::RenderPassEncoder& cmd, World& world, Entity view) const {
        render_range(cmd, world, view, 0, items.size());
    }
    void render_range(const wgpu::RenderPassEncoder& cmd,
                      const World& world,
                      Entity view,
                      std::size_t start = 0,
                      std::size_t end   = std::numeric_limits<size_t>::max()) const {
        end = std::min(end, items.size());
        if (start >= end) return;

        auto&& draw_functions = world.resource<DrawFunctions<T>>();
        draw_functions.prepare(world);
        for (size_t i = start; i < end; i += batch_size(items[i])) {
            auto& item = items[i];
            if (auto draw_function = draw_functions.get(item.draw_function()); draw_function) {
                auto result = draw_function->draw(world, cmd, view, item);
                if (!result) {
                    spdlog::error("[render] Draw function {} failed for item {:#x}. Error: {}.",
                                  static_cast<uint32_t>(item.draw_function()), item.entity().index,
                                  to_str(result.error()));
                }
            } else {
                spdlog::error("[render] Draw function {} not found for item {:#x}.",
                              static_cast<uint32_t>(item.draw_function()), item.entity().index);
            }
        }
    }
};
export struct RenderCommandError {
    enum class Type {
        Skip,
        Failure,
    } type;
    std::string message;
};
template <template <typename> typename R, typename P>
concept RenderCommand = requires {
    requires PhaseItem<P>;
    requires std::is_member_function_pointer_v<decltype(&R<P>::render)>;
    requires requires(R<P>& command, const World& world) {
        { command.prepare(world) };
    };
    requires std::constructible_from<R<P>>;
    requires std::movable<R<P>>;
    requires function_traits<decltype(&R<P>::render)>::arity == 5;
    requires std::same_as<std::expected<void, RenderCommandError>,
                          typename function_traits<decltype(&R<P>::render)>::return_type>;
    requires std::same_as<const P&, std::tuple_element_t<0, function_traits<decltype(&R<P>::render)>::args_tuple>>;
    requires core::query_data<std::tuple_element_t<1, function_traits<decltype(&R<P>::render)>::args_tuple>>;
    requires specialization_of<std::tuple_element_t<2, function_traits<decltype(&R<P>::render)>::args_tuple>,
                               std::optional>;
    requires core::query_data<
        typename std::tuple_element_t<2, function_traits<decltype(&R<P>::render)>::args_tuple>::value_type>;
    requires core::system_param<typename std::tuple_element_t<3, function_traits<decltype(&R<P>::render)>::args_tuple>>;
    requires std::convertible_to<const wgpu::RenderPassEncoder&,
                                 std::tuple_element_t<4, function_traits<decltype(&R<P>::render)>::args_tuple>>;
};

template <template <typename> typename R, PhaseItem P>
    requires RenderCommand<R, P>
struct RenderCommandState {
    using command_type      = R<P>;
    using func_traits       = function_traits<decltype(&command_type::render)>;
    using view_query_data   = std::tuple_element_t<1, typename func_traits::args_tuple>;
    using entity_query_data = typename std::tuple_element_t<2, typename func_traits::args_tuple>::value_type;
    using system_param      = std::tuple_element_t<3, typename func_traits::args_tuple>;

   private:
    using combined_param = core::ROSystemParam<core::ParamSet<view_query_data, entity_query_data, system_param>>;
    using combined_state = typename combined_param::State;
    using combined_item  = typename combined_param::Item;

   public:
    RenderCommandState(World& world) : command(), param_state(combined_param::init_state(world)) {
        combined_param::init_access(param_state, meta, access, world);
    }
    void prepare(const World& world) {
        params = combined_param::get_param(param_state, meta, world, world.change_tick());
        command.prepare(world);
    }
    std::expected<void, DrawError> draw(const World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        Entity view,
                                        const P& item) {
        if (!params) {
            throw std::runtime_error("Failed to get system parameters for render command.");
        }
        auto&& [view_query, entity_query, system_param] = *params;
        auto view_query_result                          = view_query.get(view);
        if (!view_query_result) return std::unexpected(DrawError::invalid_view_query());
        auto entity_query_result = entity_query.get(item.entity());
        if (!entity_query_result) return std::unexpected(DrawError::invalid_entity_query());
        auto result = command.render(world, ctx, view, item, *view_query_result, *entity_query_result, system_param);
        if (!result) {
            auto msg =
                std::format("Render command {} failed for item {:#x}. Error: {}.",
                            meta::type_id<command_type>::short_name(), item.entity().index, result.error().message);
            return std::unexpected(DrawError::render_command_failure(std::move(msg)));
        }
        return {};
    }

   private:
    core::SystemMeta meta;
    core::FilteredAccessSet access;
    combined_state param_state;
    std::optional<combined_item> params;
    command_type command;
};
template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
struct RenderCommandSequence {
   public:
    RenderCommandSequence(World& world) : m_commands(std::make_tuple(RenderCommandState<R, P>(world)...)) {}

    void prepare(const World& world) {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (std::get<I>(m_commands).prepare(world), ...);
        }(std::index_sequence_for<R<P>...>{});
    }

    std::expected<void, DrawError> draw(const World& world,
                                        const wgpu::RenderPassEncoder& cmd,
                                        Entity view,
                                        const P& item) {
        return [&]<size_t I>(this auto&& self, std::integral_constant<size_t, I>) {
            auto res = std::get<I>(m_commands).draw(world, cmd, view, item);
            if (!res) return res;
            if constexpr (I + 1 < sizeof...(R)) {
                return self(std::integral_constant<size_t, I + 1>{});
            } else {
                return res;
            }
        }(std::integral_constant<size_t, 0>{});
    }

   private:
    std::tuple<RenderCommandState<R, P>...> m_commands;
};

export template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
DrawFunctionId app_add_render_commands(core::App& app) {
    auto& draw_functions = app.world_mut().resource_mut<DrawFunctions<P>>();
    return draw_functions.template add<RenderCommandSequence<P, R...>>(app.world_mut());
}
}  // namespace render::phase
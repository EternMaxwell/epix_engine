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

namespace epix::render::phase {
/** @brief Strongly-typed index identifying a registered draw function. */
export struct DrawFunctionId : core::int_base<uint32_t> {
    using core::int_base<uint32_t>::int_base;
    auto operator<=>(const DrawFunctionId&) const = default;
    bool operator==(const DrawFunctionId&) const  = default;
};

/**
 * @brief Type-erased sort key for opaque phase items.
 *
 * Wraps any value type that satisfies `std::three_way_comparable<T, std::strong_ordering>`.
 * When comparing two keys, typeid on the polymorphic Concept is used to establish a stable
 * total order across heterogeneous key types; values are then compared within the same type.
 * This allows different render subsystems to attach custom batch keys to Opaque2D items.
 * Move-only (backed by unique_ptr).
 */
export struct OpaqueSortKey {
   private:
    struct Concept {
        virtual std::strong_ordering compare(const Concept& other) const = 0;
        virtual ~Concept()                                               = default;
    };

    template <typename T>
    struct Model : Concept {
        T value;
        explicit Model(T v) : value(std::move(v)) {}
        std::strong_ordering compare(const Concept& other) const override {
            return value <=> static_cast<const Model<T>&>(other).value;
        }
    };

    std::unique_ptr<Concept> impl;

   public:
    OpaqueSortKey()                                = default;
    OpaqueSortKey(OpaqueSortKey&&)                 = default;
    OpaqueSortKey& operator=(OpaqueSortKey&&)      = default;
    OpaqueSortKey(const OpaqueSortKey&)            = delete;
    OpaqueSortKey& operator=(const OpaqueSortKey&) = delete;

    template <typename T>
        requires std::three_way_comparable<T, std::strong_ordering>
    explicit OpaqueSortKey(T value) : impl(std::make_unique<Model<T>>(std::move(value))) {}

    std::strong_ordering operator<=>(const OpaqueSortKey& other) const {
        if (!impl && !other.impl) return std::strong_ordering::equal;
        if (!impl) return std::strong_ordering::less;
        if (!other.impl) return std::strong_ordering::greater;

        // typeid on the polymorphic object gives the concrete Model<T> type — no extra virtual needed
        const std::type_info& ta = typeid(*impl);
        const std::type_info& tb = typeid(*other.impl);
        if (ta != tb) {
            return ta.before(tb) ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        return impl->compare(*other.impl);
    }
    bool operator==(const OpaqueSortKey& other) const { return (*this <=> other) == std::strong_ordering::equal; }
};

/** @brief Concept for a render phase item that provides entity, sort key,
 * and draw function identifiers. */
export template <typename T>
concept PhaseItem = requires(const T item) {
    // the entity associated with this item
    { item.entity() } -> std::same_as<Entity>;
    // the sort key for this item, the smaller the key, the earlier it is rendered
    { item.sort_key() } -> std::three_way_comparable;
    // the draw function index for this item
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
};

/** @brief Concept extending PhaseItem with batch size support for
 * instanced draws. */
export template <typename P>
concept BatchedPhaseItem = PhaseItem<P> && requires(const P item) {
    // batch size/count for this item
    { item.batch_size() } -> std::convertible_to<size_t>;
};

/** @brief Concept extending PhaseItem with a cached pipeline ID for
 * pipeline state management. */
export template <typename P>
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(const P item) {
    // the pipeline cache key for this item
    { item.pipeline() } -> std::convertible_to<CachedPipelineId>;
};

/** @brief Error returned by draw functions, with type indicating whether
 * to skip or report failure. */
export struct DrawError {
    enum class ErrorType {
        Skip,
        RenderCommandFailure,
        InvalidViewQuery,
        InvalidEntityQuery,
        ViewEntityMissing,
    } type;
    std::string message;

    static DrawError skip(std::string message = {}) {
        return DrawError{.type = ErrorType::Skip, .message = std::move(message)};
    }
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
        case DrawError::ErrorType::Skip:
            return "Skipped";
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

/** @brief Concept for a draw function that can prepare and draw phase
 * items.
 * @tparam FuncT The draw function type.
 * @tparam P The phase item type. */
export template <typename FuncT, typename P>
concept Draw =
    PhaseItem<P> &&
    requires(FuncT func, const World& world, const wgpu::RenderPassEncoder& ctx, Entity view, const P& item) {
        { func.prepare(world) };
        { func.draw(world, ctx, view, item) } -> std::same_as<std::expected<void, DrawError>>;
    };

/** @brief Abstract base class for type-erased draw functions for a
 * specific phase item type.
 * @tparam P The phase item type. */
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

/** @brief A draw function that does nothing — useful as a placeholder. */
export template <PhaseItem P>
struct EmptyDrawFunction : DrawFunction<P> {
    std::expected<void, DrawError> draw(const World&, const wgpu::RenderPassEncoder&, Entity, const P&) override {
        return {};
    }
};

template <PhaseItem P>
struct DrawFunctionsInternal {
   private:
    std::vector<std::unique_ptr<DrawFunction<P>>> m_functions;
    std::unordered_map<meta::type_index, uint32_t> m_indices;

   public:
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
        m_functions.prepare(world);
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
        return m_functions.get(id);
    }

   private:
    std::shared_ptr<std::pair<std::shared_mutex, DrawFunctionsInternal<P>>> m_data =
        std::make_shared<std::pair<std::shared_mutex, DrawFunctionsInternal<P>>>();
};

/** @brief Component holding sorted phase items and executing their draw
 * functions during rendering.
 * @tparam T The phase item type. */
export template <PhaseItem T>
struct RenderPhase {
   public:
    RenderPhase()                              = default;
    RenderPhase(const RenderPhase&)            = delete;
    RenderPhase(RenderPhase&&)                 = default;
    RenderPhase& operator=(const RenderPhase&) = delete;
    RenderPhase& operator=(RenderPhase&&)      = default;

   public:
    using SortKey = decltype(std::declval<const T>().sort_key());

    std::vector<T> items;

    std::size_t batch_size(const T& item) const {
        if constexpr (BatchedPhaseItem<T>) {
            return std::max<size_t>(1, item.batch_size());
        } else {
            return 1;
        }
    }

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
    void render(const wgpu::RenderPassEncoder& cmd, const World& world, Entity view) const {
        render_range(cmd, world, view, 0, items.size());
    }
    void render_range(const wgpu::RenderPassEncoder& cmd,
                      const World& world,
                      Entity view,
                      std::size_t start = 0,
                      std::size_t end   = std::numeric_limits<std::size_t>::max()) const {
        end = std::min(end, items.size());
        if (start >= end) return;

        auto&& draw_functions = world.resource<DrawFunctions<T>>();
        draw_functions.prepare(world);
        for (std::size_t i = start; i < end; i += batch_size(items[i])) {
            auto& item = items[i];
            if (auto draw_function = draw_functions.get(item.draw_function()); draw_function) {
                auto result = draw_function->get().draw(world, cmd, view, item);
                if (!result) {
                    auto&& error = result.error();
                    if (error.type == DrawError::ErrorType::Skip) {
                        continue;
                    }
                    if (!error.message.empty()) {
                        spdlog::error("[render] Draw function {} failed for item {:#x}. Error: {}.",
                                      static_cast<std::uint32_t>(item.draw_function()), item.entity().index,
                                      error.message);
                    } else {
                        spdlog::error("[render] Draw function {} failed for item {:#x}. Error: {}.",
                                      static_cast<std::uint32_t>(item.draw_function()), item.entity().index,
                                      to_str(error));
                    }
                }
            } else {
                spdlog::error("[render] Draw function {} not found for item {:#x}.",
                              static_cast<std::uint32_t>(item.draw_function()), item.entity().index);
            }
        }
    }
};
/** @brief Error returned by individual render commands within a draw
 * function chain. */
export struct RenderCommandError {
    enum class Type {
        Skip,
        Failure,
    } type;
    std::string message;
};

template <template <typename> typename R, typename P>
using render_command_traits = function_traits<decltype(&R<P>::render)>;

/** @brief Concept for a struct template that forms a render command
 * within a draw function sequence.
 * @tparam R The command template (parameterized on PhaseItem).
 * @tparam P The phase item type. */
export template <template <typename> typename R, typename P>
concept RenderCommand = requires {
    requires PhaseItem<P>;
    requires std::is_member_function_pointer_v<decltype(&R<P>::render)>;
    requires requires(R<P>& command, const World& world) {
        { command.prepare(world) };
    };
    requires std::constructible_from<R<P>>;
    requires std::movable<R<P>>;
    requires render_command_traits<R, P>::arity == 5;
    requires std::same_as<std::expected<void, RenderCommandError>, typename render_command_traits<R, P>::return_type>;
    requires std::same_as<const P&, std::tuple_element_t<0, typename render_command_traits<R, P>::args_tuple>>;
    requires core::query_data<std::tuple_element_t<1, typename render_command_traits<R, P>::args_tuple>>;
    requires specialization_of<std::tuple_element_t<2, typename render_command_traits<R, P>::args_tuple>,
                               std::optional>;
    requires core::query_data<
        typename std::tuple_element_t<2, typename render_command_traits<R, P>::args_tuple>::value_type>;
    requires core::system_param<typename std::tuple_element_t<3, typename render_command_traits<R, P>::args_tuple>>;
    requires std::convertible_to<const wgpu::RenderPassEncoder&,
                                 std::tuple_element_t<4, typename render_command_traits<R, P>::args_tuple>>;
};

template <template <typename> typename R, PhaseItem P>
    requires RenderCommand<R, P>
struct RenderCommandState {
    using command_type       = R<P>;
    using func_traits        = render_command_traits<R, P>;
    using view_query_data    = std::tuple_element_t<1, typename func_traits::args_tuple>;
    using entity_query_data  = typename std::tuple_element_t<2, typename func_traits::args_tuple>::value_type;
    using view_query_param   = core::Query<view_query_data>;
    using entity_query_param = core::Query<entity_query_data>;
    using system_param       = std::tuple_element_t<3, typename func_traits::args_tuple>;

   private:
    using combined_param = core::ROSystemParam<core::ParamSet<view_query_param, entity_query_param, system_param>>;
    using combined_state = typename combined_param::State;
    using combined_item  = typename combined_param::Item;

   public:
    explicit RenderCommandState(World& world) {
        param_state = combined_param::init_state(world);
        combined_param::init_access(*param_state, meta, access, world);
    }

    void prepare(const World& world) {
        params = combined_param::get_param(*param_state, meta, world, world.change_tick());
        command.prepare(world);
    }
    std::expected<void, DrawError> draw(const World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        Entity view,
                                        const P& item) {
        if (!params) {
            throw std::runtime_error("Failed to get system parameters for render command.");
        }
        auto&& [view_query, entity_query, system_param] = params->get();
        auto view_query_result                          = view_query.get(view);
        if (!view_query_result) return std::unexpected(DrawError::invalid_view_query());
        auto entity_query_result = entity_query.get(item.entity());
        auto result              = command.render(item, *view_query_result, entity_query_result, system_param, ctx);
        if (!result) {
            if (result.error().type == RenderCommandError::Type::Skip) {
                return std::unexpected(DrawError::skip(std::move(result.error().message)));
            }
            auto msg = result.error().message.empty()
                           ? std::format("Render command {} failed for item {:#x}.",
                                         meta::type_id<command_type>::short_name(), item.entity().index)
                           : std::format("Render command {} failed for item {:#x}. Error: {}.",
                                         meta::type_id<command_type>::short_name(), item.entity().index,
                                         result.error().message);
            return std::unexpected(DrawError::render_command_failure(std::move(msg)));
        }
        return {};
    }

   private:
    core::SystemMeta meta;
    core::FilteredAccessSet access;
    std::optional<combined_state> param_state;
    std::optional<combined_item> params;
    command_type command;
};

/** @brief Render command that sets the cached render pipeline on the
 * encoder for a CachedRenderPipelinePhaseItem.
 * @tparam P The phase item type. */
export template <CachedRenderPipelinePhaseItem P>
struct SetItemPipeline {
    void prepare(const World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   Item<>,
                                                   std::optional<Item<>>,
                                                   ParamSet<Res<PipelineServer>> params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        auto&& [pipeline_server] = params.get();
        auto pipeline            = pipeline_server->get_render_pipeline(item.pipeline());
        if (!pipeline) {
            if (std::holds_alternative<GetPipelineNotReady>(pipeline.error())) {
                return std::unexpected(RenderCommandError{
                    .type    = RenderCommandError::Type::Skip,
                    .message = std::format("Render pipeline {} is not ready for item {:#x}.", item.pipeline().get(),
                                           item.entity().index),
                });
            }
            auto detail = std::visit(
                []<typename T>(const T& error) -> std::string {
                    using error_t = std::decay_t<T>;
                    if constexpr (std::is_same_v<error_t, GetPipelineNotReady>) {
                        return "pipeline not ready";
                    } else if constexpr (std::is_same_v<error_t, GetPipelineInvalidId>) {
                        return "invalid pipeline id";
                    } else {
                        return std::visit(
                            []<typename Inner>(const Inner& inner) -> std::string {
                                using inner_t = std::decay_t<Inner>;
                                if constexpr (std::is_same_v<inner_t, PipelineError>) {
                                    return "pipeline creation failure";
                                } else if constexpr (std::is_same_v<inner_t, shader::ShaderCacheError>) {
                                    if (std::holds_alternative<shader::ShaderCacheError::ShaderNotLoaded>(inner.data) ||
                                        std::holds_alternative<shader::ShaderCacheError::ShaderImportNotYetAvailable>(
                                            inner.data)) {
                                        return "shader not loaded";
                                    }
                                    return "shader error";
                                } else {
                                    return "unknown pipeline server error";
                                }
                            },
                            error);
                    }
                },
                pipeline.error());
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Failure,
                .message = std::format("Failed to resolve render pipeline {} for item {:#x}: {}.",
                                       item.pipeline().get(), item.entity().index, detail),
            });
        }

        encoder.setPipeline(pipeline->get().pipeline());
        return {};
    }
};
template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
struct RenderCommandSequence {
   public:
    explicit RenderCommandSequence(World& world)
        : m_commands([&]<size_t... I>(std::index_sequence<I...>) {
              return std::tuple<RenderCommandState<R, P>...>{((void)I, RenderCommandState<R, P>(world))...};
          }(std::index_sequence_for<R<P>...>{})) {}

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

/** @brief Register a sequence of render commands as a draw function for
 * phase P.
 * @tparam P The phase item type.
 * @tparam R Render command templates to chain.
 * @return The DrawFunctionId of the registered sequence. */
export template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
DrawFunctionId app_add_render_commands(core::App& app) {
    auto& world          = app.world_mut();
    auto& draw_functions = world.resource_mut<DrawFunctions<P>>();
    return draw_functions.template add<RenderCommandSequence<P, R...>>(world);
}

/** @brief System that sorts all RenderPhase<P> components by their sort
 * keys. */
export template <PhaseItem P>
void sort_phase_items(Query<Item<RenderPhase<P>&>> phases) {
    for (auto&& [phase] : phases.iter()) {
        phase.sort();
    }
}
}  // namespace epix::render::phase
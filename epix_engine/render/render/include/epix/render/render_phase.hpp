#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <epix/render/label.hpp>
#include <epix/traits.hpp>
#include <epix/utils.hpp>
#include <expected>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/render/graph.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/pipeline_server.hpp>

namespace epix::render::phase {
/** @brief Strongly-typed index identifying a registered draw function. */
EPIX_EXPORT struct DrawFunctionId : utils::int_base<uint32_t> {
    using utils::int_base<uint32_t>::int_base;
    auto operator<=>(const DrawFunctionId&) const noexcept = default;
    bool operator==(const DrawFunctionId&) const noexcept  = default;
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
EPIX_EXPORT struct OpaqueSortKey {
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
    OpaqueSortKey() noexcept                           = default;
    OpaqueSortKey(OpaqueSortKey&&) noexcept            = default;
    OpaqueSortKey& operator=(OpaqueSortKey&&) noexcept = default;
    OpaqueSortKey(const OpaqueSortKey&)                = delete;
    OpaqueSortKey& operator=(const OpaqueSortKey&)     = delete;

    template <typename T>
        requires std::three_way_comparable<T, std::strong_ordering>
    explicit OpaqueSortKey(T value) : impl(std::make_unique<Model<T>>(std::move(value))) {}

    std::strong_ordering operator<=>(const OpaqueSortKey& other) const {
        if (!impl && !other.impl) return std::strong_ordering::equal;
        if (!impl) return std::strong_ordering::less;
        if (!other.impl) return std::strong_ordering::greater;

        // typeid on the polymorphic object gives the concrete Model<T> type — no extra virtual needed
        Concept& a               = *impl;
        Concept& b               = *other.impl;
        const std::type_info& ta = typeid(a);
        const std::type_info& tb = typeid(b);
        if (ta != tb) {
            return ta.before(tb) ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        return impl->compare(*other.impl);
    }
    bool operator==(const OpaqueSortKey& other) const { return (*this <=> other) == std::strong_ordering::equal; }
};

/** @brief The extra index associated with a phase item besides its instance
 * range (Bevy `PhaseItemExtraIndex`). A
 * dynamic offset is one value, while an
 * indirect draw carries both the command range and its optional multi-draw
 *
 * batch-set index. */
EPIX_EXPORT struct PhaseItemExtraIndex {
    enum class Type : std::uint8_t {
        None,
        DynamicOffset,
        IndirectParametersIndex,
    } type = Type::None;

    /** Dynamic uniform-buffer offset when `type == DynamicOffset`. */
    std::uint32_t value = 0;
    /** Half-open indirect-command range when `type == IndirectParametersIndex`.
     * It is intentionally a range: GPU
     * culling can reduce it to fewer actual
     * commands when multi-draw-indirect-count is available. */
    std::pair<std::uint32_t, std::uint32_t> indirect_range{0, 0};
    /** Optional multi-draw batch-set index for an indirect command range. */
    std::optional<std::uint32_t> batch_set_index;

    constexpr PhaseItemExtraIndex() noexcept = default;
    constexpr PhaseItemExtraIndex(Type type,
                                  std::uint32_t value,
                                  std::pair<std::uint32_t, std::uint32_t> indirect_range = {0, 0},
                                  std::optional<std::uint32_t> batch_set_index           = std::nullopt) noexcept
        : type(type), value(value), indirect_range(indirect_range), batch_set_index(batch_set_index) {}
    static const PhaseItemExtraIndex None;
    static constexpr PhaseItemExtraIndex dynamic_offset(std::uint32_t offset) noexcept {
        return {Type::DynamicOffset, offset};
    }
    static constexpr PhaseItemExtraIndex indirect_parameters_index(std::uint32_t index) noexcept {
        return indirect_parameters_range(index, index + 1);
    }
    static constexpr PhaseItemExtraIndex indirect_parameters_range(
        std::uint32_t start, std::uint32_t end, std::optional<std::uint32_t> batch_set = std::nullopt) noexcept {
        return {Type::IndirectParametersIndex, 0, {start, end}, batch_set};
    }

    constexpr bool operator==(const PhaseItemExtraIndex&) const noexcept = default;
};

inline const PhaseItemExtraIndex PhaseItemExtraIndex::None{};

/** @brief Length of a batch range (Bevy `Range::len`). */
EPIX_EXPORT inline std::uint32_t batch_range_len(const std::pair<std::uint32_t, std::uint32_t>& range) noexcept {
    return range.second - range.first;
}

/** @brief Concept for a render phase item providing entity, main entity,
 * sort key, draw function, batch range and extra index (Bevy 0.18
 * `PhaseItem`, render_phase/mod.rs:1517-1548). */
EPIX_EXPORT template <typename T>
concept PhaseItem = requires(const T item) {
    // the render entity associated with this item
    { item.entity() } -> std::same_as<epix::ecs::Entity>;
    // the main-world entity represented by this item
    { item.main_entity() } -> std::same_as<sync_world::MainEntity>;
    // the sort key for this item, the smaller the key, the earlier it is rendered
    { item.sort_key() } -> std::three_way_comparable;
    // the draw function index for this item
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
    // the stored instance range covered by this item's batch (Bevy
    // batch_range: Range<u32>; C++ cannot share a field and a method name,
    // so the field is accessed directly)
    { item.batch_range } -> std::convertible_to<const std::pair<std::uint32_t, std::uint32_t>&>;
    // the extra index (dynamic offset / indirect parameters)
    { item.extra_index() } -> std::same_as<PhaseItemExtraIndex>;
};

/** @brief Extends a phase item with Bevy's mutable extra-index contract,
 * used by automatic CPU batching and GPU
 * preprocessing. */
EPIX_EXPORT template <typename T>
concept MutablePhaseItemExtraIndex = PhaseItem<T> && requires(T item, PhaseItemExtraIndex extra_index) {
    { item.set_extra_index(extra_index) } -> std::same_as<void>;
};

/** @brief Concept extending PhaseItem with a cached pipeline ID for
 * pipeline state management. */
EPIX_EXPORT template <typename P>
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(const P item) {
    // the pipeline cache key for this item
    { item.pipeline() } -> std::convertible_to<CachedPipelineId>;
};

/** @brief Error returned by draw functions, with type indicating whether
 * to skip or report failure. */
EPIX_EXPORT struct DrawError {
    enum class ErrorType {
        Skip,
        RenderCommandFailure,
        InvalidViewQuery,
        InvalidEntityQuery,
        ViewEntityMissing,
    } type;
    std::string message;

    static DrawError skip(std::string message = {}) noexcept {
        return DrawError{.type = ErrorType::Skip, .message = std::move(message)};
    }
    static DrawError render_command_failure(std::string message = {}) noexcept {
        return DrawError{.type = ErrorType::RenderCommandFailure, .message = std::move(message)};
    }
    static DrawError invalid_view_query(std::string message = {}) noexcept {
        return DrawError{.type = ErrorType::InvalidViewQuery, .message = std::move(message)};
    }
    static DrawError invalid_entity_query(std::string message = {}) noexcept {
        return DrawError{.type = ErrorType::InvalidEntityQuery, .message = std::move(message)};
    }
    static DrawError view_entity_missing(std::string message = {}) noexcept {
        return DrawError{.type = ErrorType::ViewEntityMissing, .message = std::move(message)};
    }
};

inline std::string_view to_str(DrawError error) noexcept {
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
EPIX_EXPORT template <typename FuncT, typename P>
concept Draw = PhaseItem<P> && requires(FuncT func,
                                        const epix::ecs::World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        epix::ecs::Entity view,
                                        const P& item) {
    { func.prepare(world) };
    { func.draw(world, ctx, view, item) } -> std::same_as<std::expected<void, DrawError>>;
};

/** @brief Abstract base class for type-erased draw functions for a
 * specific phase item type.
 * @tparam P The phase item type. */
EPIX_EXPORT template <PhaseItem P>
struct DrawFunction {
    virtual void prepare(const epix::ecs::World& world) {}
    virtual std::expected<void, DrawError> draw(const epix::ecs::World& world,
                                                const wgpu::RenderPassEncoder& ctx,
                                                epix::ecs::Entity view,
                                                const P& item) = 0;

    virtual ~DrawFunction() = default;
};

template <PhaseItem P, Draw<P> Func>
struct DrawFunctionImpl : DrawFunction<P> {
   public:
    template <typename... Args>
    DrawFunctionImpl(Args&&... args) : m_func(std::forward<Args>(args)...) {}

    void prepare(const epix::ecs::World& world) override { m_func.prepare(world); }

    std::expected<void, DrawError> draw(const epix::ecs::World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        epix::ecs::Entity view,
                                        const P& item) override {
        return m_func.draw(world, ctx, view, item);
    }

   private:
    Func m_func;
};

/** @brief A draw function that does nothing — useful as a placeholder. */
EPIX_EXPORT template <PhaseItem P>
struct EmptyDrawFunction : DrawFunction<P> {
    std::expected<void, DrawError> draw(const epix::ecs::World&,
                                        const wgpu::RenderPassEncoder&,
                                        epix::ecs::Entity,
                                        const P&) noexcept override {
        return {};
    }
};

template <PhaseItem P>
struct DrawFunctionsInternal {
   private:
    std::vector<std::unique_ptr<DrawFunction<P>>> m_functions;
    std::unordered_map<meta::type_index, uint32_t> m_indices;

   public:
    void prepare(const epix::ecs::World& world) {
        for (auto&& func : m_functions) {
            func->prepare(world);
        }
    }
    /** @brief Append a draw function and map it to the type `T` (Bevy
     * DrawFunctionsInternal::add_with: the mapped key is the first template
     * parameter, draw.rs:84-92). */
    template <typename T, Draw<P> D, typename... Args>
        requires std::constructible_from<D, Args...>
    DrawFunctionId add_with(Args&&... args) {
        const auto index = static_cast<uint32_t>(m_functions.size());
        // Bevy always appends a new function and re-maps the type to the NEW
        // id (draw.rs:74-84); a re-registered draw function replaces the old one.
        m_indices.insert_or_assign(meta::type_id<T>(), index);
        if constexpr (std::derived_from<D, DrawFunction<P>>) {
            m_functions.emplace_back(std::make_unique<D>(std::forward<Args>(args)...));
        } else {
            m_functions.emplace_back(std::make_unique<DrawFunctionImpl<P, D>>(std::forward<Args>(args)...));
        }
        return DrawFunctionId(index);
    }
    /** @brief Append a draw function mapped to its own type (Bevy
     * DrawFunctionsInternal::add). */
    template <Draw<P> T, typename... Args>
        requires std::constructible_from<T, Args...>
    DrawFunctionId add(Args&&... args) {
        return add_with<T, T>(std::forward<Args>(args)...);
    }
    /** @brief The id of the draw function registered under type T; throws if
     * not registered (Bevy DrawFunctionsInternal::id panics). */
    template <typename T>
    DrawFunctionId id() const {
        if (auto it = m_indices.find(meta::type_id<T>()); it != m_indices.end()) {
            return DrawFunctionId(it->second);
        }
        throw std::runtime_error(
            std::format("Draw function {} not found for {}", meta::type_id<T>().name(), meta::type_id<P>().name()));
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
EPIX_EXPORT template <PhaseItem P>
struct DrawFunctions {
    void prepare(const epix::ecs::World& world) const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::unique_lock lock(m_mutex);
        m_functions.prepare(world);
    }
    template <Draw<P> T, typename... Args>
        requires std::constructible_from<T, Args...>
    DrawFunctionId add(Args&&... args) const {
        auto&& [m_mutex, m_functions] = *m_data;
        // Bevy always appends and re-maps the type to the NEW id (draw.rs:74-84).
        std::unique_lock lock(m_mutex);
        return m_functions.template add<T>(std::forward<Args>(args)...);
    }
    /** @brief Append a draw function mapped to the type `T` (Bevy
     * DrawFunctions::add_with). */
    template <typename T, Draw<P> D, typename... Args>
        requires std::constructible_from<D, Args...>
    DrawFunctionId add_with(Args&&... args) const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::unique_lock lock(m_mutex);
        return m_functions.template add_with<T, D>(std::forward<Args>(args)...);
    }
    /** @brief The id of the draw function registered under type T; throws if
     * not registered (Bevy DrawFunctions::id panics). */
    template <typename T>
    DrawFunctionId id() const {
        auto&& [m_mutex, m_functions] = *m_data;
        std::shared_lock lock(m_mutex);
        return m_functions.template id<T>();
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
EPIX_EXPORT template <PhaseItem T>
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

    /** @brief Length of the item's batch range, at least 1 (Bevy
     * batch_range().len()). */
    std::size_t batch_size(const T& item) const { return std::max<std::size_t>(1, batch_range_len(item.batch_range)); }

   public:
    void add(const T& item) { items.push_back(item); }
    void add(T&& item) { items.push_back(std::move(item)); }
    /** @brief Remove all items (Bevy SortedRenderPhase::clear). */
    void clear() { items.clear(); }
    void sort() {
        if constexpr (requires { T::sort(items); }) {
            T::sort(items);
        } else {
            // Bevy sorts with a stable sort (sort_by_key on the IndexMap); equal
            // keys keep their insertion order so same-depth sprites don't flicker.
            std::ranges::stable_sort(items, [](const T& a, const T& b) { return a.sort_key() < b.sort_key(); });
        }
    }
    auto iter_entities() const { return std::views::transform(items, T::entity); }
    void render(const wgpu::RenderPassEncoder& cmd, const epix::ecs::World& world, epix::ecs::Entity view) const {
        render_range(cmd, world, view, 0, items.size());
    }
    void render_range(const wgpu::RenderPassEncoder& cmd,
                      const epix::ecs::World& world,
                      epix::ecs::Entity view,
                      std::size_t start = 0,
                      std::size_t end   = std::numeric_limits<std::size_t>::max()) const {
        end = std::min(end, items.size());
        if (start >= end) return;

        auto&& draw_functions = world.resource<DrawFunctions<T>>();
        draw_functions.prepare(world);
        // Bevy: an empty batch range is skipped without invoking the draw
        // function; otherwise skip `batch_range.len()` items after each
        // batched draw (render_phase/mod.rs:1470-1487).
        for (std::size_t i = start; i < end;) {
            auto& item            = items[i];
            const std::size_t len = batch_range_len(item.batch_range);
            if (len == 0) {
                ++i;
                continue;
            }
            if (auto draw_function = draw_functions.get(item.draw_function()); draw_function) {
                auto result = draw_function->get().draw(world, cmd, view, item);
                if (result) {
                    // drawn
                } else {
                    auto&& error = result.error();
                    if (error.type == DrawError::ErrorType::Skip) {
                        // skip: do not log, still advance past the batch
                    } else if (!error.message.empty()) {
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
            i += len;
        }
    }
};
/** @brief Error returned by individual render commands within a draw
 * function chain. */
EPIX_EXPORT struct RenderCommandError {
    enum class Type {
        Skip,
        Failure,
    } type;
    std::string message;
};

template <template <typename> typename R, typename P>
using render_command_traits = traits::function_traits<decltype(&R<P>::render)>;

/** @brief Concept for a struct template that forms a render command
 * within a draw function sequence.
 * @tparam R The command template (parameterized on PhaseItem).
 * @tparam P The phase item type. */
EPIX_EXPORT template <template <typename> typename R, typename P>
concept RenderCommand = requires {
    requires PhaseItem<P>;
    requires std::is_member_function_pointer_v<decltype(&R<P>::render)>;
    requires requires(R<P>& command, const epix::ecs::World& world) {
        { command.prepare(world) };
    };
    requires std::constructible_from<R<P>>;
    requires std::movable<R<P>>;
    requires render_command_traits<R, P>::arity == 5;
    requires std::same_as<std::expected<void, RenderCommandError>, typename render_command_traits<R, P>::return_type>;
    requires std::same_as<const P&, std::tuple_element_t<0, typename render_command_traits<R, P>::args_tuple>>;
    requires ecs::query_data<std::tuple_element_t<1, typename render_command_traits<R, P>::args_tuple>>;
    requires traits::specialization_of<std::tuple_element_t<2, typename render_command_traits<R, P>::args_tuple>,
                                       std::optional>;
    requires ecs::query_data<
        typename std::tuple_element_t<2, typename render_command_traits<R, P>::args_tuple>::value_type>;
    requires ecs::system_param<typename std::tuple_element_t<3, typename render_command_traits<R, P>::args_tuple>>;
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
    using view_query_param   = epix::ecs::Query<view_query_data>;
    using entity_query_param = epix::ecs::Query<entity_query_data>;
    using system_param       = std::tuple_element_t<3, typename func_traits::args_tuple>;

   private:
    using combined_param =
        epix::ecs::ROSystemParam<epix::ecs::ParamSet<view_query_param, entity_query_param, system_param>>;
    using combined_state = typename combined_param::State;
    using combined_item  = typename combined_param::Item;

   public:
    explicit RenderCommandState(epix::ecs::World& world) {
        param_state = combined_param::init_state(world);
        combined_param::init_access(*param_state, meta, access, world);
    }

    void prepare(const epix::ecs::World& world) {
        params = combined_param::get_param(*param_state, meta, world, world.change_tick());
        command.prepare(world);
    }
    std::expected<void, DrawError> draw(const epix::ecs::World& world,
                                        const wgpu::RenderPassEncoder& ctx,
                                        epix::ecs::Entity view,
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
    ecs::SystemMeta meta;
    ecs::FilteredAccessSet access;
    std::optional<combined_state> param_state;
    std::optional<combined_item> params;
    command_type command;
};

/** @brief Render command that sets the cached render pipeline on the
 * encoder for a CachedRenderPipelinePhaseItem.
 * @tparam P The phase item type. */
EPIX_EXPORT template <CachedRenderPipelinePhaseItem P>
struct SetItemPipeline {
    void prepare(const epix::ecs::World&) noexcept {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   epix::ecs::Item<>,
                                                   std::optional<epix::ecs::Item<>>,
                                                   epix::ecs::ParamSet<epix::ecs::Res<PipelineServer>> params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        auto&& [pipeline_server] = params.get();
        auto pipeline            = pipeline_server->get_render_pipeline(item.pipeline());
        if (!pipeline) {
            // Bevy: ANY cache miss (not ready, invalid id, or creation failure)
            // is a Skip — the item is simply not drawn this frame
            // (mod.rs:1740-1748).
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("Render pipeline {} is not ready for item {:#x}.", item.pipeline().get(),
                                       item.entity().index),
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
    explicit RenderCommandSequence(epix::ecs::World& world)
        : m_commands([&]<size_t... I>(std::index_sequence<I...>) {
              return std::tuple<RenderCommandState<R, P>...>{((void)I, RenderCommandState<R, P>(world))...};
          }(std::index_sequence_for<R<P>...>{})) {}

    void prepare(const epix::ecs::World& world) {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (std::get<I>(m_commands).prepare(world), ...);
        }(std::index_sequence_for<R<P>...>{});
    }

    std::expected<void, DrawError> draw(const epix::ecs::World& world,
                                        const wgpu::RenderPassEncoder& cmd,
                                        epix::ecs::Entity view,
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
EPIX_EXPORT template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
DrawFunctionId app_add_render_commands(app::App& app) {
    auto& world          = app.world_mut();
    auto& draw_functions = world.resource_mut<DrawFunctions<P>>();
    return draw_functions.template add<RenderCommandSequence<P, R...>>(world);
}

/** @brief System that sorts all RenderPhase<P> components by their sort
 * keys. */
EPIX_EXPORT template <PhaseItem P>
void sort_phase_items(epix::ecs::Query<epix::ecs::Item<RenderPhase<P>&>> phases) {
    for (auto&& [phase] : phases.iter()) {
        phase.sort();
    }
}

/** @brief A type usable as a binned phase item batch-set key (Bevy
 * PhaseItemBatchSetKey trait, render_phase/mod.rs:1662). Bevy requires a
 * stable ordering/hash and an `indexed()`
 * discriminator, which selects the
 * indexed or non-indexed indirect-command layout. */
EPIX_EXPORT template <typename T>
concept PhaseItemBatchSetKey = std::equality_comparable<T> && std::totally_ordered<T> && requires(const T key) {
    { key.indexed() } -> std::convertible_to<bool>;
};

/** @brief Concept extending PhaseItem for binned (data-oriented) phases (Bevy
 * `BinnedPhaseItem`). Requires `BinKey`/`BatchSetKey` types plus `bin_key()`,
 * `batch_set_key()` and `batchable()` accessors (0.18 semantics); BatchSetKey
 * must satisfy PhaseItemBatchSetKey. */
EPIX_EXPORT template <typename P>
concept BinnedPhaseItem = PhaseItem<P> && requires(const P item) {
    typename P::BinKey;
    typename P::BatchSetKey;
    requires PhaseItemBatchSetKey<typename P::BatchSetKey>;
    { item.bin_key() } -> std::same_as<const typename P::BinKey&>;
    { item.batch_set_key() } -> std::same_as<const typename P::BatchSetKey&>;
    { item.batchable() } -> std::convertible_to<bool>;
};

/** @brief Concept for a phase item that participates in the sorted phase path
 * (Bevy `SortedPhaseItem`). `indexed()`
 * selects the correct indirect-command
 * layout when GPU preprocessing is active. */
EPIX_EXPORT template <typename P>
concept SortedPhaseItem = PhaseItem<P> && requires(const P item) {
    { item.indexed() } -> std::convertible_to<bool>;
};

/**
 * @brief How a binned phase item is batched when rendered (Bevy `BatchMode`).
 */
EPIX_EXPORT enum class BatchMode {
    /** @brief Batches consecutive items with the same bin key. */
    Sequential,
    /** @brief No batching; one item per entity. */
    PerEntity,
};

/**
 * @brief Marker component disabling automatic batching for an entity (Bevy
 * `NoAutomaticBatching`).
 */
EPIX_EXPORT struct NoAutomaticBatching {};

/**
 * @brief A distance calculator for the draw order of phase items (Bevy
 * `ViewRangefinder3d`). Computes the view-space Z of a world-space position.
 */
EPIX_EXPORT struct ViewRangefinder3d {
    /** @brief Row 2 of the view-from-world matrix. */
    glm::vec4 view_from_world_row_2 = glm::vec4(0.0f);

    /** @brief Create from a world-from-view transform (inverse is cached). */
    static ViewRangefinder3d from_world_from_view(const glm::mat4& world_from_view) noexcept {
        const glm::mat4 view_from_world = glm::inverse(world_from_view);
        // row 2 (the z row) of the column-major view matrix
        return ViewRangefinder3d{
            glm::vec4(view_from_world[0].z, view_from_world[1].z, view_from_world[2].z, view_from_world[3].z)};
    }

    /** @brief Calculates the distance (view-space Z) for the given world-space position. */
    float distance(const glm::vec3& position) const noexcept {
        return glm::dot(view_from_world_row_2, glm::vec4(position, 1.0f));
    }
};

/**
 * @brief Strongly-typed label for draw functions (Bevy `DrawFunctionLabel`).
 */
EPIX_EXPORT EPIX_MAKE_LABEL(DrawFunctionLabel);
/** @brief Interned form of `DrawFunctionLabel` (Bevy `InternedDrawFunctionLabel`). */
using InternedDrawFunctionLabel = label::Interned<DrawFunctionLabel>;

/**
 * @brief Strongly-typed label for shader imports (Bevy `ShaderLabel`).
 */
EPIX_EXPORT EPIX_MAKE_LABEL(ShaderLabel);
/** @brief Interned form of `ShaderLabel` (Bevy `InternedShaderLabel`). */
using InternedShaderLabel = label::Interned<ShaderLabel>;

/**
 * @brief A wrapper around `wgpu::RenderPassEncoder` that tracks pipeline, bind
 * group and buffer state to avoid redundant state changes, and accumulates
 * dynamic offsets (Bevy `TrackedRenderPass`).
 */
EPIX_EXPORT class TrackedRenderPass {
   public:
    /** @brief Identity of a bound buffer region: (buffer handle, offset,
     * size) (Bevy BufferSliceKey). */
    using BufferSliceKey = std::tuple<const void*, std::uint64_t, std::uint64_t>;

    /** @brief Construct with the device so the state arrays match the
     * device limits (Bevy TrackedRenderPass::new(device, pass)). */
    explicit TrackedRenderPass(const wgpu::Device& device, wgpu::RenderPassEncoder pass) : m_pass(std::move(pass)) {
        wgpu::Limits limits{};
        device.getLimits(&limits);
        m_bind_groups.resize(limits.maxBindGroups);
        m_vertex_buffers.resize(limits.maxVertexBuffers);
    }

    /** @brief Set the render pipeline. Redundant sets are skipped (Bevy
     * DrawState::set_pipeline). */
    void set_pipeline(const wgpu::RenderPipeline& pipeline) {
        const void* id = pipeline.raw();
        if (m_pipeline == id) return;
        m_pass.setPipeline(pipeline);
        m_pipeline     = id;
        m_stores_state = true;
    }

    /** @brief Bind a bind group at `index` with optional dynamic offsets.
     * Redundant binds (same group and offsets) are skipped. */
    void set_bind_group(std::uint32_t index,
                        const wgpu::BindGroup& bind_group,
                        std::span<const std::uint32_t> offsets) {
        const void* id              = bind_group.raw();
        const auto& current_offsets = index < m_bind_groups.size() ? m_bind_groups[index].second : m_empty_offsets;
        if (index < m_bind_groups.size() && m_bind_groups[index].first == id &&
            current_offsets.size() == offsets.size() &&
            std::equal(current_offsets.begin(), current_offsets.end(), offsets.begin())) {
            return;
        }
        m_pass.setBindGroup(index, bind_group, offsets);
        if (index >= m_bind_groups.size()) {
            m_bind_groups.resize(static_cast<std::size_t>(index) + 1);
        }
        m_bind_groups[index].first = id;
        m_bind_groups[index].second.assign(offsets.begin(), offsets.end());
        m_stores_state = true;
    }

    /** @brief Bind a vertex buffer at `slot`. Redundant binds are skipped. */
    void set_vertex_buffer(std::uint32_t slot,
                           const wgpu::Buffer& buffer,
                           std::uint64_t offset = 0,
                           std::uint64_t size   = 0) {
        const BufferSliceKey key{buffer.raw(), offset, size};
        if (slot < m_vertex_buffers.size() && m_vertex_buffers[slot] == key) return;
        m_pass.setVertexBuffer(slot, buffer, offset, size);
        if (slot >= m_vertex_buffers.size()) {
            m_vertex_buffers.resize(static_cast<std::size_t>(slot) + 1);
        }
        m_vertex_buffers[slot] = key;
        m_stores_state         = true;
    }

    /** @brief Bind an index buffer. Redundant binds (same slice and format)
     * are skipped. */
    void set_index_buffer(const wgpu::Buffer& buffer,
                          wgpu::IndexFormat format,
                          std::uint64_t offset = 0,
                          std::uint64_t size   = 0) {
        const BufferSliceKey key{buffer.raw(), offset, size};
        if (m_index_buffer && m_index_buffer->first == key && m_index_buffer->second == format) return;
        m_pass.setIndexBuffer(buffer, format, offset, size);
        m_index_buffer = std::pair<BufferSliceKey, wgpu::IndexFormat>{key, format};
        m_stores_state = true;
    }

    /** @brief Issue a non-indexed draw (Bevy draw(vertices: Range<u32>,
     * instances: Range<u32>)). */
    void draw(std::pair<std::uint32_t, std::uint32_t> vertices, std::pair<std::uint32_t, std::uint32_t> instances) {
        m_pass.draw(vertices.second - vertices.first, instances.second - instances.first, vertices.first,
                    instances.first);
    }

    /** @brief Issue an indexed draw (Bevy draw_indexed(indices: Range<u32>,
     * base_vertex, instances: Range<u32>)). */
    void draw_indexed(std::pair<std::uint32_t, std::uint32_t> indices,
                      std::int32_t base_vertex,
                      std::pair<std::uint32_t, std::uint32_t> instances) {
        m_pass.drawIndexed(indices.second - indices.first, instances.second - instances.first, indices.first,
                           base_vertex, instances.first);
    }

    /** @brief Push a debug group. */
    void push_debug_group(std::string_view label) { m_pass.pushDebugGroup(wgpu::StringView(label.data())); }
    /** @brief Pop a debug group. */
    void pop_debug_group() { m_pass.popDebugGroup(); }
    /** @brief Insert a debug marker (Bevy TrackedRenderPass::insert_debug_marker). */
    void insert_debug_marker(std::string_view label) { m_pass.insertDebugMarker(wgpu::StringView(label.data())); }

    /** @brief Set the stencil reference value (Bevy
     * TrackedRenderPass::set_stencil_reference). */
    void set_stencil_reference(std::uint32_t reference) { m_pass.setStencilReference(reference); }

    /** @brief Set the scissor rectangle (Bevy
     * TrackedRenderPass::set_scissor_rect). */
    void set_scissor_rect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
        m_pass.setScissorRect(x, y, width, height);
    }

    /** @brief Set the viewport (Bevy TrackedRenderPass::set_viewport). */
    void set_viewport(float x, float y, float width, float height, float min_depth, float max_depth) {
        m_pass.setViewport(x, y, width, height, min_depth, max_depth);
    }

    /** @brief Set the blend constant color (Bevy
     * TrackedRenderPass::set_blend_constant). */
    void set_blend_constant(wgpu::Color color) { m_pass.setBlendConstant(color); }

    /** @brief Set push constants (Bevy TrackedRenderPass::set_push_constants). */
    void set_push_constants(wgpu::ShaderStage stages, std::uint32_t offset, const void* data, std::size_t size) {
        m_pass.setPushConstants(stages, offset, static_cast<std::uint32_t>(size), data);
    }

    /** @brief Issue an indirect draw (Bevy
     * TrackedRenderPass::draw_indirect). */
    void draw_indirect(const wgpu::Buffer& indirect_buffer, std::uint64_t indirect_offset) {
        m_pass.drawIndirect(indirect_buffer, indirect_offset);
    }

    /** @brief Issue an indexed indirect draw (Bevy
     * TrackedRenderPass::draw_indexed_indirect). */
    void draw_indexed_indirect(const wgpu::Buffer& indirect_buffer, std::uint64_t indirect_offset) {
        m_pass.drawIndexedIndirect(indirect_buffer, indirect_offset);
    }

    /** @brief Issue multiple non-indexed indirect draws (Bevy
     * `TrackedRenderPass::multi_draw_indirect`). */
    void multi_draw_indirect(const wgpu::Buffer& indirect_buffer, std::uint64_t indirect_offset, std::uint32_t count) {
        m_pass.multiDrawIndirect(indirect_buffer, indirect_offset, count);
    }

    /** @brief Issue multiple indexed indirect draws (Bevy
     * `TrackedRenderPass::multi_draw_indexed_indirect`). */
    void multi_draw_indexed_indirect(const wgpu::Buffer& indirect_buffer,
                                     std::uint64_t indirect_offset,
                                     std::uint32_t count) {
        m_pass.multiDrawIndexedIndirect(indirect_buffer, indirect_offset, count);
    }

    /** @brief Issue non-indexed indirect draws whose count is held in a GPU
     * buffer (Bevy
     * `multi_draw_indirect_count`). */
    void multi_draw_indirect_count(const wgpu::Buffer& indirect_buffer,
                                   std::uint64_t indirect_offset,
                                   const wgpu::Buffer& count_buffer,
                                   std::uint64_t count_buffer_offset,
                                   std::uint32_t max_count) {
        m_pass.multiDrawIndirectCount(indirect_buffer, indirect_offset, count_buffer, count_buffer_offset, max_count);
    }

    /** @brief Indexed counterpart of `multi_draw_indirect_count` (Bevy
     * `multi_draw_indexed_indirect_count`). */
    void multi_draw_indexed_indirect_count(const wgpu::Buffer& indirect_buffer,
                                           std::uint64_t indirect_offset,
                                           const wgpu::Buffer& count_buffer,
                                           std::uint64_t count_buffer_offset,
                                           std::uint32_t max_count) {
        m_pass.multiDrawIndexedIndirectCount(indirect_buffer, indirect_offset, count_buffer, count_buffer_offset,
                                             max_count);
    }

    /** @brief Access the underlying encoder (e.g. to end the pass).
     * Invalidates internal tracking state (Bevy wgpu_pass). */
    wgpu::RenderPassEncoder& pass() noexcept {
        reset_tracking();
        return m_pass;
    }

   private:
    /** @brief Clear all tracked state (Bevy DrawState::reset_tracking). */
    void reset_tracking() noexcept {
        if (!m_stores_state) return;
        m_pipeline = nullptr;
        for (auto& [group, offsets] : m_bind_groups) {
            (void)group;
            group = nullptr;
            offsets.clear();
        }
        for (auto& buffer : m_vertex_buffers) {
            buffer.reset();
        }
        m_index_buffer.reset();
        m_stores_state = false;
    }

    wgpu::RenderPassEncoder m_pass;
    /** @brief Currently bound pipeline handle (Bevy DrawState::pipeline). */
    const void* m_pipeline = nullptr;
    /** @brief Per-index (bind group handle, dynamic offsets). */
    std::vector<std::pair<const void*, std::vector<std::uint32_t>>> m_bind_groups;
    /** @brief Empty offsets fallback for out-of-range comparisons. */
    std::vector<std::uint32_t> m_empty_offsets;
    /** @brief Per-slot vertex buffer slice identity. */
    std::vector<std::optional<BufferSliceKey>> m_vertex_buffers;
    /** @brief Bound index buffer slice identity + format. */
    std::optional<std::pair<BufferSliceKey, wgpu::IndexFormat>> m_index_buffer;
    /** @brief True when any state is tracked (Bevy stores_state). */
    bool m_stores_state = false;
};

}  // namespace epix::render::phase

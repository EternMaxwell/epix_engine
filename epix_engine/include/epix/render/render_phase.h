#pragma once

#include "common.h"
#include "graph.h"
#include "pipeline.h"
#include "view.h"

namespace epix::render::render_phase {
struct DrawFunctionId {
    uint32_t id;
    template <typename T>
        requires(!std::same_as<std::decay_t<T>, DrawFunctionId>)
    DrawFunctionId(T&& t) : id(static_cast<uint32_t>(t)) {}
    DrawFunctionId() : id(0) {}
    auto operator<=>(const DrawFunctionId&) const = default;
    bool operator==(const DrawFunctionId&) const  = default;
    operator uint32_t() const { return id; }
};
struct DrawContext {
    nvrhi::CommandListHandle commandlist;
    nvrhi::FramebufferHandle framebuffer;

    operator nvrhi::CommandListHandle() const { return commandlist; }
    operator nvrhi::FramebufferHandle() const { return framebuffer; }
};

template <typename T>
concept PhaseItem = requires(const T item) {
    // the entity associated with this item
    { item.entity() } -> std::same_as<app::Entity>;
    // the sort key for this item, the smaller the key, the earlier it is rendered
    { item.sort_key() } -> std::three_way_comparable;
    // the draw function index for this item
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
};

template <typename P>
concept BatchedPhaseItem = PhaseItem<P> && requires(const P item) {
    // batch size/count for this item
    { P::batch_size() } -> std::convertible_to<size_t>;
};

template <typename P>
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(const P item) {
    // the pipeline cache key for this item
    { item.pipeline() } -> std::convertible_to<RenderPipelineId>;
};

template <typename FuncT, typename P>
concept Draw = PhaseItem<P> && requires(FuncT func, World& world, DrawContext ctx, Entity view, P& item) {
    { func.prepare(world) };
    // draw function, since DrawContext can be converted to nvrhi::CommandListHandle, the function can also be
    // void draw(World&, nvrhi::CommandListHandle, Entity, P&)
    { func.draw(world, ctx, view, item) };
};

template <PhaseItem P>
struct DrawFunction {
    virtual void prepare(World& world) {}
    virtual void draw(World& world, DrawContext ctx, Entity view, P& item) = 0;

    virtual ~DrawFunction() = default;
};

template <PhaseItem P, Draw<P> Func>
struct DrawFunctionImpl : DrawFunction<P> {
   public:
    template <typename... Args>
    DrawFunctionImpl(Args&&... args) : m_func(std::forward<Args>(args)...) {}

    void prepare(World& world) override { m_func.prepare(world); }

    void draw(World& world, DrawContext cmd, Entity view, P& item) override { m_func.draw(world, cmd, view, item); }

   private:
    Func m_func;
};

template <PhaseItem P>
struct EmptyDrawFunction : DrawFunction<P> {
    void draw(World&, DrawContext, Entity, P&) override {}
};

template <PhaseItem P>
struct DrawFunctions {
   public:
    void prepare(World& world) {
        for (auto&& func : m_functions) {
            func->prepare(world);
        }
    }

    template <Draw<P> T, typename... Args>
    DrawFunctionId add(Args&&... args) {
        return _add_function<T>(std::forward<Args>(args)...);
    }
    template <typename Func>
        requires Draw<P, std::decay_t<Func>>
    DrawFunctionId add(Func&& func) {
        return _add_function<std::decay_t<Func>>(std::forward<Func>(func));
    }

    DrawFunction<P>* get(DrawFunctionId id) {
        if (id.id >= m_functions.size()) {
            return nullptr;
        }
        return m_functions[id.id].get();
    }
    template <Draw<P> T = EmptyDrawFunction<P>>
    std::optional<DrawFunctionId> get_id(const epix::meta::type_index& type = epix::meta::type_id<T>()) {
        if (auto it = m_indices.find(type); it != m_indices.end()) {
            return DrawFunctionId(it->second);
        }
        return std::nullopt;
    }

   private:
    std::vector<std::unique_ptr<DrawFunction<P>>> m_functions;
    entt::dense_map<epix::meta::type_index, uint32_t> m_indices;

    template <Draw<P> T, typename... Args>
        requires std::constructible_from<T, Args...>
    DrawFunctionId _add_function(Args&&... args) {
        epix::meta::type_index type = epix::meta::type_id<T>();
        if (auto it = m_indices.find(type); it != m_indices.end()) {
            return DrawFunctionId(it->second);
        }
        auto index = static_cast<uint32_t>(m_functions.size());
        auto func  = std::make_unique<DrawFunctionImpl<P, T>>(std::forward<Args>(args)...);
        m_functions.emplace_back(std::move(func));
        m_indices.emplace(type, index);
        return DrawFunctionId(index);
    }
};

template <PhaseItem T>
struct RenderPhase {
   public:
    using SortKey = decltype(std::declval<T>().sort_key());

   public:
    std::vector<T> items;

   public:
    void add(const T& item) { items.push_back(item); }
    void add(T&& item) { items.emplace_back(std::move(item)); }

    void sort() {
        if constexpr (_item_has_own_sort_fn) {
            T::sort(items);
        } else {
            // sort using sort key
            std::sort(items.begin(), items.end(),
                      [this](const T& a, const T& b) { return _item_sort_key(a) < _item_sort_key(b); });
        }
    }

    auto iter_entities() const {
        return items | std::views::transform([this](const T& item) { return _item_entity(item); });
    }

    void render(DrawContext cmd, World& world, Entity view) { render_range(cmd, world, view, 0, items.size()); }
    void render_range(
        DrawContext cmd, World& world, Entity view, size_t start = 0, size_t end = std::numeric_limits<size_t>::max()) {
        end = std::min(end, items.size());
        if (start >= end) return;

        auto&& draw_functions = world.resource<DrawFunctions<T>>();
        draw_functions.prepare(world);
        for (size_t i = start; i < end; i += _item_batch_size(items[i])) {
            auto& item = items[i];
            if (auto draw_function = draw_functions.get(_item_draw_function(item)); draw_function) {
                draw_function->draw(world, cmd, _item_entity(item), item);
            } else {
                spdlog::warn("Draw function {} not found for item {:#x}. Skipping.",
                             static_cast<uint32_t>(_item_draw_function(item)), _item_entity(item).index());
            }
        }
    }

   private:
    Entity _item_entity(const T& item) const { return item.entity(); }
    SortKey _item_sort_key(const T& item) const { return item.sort_key(); }
    DrawFunctionId _item_draw_function(const T& item) const { return item.draw_function(); }
    size_t _item_batch_size(const T& item) const {
        if constexpr (BatchedPhaseItem<T>) {
            return T::batch_size();
        } else {
            return 1;
        }
    }

   private:
    static constexpr bool _item_has_own_sort_fn = requires(T item) { T::sort(items); };
};

template <typename T>
concept is_member_function_pointer = std::is_member_function_pointer_v<T>;
template <typename T>
concept MaybeRenderCommand = requires {
    { &T::render } -> is_member_function_pointer;
};
template <typename F>
struct member_function_pointer_traits;
template <typename R, typename C, typename... Args>
struct member_function_pointer_traits<R (C::*)(Args...)> {
    using class_type  = C;
    using return_type = R;
    using arg_types   = std::tuple<Args...>;
};
using RenderCommandResult = void;
template <template <typename> typename RenderCommand, PhaseItem P>
struct RenderCommandInfo {
    using T           = RenderCommand<P>;
    using traits      = member_function_pointer_traits<decltype(&T::render)>;
    using class_type  = typename traits::class_type;
    using return_type = typename traits::return_type;
    using arg_types   = typename traits::arg_types;

    static_assert(std::same_as<return_type, RenderCommandResult>, "Render command must return void");
    static_assert(std::tuple_size_v<arg_types> == 5, "Render command must have exactly 5 parameters.");
    static_assert(std::same_as<std::tuple_element_t<0, arg_types>, const P&> &&
                      std::same_as<std::tuple_element_t<4, arg_types>, nvrhi::CommandListHandle>,
                  "The first parameter of render command must be const T&, and the last parameter must be "
                  "nvrhi::CommandListHandle.");
    static_assert(epix::util::type_traits::specialization_of<std::tuple_element_t<1, arg_types>, epix::Item>,
                  "The second parameter of render command must be of type epix::Item<>, for view entity data.");
    static_assert(std::tuple_element_t<1, arg_types>::readonly,
                  "The second parameter of render command must be of type epix::Item<> with readonly access.");
    static_assert(epix::util::type_traits::specialization_of<std::tuple_element_t<2, arg_types>, std::optional>,
                  "The third parameter of render command must be of type std::optional<> for entity data.");
    static_assert(
        epix::util::type_traits::specialization_of<typename std::tuple_element_t<2, arg_types>::value_type, epix::Item>,
        "The third parameter of render command must be of type std::optional<epix::Item<...>> for entity data.");
    static_assert(
        std::tuple_element_t<2, arg_types>::value_type::readonly,
        "The third parameter of render command must be of type std::optional<epix::Item<...>> with readonly access.");
    static_assert(epix::app::ValidParam<std::remove_reference_t<std::tuple_element_t<3, arg_types>>>,
                  "The fourth parameter of render command must be a valid system parameter.");

    using view_item    = std::tuple_element_t<1, arg_types>;
    using entity_item  = typename std::tuple_element_t<2, arg_types>::value_type;
    using system_param = std::remove_reference_t<std::tuple_element_t<3, arg_types>>;
};
template <template <typename> typename R, typename P>
concept RenderCommand = requires {
    requires PhaseItem<P>;
    requires MaybeRenderCommand<R<P>>;
    requires std::is_default_constructible_v<R<P>>;
    typename RenderCommandInfo<R, P>::class_type;
};
template <template <typename> typename R, PhaseItem P>
    requires RenderCommand<R, P>
struct RenderCommandState {
    using view_query_t   = Query<typename RenderCommandInfo<R, P>::view_item>;
    using entity_query_t = Query<typename RenderCommandInfo<R, P>::entity_item>;
    using system_param_t = typename app::SystemParam<typename RenderCommandInfo<R, P>::system_param>;
    using param_state_t  = typename system_param_t::State;

   private:
    using T = R<P>;
    union {
        view_query_t view_query;
        entity_query_t entity_query;
        param_state_t system_param_state;
    };
    system_param_t system_param;
    app::SystemMeta meta;
    T command;
    bool inited = false;

   public:
    RenderCommandState() : command() {}
    ~RenderCommandState() {
        if (inited) {
            // destruct old state
            view_query.~Query<typename RenderCommandInfo<R, P>::view_item>();
            entity_query.~Query<typename RenderCommandInfo<R, P>::entity_item>();
            system_param.~SystemParam<typename RenderCommandInfo<R, P>::system_param>();
        }
        inited = false;
    }
    void prepare(World& world) {
        if (inited) {
            // destruct old state
            view_query.~view_query_t();
            entity_query.~entity_query_t();
            system_param.~system_param_t();
        }
        // construct new state
        new (&view_query) view_query_t(world);
        new (&entity_query) entity_query_t(world);
        new (&system_param) system_param_t();
        new (&system_param_state) param_state_t(system_param.init(world, meta));
        system_param.update(system_param_state, world, meta);
        inited = true;
    }
    void draw(World& world, DrawContext ctx, Entity view, P& item) {
        if (!inited) {
            throw std::runtime_error("Render command state is not initialized.");
        }
        auto view_data   = view_query.get(view);
        auto entity_data = entity_query.try_get(item.entity());
        auto& param      = system_param.get(system_param_state);
        command.render(item, view_data, entity_data, param, ctx);
    }
};

template <PhaseItem P>
    requires CachedRenderPipelinePhaseItem<P>
struct SetItemPipeline {
    void render(const P& item,
                const Item<>&,
                const std::optional<Item<>>&,
                ParamSet<Res<render::PipelineServer>>& pipelines,
                DrawContext ctx) {
        auto&& [server] = pipelines.get();
        server->get_render_pipeline(item.pipeline(), ctx.framebuffer).transform([&](RenderPipeline& pipeline) {
            ctx.commandlist->setGraphicsState(nvrhi::GraphicsState().setPipeline(pipeline.handle));
        });
    }
};

template <PhaseItem P, template <typename> typename... R>
    requires((RenderCommand<R, P> && ...))
struct RenderCommandSequence : DrawFunction<P> {
   public:
    RenderCommandSequence() : m_commands(RenderCommandState<R, P>()...) {}

    void prepare(World& world) override {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (std::get<I>(m_commands).prepare(world), ...);
        }(std::index_sequence_for<R<P>...>{});
    }

    void draw(World& world, DrawContext cmd, Entity view, P& item) override {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (std::get<I>(m_commands).draw(world, cmd, view, item), ...);
        }(std::index_sequence_for<R<P>...>{});
    }

   private:
    std::tuple<RenderCommandState<R, P>...> m_commands;
};
template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
DrawFunctionId get_or_add_render_commands(DrawFunctions<P>& draw_functions) {
    return draw_functions.template get_id<RenderCommandSequence<P, R...>>().value_or(
        draw_functions.template add<RenderCommandSequence<P, R...>>());
}
template <PhaseItem P, template <typename> typename... R>
    requires(RenderCommand<R, P> && ...)
DrawFunctionId add_render_commands(DrawFunctions<P>& draw_functions) {
    // Or maybe handle duplicated adds here?
    return draw_functions.template add(RenderCommandSequence<P, R...>{});
}
}  // namespace epix::render::render_phase
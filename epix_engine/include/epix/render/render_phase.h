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

template <typename T>
concept PhaseItem = requires(T item) {
    // the entity associated with this item
    { item.entity() } -> std::same_as<app::Entity>;
    // the sort key for this item, the smaller the key, the earlier it is rendered
    { item.sort_key() } -> std::three_way_comparable;
    // the draw function index for this item
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
};

template <typename P>
concept BatchedPhaseItem = PhaseItem<P> && requires(P item) {
    // batch size/count for this item
    { P::batch_size() } -> std::convertible_to<size_t>;
};

template <typename P>
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(P item) {
    // the pipeline cache key for this item
    { item.pipeline() } -> std::same_as<RenderPipelineId>;
};

template <typename FuncT, typename P>
concept Draw =
    PhaseItem<P> && requires(FuncT func, World& world, nvrhi::CommandListHandle cmd, Entity entity, P& item) {
        { func.prepare(world) };
        { func.draw(world, cmd, entity, item) };
    };

template <PhaseItem P>
struct DrawFunction {
    virtual void prepare(World& world) {}
    virtual void draw(World& world, nvrhi::CommandListHandle cmd, Entity entity, P& item) = 0;

    virtual ~DrawFunction() = default;
};

template <PhaseItem P, Draw<P> Func>
struct DrawFunctionImpl : DrawFunction<P> {
   public:
    template <typename... Args>
    DrawFunctionImpl(Args&&... args) : m_func(std::forward<Args>(args)...) {}

    void prepare(World& world) override { m_func.prepare(world); }

    void draw(World& world, nvrhi::CommandListHandle cmd, Entity entity, P& item) override {
        m_func.draw(world, cmd, entity, item);
    }

   private:
    Func m_func;
};

template <PhaseItem P>
struct EmptyDrawFunction : DrawFunction<P> {
    void draw(World&, nvrhi::CommandListHandle, Entity, P&) override {}
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
        auto index                  = static_cast<uint32_t>(m_functions.size());
        auto func                   = std::make_unique<DrawFunctionImpl<P, T>>(std::forward<Args>(args)...);
        epix::meta::type_index type = epix::meta::type_id<T>();
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

    void render(nvrhi::CommandListHandle cmd, World& world, Entity view) {
        render_range(cmd, world, view, 0, items.size());
    }
    void render_range(nvrhi::CommandListHandle cmd,
                      World& world,
                      Entity view,
                      size_t start = 0,
                      size_t end   = std::numeric_limits<size_t>::max()) {
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
}  // namespace epix::render::render_phase
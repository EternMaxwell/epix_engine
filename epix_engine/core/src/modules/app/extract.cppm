module;

export module epix.core:app.extract;

import std;
import epix.meta;

import :world;
import :system;
import :tick;
import :query;
import :ticks;

namespace core {
template <system_param T>
struct Extract : public T {
   public:
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit Extract(Args&&... args) : T(std::forward<Args>(args)...) {}

    Extract(const Extract&)            = default;
    Extract(Extract&&)                 = default;
    Extract& operator=(const Extract&) = default;
    Extract& operator=(Extract&&)      = default;
};

struct ExtractedWorld {
    std::reference_wrapper<World> world;
};
template <typename T>
struct SystemParam<Extract<T>> : SystemParam<T> {
    using Base  = SystemParam<T>;
    using State = typename Base::State;
    using Item  = Extract<typename Base::Item>;

    static State init_state(World& world) { return Base::init_state(world.resource_mut<ExtractedWorld>().world); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world) {
        // This is a workaround to initialize access for ensuring no conflicts. But the access should be separated for
        // each world. Affects some performance but for readonly extract it is zero-cost.
        Base::init_access(state, meta, access, world.resource<ExtractedWorld>().world);
        SystemMeta temp;
        FilteredAccessSet temp_access;
        Base::init_access(state, temp, temp_access, world.resource<ExtractedWorld>().world);
        if (temp.is_deferred())
            throw std::runtime_error(
                std::format("Extract<T> with deferred param T=[{}] is not allowed.", meta::type_id<T>::short_name()));
    }
    static void apply(State& state, const SystemMeta& meta, World& world) {}
    static void queue(State& state, const SystemMeta& meta, DeferredWorld deferred_world) {}
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta& meta, World& world) {
        return Base::validate_param(state, meta, world.resource_mut<ExtractedWorld>().world);
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        // return Item(Base::get_param(state, meta, world.resource_mut<ExtractedWorld>().world, tick));
        SystemMeta temp;
        temp.flags            = meta.flags;
        auto& extracted_world = world.resource_mut<ExtractedWorld>().world.get();
        temp.last_run         = extracted_world.last_change_tick();
        return Item(Base::get_param(state, temp, extracted_world, extracted_world.change_tick()));
    }
};
static_assert(system_param<Extract<ResMut<int>>>);
}  // namespace core
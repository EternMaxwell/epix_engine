#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cstdint>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#endif

#include <epix/ecs/component/register.hpp>
#include <epix/ecs/component/removal_detection.hpp>
#include <epix/ecs/system/param.hpp>
#include <epix/ecs/world/detail/access.hpp>

namespace epix::ecs {

/** @brief Persistent cursor used by RemovedComponents<T>. */
EPIX_EXPORT template <typename T>
struct RemovedComponentReader {
    EventCursor<RemovedComponentEntity> reader;
};

/**
 * @brief System parameter that reads entities whose T component was removed or
 * whose entity was despawned.
 *
 * Like Bevy's RemovedComponents, every system owns an independent reader cursor.
 * Reading consumes events only for that system; it does not clear the World stream.
 */
EPIX_EXPORT template <typename T>
struct RemovedComponents {
   private:
    TypeId component_id_;
    RemovedComponentReader<T>* reader_;
    const RemovedComponentEvents* events_;

    RemovedComponents(TypeId component_id,
                      RemovedComponentReader<T>& reader,
                      const RemovedComponentEvents& events) noexcept
        : component_id_(component_id), reader_(&reader), events_(&events) {
        clamp_cursor();
    }

    const Events<RemovedComponentEntity>* event_stream() const noexcept {
        return events_->get(component_id_)
            .transform([](auto ref) { return std::addressof(ref.get()); })
            .value_or(nullptr);
    }

    void clamp_cursor() noexcept {
        if (auto* stream = event_stream()) {
            reader_->reader.index = std::clamp(reader_->reader.index, stream->head(), stream->tail());
        }
    }

    friend struct SystemParam<RemovedComponents<T>>;

   public:
    /** Iterate unread removed entities and advance this system's cursor. */
    auto read() {
        auto* stream              = event_stream();
        const std::uint32_t begin = reader_->reader.index;
        const std::uint32_t end   = stream ? stream->tail() : begin;
        return std::views::iota(begin, end) | std::views::transform([this, stream](std::uint32_t index) {
                   reader_->reader.index++;
                   return stream->get(index)->entity;
               });
    }

    /** Iterate unread removed entities together with their event IDs. */
    auto read_with_id() {
        auto* stream              = event_stream();
        const std::uint32_t begin = reader_->reader.index;
        const std::uint32_t end   = stream ? stream->tail() : begin;
        return std::views::iota(begin, end) | std::views::transform([this, stream](std::uint32_t index) {
                   reader_->reader.index++;
                   return std::tuple<Entity, std::uint32_t>(stream->get(index)->entity, index);
               });
    }

    std::uint32_t len() const noexcept {
        auto* stream = event_stream();
        return stream ? stream->tail() - reader_->reader.index : 0;
    }
    std::uint32_t size() const noexcept { return len(); }
    bool is_empty() const noexcept { return len() == 0; }
    bool empty() const noexcept { return is_empty(); }
    void clear() noexcept {
        if (auto* stream = event_stream()) reader_->reader.index = stream->tail();
    }

    const RemovedComponentReader<T>& reader() const noexcept { return *reader_; }
    RemovedComponentReader<T>& reader_mut() noexcept { return *reader_; }
    std::optional<std::reference_wrapper<const Events<RemovedComponentEntity>>> events() const noexcept {
        return events_->get(component_id_);
    }
};

template <typename T>
struct SystemParam<RemovedComponents<T>> : ParamBase {
    struct State {
        TypeId component_id;
        RemovedComponentReader<T> reader;
    };

    using Item                     = RemovedComponents<T>;
    static constexpr bool readonly = true;

    static State init_state(World& world) {
        return State{.component_id = internal::world_registrator(world).template register_component<T>()};
    }

    static Item get_param(State& state, const SystemMeta&, World& world, Tick) noexcept {
        return Item(state.component_id, state.reader, internal::world_removed_components(world));
    }
};

static_assert(system_param<RemovedComponents<int>>);

}  // namespace epix::ecs

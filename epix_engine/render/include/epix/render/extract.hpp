#pragma once

#include "schedule.hpp"

namespace epix::render {
inline struct ExtractScheduleT {
} ExtractSchedule;
template <std::copyable T>
struct ExtractResourcePlugin {
    static void extract_fn(Commands cmd, ParamSet<std::optional<ResMut<T>>, Extract<ResMut<T>>> resources) {
        auto&& [res, extract] = resources.get();
        if (!res) {
            cmd.insert_resource(extract.get());
        } else if (extract.is_modified()) {
            res.value().get_mut() = extract.get();
        }
    }
    void build(App& app) {
        app.sub_app_mut(Render).add_systems(
            ExtractSchedule,
            into(extract_fn).set_name(std::format("extract resource '{}'", meta::type_id<T>().short_name())));
    }
};
/// A helper marker to tell that a entity has a custom rendering process instead of handled by the engine. This is only
/// for standard rendering process. For non-standard rendering processes, user is expected to use other markers provided
/// by those modules providing the rendering process.
struct CustomRendered {};
}  // namespace epix::render
#pragma once

#include "schedule.hpp"

namespace epix::render {
inline struct ExtractScheduleT {
} ExtractSchedule;
template <std::copyable T>
struct ExtractResourcePlugin {
    static void extract_fn(Commands cmd, std::optional<ResMut<T>> res, Extract<ResMut<T>> extract) {
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
}  // namespace epix::render
export module epix.render:extract;

import epix.core;
import std;

import :schedule;

namespace epix::render {
using namespace epix::core;
/** @brief Schedule sentinel for the extract phase that copies data from
 * the main world into the render world. */
export struct ExtractScheduleT {
} ExtractSchedule;
template <std::copyable T>
void extract_fn(Commands cmd, ParamSet<std::optional<ResMut<T>>, Extract<ResMut<T>>> resources) {
    auto&& [res, extract] = resources.get();
    if (!res) {
        cmd.insert_resource(extract.get());
    } else if (extract.is_modified()) {
        res.value().get_mut() = extract.get();
    }
}
/** @brief Plugin that extracts a copyable resource from the main world
 * into the render world each frame.
 * @tparam T A copyable resource type. */
export template <std::copyable T>
struct ExtractResourcePlugin {
    void build(App& app) {
        app.sub_app_mut(Render).add_systems(
            ExtractSchedule,
            into(extract_fn<T>).set_name(std::format("extract resource '{}'", meta::type_id<T>().short_name())));
    }
};
/** @brief Marker component indicating an entity has a custom rendering
 * process and should be skipped by standard render pipelines. */
export struct CustomRendered {};
}  // namespace render
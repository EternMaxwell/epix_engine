#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <format>
#include <optional>
#endif

#ifndef EPIX_CXX_MODULE
#include <epix/core.hpp>
#endif
#ifndef EPIX_CXX_MODULE
#include <epix/meta.hpp>
#endif
#include <epix/render/schedule.hpp>

namespace epix::render {
/** @brief Schedule sentinel for the extract phase that copies data from
 * the main world into the render world. */
EPIX_EXPORT inline struct ExtractScheduleT {
} ExtractSchedule;
template <std::copyable T>
void extract_fn(
    epix::core::Commands cmd,
    epix::core::ParamSet<std::optional<epix::core::ResMut<T>>, epix::core::Extract<epix::core::ResMut<T>>> resources) {
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
EPIX_EXPORT template <std::copyable T>
struct ExtractResourcePlugin {
    void attach(epix::core::App& app) {
        app.sub_app_mut(Render).add_systems(
            ExtractSchedule,
            into(extract_fn<T>).set_name(std::format("extract resource '{}'", meta::type_id<T>().short_name())));
    }
};
/** @brief Marker component indicating an entity has a custom rendering
 * process and should be skipped by standard render pipelines. */
EPIX_EXPORT struct CustomRendered {};
}  // namespace epix::render
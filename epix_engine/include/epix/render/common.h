#pragma once

#include <epix/app.h>
#include <epix/common.h>

namespace epix::render {
inline struct ExtractScheduleT {
} ExtractSchedule;
struct RenderT {
    EPIX_API static Schedule render_schedule();
};
inline RenderT Render;

enum class RenderSet {
    PostExtract,
    PrepareAssets,
    ManageViews,
    Queue,
    PhaseSort,
    Prepare,
    PrepareResources,
    PrepareFlush,
    PrepareSets,
    Render,
    Cleanup,
};

template <typename ResT>
struct ExtractResourcePlugin {
    void build(App& app) {
        if (auto render_app = app.get_sub_app(Render)) {
            render_app->add_systems(ExtractSchedule,
                                    into([](Commands& cmd, std::optional<ResMut<ResT>> render_res,
                                            Extract<std::optional<Res<ResT>>> extracted_res) {
                                        if (extracted_res) {
                                            if (render_res) {
                                                (*render_res).get() = **extracted_res;
                                            } else {
                                                cmd.insert_resource(**extracted_res);
                                            }
                                        }
                                    }).set_name(std::format("extract resource<{}>", typeid(ResT).name())));
        }
    }
};
}  // namespace epix::render
#include "epix/input.h"
#include "epix/render.h"
#include "epix/render/window.h"
#include "epix/utils/time.h"
#include "epix/window.h"

void test_func() { std::cout << "Test function called!" << std::endl; }

int main() {
    if (glfwInit() == GLFW_FALSE) {
        return -1;
    }
    glfwDefaultWindowHints();

    using namespace epix::window;
    using namespace epix::glfw;
    using namespace epix::window::window;

    Window window_desc;
    Window window_desc2;

    window_desc.title = "Test Window";

    window_desc.set_size(800, 200);
    window_desc.opacity      = 0.5f;
    window_desc.present_mode = PresentMode::Fifo;

    window_desc2.title = "Test Window 2";

    window_desc2.set_size(800, 400);
    window_desc2.opacity      = 0.7f;
    window_desc2.present_mode = PresentMode::Fifo;

    epix::App app = epix::App::create(epix::AppConfig{
        // .mark_frame = true,
        // .enable_tracy = true,
    });

    struct FrameCounter {
        int count = 0;
    };

    // app.world(epix::app::MainWorld).spawn(window_desc2);
    app.add_plugins(epix::window::WindowPlugin{})
        .plugin_scope([&](epix::window::WindowPlugin& plugin) {
            plugin.exit_condition = epix::window::ExitCondition::OnAllClosed;
            plugin.primary_window = window_desc;
        })
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::render::RenderPlugin{})
        .add_systems(
            epix::Update,
            epix::into(epix::input::print_inputs, epix::window::print_events)
                .set_name("print inputs")
        )
        .add_systems(
            epix::Update,
            epix::into(
                [](epix::EventReader<epix::input::KeyInput> key_reader,
                   epix::Query<epix::Get<epix::window::Window>> windows,
                   epix::ResMut<epix::Schedules> schedules) {
                    for (auto&& [key, scancode, pressed, repeat, window] :
                         key_reader.read()) {
                        if (pressed) {
                            if (key == epix::input::KeyCode::KeyF11) {
                                if (auto window_opt = windows.get(window)) {
                                    auto&& [window_desc] = *window_opt;
                                    if (window_desc.mode ==
                                        epix::window::window::WindowMode::
                                            Windowed) {
                                        window_desc.mode = epix::window::
                                            window::WindowMode::Fullscreen;
                                    } else {
                                        window_desc.mode = epix::window::
                                            window::WindowMode::Windowed;
                                    }
                                }
                            }
                            if (key == epix::input::KeyCode::KeySpace) {
                                if (auto schedule =
                                        schedules->get(epix::Update)) {
                                    static bool exist = false;
                                    if (!exist) {
                                        exist = true;
                                        schedule->add_systems(
                                            epix::into(test_func).set_name(
                                                "test function"
                                            )
                                        );
                                    } else {
                                        schedule->remove_system(test_func);
                                        exist = false;
                                    }
                                }
                            }
                        }
                    }
                },
                [](epix::Res<epix::AppProfiler> profiler,
                   epix::Local<std::optional<epix::utils::time::Timer>> timer) {
                    if (!timer->has_value()) {
                        *timer = epix::utils::time::Timer::repeat(1.0);
                    }
                    if (timer->value().tick()) {
                        spdlog::info(
                            "Frame time: {:9.5f}ms; FPS: {:7.2f}",
                            profiler->time_avg(), 1000.0 / profiler->time_avg()
                        );
                        for (auto&& [label, profiler] :
                             profiler->schedule_profilers()) {
                            spdlog::info(
                                "Schedule {:<40}: flush: {:9.5f}ms, build: "
                                "{:9.5f}ms, "
                                "prepare: {:9.5f}ms, run: {:9.5f}ms, with {:3} "
                                "systems, {:3} sets",
                                label.name(), profiler.flush_time_avg(),
                                profiler.build_time_avg(),
                                profiler.prepare_time_avg(),
                                profiler.run_time_avg(),
                                profiler.system_count(), profiler.set_count()
                            );
                        }
                    }
                }
            ).set_names({"toggle fullscreen", "print profiling info"})
        );
    // app.add_systems(
    //     epix::Update, epix::into([](epix::Local<FrameCounter> count) {
    //         std::cout << "Frame: " << count->count++ << std::endl;
    //     })
    // );
    app.run();
}
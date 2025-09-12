#include "epix/input.h"
#include "epix/render.h"
#include "epix/render/window.h"
#include "epix/utils/time.h"
#include "epix/window.h"

void test_func() { std::cout << "Test function called!" << std::endl; }

enum class TestFuncState {
    On,
    Off,
};

int main() {
    if (glfwInit() == GLFW_FALSE) {
        return -1;
    }
    glfwDefaultWindowHints();

    using namespace epix::window;
    using namespace epix::glfw;

    Window window_desc;
    Window window_desc2;

    window_desc.title = "Test Window";

    window_desc.size    = {800, 200};
    window_desc.opacity = 0.5f;
    // window_desc.present_mode = PresentMode::Fifo;

    window_desc2.title = "Test Window 2";

    window_desc2.size    = {800, 400};
    window_desc2.opacity = 0.7f;
    // window_desc2.present_mode = PresentMode::Fifo;

    epix::App app = epix::App::create(epix::AppConfig{
        // .mark_frame = true,
        // .enable_tracy = true,
    });

    struct FrameCounter {
        int count = 0;
    };

    Window window_desc3;
    window_desc3.title    = "Test Window 3";
    window_desc3.size     = {800, 600};
    window_desc3.opacity  = 0.9f;
    window_desc3.pos      = {100, 100};
    window_desc3.pos_type = PosType::Relative;

    app.spawn(window_desc2).with_child(window_desc3);
    app.insert_state(TestFuncState::Off);
    app.add_plugins(epix::window::WindowPlugin{})
        .plugin_scope([&](epix::window::WindowPlugin& plugin) {
            plugin.exit_condition = epix::window::ExitCondition::OnAllClosed;
            plugin.primary_window = window_desc;
        })
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::render::RenderPlugin{}.set_validation(0))
        .add_systems(epix::Update,
                     epix::into(epix::input::print_inputs, epix::window::print_events).set_name("print inputs"))
        .add_systems(
            epix::Update,
            epix::into(
                [](epix::EventReader<epix::input::KeyInput> key_reader,
                   epix::Query<epix::Item<epix::Mut<epix::window::Window>>> windows,
                   epix::ResMut<epix::Schedules> schedules) {
                    for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
                        if (pressed) {
                            if (key == epix::input::KeyCode::KeyF11) {
                                if (auto window_opt = windows.try_get(window)) {
                                    auto&& [window_desc] = *window_opt;
                                    if (window_desc.window_mode == epix::window::WindowMode::Windowed) {
                                        window_desc.window_mode = epix::window::WindowMode::Fullscreen;
                                    } else {
                                        window_desc.window_mode = epix::window::WindowMode::Windowed;
                                    }
                                }
                            }
                        }
                    }
                },
                [](epix::EventReader<epix::input::KeyInput> key_reader,
                   epix::Query<epix::Item<epix::Mut<epix::window::Window>>> windows,
                   epix::ResMut<epix::NextState<TestFuncState>> next_state) {
                    for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
                        if (key == epix::input::KeyCode::KeySpace && pressed && !repeat) {
                            if (*next_state == TestFuncState::Off) {
                                *next_state = TestFuncState::On;
                            } else {
                                *next_state = TestFuncState::Off;
                            }
                        }
                    }
                },
                [](epix::Res<epix::AppProfiler> profiler, epix::Local<std::optional<epix::utils::time::Timer>> timer) {
                    if (!timer->has_value()) {
                        *timer = epix::utils::time::Timer::repeat(0.5);
                    }
                    if (timer->value().tick()) {
                        spdlog::info("Frame time: {:9.5f}ms; FPS: {:7.2f}", profiler->time_avg(),
                                     1000.0 / profiler->time_avg());
                        for (auto&& [label, profiler] : profiler->schedule_profilers()) {
                            spdlog::info(
                                "Schedule {:<40}: build: {:9.5f}ms, run: "
                                "{:9.5f}ms, with {:3} systems, {:3} sets",
                                label.name(), profiler->build_time_avg(), profiler->run_time_avg(),
                                profiler->system_count(), profiler->set_count());
                        }
                    }
                })
                .set_names({"toggle fullscreen", "print profiling info"}))
        .add_systems(epix::OnEnter(TestFuncState::On), epix::into([](epix::ResMut<epix::Schedules> schedules) {
                         spdlog::info("Test function enabled.");
                         // return;
                         if (auto schedule = schedules->get(epix::Update)) {
                             schedule->add_systems(epix::into(test_func).set_name("test function"));
                         }
                     }))
        .add_systems(epix::OnEnter(TestFuncState::Off), epix::into([](epix::ResMut<epix::Schedules> schedules) {
                         spdlog::info("Test function disabled.");
                         //  return;
                         if (auto schedule = schedules->get(epix::Update)) {
                             schedule->remove_system(test_func);
                         }
                     }));
    // app.add_systems(
    //     epix::Update, epix::into([](epix::Local<FrameCounter> count) {
    //         std::cout << "Frame: " << count->count++ << std::endl;
    //     })
    // );
    // app.config.mark_frame   = true;
    // app.config.enable_tracy = true;
    app.run();
}
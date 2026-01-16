#include <spdlog/spdlog.h>

import epix.core;
import epix.window;
import epix.glfw.core;
import epix.input;

void test_func() { std::cout << "Test function called!" << std::endl; }

enum class TestFuncState {
    On,
    Off,
};

int main() {
    using namespace window;
    using namespace glfw;
    using namespace core;

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

    App app = App::create();

    Window window_desc3;
    window_desc3.title    = "Test Window 3";
    window_desc3.size     = {800, 600};
    window_desc3.opacity  = 0.9f;
    window_desc3.pos      = {100, 100};
    window_desc3.pos_type = PosType::Relative;

    app.world_mut().spawn(window_desc2).then([&](EntityWorldMut& parent_mut) { parent_mut.spawn(window_desc3); });
    app.insert_state(TestFuncState::Off);
    app.add_plugins(WindowPlugin{})
        .plugin_scope([&](WindowPlugin& plugin) {
            plugin.exit_condition = ExitCondition::OnAllClosed;
            plugin.primary_window = window_desc;
        })
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_systems(Update, into(input::log_inputs, log_events).set_name("print inputs"))
        .add_systems(Update, into(
                                 [](EventReader<input::KeyInput> key_reader, Query<Item<Mut<Window>>> windows,
                                    ResMut<Schedules> schedules) {
                                     for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
                                         if (pressed) {
                                             if (key == input::KeyCode::KeyF11) {
                                                 if (auto window_opt = windows.get(window)) {
                                                     auto&& [window_desc] = *window_opt;
                                                     if (window_desc->window_mode == WindowMode::Windowed) {
                                                         window_desc->window_mode = WindowMode::Fullscreen;
                                                     } else {
                                                         window_desc->window_mode = WindowMode::Windowed;
                                                     }
                                                 }
                                             }
                                         }
                                     }
                                 },
                                 [](EventReader<input::KeyInput> key_reader, Query<Item<Mut<Window>>> windows,
                                    ResMut<NextState<TestFuncState>> next_state) {
                                     for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
                                         if (key == input::KeyCode::KeySpace && pressed && !repeat) {
                                             if (*next_state == TestFuncState::Off) {
                                                 *next_state = TestFuncState::On;
                                             } else {
                                                 *next_state = TestFuncState::Off;
                                             }
                                         }
                                     }
                                 })
                                 .set_names(std::array{"toggle fullscreen", "print profiling info"}))
        .add_systems(OnEnter(TestFuncState::On), into([](ResMut<Schedules> schedules) {
                         spdlog::info("Test function enabled.");
                         // return;
                         if (auto schedule = schedules->get_schedule_mut(Update)) {
                             schedule.value().get().add_systems(into(test_func).set_name("test function"));
                         }
                     }))
        .add_systems(OnEnter(TestFuncState::Off), into([](ResMut<Schedules> schedules) {
                         spdlog::info("Test function disabled.");
                         //  return;
                         if (auto schedule = schedules->get_schedule_mut(Update)) {
                             schedule.value().get().remove_system(test_func);
                         }
                     }));
    // app.add_systems(
    //     Update, into([](Local<FrameCounter> count) {
    //         std::cout << "Frame: " << count->count++ << std::endl;
    //     })
    // );
    // app.config.mark_frame   = true;
    // app.config.enable_tracy = true;
    app.run();
}
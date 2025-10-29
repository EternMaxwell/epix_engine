#include "epix/glfw/glfw.hpp"
#include "epix/input.hpp"
#include "epix/window.hpp"

void test_func() { std::cout << "Test function called!" << std::endl; }

enum class TestFuncState {
    On,
    Off,
};

int main() {
    using namespace epix::window;
    using namespace epix::glfw;
    using namespace epix::core;

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

    epix::App app = epix::App::create();

    Window window_desc3;
    window_desc3.title    = "Test Window 3";
    window_desc3.size     = {800, 600};
    window_desc3.opacity  = 0.9f;
    window_desc3.pos      = {100, 100};
    window_desc3.pos_type = PosType::Relative;

    app.world_mut().spawn(window_desc2).then([&](epix::EntityWorldMut& parent_mut) { parent_mut.spawn(window_desc3); });
    app.insert_state(TestFuncState::Off);
    app.add_plugins(epix::window::WindowPlugin{})
        .plugin_scope([&](epix::window::WindowPlugin& plugin) {
            plugin.exit_condition = epix::window::ExitCondition::OnAllClosed;
            plugin.primary_window = window_desc;
        })
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_systems(epix::Update,
                     epix::into(epix::input::log_inputs, epix::window::log_events).set_name("print inputs"))
        .add_systems(epix::Update,
                     epix::into(
                         [](epix::EventReader<epix::input::KeyInput> key_reader,
                            epix::Query<epix::Item<epix::Mut<epix::window::Window>>> windows,
                            epix::ResMut<app::Schedules> schedules) {
                             for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
                                 if (pressed) {
                                     if (key == epix::input::KeyCode::KeyF11) {
                                         if (auto window_opt = windows.get(window)) {
                                             auto&& [window_desc] = *window_opt;
                                             if (window_desc->window_mode == epix::window::WindowMode::Windowed) {
                                                 window_desc->window_mode = epix::window::WindowMode::Fullscreen;
                                             } else {
                                                 window_desc->window_mode = epix::window::WindowMode::Windowed;
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
                         })
                         .set_names(std::array{"toggle fullscreen", "print profiling info"}))
        .add_systems(epix::OnEnter(TestFuncState::On), epix::into([](epix::ResMut<app::Schedules> schedules) {
                         spdlog::info("Test function enabled.");
                         // return;
                         if (auto schedule = schedules->get_schedule_mut(epix::Update)) {
                             schedule.value().get().add_systems(epix::into(test_func).set_name("test function"));
                         }
                     }))
        .add_systems(epix::OnEnter(TestFuncState::Off), epix::into([](epix::ResMut<app::Schedules> schedules) {
                         spdlog::info("Test function disabled.");
                         //  return;
                         if (auto schedule = schedules->get_schedule_mut(epix::Update)) {
                             schedule.value().get().remove_system(test_func);
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
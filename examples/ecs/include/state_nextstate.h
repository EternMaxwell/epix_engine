#include <epix/prelude.h>

namespace test_state {
using namespace epix;
using namespace prelude;

int loops = 10;

enum class States { Start = 0, Middle, End };

void is_state_start(Res<State<States>> state) {
    if (state.has_value()) {
        if (state->is_state(States::Start)) {
            std::cout << "is_state_start: true" << std::endl;
        } else {
            std::cout << "is_state_start: false" << std::endl;
        }
    }
}

void is_state_middle(Res<State<States>> state) {
    if (state.has_value()) {
        if (state->is_state(States::Middle)) {
            std::cout << "is_state_middle: true" << std::endl;
        } else {
            std::cout << "is_state_middle: false" << std::endl;
        }
    }
}

void is_state_end(Res<State<States>> state) {
    if (state.has_value()) {
        if (state->is_state(States::End)) {
            std::cout << "is_state_end: true" << std::endl;
        } else {
            std::cout << "is_state_end: false" << std::endl;
        }
    }
}

void set_state_start(ResMut<NextState<States>> state) {
    if (state.has_value()) {
        state->set_state(States::Start);
    }
}

void set_state_middle(ResMut<NextState<States>> state) {
    if (state.has_value()) {
        std::cout << "set_state_middle" << std::endl;
        state->set_state(States::Middle);
    }
}

void set_state_end(ResMut<NextState<States>> state) {
    if (state.has_value()) {
        std::cout << "set_state_end" << std::endl;
        state->set_state(States::End);
    }
}

void exit(EventWriter<AppExit> exit) {
    std::cout << "tick: " << loops << std::endl;
    if ((loops--) <= 0) exit.write(AppExit{});
}

class StateTestPlugin : public Plugin {
   public:
    void build(App& app) override {
        app.init_state<States>()
            ->add_system(Startup, is_state_start)
            ->add_system(OnEnter(States::Middle), into(set_state_end))
            ->add_system(OnEnter(States::Start), into(set_state_middle))
            ->add_system(Update, is_state_middle)
            ->add_system(Update, into(is_state_end).after(is_state_middle))
            ->add_system(Update, into(exit).after(is_state_end));
    }
};

void test() {
    App app = App::create();
    app.add_plugin(StateTestPlugin{}).enable_loop().run();
}
}  // namespace test_state
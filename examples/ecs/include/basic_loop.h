#include <epix/prelude.h>

namespace test_loop {
using namespace epix;
using namespace prelude;

void print_hello(Command command) { std::cout << "Hello, World!" << std::endl; }

int count_down = 20;

bool should_call_exit() {
    count_down--;
    return count_down <= 0;
}

void exit_app(EventWriter<AppExit> exit_event) {
    if ((count_down--) <= 0) exit_event.write(AppExit{});
}

class LoopTestPlugin : public Plugin {
   public:
    void build(App& app) override {
        app.add_system(Update, print_hello)->add_system(Update, exit_app);
    }
};

void test() {
    App app = App::create();
    app.enable_loop().add_plugin(LoopTestPlugin{}).run();
}
}  // namespace test_loop
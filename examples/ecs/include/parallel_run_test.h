#include <epix/prelude.h>

using namespace epix;

namespace test_parallel_run {
using namespace prelude;

struct Data1 {
    int data;
};

struct Data2 {
    int data;
};

std::mutex mutex;

int loops = 10;

void spawn_data1(Command command) {
    for (int i = 0; i < 10; i++) command.spawn(Data1{.data = 1});
}

void spawn_data2(Command command) {
    for (int i = 0; i < 10; i++) command.spawn(Data2{.data = 1});
}

void print_data1(Query<Get<Data1>, With<>, Without<>> query) {
    for (auto [data] : query.iter()) {
        std::cout << "Data1: " << data.data++ << "\n";
    }
    std::cout << "Data1: End\n";
}

void print_data2(Query<Get<Data2>, With<>, Without<>> query) {
    for (auto [data] : query.iter()) {
        std::cout << "Data2: " << data.data++ << "\n";
    }
    std::cout << "Data2: End\n";
}

void print_endl() { std::cout << "\n\n"; }

void exit(EventWriter<AppExit> exit_event) {
    if (!loops--) exit_event.write(AppExit{});
}

enum class Stage {
    data,
    endl,
};

class ParallelTestPlugin : public Plugin {
   public:
    void build(App& app) override {
        app.enable_loop()
            ->add_system(Startup, spawn_data1)
            ->add_system(Startup, spawn_data2)
            ->add_system(Update, print_data1)
            .in_set(Stage::data)
            ->add_system(Update, print_data2)
            .in_set(Stage::data)
            ->add_system(Update, print_endl)
            .in_set(Stage::endl)
            ->configure_sets(Stage::data, Stage::endl)
            ->add_system(Update, exit);
    }
};

void test() {
    App app = App::create();
    app.add_plugin(ParallelTestPlugin{}).run();
}
}  // namespace test_parallel_run
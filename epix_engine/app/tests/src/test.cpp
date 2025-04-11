#include <epix/app.h>

using namespace epix::prelude;

struct TestRes {
    int v;
};

struct TestComp {
    int v;
};

void func1(ResMut<TestRes> res, Query<Get<TestComp>> query) {
    std::cout << "func1" << std::endl;
    std::cout << res->v++ << std::endl;
    for (auto [comp] : query.iter()) {
        std::cout << "comp: " << comp.v++ << std::endl;
    }
};
void func2(Command cmd, Local<int> v) {
    cmd.insert_resource(TestRes{1});
    cmd.spawn(TestComp{1});
    std::cout << "func2" << std::endl;
    std::cout << "local int: " << (*v)++ << std::endl;
    // the ++ here only affects the local data, and since same function in
    // different stages are different systems, they are seperate data.
};

int main() {
    auto app = App::create();
    app.add_system(Startup, func1);
    app.add_system(Startup, into(func2).before(func1));
    app.add_system(Update, into(func2).after(func1));
    app.add_system(Update, func1);
    app.run();
    return 0;
}
#include <epix/app.h>

using namespace epix;

struct TestT {
    int value;
};

struct CustomSystemParam {
    Command cmd;
    Query<Get<TestT>> query;

    static CustomSystemParam from_system_param(
        Command cmd, Query<Get<TestT>> query
    ) {
        return CustomSystemParam{cmd, query};
    }
};

struct CustomParam2 {
    CustomSystemParam param;
    ResMut<TestT> res;

    static CustomParam2 from_system_param(
        CustomSystemParam param, ResMut<TestT> res
    ) {
        return CustomParam2{param, std::move(res)};
    }
};

void custom_system(CustomSystemParam param) {
    auto& cmd   = param.cmd;
    auto& query = param.query;
    cmd.spawn(TestT{42});
    cmd.add_resource<TestT>(std::make_shared<TestT>(TestT{42}));
    for (auto [test] : query.iter()) {
        test.value += 1;
    }
}

void custom_system2(CustomParam2 param) {
    auto& cmd   = param.param.cmd;
    auto& query = param.param.query;
    auto& res   = param.res;
    for (auto [test] : query.iter()) {
        test.value += 1;
        std::cout << "test.value: " << test.value << std::endl;
    }
    res->value += 1;
    std::cout << "res.value: " << res->value << std::endl;
}

int main() {
    App app = App::create();
    app.add_system(Startup, custom_system).add_system(Update, custom_system2);
    app.run();
}
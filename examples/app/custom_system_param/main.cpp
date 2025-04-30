#include <epix/app.h>

using namespace epix;

struct TestT {
    int value;
};

struct CustomSystemParam {
    Commands cmd;
    Query<Get<TestT>> query;

    static CustomSystemParam from_param(Commands cmd, Query<Get<TestT>> query) {
        return CustomSystemParam{cmd, query};
    }
};

struct CustomParam2 {
    CustomSystemParam param;
    ResMut<TestT> res;

    static CustomParam2 from_param(CustomSystemParam param, ResMut<TestT> res) {
        return CustomParam2{param, std::move(res)};
    }
};

void custom_system(CustomSystemParam param) {
    auto& cmd   = param.cmd;
    auto& query = param.query;
    cmd.spawn(TestT{42});
    cmd.insert_resource(TestT{42});
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

struct CustomParam3 {
    Extract<Query<Get<TestT>>> query;
    ResMut<TestT> res;

    static CustomParam3 from_param(
        Extract<Query<Get<TestT>>> query, ResMut<TestT> res
    ) {
        return CustomParam3{query, std::move(res)};
    }
};

// Testing Extract in Extract CustomParam and Testing recursive Extract
// Double Extract will acted as no Extract
void custom_system3(Extract<Extract<CustomParam3>> param) {
    auto& query = param.query;
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
    app.add_systems(Startup, into(custom_system))
        .add_systems(Update, into(custom_system2, custom_system3).chain());
    app.run();
}
#include <gtest/gtest.h>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <vector>

using namespace epix::ecs;

struct ExtractAccessSource {
    int value = 0;
};

TEST(Extract, UsesExtractedWorldProxyAccessForReadAndMutation) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    main_world.insert_resource(ExtractAccessSource{42});
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});

    std::vector<bool> observed_changes;
    auto read_extract = make_system_unique([&observed_changes](epix::app::Extract<epix::ecs::Res<ExtractAccessSource>> source) {
        observed_changes.push_back(source.is_modified());
    });
    auto write_extract = make_system_unique([](epix::app::Extract<epix::ecs::ResMut<ExtractAccessSource>> source) {
        source->value = 43;
    });
    auto direct_main_world = make_system_unique([](epix::ecs::ResMut<epix::app::ExtractedWorld>) {});

    const auto read_access   = read_extract->initialize(render_world);
    const auto write_access  = write_extract->initialize(render_world);
    const auto direct_access = direct_main_world->initialize(render_world);

    EXPECT_FALSE(read_access.get_conflicts(write_access).empty());
    EXPECT_FALSE(read_access.get_conflicts(direct_access).empty());
    EXPECT_TRUE(read_extract->run({}, render_world).has_value());
    EXPECT_TRUE(read_extract->run({}, render_world).has_value());
    main_world.resource_mut<ExtractAccessSource>().value = 42;
    EXPECT_TRUE(read_extract->run({}, render_world).has_value());
    EXPECT_TRUE(write_extract->run({}, render_world).has_value());
    ASSERT_EQ(observed_changes.size(), 3u);
    EXPECT_TRUE(observed_changes[0]);
    EXPECT_FALSE(observed_changes[1]);
    EXPECT_TRUE(observed_changes[2]);
    EXPECT_EQ(main_world.resource<ExtractAccessSource>().value, 43);
}

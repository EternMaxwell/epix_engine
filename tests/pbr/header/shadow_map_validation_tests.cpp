#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_systems.hpp>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;

class ValidationWarnings : public spdlog::sinks::base_sink<std::mutex> {
   public:
    std::vector<std::string> messages;
   protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        const std::string text(message.payload.data(), message.payload.size());
        if (message.level == spdlog::level::warn &&
            text.starts_with("[pbr] Non-power-of-two DirectionalLightShadowMap")) messages.push_back(text);
    }
    void flush_() override {}
};

struct MainStep {};
struct RenderStep {};
struct MapSample { ecs::Tick point; ecs::Tick directional; std::size_t size; };

class ShadowMapValidation : public testing::Test {
   protected:
    ecs::World main{2};
    ecs::World render{2};
    ecs::Schedule validation{ecs::ScheduleLabel::from_type<MainStep>()};
    ecs::Schedule extraction{ecs::ScheduleLabel::from_type<RenderStep>()};
    std::shared_ptr<ValidationWarnings> warnings = std::make_shared<ValidationWarnings>();
    std::shared_ptr<spdlog::logger> logger;

    void SetUp() override {
        main.insert_resource(pbr::PointLightShadowMap{.size = 32});
        main.insert_resource(pbr::DirectionalLightShadowMap{.size = 64});
        main.init_resource<pbr::GlobalVisibleClusterableObjects>();
        render.insert_resource(epix::app::ExtractedWorld{main});
        validation.add_systems(ecs::into(pbr::validate_shadow_map_size));
        extraction.add_systems(ecs::into(pbr::extract_lights));
        ASSERT_TRUE(validation.prepare(true).has_value());
        ASSERT_TRUE(extraction.prepare(true).has_value());
        validation.initialize_systems(main);
        extraction.initialize_systems(render);
        logger = spdlog::default_logger();
        logger->sinks().push_back(warnings);
    }

    void TearDown() override {
        if (logger) std::erase(logger->sinks(), warnings);
    }

    void frame() {
        validation.execute(main);
        extraction.execute(render);
    }

    MapSample sample(ecs::World& world) {
        auto observer = ecs::make_system_unique([](ecs::Res<pbr::PointLightShadowMap> point,
                                                   ecs::Res<pbr::DirectionalLightShadowMap> directional) {
            return MapSample{point.last_modified(), directional.last_modified(), directional->size};
        });
        observer->initialize(world);
        const auto result = observer->run({}, world);
        EXPECT_TRUE(result.has_value());
        return result ? *result : MapSample{};
    }
};

TEST_F(ShadowMapValidation, NewlyAddedValidSizeDoesNotAcquireAValidatorWrite) {
    const auto before = sample(main);
    frame();
    EXPECT_EQ(sample(main).directional, before.directional);
    EXPECT_EQ(sample(render).size, 64u);
    EXPECT_TRUE(warnings->messages.empty());
}

TEST_F(ShadowMapValidation, UnchangedFramesPreserveSourceAndExtractedTicks) {
    frame();
    const auto source = sample(main);
    const auto extracted = sample(render);
    for (int index = 0; index < 3; ++index) {
        frame();
        EXPECT_EQ(sample(main).directional, source.directional);
        const auto current = sample(render);
        EXPECT_EQ(current.directional, extracted.directional);
        EXPECT_EQ(current.point, extracted.point);
    }
    EXPECT_TRUE(warnings->messages.empty());
}

TEST_F(ShadowMapValidation, ExternalValidWritePropagatesOnceWithoutAnExtraValidatorWrite) {
    frame();
    const auto before = sample(render);
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = 128;
    const auto written = sample(main);
    frame();
    EXPECT_EQ(sample(main).directional, written.directional);
    const auto changed = sample(render);
    EXPECT_EQ(changed.size, 128u);
    EXPECT_NE(changed.directional, before.directional);
    EXPECT_EQ(changed.point, before.point);
    frame();
    EXPECT_EQ(sample(render).directional, changed.directional);
    EXPECT_TRUE(warnings->messages.empty());
}

TEST_F(ShadowMapValidation, EqualValueExternalWriteRemainsObservableExactlyOnce) {
    frame();
    const auto before = sample(render);
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = 64;
    const auto written = sample(main);
    frame();
    EXPECT_EQ(sample(main).directional, written.directional);
    const auto changed = sample(render);
    EXPECT_EQ(changed.size, 64u);
    EXPECT_NE(changed.directional, before.directional);
    frame();
    EXPECT_EQ(sample(render).directional, changed.directional);
    EXPECT_TRUE(warnings->messages.empty());
}

TEST_F(ShadowMapValidation, InvalidSizeStillRoundsAndWarns) {
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = 70;
    frame();
    EXPECT_EQ(sample(main).size, 128u);
    EXPECT_EQ(sample(render).size, 128u);
    ASSERT_EQ(warnings->messages.size(), 1u);
    EXPECT_NE(warnings->messages[0].find("correcting 70 to 128"), std::string::npos);
}

struct RoundingCase { std::size_t input; std::size_t expected; };
class CorrectedShadowMap : public ShadowMapValidation, public testing::WithParamInterface<RoundingCase> {};

TEST_P(CorrectedShadowMap, CorrectionWarnsOnceThenBecomesCleanOnBothWorlds) {
    const auto [input, expected] = GetParam();
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = input;
    frame();
    const auto source = sample(main);
    const auto extracted = sample(render);
    EXPECT_EQ(source.size, expected);
    EXPECT_EQ(extracted.size, expected);
    ASSERT_EQ(warnings->messages.size(), 1u);
    const auto text = "correcting " + std::to_string(input) + " to " + std::to_string(expected);
    EXPECT_NE(warnings->messages[0].find(text), std::string::npos);
    frame();
    frame();
    EXPECT_EQ(sample(main).directional, source.directional);
    EXPECT_EQ(sample(render).directional, extracted.directional);
    EXPECT_EQ(warnings->messages.size(), 1u);
}

constexpr std::size_t highest_power = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);

class OverflowShadowMap : public ShadowMapValidation, public testing::WithParamInterface<std::size_t> {};

TEST_P(OverflowShadowMap, OverflowMatchesTheNativeUnsignedProfileAndTerminates) {
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = GetParam();
#ifndef NDEBUG
    // Rust next_power_of_two panics before warning/writing in the checked profile.
    // The C++ system boundary represents a thrown overflow as a failed run.
    auto validator = ecs::make_system_unique(pbr::validate_shadow_map_size);
    validator->initialize(main);
    EXPECT_FALSE(validator->run({}, main).has_value());
    EXPECT_EQ(sample(main).size, GetParam());
    EXPECT_TRUE(warnings->messages.empty());
#else
    // Rust's unchecked profile wraps to zero. Its own write is not a new change
    // for the next run of the same system; an external zero write maps to one.
    frame();
    EXPECT_EQ(sample(main).size, 0u);
    EXPECT_EQ(sample(render).size, 0u);
    ASSERT_EQ(warnings->messages.size(), 1u);
    const auto zero = sample(render);
    frame();
    EXPECT_EQ(sample(render).size, 0u);
    EXPECT_EQ(sample(render).directional, zero.directional);
    EXPECT_EQ(warnings->messages.size(), 1u);
    main.resource_mut<pbr::DirectionalLightShadowMap>().size = 0;
    frame();
    EXPECT_EQ(sample(main).size, 1u);
    const auto settled = sample(render);
    EXPECT_EQ(settled.size, 1u);
    ASSERT_EQ(warnings->messages.size(), 2u);
    frame();
    EXPECT_EQ(sample(render).directional, settled.directional);
    EXPECT_EQ(warnings->messages.size(), 2u);
#endif
}

INSTANTIATE_TEST_SUITE_P(NativeOverflowBoundaries, OverflowShadowMap,
    testing::Values(highest_power + 1, std::numeric_limits<std::size_t>::max()));

INSTANTIATE_TEST_SUITE_P(ZeroAndRoundingBoundaries, CorrectedShadowMap, testing::Values(
    RoundingCase{0, 1}, RoundingCase{65, 128}, RoundingCase{1025, 2048},
    RoundingCase{highest_power - 1, highest_power}));
}  // namespace

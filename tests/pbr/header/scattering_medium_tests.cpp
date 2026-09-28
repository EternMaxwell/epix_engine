#include <gtest/gtest.h>

#include <epix/pbr/atmosphere.hpp>
#include <epix/pbr/scattering_medium.hpp>

namespace {

using epix::pbr::Falloff;
using epix::pbr::PhaseFunction;
using epix::pbr::ScatteringMedium;

/** @brief Bevy earthlike values, and the Falloff arms the medium needs (Bevy
 *  `ScatteringMedium::earthlike`, `bevy_pbr/src/medium.rs:131-161`, and
 *  `Falloff::sample`, `:273-291`). */
TEST(ScatteringMedium, EarthlikeMatchesBevyTermForTerm) {
    const ScatteringMedium medium = ScatteringMedium::earthlike(64, 32);
    EXPECT_EQ(medium.falloff_resolution, 64u);
    EXPECT_EQ(medium.phase_resolution, 32u);
    ASSERT_EQ(medium.terms.size(), 3u) << "Rayleigh, Mie and ozone";

    const auto& rayleigh = medium.terms[0];
    EXPECT_EQ(rayleigh.absorption, glm::vec3(0.0f)) << "Rayleigh scattering does not absorb";
    EXPECT_EQ(rayleigh.scattering, glm::vec3(5.802e-6f, 13.558e-6f, 33.100e-6f));
    EXPECT_EQ(rayleigh.falloff.kind, Falloff::Kind::Exponential);
    EXPECT_FLOAT_EQ(rayleigh.falloff.scale, 8.0f / 60.0f);
    EXPECT_EQ(rayleigh.phase.kind, PhaseFunction::Kind::Rayleigh);

    const auto& mie = medium.terms[1];
    EXPECT_EQ(mie.absorption, glm::vec3(3.996e-6f));
    EXPECT_EQ(mie.scattering, glm::vec3(0.444e-6f));
    EXPECT_EQ(mie.phase.kind, PhaseFunction::Kind::Mie);
    EXPECT_FLOAT_EQ(mie.phase.asymmetry, 0.8f);

    const auto& ozone = medium.terms[2];
    EXPECT_EQ(ozone.absorption, glm::vec3(0.650e-6f, 1.881e-6f, 0.085e-6f));
    EXPECT_EQ(ozone.scattering, glm::vec3(0.0f));
    EXPECT_EQ(ozone.falloff.kind, Falloff::Kind::Tent) << "the ozone band, the only tent";
    EXPECT_FLOAT_EQ(ozone.falloff.center, 0.75f);
    EXPECT_FLOAT_EQ(ozone.falloff.width, 0.3f);
    EXPECT_EQ(ozone.phase.kind, PhaseFunction::Kind::Isotropic);
}

TEST(ScatteringMedium, FalloffArmsEvaluateAsBevyDoes) {
    const Falloff linear = Falloff::linear();
    EXPECT_FLOAT_EQ(linear.sample(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(linear.sample(1.0f), 1.0f);
    EXPECT_NEAR(linear.sample(0.25f), 0.25f, 1e-6f);

    const Falloff exponential = Falloff::exponential(8.0f / 60.0f);
    EXPECT_NEAR(exponential.sample(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(exponential.sample(1.0f), 1.0f, 1e-6f);

    // The discontinuity at scale == 0 is filled with linear, which is Bevy choice.
    EXPECT_FLOAT_EQ(Falloff::exponential(0.0f).sample(0.4f), 0.4f);

    const Falloff tent = Falloff::tent(0.75f, 0.3f);
    EXPECT_NEAR(tent.sample(0.75f), 1.0f, 1e-6f) << "the centre is the densest point";
    EXPECT_NEAR(tent.sample(0.60f), 0.0f, 1e-6f) << "half the width away, and the band ends";
    EXPECT_NEAR(tent.sample(0.90f), 0.0f, 1e-6f);
    EXPECT_NEAR(tent.sample(0.0f), 0.0f, 1e-6f) << "Bevy clamps rather than going negative";
}

TEST(ScatteringMedium, TheSampledStandInForBevyCurveInterpolates) {
    // What a curve arm becomes here: values at even intervals over [0, 1], read with
    // linear interpolation. An empty table reads zero, like a curve with no values.
    const Falloff sampled = Falloff::sampled({0.0f, 1.0f, 0.0f});
    EXPECT_FLOAT_EQ(sampled.sample(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(sampled.sample(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(sampled.sample(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(sampled.sample(0.25f), 0.5f) << "halfway between the first two samples";
    EXPECT_FLOAT_EQ(Falloff::sampled({}).sample(0.5f), 0.0f);
}

TEST(ScatteringMedium, BuildersScaleEveryTermAndKeepTheTerms) {
    const ScatteringMedium medium = ScatteringMedium::earthlike(16, 16).with_label("sky");
    EXPECT_EQ(medium.label, "sky");

    const ScatteringMedium denser = medium.with_density_multiplier(2.0f);
    EXPECT_EQ(denser.terms.size(), 3u) << "scaling does not drop terms";
    EXPECT_EQ(denser.label, "sky") << "and it carries the label through";
    EXPECT_EQ(denser.terms[1].absorption, glm::vec3(3.996e-6f) * 2.0f);
    EXPECT_EQ(denser.terms[1].scattering, glm::vec3(0.444e-6f) * 2.0f);
    EXPECT_EQ(denser.terms[0].absorption, glm::vec3(0.0f)) << "zero stays zero";
}

/** @brief The phase function values Bevy computes when filling the LUTs (Bevy
 *  `PhaseFunction::sample`, `bevy_pbr/src/medium.rs:369-381`). */
TEST(ScatteringMedium, PhaseFunctionValuesMatchBevy) {
    constexpr float PI = 3.14159265358979323846f;
    EXPECT_NEAR(PhaseFunction::isotropic().sample(0.3f), 0.25f / PI, 1e-7f)
        << "isotropic is a constant, and independent of direction";
    EXPECT_NEAR(PhaseFunction::rayleigh().sample(0.0f), 0.1875f / PI, 1e-7f);
    EXPECT_NEAR(PhaseFunction::rayleigh().sample(1.0f), 0.1875f / PI * 2.0f, 1e-7f)
        << "the Rayleigh arm grows with the square of the cosine";
    // With zero asymmetry the Mie arm degenerates to isotropic, denominator included.
    EXPECT_NEAR(PhaseFunction::mie(0.0f).sample(0.7f), 0.25f / PI, 1e-7f);
    EXPECT_NEAR(PhaseFunction::mie(0.8f).sample(-1.0f), 0.25f / PI * (1.0f - 0.64f) / std::pow(1.0f + 0.64f + 1.6f, 1.5f), 1e-6f);

    // Bevy derives Default, and its default arm is Mie with asymmetry 0.8 - not the
    // first enumerator, which is why the port names it rather than relying on order.
    EXPECT_EQ(PhaseFunction::default_phase().kind, PhaseFunction::Kind::Mie);
    EXPECT_FLOAT_EQ(PhaseFunction::default_phase().asymmetry, 0.8f);
}

/** @brief The LUT data the GPU representation uploads (Bevy
 *  `GpuScatteringMedium::prepare_asset`, `bevy_pbr/src/medium.rs:429-475`). */
TEST(ScatteringMedium, LutDataMatchesBevySizingAndSampling) {
    const ScatteringMedium medium = ScatteringMedium::earthlike(8, 4);

    const auto density = medium.density_lut();
    ASSERT_EQ(density.size(), 16u) << "two rows of falloff_resolution";
    // Row 0 is absorption: earthlike absorbs only through ozone, whose tent is centred
    // at 0.75 with half-width 0.15, so a cell centre inside the band picks it up and
    // one outside stays zero.
    const float inside = density[5].x;   // cell centre 5.5/8 = 0.6875, inside 0.6..0.9
    const float outside = density[0].x;  // cell centre 0.0625, far outside
    EXPECT_GT(inside, 0.0f) << "the ozone band is where absorption comes from";
    EXPECT_NEAR(outside, 0.0f, 1e-12f);
    // Row 1 is scattering, which Rayleigh and Mie contribute everywhere.
    EXPECT_GT(density[8 + 4].x, 0.0f);

    const auto scattering = medium.scattering_lut();
    ASSERT_EQ(scattering.size(), 32u) << "falloff_resolution * phase_resolution";
    // The falloff parameter is the fast axis, as Bevy indexes it: i = raw % phase.
    EXPECT_GT(scattering[3].x, 0.0f);
}
/** @brief The atmosphere component and what extraction keeps (Bevy `Atmosphere`,
 *  `bevy_pbr/src/atmosphere/mod.rs:210-245`, and `impl ExtractComponent` at `:247-262`). */
TEST(ScatteringMedium, AtmosphereEarthlikeAndExtraction) {
    const auto handle = epix::assets::Handle<ScatteringMedium>{
        epix::assets::AssetId<ScatteringMedium>::invalid()};
    const auto atmosphere = epix::pbr::Atmosphere::earthlike(handle);

    EXPECT_FLOAT_EQ(atmosphere.bottom_radius, 6'360'000.0f) << "Earth radius, in metres";
    EXPECT_FLOAT_EQ(atmosphere.top_radius, 6'460'000.0f) << "where the atmosphere ends";
    EXPECT_EQ(atmosphere.ground_albedo, glm::vec3(0.3f));

    // Extraction copies the three values and reduces the handle to an id, because the
    // render world holds no handles.
    const auto extracted = epix::render::ExtractComponent<epix::pbr::Atmosphere>::extract_component(atmosphere);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_FLOAT_EQ(extracted->bottom_radius, atmosphere.bottom_radius);
    EXPECT_FLOAT_EQ(extracted->top_radius, atmosphere.top_radius);
    EXPECT_EQ(extracted->ground_albedo, atmosphere.ground_albedo);
    EXPECT_EQ(extracted->medium, atmosphere.medium.id());
}
}  // namespace
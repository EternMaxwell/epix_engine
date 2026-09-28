#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <epix/common.hpp>

#include <glm/glm.hpp>

namespace epix::pbr {

/** @brief How a medium density changes with the falloff parameter (Bevy
 *  `Falloff`, `bevy_pbr/src/medium.rs:210-291`).
 *
 *  `p = 1` is where the medium is densest and `p = 0` where it fades out completely,
 *  so every arm maps 1 to 1 and 0 to 0 except `Tent`, which is a band.
 *
 *  Bevy has one more arm, `Curve`, built from a `Curve<f32>`. This port has no curve
 *  type, so - by the decision recorded on `PBR-886` - a custom distribution is carried
 *  as **sampled tables** instead: `Sampled` holds the values at even intervals over
 *  `[0, 1]` and interpolates, which is what the GPU side ultimately needs anyway. */
EPIX_EXPORT struct Falloff {
    enum class Kind : std::uint8_t { Linear, Exponential, Tent, Sampled };

    Kind kind = Kind::Linear;
    /** @brief The exponential arm scale (Bevy `Exponential { scale }`). Values near
     *  zero fall off steeply; negative values change the concavity. */
    float scale = 0.0f;
    /** @brief The tent arm centre (Bevy `Tent { center, width }`). */
    float center = 0.0f;
    /** @brief The tent arm width. Bevy divides by half of it, so this is the full
     *  base width of the triangle. */
    float width = 0.0f;
    /** @brief The sampled arm values at even intervals over `[0, 1]`, used in place of
     *  Bevy `Curve`. Empty means every sample reads zero, matching Bevy sampling a
     *  curve with no values. */
    std::vector<float> samples;

    static Falloff linear() noexcept { return Falloff{}; }
    static Falloff exponential(float scale) noexcept {
        Falloff f;
        f.kind  = Kind::Exponential;
        f.scale = scale;
        return f;
    }
    static Falloff tent(float center, float width) noexcept {
        Falloff f;
        f.kind   = Kind::Tent;
        f.center = center;
        f.width  = width;
        return f;
    }
    static Falloff sampled(std::vector<float> samples) noexcept {
        Falloff f;
        f.kind    = Kind::Sampled;
        f.samples = std::move(samples);
        return f;
    }

    /** @brief The falloff value at `p` (Bevy `Falloff::sample`, `:273-291`). */
    [[nodiscard]] float sample(float p) const noexcept {
        switch (kind) {
            case Kind::Linear: return p;
            case Kind::Exponential: {
                // Bevy fills the discontinuity at scale == 0 by choosing linear.
                if (scale == 0.0f) return p;
                const float s     = -1.0f / scale;
                const float exp_s = std::exp(s);
                return (std::exp((1.0f - p) * s) - exp_s) / (1.0f - exp_s);
            }
            case Kind::Tent:
                return std::max(1.0f - std::abs(p - center) / (0.5f * width), 0.0f);
            case Kind::Sampled: {
                if (samples.empty()) return 0.0f;
                if (samples.size() == 1) return samples.front();
                const float  clamped = std::clamp(p, 0.0f, 1.0f);
                const float  scaled  = clamped * static_cast<float>(samples.size() - 1);
                const auto   index   = static_cast<std::size_t>(scaled);
                const float  frac    = scaled - static_cast<float>(index);
                const float  a       = samples[index];
                const float  b       = samples[std::min(index + 1, samples.size() - 1)];
                return a + (b - a) * frac;
            }
        }
        return p;
    }
};

/** @brief How a term scatters light by direction (Bevy `PhaseFunction`,
 *  `bevy_pbr/src/medium.rs:309-330`).
 *
 *  The data only: Bevy evaluates the phase function when it builds the LUTs on the
 *  GPU, which is not this port's side of the boundary. `Sampled` stands in for Bevy
 *  `Curve` arms the same way `Falloff::Sampled` does. */
EPIX_EXPORT struct PhaseFunction {
    enum class Kind : std::uint8_t { Isotropic, Rayleigh, Mie, Sampled };

    Kind kind = Kind::Isotropic;
    /** @brief The Mie asymmetry, in `[-1, 1]`: positive scatters forward. */
    float asymmetry = 0.0f;
    /** @brief The sampled arm values at even intervals over the domain. */
    std::vector<float> samples;

    static PhaseFunction isotropic() noexcept { return PhaseFunction{}; }
    static PhaseFunction rayleigh() noexcept {
        PhaseFunction f;
        f.kind = Kind::Rayleigh;
        return f;
    }
    static PhaseFunction mie(float asymmetry) noexcept {
        PhaseFunction f;
        f.kind      = Kind::Mie;
        f.asymmetry = asymmetry;
        return f;
    }
};

/** @brief One component of a medium (Bevy `ScatteringTerm`, `:174-192`). */
EPIX_EXPORT struct ScatteringTerm {
    /** @brief Optical absorption density per metre, per wavelength. */
    glm::vec3 absorption{0.0f};
    /** @brief Optical scattering density per metre, per wavelength. */
    glm::vec3 scattering{0.0f};
    /** @brief How the term density varies with the falloff parameter. */
    Falloff falloff{};
    /** @brief The character of the scattering. */
    PhaseFunction phase{};
};

/** @brief An asset defining how a medium scatters light (Bevy `ScatteringMedium`,
 *  `bevy_pbr/src/medium.rs:69-82`). */
EPIX_EXPORT struct ScatteringMedium {
    /** @brief An optional label, used when creating the LUTs on the GPU. */
    std::string label;
    /** @brief Samples per scattering term for its falloff distribution. */
    std::uint32_t falloff_resolution = 0;
    /** @brief Samples per scattering term for its phase function. */
    std::uint32_t phase_resolution = 0;
    /** @brief The terms that compose the medium. Bevy uses a `SmallVec<[_; 1]>`; the
     *  port uses a vector, which the GPU side reads in order either way. */
    std::vector<ScatteringTerm> terms;

    /** @brief Build a medium from its terms (Bevy `ScatteringMedium::new`). */
    static ScatteringMedium make(std::uint32_t falloff_resolution, std::uint32_t phase_resolution,
                                 std::vector<ScatteringTerm> terms) {
        ScatteringMedium medium;
        medium.falloff_resolution = falloff_resolution;
        medium.phase_resolution   = phase_resolution;
        medium.terms              = std::move(terms);
        return medium;
    }

    /** @brief This medium with a label (Bevy `with_label`). */
    ScatteringMedium with_label(std::string value) const {
        ScatteringMedium copy = *this;
        copy.label            = std::move(value);
        return copy;
    }

    /** @brief This medium with every term scaled by `multiplier` (Bevy
     *  `with_density_multiplier`, `:116`). */
    ScatteringMedium with_density_multiplier(float multiplier) const {
        ScatteringMedium copy = *this;
        for (auto& term : copy.terms) {
            term.absorption *= multiplier;
            term.scattering *= multiplier;
        }
        return copy;
    }

    /** @brief A medium approximating Earth atmosphere (Bevy `earthlike`, `:131-161`):
     *  a Rayleigh term, a Mie term and an ozone term, with Bevy values verbatim -
     *  including the ozone band, which is the only `Tent` falloff among them and the
     *  reason the arm exists. */
    static ScatteringMedium earthlike(std::uint32_t falloff_resolution, std::uint32_t phase_resolution) {
        return make(falloff_resolution, phase_resolution,
                    {
                        ScatteringTerm{.absorption = glm::vec3{0.0f},
                                       .scattering = glm::vec3{5.802e-6f, 13.558e-6f, 33.100e-6f},
                                       .falloff    = Falloff::exponential(8.0f / 60.0f),
                                       .phase      = PhaseFunction::rayleigh()},
                        ScatteringTerm{.absorption = glm::vec3{3.996e-6f},
                                       .scattering = glm::vec3{0.444e-6f},
                                       .falloff    = Falloff::exponential(1.2f / 60.0f),
                                       .phase      = PhaseFunction::mie(0.8f)},
                        ScatteringTerm{.absorption = glm::vec3{0.650e-6f, 1.881e-6f, 0.085e-6f},
                                       .scattering = glm::vec3{0.0f},
                                       .falloff    = Falloff::tent(0.75f, 0.3f),
                                       .phase      = PhaseFunction::isotropic()},
                    });
    }
};

}  // namespace epix::pbr
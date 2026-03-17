import std;
import glm;
import epix.assets;
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.extension.grid;

namespace {
using namespace core;
using ext::grid::packed_grid;

constexpr int kN          = 200;
constexpr int kIter       = 20;
constexpr float kGravity  = 0.5f;
constexpr float kTargetD  = 1.0f;
constexpr float kMaxVel   = 8.0f;
constexpr float kCellSize = 4.0f;

enum class PaintTool {
    Water,
    Wall,
    Eraser,
};

struct Fluid {
    packed_grid<2, float> D{{kN, kN}, 0.0f};
    packed_grid<2, float> newD{{kN, kN}, 0.0f};
    packed_grid<2, std::uint8_t> S{{kN, kN}, 0};
    packed_grid<2, float> P{{kN, kN}, 0.0f};

    packed_grid<2, float> U{{kN + 1, kN}, 0.0f};
    packed_grid<2, float> newU{{kN + 1, kN}, 0.0f};
    packed_grid<2, float> V{{kN, kN + 1}, 0.0f};
    packed_grid<2, float> newV{{kN, kN + 1}, 0.0f};

    packed_grid<2, float> fluxU{{kN + 1, kN}, 0.0f};
    packed_grid<2, float> fluxV{{kN, kN + 1}, 0.0f};

    std::vector<int> active_indices{};
    std::vector<int> core_indices{};

    float target_total_mass  = 0.0f;
    float current_total_mass = 0.0f;

    PaintTool tool   = PaintTool::Water;
    bool paused      = false;
    int pen_size     = 2;
    float dt_scale   = 5.0f;
    float stickiness = 0.0f;

    static constexpr std::uint32_t u32(int v) { return static_cast<std::uint32_t>(v); }

    static constexpr int idx(int x, int y) { return x + y * kN; }

    static constexpr auto deref = [](auto&& value) { return value.get(); };

    float getD(int x, int y) const { return D.get({u32(x), u32(y)}).transform(deref).value_or(0.0f); }

    std::uint8_t getS(int x, int y) const { return S.get({u32(x), u32(y)}).transform(deref).value_or(1); }

    float getP(int x, int y) const { return P.get({u32(x), u32(y)}).transform(deref).value_or(0.0f); }

    float getU(int x, int y) const { return U.get({u32(x), u32(y)}).transform(deref).value_or(0.0f); }

    float getV(int x, int y) const { return V.get({u32(x), u32(y)}).transform(deref).value_or(0.0f); }

    void setD(int x, int y, float v) { (void)D.set({u32(x), u32(y)}, v); }

    void setS(int x, int y, std::uint8_t v) { (void)S.set({u32(x), u32(y)}, v); }

    void setP(int x, int y, float v) { (void)P.set({u32(x), u32(y)}, v); }

    void setU(int x, int y, float v) { (void)U.set({u32(x), u32(y)}, v); }

    void setV(int x, int y, float v) { (void)V.set({u32(x), u32(y)}, v); }

    float sampleU(float x, float y) const {
        x = std::clamp(x, 0.0f, static_cast<float>(kN));
        y = std::clamp(y, 0.0f, static_cast<float>(kN - 1));

        int x0 = static_cast<int>(std::floor(x));
        int y0 = static_cast<int>(std::floor(y - 0.5f));
        x0     = std::clamp(x0, 0, kN - 1);
        y0     = std::clamp(y0, 0, kN - 2);

        const float s = x - static_cast<float>(x0);
        const float t = y - (static_cast<float>(y0) + 0.5f);

        const float a = getU(x0, y0);
        const float b = getU(std::min(x0 + 1, kN), y0);
        const float c = getU(x0, y0 + 1);
        const float d = getU(std::min(x0 + 1, kN), y0 + 1);
        return (1.0f - s) * (1.0f - t) * a + s * (1.0f - t) * b + (1.0f - s) * t * c + s * t * d;
    }

    float sampleV(float x, float y) const {
        x = std::clamp(x, 0.0f, static_cast<float>(kN - 1));
        y = std::clamp(y, 0.0f, static_cast<float>(kN));

        int x0 = static_cast<int>(std::floor(x - 0.5f));
        int y0 = static_cast<int>(std::floor(y));
        x0     = std::clamp(x0, 0, kN - 2);
        y0     = std::clamp(y0, 0, kN - 1);

        const float s = x - (static_cast<float>(x0) + 0.5f);
        const float t = y - static_cast<float>(y0);

        const float a = getV(x0, y0);
        const float b = getV(x0 + 1, y0);
        const float c = getV(x0, std::min(y0 + 1, kN));
        const float d = getV(x0 + 1, std::min(y0 + 1, kN));
        return (1.0f - s) * (1.0f - t) * a + s * (1.0f - t) * b + (1.0f - s) * t * c + s * t * d;
    }

    void reset() {
        D.clear();
        newD.clear();
        S.clear();
        P.clear();
        U.clear();
        newU.clear();
        V.clear();
        newV.clear();
        fluxU.clear();
        fluxV.clear();

        target_total_mass  = 0.0f;
        current_total_mass = 0.0f;

        for (int i = 0; i < kN; ++i) {
            setS(i, 0, 1);
            setS(i, kN - 1, 1);
            setS(0, i, 1);
            setS(kN - 1, i, 1);
        }

        for (int x = 30; x < 70; ++x) setS(x, 70, 1);
        for (int y = 30; y < 70; ++y) {
            setS(30, y, 1);
            setS(70, y, 1);
        }
    }

    void apply_brush(int cx, int cy, PaintTool paint) {
        for (int oy = -pen_size; oy <= pen_size; ++oy) {
            for (int ox = -pen_size; ox <= pen_size; ++ox) {
                const int x = cx + ox;
                const int y = cy + oy;
                if (x <= 0 || x >= kN - 1 || y <= 0 || y >= kN - 1) continue;

                const int id = idx(x, y);
                if (paint == PaintTool::Water && getS(x, y) == 0) {
                    if (getD(x, y) < 1.0f) {
                        const float add = 1.0f - getD(x, y);
                        setD(x, y, 1.0f);
                        target_total_mass += add;
                    }
                } else if (paint == PaintTool::Wall && getS(x, y) == 0) {
                    target_total_mass -= getD(x, y);
                    setS(x, y, 1);
                    setD(x, y, 0.0f);
                    setU(x, y, 0.0f);
                    setU(x + 1, y, 0.0f);
                    setV(x, y, 0.0f);
                    setV(x, y + 1, 0.0f);
                } else if (paint == PaintTool::Eraser) {
                    target_total_mass -= getD(x, y);
                    setS(x, y, 0);
                    setD(x, y, 0.0f);
                }
                (void)id;
            }
        }
        if (target_total_mass < 0.0f) target_total_mass = 0.0f;
    }

    void extrapolateVelocity() {
        for (int i = 0; i < kN * kN; ++i) {
            if (D.get({u32(i % kN), u32(i / kN)}).value().get() < 0.1f) {
                const int x = i % kN;
                const int y = i / kN;
                float sumU = 0.0f, sumV = 0.0f;
                int cU = 0, cV = 0;

                if (x > 0 && getD(x - 1, y) > 0.2f) {
                    sumU += getU(x, y);
                    sumV += getV(x, y);
                    cU++;
                    cV++;
                }
                if (x < kN - 1 && getD(x + 1, y) > 0.2f) {
                    sumU += getU(x + 1, y);
                    sumV += getV(x, y);
                    cU++;
                    cV++;
                }
                if (y > 0 && getD(x, y - 1) > 0.2f) {
                    sumU += getU(x, y);
                    sumV += getV(x, y);
                    cU++;
                    cV++;
                }
                if (y < kN - 1 && getD(x, y + 1) > 0.2f) {
                    sumU += getU(x, y);
                    sumV += getV(x, y + 1);
                    cU++;
                    cV++;
                }
                if (cU > 0 && cV > 0) {
                    const float aU = sumU / static_cast<float>(cU);
                    const float aV = sumV / static_cast<float>(cV);
                    setU(x, y, aU);
                    setU(x + 1, y, aU);
                    setV(x, y, aV);
                    setV(x, y + 1, aV);
                }
            }
        }
    }

    void applyViscosity(float sticky, float dt) {
        const int iterations     = std::max(0, static_cast<int>(std::round(sticky * 4.0f)));
        const float nu           = std::min(0.5f, 0.05f * sticky);
        const float wallFriction = std::clamp(1.0f - (sticky * 0.08f), 0.0f, 1.0f);

        if (iterations > 0 && nu > 0.0f) {
            for (int k = 0; k < iterations; ++k) {
                for (const int id : active_indices) {
                    const int x = id % kN;
                    const int y = id / kN;

                    if (x > 0 && x < kN && y > 0 && y < kN - 1) {
                        const float uC        = getU(x, y);
                        const float neighbors = getU(x - 1, y) + getU(x + 1, y) + getU(x, y - 1) + getU(x, y + 1);
                        setU(x, y, uC + (neighbors - 4.0f * uC) * nu * dt * 60.0f);
                    }
                    if (y > 0 && y < kN && x > 0 && x < kN - 1) {
                        const float vC        = getV(x, y);
                        const float neighbors = getV(x - 1, y) + getV(x + 1, y) + getV(x, y - 1) + getV(x, y + 1);
                        setV(x, y, vC + (neighbors - 4.0f * vC) * nu * dt * 60.0f);
                    }
                }
            }
        }

        if (wallFriction < 1.0f) {
            for (const int id : active_indices) {
                const int x = id % kN;
                const int y = id / kN;
                if ((x > 0 && getS(x - 1, y)) || (x < kN - 1 && getS(x + 1, y)) || (y > 0 && getS(x, y - 1)) ||
                    (y < kN - 1 && getS(x, y + 1))) {
                    setU(x, y, getU(x, y) * wallFriction);
                    setU(x + 1, y, getU(x + 1, y) * wallFriction);
                    setV(x, y, getV(x, y) * wallFriction);
                    setV(x, y + 1, getV(x, y + 1) * wallFriction);
                }
            }
        }
    }

    void applySurfaceTension(float dt) {
        constexpr float kStrength = 0.2f;
        for (int y = 1; y < kN - 1; ++y) {
            for (int x = 1; x < kN - 1; ++x) {
                const float d = getD(x, y);
                if (d < 0.2f || d > 0.8f) continue;

                const float dL = getD(x - 1, y);
                const float dR = getD(x + 1, y);
                const float dT = getD(x, y - 1);
                const float dB = getD(x, y + 1);
                const float nX = dR - dL;
                const float nY = dB - dT;

                if (std::abs(nX) > 0.1f) {
                    setU(x, y, getU(x, y) + nX * kStrength * dt);
                    setU(x + 1, y, getU(x + 1, y) + nX * kStrength * dt);
                }
                if (std::abs(nY) > 0.1f) {
                    setV(x, y, getV(x, y) + nY * kStrength * dt);
                    setV(x, y + 1, getV(x, y + 1) + nY * kStrength * dt);
                }
            }
        }
    }

    void advectVelocityRK2(float dt) {
        for (int y = 0; y < kN; ++y) {
            for (int x = 1; x < kN; ++x) {
                if (getS(x, y) || getS(x - 1, y)) {
                    (void)newU.set({u32(x), u32(y)}, 0.0f);
                    continue;
                }

                const float uVal = getU(x, y);
                const float vAvg = 0.25f * (getV(x, y) + getV(x - 1, y) + getV(x, y + 1) + getV(x - 1, y + 1));
                const float midX = static_cast<float>(x) - uVal * 0.5f * dt;
                const float midY = (static_cast<float>(y) + 0.5f) - vAvg * 0.5f * dt;
                const float midU = sampleU(midX, midY);
                const float midV = sampleV(midX, midY);
                (void)newU.set({u32(x), u32(y)},
                               sampleU(static_cast<float>(x) - midU * dt, (static_cast<float>(y) + 0.5f) - midV * dt));
            }
        }

        for (int y = 1; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getS(x, y) || getS(x, y - 1)) {
                    (void)newV.set({u32(x), u32(y)}, 0.0f);
                    continue;
                }

                const float vVal = getV(x, y);
                const float uAvg = 0.25f * (getU(x, y) + getU(x, y - 1) + getU(x + 1, y) + getU(x + 1, y - 1));
                const float midX = (static_cast<float>(x) + 0.5f) - uAvg * 0.5f * dt;
                const float midY = static_cast<float>(y) - vVal * 0.5f * dt;
                const float midU = sampleU(midX, midY);
                const float midV = sampleV(midX, midY);
                (void)newV.set({u32(x), u32(y)},
                               sampleV((static_cast<float>(x) + 0.5f) - midU * dt, static_cast<float>(y) - midV * dt));
            }
        }

        U = newU;
        V = newV;
    }

    void physicsStep(float dt) {
        const float sticky = stickiness + 0.001f;

        active_indices.clear();
        core_indices.clear();
        current_total_mass = 0.0f;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getS(x, y) != 0) continue;
                const float d = getD(x, y);
                current_total_mass += d;
                if (d > 0.05f) active_indices.push_back(idx(x, y));
                if (d > 0.8f && d < 1.3f) core_indices.push_back(idx(x, y));
            }
        }

        if (target_total_mass > 0.001f && !core_indices.empty()) {
            float diff                = target_total_mass - current_total_mass;
            const float maxCorrection = target_total_mass * 0.1f * dt;
            diff                      = std::clamp(diff, -maxCorrection, maxCorrection);

            if (std::abs(diff) > 0.0001f) {
                const float addPerCell = diff / static_cast<float>(core_indices.size());
                for (const int id : core_indices) {
                    const int x = id % kN;
                    const int y = id / kN;
                    setD(x, y, getD(x, y) + addPerCell);
                }
            }
        } else if (current_total_mass < 1.0f) {
            target_total_mass = 0.0f;
        }

        for (int y = 1; y < kN - 1; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getD(x, y) > 0.01f || getD(x, y - 1) > 0.01f) {
                    setV(x, y, getV(x, y) + kGravity * dt);
                }
            }
        }

        applySurfaceTension(dt);
        applyViscosity(sticky, dt);

        P.clear();
        constexpr float omega = 1.8f;

        for (int k = 0; k < kIter; ++k) {
            if ((k % 2) == 0) {
                for (std::size_t ii = 0; ii < active_indices.size(); ++ii) {
                    const int id = active_indices[ii];
                    const int x  = id % kN;
                    const int y  = id / kN;
                    if (getD(x, y) < 0.2f) continue;

                    const float uL = getU(x, y);
                    const float uR = getU(x + 1, y);
                    const float vT = getV(x, y);
                    const float vB = getV(x, y + 1);

                    const float velDiv     = uR - uL + vB - vT;
                    const float densityErr = getD(x, y) - kTargetD;
                    const float targetDiv  = (densityErr > 0.0f) ? (densityErr * 0.1f) : 0.0f;
                    const float totalDiv   = velDiv - targetDiv;

                    const int sL = (x > 0) ? static_cast<int>(getS(x - 1, y)) : 1;
                    const int sR = (x < kN - 1) ? static_cast<int>(getS(x + 1, y)) : 1;
                    const int sT = (y > 0) ? static_cast<int>(getS(x, y - 1)) : 1;
                    const int sB = (y < kN - 1) ? static_cast<int>(getS(x, y + 1)) : 1;

                    const int n = 4 - (sL + sR + sT + sB);
                    if (n == 0) continue;

                    float pCorr  = (-totalDiv / static_cast<float>(n)) * omega;
                    float weight = getD(x, y);
                    weight       = (weight < 1.0f) ? (weight * weight) : 1.0f;
                    pCorr *= weight;

                    setP(x, y, getP(x, y) + pCorr);
                    if (sL == 0) setU(x, y, getU(x, y) - pCorr);
                    if (sR == 0) setU(x + 1, y, getU(x + 1, y) + pCorr);
                    if (sT == 0) setV(x, y, getV(x, y) - pCorr);
                    if (sB == 0) setV(x, y + 1, getV(x, y + 1) + pCorr);
                }
            } else {
                for (int ii = static_cast<int>(active_indices.size()) - 1; ii >= 0; --ii) {
                    const int id = active_indices[static_cast<std::size_t>(ii)];
                    const int x  = id % kN;
                    const int y  = id / kN;
                    if (getD(x, y) < 0.2f) continue;

                    const float uL = getU(x, y);
                    const float uR = getU(x + 1, y);
                    const float vT = getV(x, y);
                    const float vB = getV(x, y + 1);

                    const float velDiv     = uR - uL + vB - vT;
                    const float densityErr = getD(x, y) - kTargetD;
                    const float targetDiv  = (densityErr > 0.0f) ? (densityErr * 0.1f) : 0.0f;
                    const float totalDiv   = velDiv - targetDiv;

                    const int sL = (x > 0) ? static_cast<int>(getS(x - 1, y)) : 1;
                    const int sR = (x < kN - 1) ? static_cast<int>(getS(x + 1, y)) : 1;
                    const int sT = (y > 0) ? static_cast<int>(getS(x, y - 1)) : 1;
                    const int sB = (y < kN - 1) ? static_cast<int>(getS(x, y + 1)) : 1;

                    const int n = 4 - (sL + sR + sT + sB);
                    if (n == 0) continue;

                    float pCorr  = (-totalDiv / static_cast<float>(n)) * omega;
                    float weight = getD(x, y);
                    weight       = (weight < 1.0f) ? (weight * weight) : 1.0f;
                    pCorr *= weight;

                    setP(x, y, getP(x, y) + pCorr);
                    if (sL == 0) setU(x, y, getU(x, y) - pCorr);
                    if (sR == 0) setU(x + 1, y, getU(x + 1, y) + pCorr);
                    if (sT == 0) setV(x, y, getV(x, y) - pCorr);
                    if (sB == 0) setV(x, y + 1, getV(x, y + 1) + pCorr);
                }
            }
        }

        extrapolateVelocity();

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                const float uVal = getU(x, y);
                if (std::abs(uVal) > kMaxVel) setU(x, y, std::copysign(kMaxVel, uVal));
            }
        }
        for (int y = 0; y <= kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                const float vVal = getV(x, y);
                if (std::abs(vVal) > kMaxVel) setV(x, y, std::copysign(kMaxVel, vVal));
            }
        }

        advectVelocityRK2(dt);

        fluxU.clear();
        fluxV.clear();

        for (int y = 0; y < kN; ++y) {
            for (int x = 1; x < kN; ++x) {
                if (getS(x, y) || getS(x - 1, y)) continue;
                const float uVal = getU(x, y);
                const int sx     = (uVal > 0.0f) ? (x - 1) : x;
                (void)fluxU.set({u32(x), u32(y)}, getD(sx, y) * uVal * dt);
            }
        }

        for (int y = 1; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getS(x, y) || getS(x, y - 1)) continue;
                const float vVal = getV(x, y);
                const int sy     = (vVal > 0.0f) ? (y - 1) : y;
                (void)fluxV.set({u32(x), u32(y)}, getD(x, sy) * vVal * dt);
            }
        }

        newD = D;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getS(x, y)) continue;
                const float flowX =
                    fluxU.get({u32(x), u32(y)}).value().get() - fluxU.get({u32(x + 1), u32(y)}).value().get();
                const float flowY =
                    fluxV.get({u32(x), u32(y)}).value().get() - fluxV.get({u32(x), u32(y + 1)}).value().get();
                float d = newD.get({u32(x), u32(y)}).value().get() + flowX + flowY;
                if (d < 0.001f) d = 0.0f;
                (void)newD.set({u32(x), u32(y)}, d);
            }
        }
        D = newD;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                const int sL = (x == 0) ? 1 : static_cast<int>(getS(x - 1, y));
                const int sR = (x == kN) ? 1 : static_cast<int>(getS(x, y));
                if (sL || sR) setU(x, y, 0.0f);
            }
        }

        for (int x = 0; x < kN; ++x) {
            for (int y = 0; y <= kN; ++y) {
                const int sT = (y == 0) ? 1 : static_cast<int>(getS(x, y - 1));
                const int sB = (y == kN) ? 1 : static_cast<int>(getS(x, y));
                if (sT || sB) setV(x, y, 0.0f);
            }
        }
    }

    void solve(float totalDt) {
        float maxVel = 0.0f;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                maxVel = std::max(maxVel, std::abs(getU(x, y)));
            }
        }
        for (int y = 0; y <= kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                maxVel = std::max(maxVel, std::abs(getV(x, y)));
            }
        }

        if (maxVel < 0.1f) maxVel = 0.1f;
        float maxAllowedDt = 0.8f / maxVel;
        if (maxAllowedDt > 0.2f) maxAllowedDt = 0.2f;

        float remaining = totalDt;
        int substeps    = 0;
        while (remaining > 0.0001f && substeps < 10) {
            float stepDt = remaining;
            if (stepDt > maxAllowedDt) stepDt = maxAllowedDt;
            physicsStep(stepDt);
            remaining -= stepDt;
            ++substeps;
        }
    }
};

struct FluidState {
    Fluid sim;
    assets::Handle<mesh::Mesh> mesh_handle;
};

glm::vec2 screen_to_world(glm::vec2 screen_pos,
                          glm::vec2 window_size,
                          const render::camera::Camera& camera,
                          const render::camera::Projection& projection,
                          const transform::Transform& cam_transform) {
    (void)projection;
    const float ndc_x = (screen_pos.x / window_size.x) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (screen_pos.y / window_size.y) * 2.0f;

    const glm::mat4 proj_matrix = camera.computed.projection;
    const glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    const glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    const glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

std::optional<std::pair<int, int>> world_to_cell(glm::vec2 world_pos) {
    const float world_w = static_cast<float>(kN) * kCellSize;
    const float world_h = static_cast<float>(kN) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    const int gx       = static_cast<int>(std::floor((world_pos.x - min_x) / kCellSize));
    const int gy_world = static_cast<int>(std::floor((world_pos.y - min_y) / kCellSize));
    const int gy       = (kN - 1) - gy_world;

    if (gx < 0 || gy < 0 || gx >= kN || gy >= kN) return std::nullopt;
    return std::pair{gx, gy};
}

mesh::Mesh build_mesh(const Fluid& sim) {
    const float world_w = static_cast<float>(kN) * kCellSize;
    const float world_h = static_cast<float>(kN) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    std::vector<std::uint32_t> indices;

    positions.reserve(static_cast<std::size_t>(kN * kN * 4));
    colors.reserve(static_cast<std::size_t>(kN * kN * 4));
    indices.reserve(static_cast<std::size_t>(kN * kN * 6));

    std::uint32_t base = 0;
    for (int y = 0; y < kN; ++y) {
        for (int x = 0; x < kN; ++x) {
            const bool wall = sim.getS(x, y) != 0;
            const float d   = sim.getD(x, y);
            if (!wall && d < 0.01f) continue;

            const int draw_y = (kN - 1) - y;

            const float x0 = min_x + static_cast<float>(x) * kCellSize;
            const float y0 = min_y + static_cast<float>(draw_y) * kCellSize;
            const float x1 = x0 + kCellSize;
            const float y1 = y0 + kCellSize;

            positions.push_back({x0, y0, 0.0f});
            positions.push_back({x1, y0, 0.0f});
            positions.push_back({x1, y1, 0.0f});
            positions.push_back({x0, y1, 0.0f});

            glm::vec4 c;
            if (wall) {
                c = glm::vec4(0.53f, 0.53f, 0.53f, 1.0f);
            } else if (d < 0.8f) {
                const float t = std::clamp(d / 0.8f, 0.0f, 1.0f);
                c = glm::vec4((5.0f * (1.0f - t)) / 255.0f, (20.0f + 80.0f * t) / 255.0f, (60.0f + 160.0f * t) / 255.0f,
                              1.0f);
            } else if (d < 1.0f) {
                const float t = std::clamp((d - 0.8f) / 0.2f, 0.0f, 1.0f);
                c             = glm::vec4(0.0f, (100.0f + 80.0f * t) / 255.0f, (220.0f + 35.0f * t) / 255.0f, 1.0f);
            } else {
                const float t = std::clamp((d - 1.0f) / 0.3f, 0.0f, 1.0f);
                c             = glm::vec4((220.0f * t) / 255.0f, (180.0f + 60.0f * t) / 255.0f, 1.0f, 1.0f);
            }

            colors.push_back(c);
            colors.push_back(c);
            colors.push_back(c);
            colors.push_back(c);

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 0);
            base += 4;
        }
    }

    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint32_t>(indices);
}

struct Plugin {
    void finish(core::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        Fluid sim;
        sim.reset();
        auto mesh_handle = mesh_assets.emplace(build_mesh(sim));

        world.spawn(mesh::Mesh2d{mesh_handle},
                    mesh::MeshMaterial2d{.color = glm::vec4(1.0f), .alpha_mode = mesh::MeshAlphaMode2d::Opaque},
                    transform::Transform{});

        world.insert_resource(FluidState{.sim = std::move(sim), .mesh_handle = std::move(mesh_handle)});

        app.add_systems(
            core::Update,
            core::into(
                [](core::ResMut<FluidState> state, core::Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                   core::Res<input::ButtonInput<input::KeyCode>> keys,
                   core::Query<core::Item<const window::CachedWindow&>, core::With<window::PrimaryWindow>> window_query,
                   core::Query<core::Item<const render::camera::Camera&, const render::camera::Projection&,
                                          const transform::Transform&>> camera_query,
                   core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                    if (keys->just_pressed(input::KeyCode::KeySpace)) state->sim.paused = !state->sim.paused;
                    if (keys->just_pressed(input::KeyCode::KeyR)) state->sim.reset();

                    if (keys->just_pressed(input::KeyCode::Key1)) state->sim.tool = PaintTool::Water;
                    if (keys->just_pressed(input::KeyCode::Key2)) state->sim.tool = PaintTool::Wall;
                    if (keys->just_pressed(input::KeyCode::Key3)) state->sim.tool = PaintTool::Eraser;

                    if (keys->just_pressed(input::KeyCode::KeyLeftBracket))
                        state->sim.pen_size = std::max(1, state->sim.pen_size - 1);
                    if (keys->just_pressed(input::KeyCode::KeyRightBracket))
                        state->sim.pen_size = std::min(20, state->sim.pen_size + 1);

                    if (keys->just_pressed(input::KeyCode::KeyMinus))
                        state->sim.dt_scale = std::max(1.0f, state->sim.dt_scale - 0.25f);
                    if (keys->just_pressed(input::KeyCode::KeyEqual))
                        state->sim.dt_scale = std::min(10.0f, state->sim.dt_scale + 0.25f);

                    if (keys->just_pressed(input::KeyCode::KeyComma))
                        state->sim.stickiness = std::max(0.0f, state->sim.stickiness - 0.1f);
                    if (keys->just_pressed(input::KeyCode::KeyPeriod))
                        state->sim.stickiness = std::min(10.0f, state->sim.stickiness + 0.1f);

                    auto win_opt = window_query.single();
                    auto cam_opt = camera_query.single();

                    if (win_opt && cam_opt) {
                        auto&& [window]                   = *win_opt;
                        auto&& [cam, proj, cam_transform] = *cam_opt;

                        const auto [cx, cy] = window.cursor_pos;
                        const auto [ww, wh] = window.size;
                        if (ww > 0 && wh > 0) {
                            const glm::vec2 world = screen_to_world(
                                glm::vec2(static_cast<float>(cx), static_cast<float>(cy)),
                                glm::vec2(static_cast<float>(ww), static_cast<float>(wh)), cam, proj, cam_transform);

                            if (auto cell = world_to_cell(world); cell.has_value()) {
                                const bool lmb = mouse_buttons->pressed(input::MouseButton::MouseButtonLeft);
                                const bool rmb = mouse_buttons->pressed(input::MouseButton::MouseButtonRight);
                                if (lmb || rmb) {
                                    const PaintTool t = rmb ? PaintTool::Eraser : state->sim.tool;
                                    state->sim.apply_brush(cell->first, cell->second, t);
                                }
                            }
                        }
                    }

                    if (!state->sim.paused) {
                        const float dt = state->sim.dt_scale * 0.05f;
                        state->sim.solve(dt);
                    }

                    (void)meshes->insert(state->mesh_handle.id(), build_mesh(state->sim));
                })
                .set_name("liquid html-port update"));
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Liquid HTML Port | 1/2/3 tool | [ ] pen | -/= dt | ,/. stick | Space pause | R reset";
    primary_window.size  = {1280, 800};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(Plugin{});

    app.run();
}

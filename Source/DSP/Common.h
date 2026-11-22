#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace me
{
constexpr double kPi = 3.14159265358979323846;

inline float flushDenormal (float x) noexcept
{
    return (std::fabs (x) < 1.0e-20f) ? 0.0f : x;
}

// Replaces NaN/Inf with 0 and hard-limits to +-limit. Last line of defence.
inline float sanitize (float x, float limit = 4.0f) noexcept
{
    if (! std::isfinite (x)) return 0.0f;
    return std::max (-limit, std::min (limit, x));
}

// Smooth saturator used for feedback and the output safety stage.
inline float softClip (float x) noexcept { return std::tanh (x); }

// Topology-preserving-transform state variable filter (Zavalishin).
// Unconditionally stable and well behaved under fast modulation.
struct SVF
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void set (double sampleRate, double freq, double q) noexcept
    {
        freq = std::max (5.0, std::min (freq, sampleRate * 0.45));
        q    = std::max (0.3, std::min (q, 5000.0));
        const double g = std::tan (kPi * freq / sampleRate);
        k  = (float) (1.0 / q);
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) (g * a1);
        a3 = (float) (g * a2);
    }

    struct Out { float lp, bp, hp; };

    Out tick (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2.0f * v1 - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        return { v2, v1, x - k * v1 - v2 };
    }
};
} // namespace me

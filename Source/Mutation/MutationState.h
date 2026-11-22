#pragma once
#include "../ParamTable.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace me
{
// A complete, restorable sound: every "sound" parameter as a normalised
// value plus the seed that the *next* mutation from this state will use.
struct MutationState
{
    float    n[kNumSound];
    uint32_t seed = 1;

    MutationState() { for (int i = 0; i < kNumSound; ++i) n[i] = toNorm (i, kParams[i].def); }

    float value (int i) const noexcept { return fromNorm (i, n[i]); }
    void  setValue (int i, float v) noexcept { n[i] = toNorm (i, v); }

    bool nearlyEqual (const MutationState& o, float eps = 1.0e-4f) const noexcept
    {
        if (seed != o.seed) return false;
        for (int i = 0; i < kNumSound; ++i) if (std::fabs (n[i] - o.n[i]) > eps) return false;
        return true;
    }

    // Locale-independent text form (integers only) for state persistence.
    std::string toString() const
    {
        std::string s = std::to_string (seed);
        for (int i = 0; i < kNumSound; ++i) s += "," + std::to_string ((long) std::lround (n[i] * 1.0e6f));
        return s;
    }

    static bool fromString (const std::string& text, MutationState& out)
    {
        MutationState r; const char* p = text.c_str(); char* end = nullptr;
        r.seed = (uint32_t) std::strtoul (p, &end, 10);
        if (end == p) return false;
        for (int i = 0; i < kNumSound; ++i)
        {
            if (*end != ',') return false;
            p = end + 1;
            const long v = std::strtol (p, &end, 10);
            if (end == p) return false;
            r.n[i] = std::fmax (0.0f, std::fmin (1.0f, (float) v * 1.0e-6f));
        }
        out = r;
        return true;
    }
};
} // namespace me

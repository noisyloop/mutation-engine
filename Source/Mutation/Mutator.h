#pragma once
#include "MutationRules.h"
#include "../DSP/Random.h"

namespace me
{
struct MutateOptions
{
    Style style      = Style::Organic;
    float intensity  = 0.5f;     // 0 = nothing changes, 1 = everything unlocked can move far
    Mask  locked     = 0;        // bit i = parameter i is locked
    SafetyLimits limits;
};

// Reproducible, musically constrained mutation.
//
//  * Deterministic: the result depends only on (base, options). The RNG is
//    seeded from base.seed and the result carries the *next* seed, so
//    pressing MUTATE repeatedly walks a reproducible sequence.
//  * Lock-stable: every parameter consumes the same number of random draws
//    whether or not it is locked, so locking one parameter never changes what
//    any other parameter would have become.
//  * Locked parameters are never modified, not even by the safety stage.
namespace Mutator
{
inline float shape (Style st, float a, float b) noexcept
{
    switch (st)
    {
        case Style::Organic:  return 0.5f * (a + b);                             // centre-weighted, smooth
        case Style::Digital:  return a;                                          // uniform, later quantised
        case Style::Unstable: { const float x = 2.0f * a - 1.0f;                 // edge-weighted extremes
                                return 0.5f + 0.5f * (x < 0 ? -1.0f : 1.0f) * std::sqrt (std::fabs (x)); }
    }
    return a;
}

inline float midiToHz (float m) noexcept { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
inline float hzToMidi (float f) noexcept { return 69.0f + 12.0f * std::log2 (std::fmax (f, 1.0f) / 440.0f); }

inline MutationState mutate (const MutationState& base, const MutateOptions& o)
{
    MutationState s = base;
    Rng rng; rng.seed (base.seed);
    const int   st    = (int) o.style;
    const float inten = std::fmax (0.0f, std::fmin (1.0f, o.intensity));

    for (int i = 0; i < kNumSound; ++i)
    {
        if (! kParams[i].mutates) continue;

        // Fixed draw budget per parameter (see lock-stability note above).
        const float d0 = rng.uniform(), d1 = rng.uniform(), d2 = rng.uniform(), d3 = rng.uniform();
        if (o.locked & bit (i)) continue;
        if (d0 >= 0.25f + 0.75f * inten) continue;                 // not touched this time

        const float k = (o.style == Style::Digital) ? std::fmin (1.0f, inten * 1.8f)
                                                    : inten * (st == 0 ? 0.35f + 0.4f * d1 : 0.6f + 0.4f * d1);
        if (k <= 0.0f) continue;

        const float cur = s.n[i];
        const float t01 = shape (o.style, d2, d3);

        if (kParams[i].kind == Kind::Choice)
        {
            if (o.style == Style::Organic || d2 >= inten * 0.8f) continue;
            const int options = (i == pid::FiltType && o.style == Style::Digital) ? 3 : numChoices (i);
            s.setValue (i, (float) std::min (options - 1, (int) (d3 * (float) options)));
            continue;
        }

        float next;
        if (i == pid::ResRoot)
        {
            // Pitch moves in semitone space. Organic/Digital land on a minor
            // pentatonic scale and snap to semitones; Unstable is free/microtonal.
            static const int scale[5] = { 0, 3, 5, 7, 10 };
            const float curM = hzToMidi (s.value (i));
            const float tgtM = (o.style == Style::Unstable)
                                   ? 24.0f + t01 * 60.0f
                                   : (float) (33 + 12 * std::min (2, (int) (d2 * 3.0f)) + scale[std::min (4, (int) (d3 * 5.0f))]);
            float m = curM + (tgtM - curM) * k;
            if (o.style != Style::Unstable) m = std::round (m);
            s.setValue (i, midiToHz (m));
            continue;
        }
        if (i == pid::ResVoices)
        {
            static const int pow2[4] = { 4, 8, 16, 32 };
            const float target = (o.style == Style::Organic) ? 8.0f + t01 * 20.0f
                               : (o.style == Style::Digital) ? (float) pow2[std::min (3, (int) (d2 * 4.0f))]
                                                             : 1.0f + t01 * 31.0f;
            s.setValue (i, std::round (s.value (i) + (target - s.value (i)) * k));   // whole voices
            continue;
        }

        const Rule r = ruleFor (i);
        float t = t01;
        if (o.style == Style::Digital && r.steps > 1) t = std::round (t * (float) (r.steps - 1)) / (float) (r.steps - 1);
        const float target = r.lo[st] + t * (r.hi[st] - r.lo[st]);
        next = std::fmax (0.0f, std::fmin (1.0f, cur + (target - cur) * k));
        s.n[i] = next;
        if (kParams[i].kind == Kind::Int) s.n[i] = toNorm (i, fromNorm (i, next));   // snap to a whole value
    }

    const uint32_t engineSeed = rng.nextU32() & 0xFFFFu;           // drawn even when locked
    if (! (o.locked & bit (pid::Seed))) s.setValue (pid::Seed, (float) engineSeed);

    s.seed = nextSeed (base.seed);
    applySafety (s, o.limits, o.locked);
    return s;
}

// Blend two mutations. Only mutable, unlocked parameters morph; everything
// else (mode, mix, output, locks...) stays as in `current`. Discrete
// parameters switch at the halfway point. Safety is re-applied because the
// midpoint of two safe sounds is not guaranteed to be safe.
inline MutationState morph (const MutationState& a, const MutationState& b, float t, Mask locked,
                            const MutationState& current, const SafetyLimits& limits = {})
{
    t = std::fmax (0.0f, std::fmin (1.0f, t));
    MutationState r = current;
    for (int i = 0; i < kNumSound; ++i)
    {
        if (! kParams[i].mutates || (locked & bit (i))) continue;
        const bool discrete = kParams[i].kind == Kind::Choice || kParams[i].kind == Kind::Bool;
        r.n[i] = discrete ? (t < 0.5f ? a.n[i] : b.n[i]) : a.n[i] + (b.n[i] - a.n[i]) * t;
        if (kParams[i].kind == Kind::Int) r.n[i] = toNorm (i, fromNorm (i, r.n[i]));
    }
    applySafety (r, limits, locked);
    return r;
}
} // namespace Mutator
} // namespace me

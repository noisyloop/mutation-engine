#pragma once
#include "MutationState.h"

namespace me
{
enum class Style { Organic = 0, Digital = 1, Unstable = 2 };

using Mask = uint64_t;
constexpr Mask bit (int i) noexcept { return Mask (1) << i; }

// ----------------------------------------------------------------- locks
inline constexpr int kNumLockGroups = 5;
struct LockGroup { Group group; int paramIndex; };
inline constexpr LockGroup kLockGroups[kNumLockGroups] = {
    { Group::Resonator, pid::LockRes }, { Group::Formant, pid::LockForm }, { Group::Filter, pid::LockFilt },
    { Group::Spectral,  pid::LockSpec }, { Group::Glitch,  pid::LockGl } };

inline Mask groupMask (Group g) noexcept
{
    Mask m = 0;
    for (int i = 0; i < kNumSound; ++i) if (kParams[i].group == g) m |= bit (i);
    return m;
}

// Per-parameter locks plus whole-group locks (bit k = kLockGroups[k]).
inline Mask lockMaskFor (Mask paramLocks, unsigned groupBits) noexcept
{
    Mask m = paramLocks;
    for (int k = 0; k < kNumLockGroups; ++k) if (groupBits & (1u << k)) m |= groupMask (kLockGroups[k].group);
    return m & ((Mask (1) << kNumSound) - 1);
}

// ---------------------------------------------------------------- safety
struct SafetyLimits
{
    float maxFeedback     = 0.85f;  // resonator feedback ceiling
    float dampThreshold   = 0.5f;   // above this feedback...
    float dampFloor       = 0.25f;  // ...damping must be at least this
    float maxFilterRes    = 12.0f;
    float minLowpassHz    = 150.0f; // a low-pass below this just mutes everything
    float lowCutoffHz     = 300.0f; // below this cutoff the Q is limited further
    float lowCutoffMaxRes = 4.0f;
    float maxDropout      = 0.85f;  // always leave some bins alive
    int   maxShift        = 96;
    int   maxVoices       = 32;     // CPU budget: lowered by the host plugin under load
};

// Brings an *unlocked* combination back into a safe, usable region. Locked
// parameters are never touched; if the user locks a dangerous value, the
// unlocked partner parameter compensates, and the DSP's own hard limits
// remain the last line of defence.
inline void applySafety (MutationState& s, const SafetyLimits& L, Mask locked) noexcept
{
    const auto free = [&] (int i) { return (locked & bit (i)) == 0; };

    if (free (pid::ResFeed) && s.value (pid::ResFeed) > L.maxFeedback) s.setValue (pid::ResFeed, L.maxFeedback);
    if (s.value (pid::ResFeed) > L.dampThreshold && s.value (pid::ResDamp) < L.dampFloor)
    {
        if (free (pid::ResDamp))      s.setValue (pid::ResDamp, L.dampFloor);
        else if (free (pid::ResFeed)) s.setValue (pid::ResFeed, L.dampThreshold);
    }

    if (free (pid::FiltRes) && s.value (pid::FiltRes) > L.maxFilterRes) s.setValue (pid::FiltRes, L.maxFilterRes);
    const float cutoff = s.value (pid::FiltCutoff);
    if (free (pid::FiltCutoff) && (int) s.value (pid::FiltType) == 0 && cutoff < L.minLowpassHz)
        s.setValue (pid::FiltCutoff, L.minLowpassHz);
    if (free (pid::FiltRes) && s.value (pid::FiltCutoff) < L.lowCutoffHz && s.value (pid::FiltRes) > L.lowCutoffMaxRes)
        s.setValue (pid::FiltRes, L.lowCutoffMaxRes);

    if (free (pid::SpDrop) && s.value (pid::SpDrop) > L.maxDropout) s.setValue (pid::SpDrop, L.maxDropout);
    if (free (pid::SpShift))
        s.setValue (pid::SpShift, std::fmax ((float) -L.maxShift, std::fmin ((float) L.maxShift, s.value (pid::SpShift))));
    if (free (pid::ResVoices) && s.value (pid::ResVoices) > (float) L.maxVoices) s.setValue (pid::ResVoices, (float) L.maxVoices);
}

// ----------------------------------------------------------------- rules
// Normalised-space target range per style [Organic, Digital, Unstable], and
// the number of discrete steps Digital snaps to.
struct Rule { float lo[3]; float hi[3]; int steps; };

inline Rule ruleFor (int i) noexcept
{
    switch (i)
    {
        case pid::ResInharm:  return { { 0.00f, 0.0f, 0.2f }, { 0.35f, 1.0f, 1.0f }, 4 };
        case pid::ResDamp:    return { { 0.20f, 0.1f, 0.0f }, { 0.75f, 0.9f, 1.0f }, 4 };
        case pid::ResFeed:    return { { 0.00f, 0.0f, 0.2f }, { 0.40f, 0.6f, 0.95f }, 4 };
        case pid::ResSpread:  return { { 0.30f, 0.0f, 0.0f }, { 0.90f, 1.0f, 1.0f }, 4 };
        case pid::FiltCutoff: return { { 0.45f, 0.35f, 0.2f }, { 0.95f, 1.0f, 1.0f }, 6 };
        case pid::FiltRes:    return { { 0.00f, 0.0f, 0.1f }, { 0.30f, 0.5f, 0.8f }, 4 };
        case pid::Vowel:      return { { 0.00f, 0.0f, 0.0f }, { 1.00f, 1.0f, 1.0f }, 5 };   // 5 steps = A E I O U
        case pid::FormShift:  return { { 0.45f, 0.4f, 0.0f }, { 0.70f, 0.8f, 1.0f }, 5 };
        case pid::FormMix:    return { { 0.00f, 0.0f, 0.0f }, { 0.60f, 1.0f, 1.0f }, 4 };
        case pid::GlProb:     return { { 0.00f, 0.0f, 0.0f }, { 0.50f, 1.0f, 1.0f }, 4 };
        case pid::GlAmt:      return { { 0.00f, 0.0f, 0.0f }, { 0.50f, 1.0f, 1.0f }, 4 };
        case pid::SpDrift:    return { { 0.00f, 0.0f, 0.0f }, { 0.50f, 1.0f, 1.0f }, 4 };
        case pid::SpShift:    return { { 0.45f, 0.25f, 0.0f }, { 0.55f, 0.75f, 1.0f }, 8 }; // +-13 / +-64 / full
        case pid::SpDrop:     return { { 0.00f, 0.0f, 0.0f }, { 0.15f, 0.40f, 0.9f }, 5 };
        case pid::SpScram:    return { { 0.00f, 0.0f, 0.0f }, { 0.30f, 0.60f, 1.0f }, 4 };
        default:              return { { 0, 0, 0 }, { 1, 1, 1 }, 0 };
    }
}

inline uint32_t nextSeed (uint32_t s) noexcept { return s * 1664525u + 1013904223u; }
} // namespace me

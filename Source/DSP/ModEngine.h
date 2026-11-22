#pragma once
#include "Common.h"
#include "Random.h"
#include "../ParamTable.h"
#include "../Mutation/MutationRules.h"

namespace me
{
// Block-rate modulation: two LFOs, an input envelope follower, a seeded
// sample-and-hold random source and MIDI velocity, routed through four
// assignment slots to a fixed set of destinations.
//
// A slot adds  amount * source  to the destination's *normalised* value, so
// +-1 can sweep the destination's whole range. Modulated values are then
// pulled back into the safe region (see safeguard()).
class ModEngine
{
public:
    enum Source { None = 0, Lfo1, Lfo2, Envelope, Random, Velocity, kNumSources };

    struct Slot { int src = 0; int dst = 0; float amount = 0.0f; };

    void prepare (double sr) { sampleRate = sr; reset (1); }

    void reset (uint32_t seed)
    {
        rng.seed (seed);
        phase[0] = 0.0f; phase[1] = 0.37f;
        env = 0.0f; rndTarget = rndValue = 0.0f; rndPhase = 0.0f; velocity = 0.0f;
        for (auto& v : values) v = 0.0f;
    }

    void reseed (uint32_t seed) { rng.seed (seed); }
    void setRates (float lfo1Hz, int shape1, float lfo2Hz, int shape2, float rndHz)
    { rate[0] = lfo1Hz; rate[1] = lfo2Hz; shape[0] = shape1; shape[1] = shape2; rndRate = rndHz; }
    void setVelocity (float v) { velocity = v; }
    void setSlots (const Slot* s) { for (int i = 0; i < kNumModSlots; ++i) slots[i] = s[i]; }

    // Advance by one block. `inputLevel` = mean |input| over the block.
    void advance (int numSamples, float inputLevel) noexcept
    {
        const float dt = (float) numSamples / (float) sampleRate;
        for (int i = 0; i < 2; ++i)
        {
            phase[i] += rate[i] * dt; phase[i] -= std::floor (phase[i]);
            values[Lfo1 + i] = wave (shape[i], phase[i]);
        }
        const float coeff = inputLevel > env ? 1.0f - std::exp (-dt / 0.005f) : 1.0f - std::exp (-dt / 0.15f);   // 5 ms attack, 150 ms release
        env += (inputLevel - env) * coeff;
        values[Envelope] = std::fmin (1.0f, env * 4.0f);

        rndPhase += rndRate * dt;
        if (rndPhase >= 1.0f) { rndPhase -= std::floor (rndPhase); rndTarget = rng.bipolar(); }
        rndValue += (rndTarget - rndValue) * (1.0f - std::exp (-dt / 0.03f));
        values[Random] = rndValue;
        values[Velocity] = velocity;
    }

    float source (int s) const noexcept { return s > 0 && s < kNumSources ? values[s] : 0.0f; }

    // Total normalised offset applied to destination parameter `paramIndex`.
    float offsetFor (int paramIndex) const noexcept
    {
        float o = 0.0f;
        for (const auto& sl : slots)
            if (sl.src > 0 && sl.dst > 0 && sl.dst < kNumModDsts && kModDst[sl.dst] == paramIndex) o += sl.amount * source (sl.src);
        return o;
    }

    float modulate (int paramIndex, float baseValue) const noexcept
    {
        const float off = offsetFor (paramIndex);
        if (std::fabs (off) < 1.0e-9f) return baseValue;
        return fromNorm (paramIndex, std::fmax (0.0f, std::fmin (1.0f, toNorm (paramIndex, baseValue) + off)));
    }

    bool isModulated (int paramIndex) const noexcept
    {
        for (const auto& sl : slots) if (sl.src > 0 && sl.dst > 0 && sl.dst < kNumModDsts && kModDst[sl.dst] == paramIndex && std::fabs (sl.amount) > 1.0e-9f) return true;
        return false;
    }

    static float wave (int shape, float p) noexcept
    {
        switch (shape)
        {
            case 0:  return (float) std::sin (2.0 * kPi * p);
            case 1:  return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            case 2:  return 2.0f * p - 1.0f;
            default: return p < 0.5f ? 1.0f : -1.0f;
        }
    }

    // Keeps *modulated* values inside the same limits the mutator enforces.
    static void safeguard (float& feedback, float& damping, float& filterRes, float& cutoff, int filterType, float& dropout,
                           const SafetyLimits& L = {}) noexcept
    {
        feedback = std::fmin (feedback, L.maxFeedback);
        if (feedback > L.dampThreshold && damping < L.dampFloor) damping = L.dampFloor;
        filterRes = std::fmin (filterRes, L.maxFilterRes);
        if (filterType == 0) cutoff = std::fmax (cutoff, L.minLowpassHz);
        if (cutoff < L.lowCutoffHz) filterRes = std::fmin (filterRes, L.lowCutoffMaxRes);
        dropout = std::fmin (dropout, L.maxDropout);
    }

private:
    double sampleRate = 44100.0;
    Rng rng;
    float phase[2] = { 0, 0.37f }, rate[2] = { 1, 0.25f };
    int shape[2] = { 0, 1 };
    float rndRate = 2.0f, rndTarget = 0, rndValue = 0, rndPhase = 0, env = 0, velocity = 0;
    float values[kNumSources] = {};
    Slot slots[kNumModSlots];
};
} // namespace me

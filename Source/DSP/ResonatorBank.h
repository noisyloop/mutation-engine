#pragma once
#include "Common.h"
#include <cstring>

namespace me
{
// 32 parallel two-pole resonators (unity-peak band-pass) with harmonic or
// inharmonic tuning, per-voice damping, stereo spread and a soft-clipped
// feedback path. Input is mono-summed, output is stereo.
class ResonatorBank
{
public:
    static constexpr int kMaxVoices = 32;

    struct Params
    {
        float rootHz     = 110.0f;
        int   voices     = 16;
        float inharmonic = 0.0f;  // 0 = harmonic series, 1 = stiff/metallic
        float damping    = 0.5f;  // 0 = long ring (~6 s), 1 = very short (~20 ms)
        float feedback   = 0.0f;  // 0..1, internally capped below runaway
        float spread     = 0.5f;  // 0..1 stereo width
    };

    void prepare (double sr) { sampleRate = sr; configured = false; reset(); setParams (params); }

    void reset()
    {
        for (auto& f : filters) f.reset();
        fbL = fbR = 0.0f;
    }

    void setParams (const Params& p)
    {
        // Cheap early-out: the plugin calls this every few samples while parameters glide.
        if (configured && p.voices == params.voices && sameBits (p.rootHz, params.rootHz) && sameBits (p.inharmonic, params.inharmonic)
            && sameBits (p.damping, params.damping) && sameBits (p.feedback, params.feedback) && sameBits (p.spread, params.spread))
            return;
        configured = true;
        params = p;
        params.voices   = std::max (1, std::min (p.voices, kMaxVoices));
        params.feedback = std::max (0.0f, std::min (p.feedback, 1.0f));
        const double B  = (double) p.inharmonic * 0.02;   // stiffness coefficient
        const double t60 = 6.0 * std::pow (0.02 / 6.0, (double) std::max (0.0f, std::min (p.damping, 1.0f)));

        active = 0;
        for (int i = 0; i < params.voices; ++i)
        {
            const double n = (double) (i + 1);
            double f = p.rootHz * n * std::sqrt (1.0 + B * n * n);
            // Inharmonic mode also detunes odd partials slightly for beating.
            f *= 1.0 + (double) p.inharmonic * 0.0035 * ((i & 1) ? 1.0 : -1.0) * std::sin (n * 1.7);

            if (f >= sampleRate * 0.45) { gainTable[i] = 0.0f; continue; }
            const double q = std::max (0.5, std::min (kPi * f * t60 / 6.9078, 3000.0));
            filters[i].set (sampleRate, f, q);

            // Higher partials a little quieter so the sum stays balanced.
            const double pan = (i % 2 == 0 ? -1.0 : 1.0) * (double) p.spread
                             * (0.35 + 0.65 * std::fabs (std::sin (n * 2.399)));
            const double ang = (pan * 0.5 + 0.5) * kPi * 0.5;
            panL[i] = (float) std::cos (ang);
            panR[i] = (float) std::sin (ang);
            gainTable[i] = (float) (1.0 / std::pow (n, 0.35));
            ++active;
        }
        for (int i = params.voices; i < kMaxVoices; ++i) gainTable[i] = 0.0f;
        norm = active > 0 ? 1.0f / std::sqrt ((float) active) : 0.0f;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float fb = params.feedback * 0.92f; // hard cap: loop gain stays < 1 before saturation
        const float x  = 0.5f * (inL + inR) + fb * softClip (0.5f * (fbL + fbR));
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < params.voices; ++i)
        {
            if (gainTable[i] <= 0.0f) continue;
            auto o = filters[i].tick (x);
            const float y = o.bp * filters[i].k * gainTable[i];
            l += y * panL[i];
            r += y * panR[i];
        }
        fbL = outL = sanitize (l * norm * 2.0f);
        fbR = outR = sanitize (r * norm * 2.0f);
    }

    /// Centre frequency of voice i (for the resonance display / tests).
    double voiceFrequency (int i) const
    {
        const double n = (double) (i + 1);
        const double B = (double) params.inharmonic * 0.02;
        return params.rootHz * n * std::sqrt (1.0 + B * n * n);
    }

private:
    static bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }   // exact, warning-free
    double sampleRate = 44100.0;
    bool configured = false;
    Params params;
    int active = 0;
    float norm = 1.0f, fbL = 0.0f, fbR = 0.0f;
    SVF filters[kMaxVoices];
    float gainTable[kMaxVoices] {}, panL[kMaxVoices] {}, panR[kMaxVoices] {};
};
} // namespace me

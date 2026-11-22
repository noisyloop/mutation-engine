#pragma once
#include "Common.h"
#include "Random.h"
#include <vector>

namespace me
{
// Tempo-synced glitch events. The engine listens to the musical position;
// on every grid step it (deterministically, via the seeded RNG) decides
// whether to start an event lasting one or two steps:
//   Stutter - loops a short slice of the most recent audio
//   Reverse - plays the last step backwards
//   Crush   - sample-rate + bit-depth reduction
//   Gate    - hard chop
// Events cross-fade in/out over 64 samples. With probability 0 the engine is
// bit-transparent and adds no latency.
class GlitchEngine
{
public:
    enum Type { Stutter = 0, Reverse, Crush, Gate, kNumTypes };

    struct Params
    {
        float bpm          = 120.0f;
        int   stepsPerBeat = 4;       // 1,2,4,8 = 1/4, 1/8, 1/16, 1/32
        float probability  = 0.0f;    // 0..1 chance per step
        float intensity    = 0.5f;    // 0..1 event severity
        int   forceType    = -1;      // -1 = random per event
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        maxLen = (int) (sr * 2.0);
        for (auto& r : ring)  r.assign ((size_t) maxLen, 0.0f);
        for (auto& s : slice) s.assign ((size_t) maxLen, 0.0f);
        reset (1);
    }

    void reset (uint32_t seed = 1)
    {
        for (auto& r : ring)  std::fill (r.begin(), r.end(), 0.0f);
        for (auto& s : slice) std::fill (s.begin(), s.end(), 0.0f);
        rng.seed (seed);
        w = 0; lastStep = -1000000000LL; active = false; remaining = 0; wetGain = 0.0f; pos = 0;
        holdCnt = 0; held[0] = held[1] = 0.0f; ppq = 0.0;
    }

    void reseed (uint32_t seed) { rng.seed (seed); }
    void setParams (const Params& p) { params = p; params.stepsPerBeat = std::max (1, p.stepsPerBeat); }
    void setPosition (double ppqPosition) { ppq = ppqPosition; }
    int  stepLengthSamples() const { return stepLen(); }

    void process (float& l, float& r) noexcept
    {
        // Advance musical time and detect grid boundaries.
        const long long step = (long long) std::floor (ppq * params.stepsPerBeat);
        if (step != lastStep) { lastStep = step; onStep(); }
        ppq += (double) params.bpm / (60.0 * sampleRate);

        const float in[2] = { l, r };
        for (int c = 0; c < 2; ++c) ring[c][(size_t) w] = in[c];
        w = (w + 1) % maxLen;

        const float target = active ? 1.0f : 0.0f;
        wetGain += std::max (-1.0f / 64.0f, std::min (1.0f / 64.0f, target - wetGain));

        if (wetGain > 0.0f)
        {
            float out[2];
            for (int c = 0; c < 2; ++c) out[c] = render (c, in[c]);
            ++pos;
            if (holdCnt == 0) { held[0] = out[0]; held[1] = out[1]; }
            holdCnt = (holdCnt + 1) % std::max (1, holdFactor);
            l = in[0] + wetGain * (out[0] - in[0]);
            r = in[1] + wetGain * (out[1] - in[1]);
            if (active && --remaining <= 0) active = false;
        }
    }

private:
    int stepLen() const
    {
        const double len = sampleRate * 60.0 / ((double) std::max (20.0f, params.bpm) * params.stepsPerBeat);
        return std::max (64, std::min (maxLen, (int) std::lround (len)));
    }

    void capture (int n)
    {
        n = std::min (n, maxLen);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                slice[c][(size_t) i] = ring[c][(size_t) (((w - n + i) % maxLen + maxLen) % maxLen)];
        sliceLen = n;
    }

    void onStep()
    {
        if (active) return;
        if (rng.uniform() >= params.probability) return;
        type = params.forceType >= 0 ? std::min (params.forceType, (int) kNumTypes - 1) : rng.range (kNumTypes);
        const int steps = 1 + (rng.uniform() < 0.3f ? 1 : 0);
        remaining = steps * stepLen();
        active = true; pos = 0; holdCnt = 0;
        const float it = std::max (0.0f, std::min (1.0f, params.intensity));
        switch (type)
        {
            case Stutter: capture (std::max (32, stepLen() >> (1 + (int) (it * 3.0f)))); break;
            case Reverse: capture (stepLen()); break;
            case Crush:   holdFactor = 2 + (int) (it * 30.0f); bits = std::max (3, 12 - (int) (it * 9.0f)); break;
            default: break;
        }
    }

    float render (int c, float x) noexcept
    {
        switch (type)
        {
            case Stutter: return slice[c][(size_t) (pos % sliceLen)];
            case Reverse: return slice[c][(size_t) (sliceLen - 1 - pos % sliceLen)];
            case Crush:
            {
                const float q = (float) (1 << (bits - 1));
                return (holdCnt == 0) ? std::round (x * q) / q : held[c];
            }
            default: return 0.0f;
        }
    }

    double sampleRate = 44100.0, ppq = 0.0;
    int maxLen = 1, w = 0, sliceLen = 1, pos = 0, remaining = 0, holdCnt = 0, holdFactor = 2, bits = 8, type = 0;
    long long lastStep = 0;
    bool active = false;
    float wetGain = 0.0f, held[2] = { 0, 0 };
    Params params;
    Rng rng;
    std::vector<float> ring[2], slice[2];
};
} // namespace me

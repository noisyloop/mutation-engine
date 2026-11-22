#pragma once
#include "Common.h"
#include "Random.h"
#include <vector>

namespace me
{
// Radix-2 complex FFT (in place).
class FFT
{
public:
    explicit FFT (int size) : n (size), cs ((size_t) size / 2), sn ((size_t) size / 2), rev ((size_t) size)
    {
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
        {
            cs[(size_t) i] = (float) std::cos (2.0 * kPi * i / n);
            sn[(size_t) i] = (float) std::sin (2.0 * kPi * i / n);
        }
    }

    void run (float* re, float* im, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        const float sgn = inverse ? 1.0f : -1.0f;
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cs[(size_t) (j * step)], wi = sgn * sn[(size_t) (j * step)];
                    const int a = i + j, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

private:
    int n;
    std::vector<float> cs, sn;
    std::vector<int> rev;
};

// Stereo STFT processor (1024-point Hann, 75% overlap). With neutral
// parameters it is a transparent delay of latencySamples().
//   freeze    - holds the current magnitudes; phases keep advancing so the
//               frozen spectrum sustains as a drone
//   drift     - random-walks each frozen partial's frequency ("drifting partials")
//   binShift  - moves the whole spectrum up/down by N bins
//   dropout   - fraction of bins zeroed per frame
//   scramble  - fraction of bins swapped with a near neighbour per frame
// All randomness comes from a seedable RNG that is independent of the audio.
class SpectralEngine
{
public:
    static constexpr int kFFT = 1024, kHop = kFFT / 4, kBins = kFFT / 2 + 1, kScrambleRange = 16;
    static constexpr int latencySamples() { return kFFT; }

    struct Params
    {
        bool  freeze   = false;
        float drift    = 0.0f;   // 0..1
        int   binShift = 0;      // -128..128
        float dropout  = 0.0f;   // 0..1
        float scramble = 0.0f;   // 0..1
    };

    SpectralEngine()
    {
        for (int i = 0; i < kFFT; ++i) win[i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFFT));
        reset (1);
    }

    void reset (uint32_t seed = 1)
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& v : inFifo[c]) v = 0.0f;
            for (auto& v : outFifo[c]) v = 0.0f;
            for (auto& v : accum[c]) v = 0.0f;
            for (auto& v : frozenMag[c]) v = 0.0f;
            for (auto& v : frozenPhase[c]) v = 0.0f;
        }
        for (auto& v : freqOff) v = 0.0f;
        rover = kFFT - kHop; frozen = false;
        rng.seed (seed);
    }

    void reseed (uint32_t seed) { rng.seed (seed); }
    void setParams (const Params& p)
    {
        params = p;
        params.binShift = std::max (-128, std::min (128, p.binShift));
        params.dropout  = std::max (0.0f, std::min (1.0f, p.dropout));
        params.scramble = std::max (0.0f, std::min (1.0f, p.scramble));
        params.drift    = std::max (0.0f, std::min (1.0f, p.drift));
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const int lat = kFFT - kHop;
        inFifo[0][rover] = inL; inFifo[1][rover] = inR;
        outL = outFifo[0][rover - lat]; outR = outFifo[1][rover - lat];
        if (++rover >= kFFT) { rover = lat; frame(); }
    }

private:
    void frame() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kFFT; ++k) { re[c][k] = inFifo[c][k] * win[k]; im[c][k] = 0.0f; }
            fft.run (re[c], im[c], false);
        }

        if (params.freeze)
        {
            if (! frozen)
            {
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < kBins; ++k)
                    {
                        frozenMag[c][k]   = std::sqrt (re[c][k] * re[c][k] + im[c][k] * im[c][k]);
                        frozenPhase[c][k] = std::atan2 (im[c][k], re[c][k]);
                    }
                frozen = true;
            }
            else
            {
                for (int k = 0; k < kBins; ++k)
                {
                    // Mean-reverting random walk of per-bin frequency offset (radians per hop).
                    freqOff[k] = 0.9f * freqOff[k] + params.drift * rng.bipolar() * 0.6f;
                    const float adv = (float) (2.0 * kPi * k * kHop / kFFT) + freqOff[k];
                    for (int c = 0; c < 2; ++c)
                    {
                        float& ph = frozenPhase[c][k];
                        ph += adv;
                        if (ph > 3.14159265f) ph -= 6.28318531f * std::floor ((ph + 3.14159265f) / 6.28318531f);
                        re[c][k] = frozenMag[c][k] * std::cos (ph);
                        im[c][k] = frozenMag[c][k] * std::sin (ph);
                    }
                }
            }
        }
        else frozen = false;

        if (params.binShift != 0)
        {
            for (int c = 0; c < 2; ++c)
            {
                for (int k = 0; k < kBins; ++k) { tmpR[k] = re[c][k]; tmpI[k] = im[c][k]; }
                for (int k = 0; k < kBins; ++k)
                {
                    const int src = k - params.binShift;
                    const bool ok = src >= 0 && src < kBins;
                    re[c][k] = ok ? tmpR[src] : 0.0f;
                    im[c][k] = ok ? tmpI[src] : 0.0f;
                }
            }
        }

        if (params.scramble > 0.0f)
            for (int k = 1; k < kBins - 1; ++k)
                if (rng.uniform() < params.scramble)
                {
                    const int j = std::max (1, std::min (kBins - 2, k + rng.range (2 * kScrambleRange + 1) - kScrambleRange));
                    for (int c = 0; c < 2; ++c) { std::swap (re[c][k], re[c][j]); std::swap (im[c][k], im[c][j]); }
                }

        if (params.dropout > 0.0f)
            for (int k = 0; k < kBins; ++k)
                if (rng.uniform() < params.dropout)
                    for (int c = 0; c < 2; ++c) re[c][k] = im[c][k] = 0.0f;

        constexpr float scale = 1.0f / ((float) kFFT * 1.5f);   // IFFT scale * Hann^2 overlap gain at 75%
        for (int c = 0; c < 2; ++c)
        {
            im[c][0] = 0.0f; im[c][kBins - 1] = 0.0f;
            for (int k = 1; k < kBins - 1; ++k) { re[c][kFFT - k] = re[c][k]; im[c][kFFT - k] = -im[c][k]; }
            fft.run (re[c], im[c], true);
            for (int k = 0; k < kFFT; ++k) accum[c][k] += sanitize (re[c][k] * scale * win[k], 16.0f);
            for (int k = 0; k < kHop; ++k) outFifo[c][k] = accum[c][k];
            for (int k = 0; k < kFFT - kHop; ++k) accum[c][k] = accum[c][k + kHop];
            for (int k = kFFT - kHop; k < kFFT; ++k) accum[c][k] = 0.0f;
            for (int k = 0; k < kFFT - kHop; ++k) inFifo[c][k] = inFifo[c][k + kHop];
        }
    }

    FFT fft { kFFT };
    Params params;
    Rng rng;
    int rover = 0;
    bool frozen = false;
    float win[kFFT];
    float inFifo[2][kFFT], outFifo[2][kHop], accum[2][kFFT];
    float re[2][kFFT], im[2][kFFT], tmpR[kBins], tmpI[kBins];
    float frozenMag[2][kBins], frozenPhase[2][kBins], freqOff[kBins];
};
} // namespace me

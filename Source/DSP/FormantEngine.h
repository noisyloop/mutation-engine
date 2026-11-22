#pragma once
#include "Common.h"

namespace me
{
// Three-peak vowel formant filter. `vowel` morphs continuously through
// A - E - I - O - U (0..4); `shift` scales all formants (0.5..2).
class FormantEngine
{
public:
    struct Vowel { float f[3]; float bw[3]; float amp[3]; };

    static Vowel vowelAt (float position)
    {
        static const Vowel table[5] = {
            { { 800, 1150, 2900 }, { 80,  90, 120 }, { 1.0f, 0.50f, 0.25f } }, // A
            { { 400, 1600, 2700 }, { 60,  80, 120 }, { 1.0f, 0.45f, 0.25f } }, // E
            { { 350, 1700, 2700 }, { 50, 100, 120 }, { 1.0f, 0.35f, 0.25f } }, // I
            { { 450,  800, 2830 }, { 70,  80, 100 }, { 1.0f, 0.45f, 0.20f } }, // O
            { { 325,  700, 2530 }, { 50,  60, 170 }, { 1.0f, 0.35f, 0.15f } }, // U
        };
        position = std::max (0.0f, std::min (position, 4.0f));
        const int i = std::min ((int) position, 3);
        const float t = position - (float) i;
        Vowel v;
        for (int k = 0; k < 3; ++k)
        {
            v.f[k]   = std::exp ((1.0f - t) * std::log (table[i].f[k]) + t * std::log (table[i + 1].f[k]));
            v.bw[k]  = (1.0f - t) * table[i].bw[k]  + t * table[i + 1].bw[k];
            v.amp[k] = (1.0f - t) * table[i].amp[k] + t * table[i + 1].amp[k];
        }
        return v;
    }

    void prepare (double sr) { sampleRate = sr; reset(); set (vowelPos, shift); }
    void reset() { for (auto& c : ch) for (auto& f : c) f.reset(); }

    void set (float vowel, float formantShift)
    {
        vowelPos = vowel; shift = formantShift;
        cur = vowelAt (vowel);
        for (int k = 0; k < 3; ++k)
        {
            const double f = (double) cur.f[k] * (double) formantShift;
            const double q = std::max (1.0, f / (double) cur.bw[k]);
            ch[0][k].set (sampleRate, f, q);
            ch[1][k].set (sampleRate, f, q);
        }
    }

    void process (float& xl, float& xr) noexcept
    {
        float sl = 0.0f, sr = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            sl += ch[0][k].tick (xl).bp * ch[0][k].k * cur.amp[k];
            sr += ch[1][k].tick (xr).bp * ch[1][k].k * cur.amp[k];
        }
        xl = sl; xr = sr;
    }

    const Vowel& current() const { return cur; }

private:
    double sampleRate = 44100.0;
    float vowelPos = 0.0f, shift = 1.0f;
    Vowel cur = vowelAt (0.0f);
    SVF ch[2][3];
};
} // namespace me

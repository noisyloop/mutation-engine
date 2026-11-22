#pragma once
#include "Common.h"

namespace me
{
// Stereo multimode filter: LP / HP / BP / Notch.
class FilterBank
{
public:
    enum Type { LowPass = 0, HighPass, BandPass, Notch };

    void prepare (double sr) { sampleRate = sr; reset(); set (type, cutoff, q); }
    void reset() { l.reset(); r.reset(); }

    void set (Type t, double cutoffHz, double resonanceQ)
    {
        type = t; cutoff = cutoffHz; q = resonanceQ;
        l.set (sampleRate, cutoffHz, resonanceQ);
        r.set (sampleRate, cutoffHz, resonanceQ);
    }

    void process (float& xl, float& xr) noexcept
    {
        xl = pick (l.tick (xl), xl);
        xr = pick (r.tick (xr), xr);
    }

private:
    float pick (SVF::Out o, float x) const noexcept
    {
        switch (type)
        {
            case LowPass:  return o.lp;
            case HighPass: return o.hp;
            case BandPass: return o.bp * l.k;
            case Notch:    return x - l.k * o.bp;
        }
        return x;
    }

    double sampleRate = 44100.0, cutoff = 1000.0, q = 0.707;
    Type type = LowPass;
    SVF l, r;
};
} // namespace me

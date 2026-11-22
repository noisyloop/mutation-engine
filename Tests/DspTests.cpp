// JUCE-free DSP unit tests: impulse responses, stability, determinism,
// parameter-change safety, and spectral placement.
#include "DSP/ResonatorBank.h"
#include "DSP/FilterBank.h"
#include "DSP/FormantEngine.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } } while (0)

static constexpr double SR = 48000.0;

struct Rng { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; } };

// Energy at one frequency (Goertzel), normalised by length.
static double energyAt (const std::vector<float>& x, double f)
{
    const double w = 2.0 * me::kPi * f / SR, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { double s = v + c * s1 - s2; s2 = s1; s1 = s; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) x.size();
}

static std::vector<float> bankImpulse (me::ResonatorBank::Params p, int n)
{
    me::ResonatorBank b; b.prepare (SR); b.setParams (p);
    std::vector<float> out (n);
    for (int i = 0; i < n; ++i) { float l, r; b.process (i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, r); out[i] = l; }
    return out;
}

static void test_resonator_impulse_decays()
{
    std::puts ("resonator: impulse response is finite and decays");
    me::ResonatorBank::Params p; p.rootHz = 220; p.voices = 32; p.damping = 0.6f;
    auto y = bankImpulse (p, (int) SR * 4);
    double early = 0, late = 0; bool finite = true;
    for (size_t i = 0; i < y.size(); ++i)
    {
        finite &= std::isfinite (y[i]);
        if (i < 4800) early += y[i] * y[i];
        if (i > y.size() - 4800) late += y[i] * y[i];
    }
    CHECK (finite, "output must be finite");
    CHECK (early > 1e-6, "impulse must excite the bank");
    CHECK (late < early * 1e-4, "tail must decay by >40 dB");
}

static void test_resonator_tuning()
{
    std::puts ("resonator: harmonic peaks at n*f0, inharmonic stretches partials");
    me::ResonatorBank::Params p; p.rootHz = 300; p.voices = 8; p.damping = 0.2f;
    auto h = bankImpulse (p, (int) SR);
    CHECK (energyAt (h, 300) > 20 * energyAt (h, 450), "harmonic: energy at f0 >> between partials");
    CHECK (energyAt (h, 900) > 20 * energyAt (h, 1050), "harmonic: energy at 3*f0 >> between partials");

    p.inharmonic = 1.0f;
    me::ResonatorBank b; b.prepare (SR); b.setParams (p);
    CHECK (b.voiceFrequency (7) > 300.0 * 8.0 * 1.05, "inharmonic: 8th partial sharper than harmonic");
    CHECK (std::fabs (b.voiceFrequency (0) - 300.0) < 3.0, "fundamental stays put");
}

static void test_resonator_feedback_stability()
{
    std::puts ("resonator: maximum feedback + loud noise stays bounded");
    me::ResonatorBank::Params p; p.rootHz = 80; p.voices = 32; p.damping = 0.0f; p.feedback = 1.0f; p.inharmonic = 0.5f;
    me::ResonatorBank b; b.prepare (SR); b.setParams (p);
    Rng r { 1 }; float peak = 0; bool finite = true;
    for (int i = 0; i < (int) SR * 5; ++i)
    {
        float l, rr; const float x = r.next();
        b.process (x, x, l, rr);
        finite &= std::isfinite (l) && std::isfinite (rr);
        peak = std::max (peak, std::max (std::fabs (l), std::fabs (rr)));
    }
    CHECK (finite, "no NaN/Inf");
    CHECK (peak <= 4.0f, "hard output bound respected");
    std::printf ("  peak = %.3f\n", peak);
}

static void test_determinism()
{
    std::puts ("determinism: identical input + params -> bit-identical output");
    me::ResonatorBank::Params p; p.rootHz = 130; p.voices = 24; p.feedback = 0.4f; p.inharmonic = 0.3f;
    std::vector<float> a, b;
    for (int run = 0; run < 2; ++run)
    {
        me::ResonatorBank bank; bank.prepare (SR); bank.setParams (p);
        Rng r { 42 }; auto& dst = run ? b : a;
        for (int i = 0; i < 20000; ++i) { float l, rr; bank.process (r.next(), r.next(), l, rr); dst.push_back (l); dst.push_back (rr); }
    }
    CHECK (a == b, "runs must match exactly");
}

static void test_parameter_sweeps_are_safe()
{
    std::puts ("parameters: rapid changes never produce NaN or spikes");
    me::ResonatorBank bank; bank.prepare (SR);
    me::FilterBank filt; filt.prepare (SR);
    me::FormantEngine form; form.prepare (SR);
    Rng r { 7 }; float peak = 0; bool finite = true;
    for (int blk = 0; blk < 600; ++blk)
    {
        me::ResonatorBank::Params p;
        p.rootHz = 20.0f + 4000.0f * std::fabs (r.next()); p.voices = 1 + (int) (31 * std::fabs (r.next()));
        p.inharmonic = std::fabs (r.next()); p.damping = std::fabs (r.next());
        p.feedback = std::fabs (r.next()); p.spread = std::fabs (r.next());
        bank.setParams (p);
        filt.set ((me::FilterBank::Type) (blk % 4), 20.0 + 18000.0 * std::fabs (r.next()), 0.5 + 20.0 * std::fabs (r.next()));
        form.set (4.0f * std::fabs (r.next()), 0.5f + 1.5f * std::fabs (r.next()));
        for (int i = 0; i < 256; ++i)
        {
            float x = r.next() * 0.5f, l, rr; bank.process (x, x, l, rr);
            form.process (l, rr); filt.process (l, rr);
            finite &= std::isfinite (l) && std::isfinite (rr);
            peak = std::max (peak, std::max (std::fabs (l), std::fabs (rr)));
        }
    }
    CHECK (finite, "no NaN/Inf under random parameter changes");
    CHECK (peak < 50.0f, "no runaway spikes");
    std::printf ("  peak = %.3f\n", peak);
}

static std::vector<float> noiseThrough (std::function<void (float&, float&)> fn)
{
    Rng r { 3 }; std::vector<float> out;
    for (int i = 0; i < (int) SR; ++i) { float l = r.next(), rr = l; fn (l, rr); out.push_back (l); }
    return out;
}

static void test_filter_modes()
{
    std::puts ("filter: LP attenuates highs, HP attenuates lows, notch carves centre");
    me::FilterBank f; f.prepare (SR);
    f.set (me::FilterBank::LowPass, 1000, 0.707);
    auto lp = noiseThrough ([&] (float& l, float& r) { f.process (l, r); });
    CHECK (energyAt (lp, 8000) < energyAt (lp, 200) * 0.01, "LP: 8k << 200 Hz");
    f.reset(); f.set (me::FilterBank::HighPass, 1000, 0.707);
    auto hp = noiseThrough ([&] (float& l, float& r) { f.process (l, r); });
    CHECK (energyAt (hp, 100) < energyAt (hp, 8000) * 0.01, "HP: 100 << 8k Hz");
    f.reset(); f.set (me::FilterBank::Notch, 2000, 4.0);
    auto nt = noiseThrough ([&] (float& l, float& r) { f.process (l, r); });
    CHECK (energyAt (nt, 2000) < energyAt (nt, 4000) * 0.05, "Notch: 2k carved out");
}

static void test_formant_peaks()
{
    std::puts ("formant: vowel A has peaks near 800 / 1150 Hz, morph moves F1");
    me::FormantEngine fe; fe.prepare (SR); fe.set (0.0f, 1.0f);
    auto a = noiseThrough ([&] (float& l, float& r) { fe.process (l, r); });
    CHECK (energyAt (a, 800) > 8 * energyAt (a, 450), "A: F1 800 > 450");
    CHECK (energyAt (a, 1150) > 4 * energyAt (a, 1900), "A: F2 1150 > 1900");
    fe.reset(); fe.set (4.0f, 1.0f); // U: F1 325
    auto u = noiseThrough ([&] (float& l, float& r) { fe.process (l, r); });
    CHECK (energyAt (u, 325) > 8 * energyAt (u, 1500), "U: F1 325 > 1500");
    auto mid = me::FormantEngine::vowelAt (0.5f);
    CHECK (mid.f[0] < 800.0f && mid.f[0] > 400.0f, "morph interpolates between A and E");
}

int main()
{
    test_resonator_impulse_decays();
    test_resonator_tuning();
    test_resonator_feedback_stability();
    test_determinism();
    test_parameter_sweeps_are_safe();
    test_filter_modes();
    test_formant_peaks();
    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

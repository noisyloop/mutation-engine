// JUCE-free DSP unit tests: impulse responses, stability, determinism,
// parameter-change safety, and spectral placement.
#include "DSP/ResonatorBank.h"
#include "DSP/FilterBank.h"
#include "DSP/FormantEngine.h"
#include "DSP/SpectralEngine.h"
#include "DSP/GlitchEngine.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
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


// ------------------------------------------------------------------ spectral
static std::vector<float> runSpectral (me::SpectralEngine& e, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i) { float l, r; e.process (in[i], in[i], l, r); out[i] = l; }
    return out;
}
static std::vector<float> noise (int n, uint32_t seed = 5) { Rng r { seed }; std::vector<float> v ((size_t) n); for (auto& x : v) x = r.next() * 0.5f; return v; }
static std::vector<float> sine (int n, double f) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (0.5 * std::sin (2 * me::kPi * f * i / SR)); return v; }

static void test_fft_roundtrip()
{
    std::puts ("fft: forward + inverse restores the signal");
    me::FFT f (1024); std::vector<float> re = noise (1024), im (1024, 0.0f), orig = re;
    f.run (re.data(), im.data(), false); f.run (re.data(), im.data(), true);
    double err = 0; for (size_t i = 0; i < 1024; ++i) err = std::max (err, (double) std::fabs (re[i] / 1024.0f - orig[i]));
    CHECK (err < 1e-5, "roundtrip error < 1e-5");
}

static void test_spectral_transparent()
{
    std::puts ("spectral: neutral params = delayed input, latency matches report");
    me::SpectralEngine e; auto in = noise (16384);
    auto out = runSpectral (e, in);
    const int L = me::SpectralEngine::latencySamples();
    double err = 0; for (size_t i = (size_t) L + 2048; i < in.size(); ++i) err = std::max (err, (double) std::fabs (out[i] - in[i - (size_t) L]));
    CHECK (err < 1e-4, "reconstruction error < 1e-4 at reported latency");
    me::SpectralEngine e2; std::vector<float> imp (4096, 0.0f); imp[100] = 1.0f;
    auto o2 = runSpectral (e2, imp); size_t pk = 0; for (size_t i = 0; i < o2.size(); ++i) if (std::fabs (o2[i]) > std::fabs (o2[pk])) pk = i;
    CHECK ((int) pk == 100 + L, "impulse emerges exactly latencySamples() later");
}

static void test_spectral_freeze()
{
    std::puts ("spectral: freeze sustains after input stops, stays bounded");
    me::SpectralEngine e; me::SpectralEngine::Params p; e.setParams (p);
    auto src = noise (24000); std::vector<float> in (src); in.resize (96000, 0.0f);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (i == 20000) { p.freeze = true; p.drift = 0.3f; e.setParams (p); }
        float l, r; e.process (in[i], in[i], l, r); out[i] = l;
    }
    double rms = 0; float peak = 0; bool finite = true;
    for (size_t i = 60000; i < out.size(); ++i) { rms += out[i] * out[i]; peak = std::max (peak, std::fabs (out[i])); finite &= std::isfinite (out[i]); }
    rms = std::sqrt (rms / 36000.0);
    CHECK (finite, "finite");
    CHECK (rms > 0.02, "frozen drone still audible 1+ s after input stopped");
    CHECK (peak < 2.0f, "frozen drone bounded");
    std::printf ("  drone rms = %.3f, peak = %.3f\n", rms, peak);
}

static void test_spectral_bins()
{
    std::puts ("spectral: bin shift moves partials, dropout silences, scramble is seeded");
    auto in = sine (24000, 1000.0);
    me::SpectralEngine e; me::SpectralEngine::Params p; p.binShift = 20; e.setParams (p);
    auto sh = runSpectral (e, in); std::vector<float> tail (sh.begin() + 12000, sh.end());
    CHECK (energyAt (tail, 1937.5) > 10 * energyAt (tail, 1000.0), "1 kHz moved up by 20 bins (937.5 Hz)");

    me::SpectralEngine d; p = {}; p.dropout = 1.0f; d.setParams (p);
    auto dr = runSpectral (d, in); double mx = 0; for (size_t i = 12000; i < dr.size(); ++i) mx = std::max (mx, (double) std::fabs (dr[i]));
    CHECK (mx < 1e-6, "dropout=1 mutes everything");

    auto n = noise (20000); p = {}; p.scramble = 0.6f; p.dropout = 0.2f;
    me::SpectralEngine a, b, c; a.reset (123); b.reset (123); c.reset (124); a.setParams (p); b.setParams (p); c.setParams (p);
    auto oa = runSpectral (a, n), ob = runSpectral (b, n), oc = runSpectral (c, n);
    CHECK (oa == ob, "same seed -> identical output");
    CHECK (oa != oc, "different seed -> different output");
    bool finite = true; float pk = 0; for (float v : oa) { finite &= std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
    CHECK (finite && pk < 4.0f, "scramble+dropout finite and bounded");
}

// -------------------------------------------------------------------- glitch
static void test_glitch()
{
    std::puts ("glitch: transparent at p=0, tempo-locked stutter, deterministic");
    auto make = [] (float prob, int type, uint32_t seed)
    {
        auto g = std::make_unique<me::GlitchEngine>(); g->prepare (SR); g->reset (seed);
        me::GlitchEngine::Params p; p.bpm = 120; p.stepsPerBeat = 4; p.probability = prob; p.intensity = 0.0f; p.forceType = type;
        g->setParams (p); g->setPosition (0.5); return g; };   // start mid-grid-step... boundary handled below
    auto ramp = [] (int i) { return (float) (i % 100000) * 1e-5f; };

    auto g0 = make (0.0f, -1, 1); bool same = true;
    for (int i = 0; i < 48000; ++i) { float l = ramp (i), r = l; g0->process (l, r); same &= (l == ramp (i)); }
    CHECK (same, "probability 0 is bit-transparent");

    // Stutter: warm up at p=0 so the ring holds the ramp, then p=1.
    auto g = std::make_unique<me::GlitchEngine>(); g->prepare (SR); g->reset (9);
    me::GlitchEngine::Params p; p.bpm = 120; p.stepsPerBeat = 4; p.probability = 0.0f; p.intensity = 0.0f; p.forceType = me::GlitchEngine::Stutter;
    g->setParams (p); g->setPosition (0.0);
    const int step = g->stepLengthSamples();           // 6000 @ 120 BPM 1/16
    std::vector<float> out;
    int n = 0;
    for (; n < step + step / 2; ++n) { float l = ramp (n), r = l; g->process (l, r); out.push_back (l); }  // step 0 + half of step 1, no events
    p.probability = 1.0f; g->setParams (p);
    for (; n < step * 3; ++n) { float l = ramp (n), r = l; g->process (l, r); out.push_back (l); }
    bool clean = true; for (int i = 0; i < 2 * step - 4; ++i) clean &= (out[(size_t) i] == ramp (i));
    CHECK (clean, "no event before the next grid step (tempo-locked)");
    const int slice = step >> 1; bool periodic = true, differs = false;
    for (int i = 2 * step + 200 + slice; i < 3 * step - 200; ++i)
    { periodic &= (out[(size_t) i] == out[(size_t) (i - slice)]); differs |= (out[(size_t) i] != ramp (i)); }
    CHECK (periodic, "stutter repeats a slice of step/2 samples");
    CHECK (differs, "stutter output differs from input");

    auto run = [&] (uint32_t seed) { auto e = make (0.6f, -1, seed); std::vector<float> o; for (int i = 0; i < 96000; ++i) { float l = ramp (i) - 0.5f, r = l; e->process (l, r); o.push_back (l); } return o; };
    auto a = run (3), b = run (3), c = run (4);
    CHECK (a == b, "same seed -> same glitch pattern");
    CHECK (a != c, "different seed -> different pattern");
    bool fin = true; for (float v : a) fin &= std::isfinite (v);
    CHECK (fin, "finite output");
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
    test_fft_roundtrip();
    test_spectral_transparent();
    test_spectral_freeze();
    test_spectral_bins();
    test_glitch();
    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

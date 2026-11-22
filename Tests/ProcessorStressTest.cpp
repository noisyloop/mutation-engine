// Step 6 beta suite: runs the real processor (headless) through the scenarios a host would.
//   latency reporting + alignment, sample-rate / block-size matrix, block-size independence,
//   bypass, automation + smoothing, preset recall, feedback safety, bad input, CPU, threading.
#include "PluginProcessor.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [line %d]\n", msg, __LINE__); } } while (0)

namespace
{
std::unique_ptr<MutationEngineProcessor> makeProc (double sr, int block)
{
    auto p = std::make_unique<MutationEngineProcessor>();
    p->presets.setUserDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("me_stress_user_presets"));
    p->setPlayConfigDetails (2, 2, sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

void setP (MutationEngineProcessor& p, const char* id, float actual)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (actual));
}

std::vector<float> noise (int n, float amp, unsigned seed = 1)
{
    juce::Random r (seed); std::vector<float> v ((size_t) n);
    for (auto& x : v) x = (r.nextFloat() * 2.0f - 1.0f) * amp;
    return v;
}

std::vector<float> sineWave (int n, double f, double sr, float amp)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr);
    return v;
}

// Feed mono `in` (both channels) through the processor in chunks of `block`; returns the left channel.
std::vector<float> run (MutationEngineProcessor& p, const std::vector<float>& in, int block, bool bypass = false)
{
    std::vector<float> out (in.size());
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        juce::AudioBuffer<float> buf (2, n);
        for (int i = 0; i < n; ++i) { buf.setSample (0, i, in[pos + (size_t) i]); buf.setSample (1, i, in[pos + (size_t) i]); }
        if (bypass) p.processBlockBypassed (buf, midi); else p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i) out[pos + (size_t) i] = buf.getSample (0, i);
    }
    return out;
}

bool sane (const std::vector<float>& v, float limit = 1.0001f)
{
    for (float x : v) if (! std::isfinite (x) || std::fabs (x) > limit) return false;
    return true;
}

double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max<size_t> (1, b - a));
}

double energyAt (const std::vector<float>& x, double f, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0; for (float v : x) { const double s = v + c * s1 - s2; s2 = s1; s1 = s; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) x.size();
}

const double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
const int kBlocks[] = { 1, 7, 64, 480, 512, 1024, 2048, 4096 };
}

static void test_latency()
{
    std::puts ("latency: reported constant; dry and wet are aligned at every sample rate and block size");
    const int L = me::SpectralEngine::latencySamples();
    for (double sr : kRates)
    {
        auto p = makeProc (sr, 512);
        CHECK (p->getLatencySamples() == L, "reported latency = spectral latency");

        // mix 0 -> output is the input delayed by exactly L samples (bit exact)
        setP (*p, "mix", 0.0f);
        const auto in = noise (8192, 0.5f, 3); bool exact = true;
        for (int block : { 512, 77 })
        {
            auto q = makeProc (sr, block); setP (*q, "mix", 0.0f);
            const auto out = run (*q, in, block);
            for (size_t i = (size_t) L; i < in.size(); ++i) exact &= out[i] == in[i - (size_t) L];
        }
        CHECK (exact, "mix=0 output is the input delayed by exactly latencySamples()");

        // fully wet, transparent wet path (Glitch mode, p=0, filter open): wet must line up with that dry
        auto w = makeProc (sr, 512);
        setP (*w, "mode", 1.0f); setP (*w, "glitch_prob", 0.0f); setP (*w, "mix", 1.0f); setP (*w, "formant_mix", 0.0f);
        setP (*w, "filt_cutoff", 20000.0f); setP (*w, "filt_res", 0.707f);
        const auto sine = sineWave (16384, 300.0, sr, 0.5f);
        const auto wet = run (*w, sine, 512);
        double best = 1e9; int bestLag = -1;
        for (int lag = L - 4; lag <= L + 6; ++lag)
        {
            double e = 0; for (size_t i = 8192; i < sine.size(); ++i) e += std::pow ((double) wet[i] - (double) sine[i - (size_t) lag], 2.0);
            if (e < best) { best = e; bestLag = lag; }
        }
        // The open low-pass in the wet path has a real group delay of sr / (2 pi Q fc) samples
        // (about 0.5 at 44.1 kHz, 2.2 at 192 kHz); everything else must line up exactly.
        const int filterDelay = (int) std::ceil (sr / (2.0 * juce::MathConstants<double>::pi * 0.707 * 20000.0));
        std::printf ("  %6.0f Hz: wet best lag = latency + %d (filter group delay <= %d)\n", sr, bestLag - L, filterDelay);
        CHECK (bestLag >= L && bestLag <= L + filterDelay, "wet path is aligned with the reported latency (within the filter's own group delay)");
    }

    std::puts ("bypass: latency-compensated pass-through");
    auto b = makeProc (48000.0, 256);
    const auto in = noise (6000, 0.5f, 9); const auto out = run (*b, in, 256, true);
    bool exact = true; for (size_t i = (size_t) L; i < in.size(); ++i) exact &= out[i] == in[i - (size_t) L];
    CHECK (exact, "bypassed output = input delayed by latencySamples()");
}

static void test_matrix()
{
    std::puts ("sample-rate x block-size x mode matrix: finite and bounded");
    int combos = 0; bool ok = true;
    for (double sr : kRates)
        for (int block : kBlocks)
            for (int mode = 0; mode < 3; ++mode)
            {
                auto p = makeProc (sr, block);
                p->loadPreset (mode == 0 ? p->presets.findByName ("Glass Throat") : mode == 1 ? p->presets.findByName ("Bit Rot") : p->presets.findByName ("Ghost Choir"));
                setP (*p, "spec_freeze", mode == 2 ? 1.0f : 0.0f);
                const auto out = run (*p, noise ((int) (sr * 0.2), 0.8f, (unsigned) combos + 1), block);
                ok &= sane (out); ++combos;
            }
    CHECK (ok, "every combination renders finite audio within +-1");
    std::printf ("  %d combinations\n", combos);

    std::puts ("sample-rate changes mid-session (prepareToPlay again) keep working");
    auto p = makeProc (48000.0, 512); bool seq = true;
    for (double sr : { 48000.0, 44100.0, 96000.0, 192000.0, 44100.0 })
    {
        p->setPlayConfigDetails (2, 2, sr, 512); p->prepareToPlay (sr, 512);
        seq &= p->getLatencySamples() == me::SpectralEngine::latencySamples();
        seq &= sane (run (*p, noise (8000, 0.7f), 512));
    }
    CHECK (seq, "re-preparing at a new rate resets cleanly");

    std::puts ("tuning is independent of sample rate");
    bool tuned = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto q = makeProc (sr, 512);
        setP (*q, "midi_track", 0.0f); setP (*q, "mix", 1.0f); setP (*q, "res_root", 440.0f); setP (*q, "res_voices", 8.0f); setP (*q, "res_inharm", 0.0f);
        setP (*q, "res_damp", 0.2f); setP (*q, "res_feedback", 0.0f); setP (*q, "formant_mix", 0.0f); setP (*q, "filt_cutoff", 18000.0f);
        const auto out = run (*q, noise ((int) sr, 0.5f), 512);
        const std::vector<float> tail (out.begin() + (long) sr / 2, out.end());
        tuned &= energyAt (tail, 440.0, sr) > 10.0 * energyAt (tail, 660.0, sr) && energyAt (tail, 880.0, sr) > 10.0 * energyAt (tail, 1100.0, sr);
    }
    CHECK (tuned, "partials sit at n*root at 44.1 / 48 / 96 kHz");
}

static void test_block_independence()
{
    std::puts ("block-size independence: the same audio and parameters give the same output");
    bool same = true; double worst = 0;
    for (int mode : { 0, 2 })
    {
        const auto in = noise (24000, 0.6f, 5); std::vector<float> ref;
        for (int block : { 512, 1, 77, 2048 })
        {
            auto p = makeProc (48000.0, block);
            p->loadPreset (p->presets.findByName (mode == 0 ? "Hollow Machine" : "Frozen Cathedral"));
            setP (*p, "spec_freeze", mode == 2 ? 1.0f : 0.0f);
            const auto out = run (*p, in, block);
            if (ref.empty()) ref = out;
            else for (size_t i = 0; i < out.size(); ++i) worst = std::max (worst, (double) std::fabs (out[i] - ref[i]));
        }
    }
    std::printf ("  worst difference between block sizes: %.2e\n", worst);
    same = worst < 1e-5;
    CHECK (same, "block size does not change the sound");
}

static void test_automation()
{
    std::puts ("automation: every parameter changed every block by a 'host'");
    auto p = makeProc (48000.0, 256);
    juce::Random rnd (42); bool ok = true; juce::MidiBuffer midi;
    for (int blk = 0; blk < 1200; ++blk)
    {
        for (int i = 0; i < me::kNumParams; ++i)
        {
            const auto* id = me::kParams[i].id;
            if (std::strcmp (id, "mut_morph") == 0) continue;                  // morph is exercised separately
            p->apvts.getParameter (id)->setValueNotifyingHost (rnd.nextFloat());
        }
        juce::AudioBuffer<float> buf (2, 256);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample (c, i, (rnd.nextFloat() - 0.5f) * 1.5f);
        p->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (c, i); ok &= std::isfinite (v) && std::fabs (v) <= 1.0001f; }
    }
    CHECK (ok, "1200 blocks of fully randomised automation stay finite and bounded");

    std::puts ("smoothing: sudden parameter jumps do not click");
    {   // dry path: output gain -24 dB -> 0 dB in one step
        auto q = makeProc (48000.0, 512); setP (*q, "mix", 0.0f); setP (*q, "output", -24.0f);
        const auto sine = sineWave (48000, 200.0, 48000.0, 0.5f);
        std::vector<float> out; for (int b = 0; b < 2; ++b) { auto part = run (*q, std::vector<float> (sine.begin() + b * 24000, sine.begin() + (b + 1) * 24000), 512); out.insert (out.end(), part.begin(), part.end()); if (b == 0) setP (*q, "output", 0.0f); }
        // note: the parameter change lands at the block boundary at sample 24000
        double maxStep = 0; for (size_t i = 24000; i < 30000; ++i) maxStep = std::max (maxStep, (double) std::fabs (out[i] - out[i - 1]));
        CHECK (maxStep < 0.03, "output gain step glides (no click)");
    }
    {   // wet/dry: mix 0 -> 1 on a resonating signal
        auto q = makeProc (48000.0, 512); setP (*q, "mix", 0.0f); setP (*q, "res_feedback", 0.4f);
        const auto sine = sineWave (60000, 150.0, 48000.0, 0.5f);
        std::vector<float> out; auto a = run (*q, std::vector<float> (sine.begin(), sine.begin() + 30000), 512); setP (*q, "mix", 1.0f);
        auto b = run (*q, std::vector<float> (sine.begin() + 30000, sine.end()), 512); out = a; out.insert (out.end(), b.begin(), b.end());
        double base = 0, jump = 0;
        for (size_t i = 20000; i < 29000; ++i) base = std::max (base, (double) std::fabs (out[i] - out[i - 1]));
        for (size_t i = 30000; i < 32500; ++i) jump = std::max (jump, (double) std::fabs (out[i] - out[i - 1]));
        std::printf ("  steady-state step %.4f, around the mix jump %.4f\n", base, jump);
        CHECK (jump < base * 3.0 + 0.03, "dry->wet jump is smoothed");
    }
}

static void test_recall()
{
    std::puts ("preset / state recall: a restored project sounds identical");
    bool allSame = true, valuesSame = true; int tested = 0;
    auto proto = makeProc (48000.0, 512);
    const auto in = noise (20000, 0.6f, 11);
    for (int i = 0; i < proto->presets.size(); ++i)
    {
        auto a = makeProc (48000.0, 512);
        a->loadPreset (i);
        juce::MemoryBlock blob; a->getStateInformation (blob);
        const auto outA = run (*a, in, 512);

        auto b = makeProc (48000.0, 512);
        b->setStateInformation (blob.getData(), (int) blob.getSize());
        for (int k = 0; k < me::kNumParams; ++k)
            valuesSame &= std::fabs (a->apvts.getParameter (me::kParams[k].id)->getValue() - b->apvts.getParameter (me::kParams[k].id)->getValue()) < 1e-6f;
        const auto outB = run (*b, in, 512);
        double worst = 0; for (size_t k = 0; k < in.size(); ++k) worst = std::max (worst, (double) std::fabs (outA[k] - outB[k]));
        allSame &= worst < 1e-6; ++tested;
    }
    std::printf ("  %d presets round-tripped\n", tested);
    CHECK (valuesSame, "every parameter value survives save/restore");
    CHECK (allSame, "restored state renders the same audio as the original");
}

static void test_safety()
{
    std::puts ("feedback safety: worst-case settings, locked or not, can not run away");
    for (float root : { 20.0f, 110.0f, 1500.0f })
    {
        auto p = makeProc (48000.0, 512);
        setP (*p, "midi_track", 0.0f); setP (*p, "mix", 1.0f); setP (*p, "res_root", root); setP (*p, "res_voices", 32.0f); setP (*p, "res_inharm", 1.0f);
        setP (*p, "res_feedback", 1.0f); setP (*p, "res_damp", 0.0f); setP (*p, "res_spread", 1.0f); setP (*p, "formant_mix", 1.0f); setP (*p, "filt_res", 20.0f);
        p->setParamLocked (me::pid::ResFeed, true); p->setParamLocked (me::pid::ResDamp, true);   // even a user-locked dangerous value
        auto burst = noise (24000, 1.0f, 2); burst.resize (48000 * 40, 0.0f);
        const auto out = run (*p, burst, 512);
        const double early = rms (out, 48000 * 2, 48000 * 4), late = rms (out, 48000 * 30, 48000 * 40);
        std::printf ("  root %.0f Hz: peak ok=%d, rms 2-4s %.2e, rms 30-40s %.2e\n", root, sane (out), early, late);
        CHECK (sane (out), "bounded by the output stage");
        CHECK (late <= early * 1.01 + 1e-9, "never grows after the input stops");
        CHECK (late < 1e-4, "rings down to silence (no self-oscillation)");
    }

    std::puts ("bad input: NaN / Inf / huge samples are survivable");
    auto p = makeProc (48000.0, 512); setP (*p, "mix", 1.0f);
    auto in = noise (48000, 0.5f, 7);
    for (size_t i = 5000; i < 5040; ++i) in[i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1) ? std::numeric_limits<float>::infinity() : 1.0e9f;
    const auto out = run (*p, in, 512);
    CHECK (sane (out), "no NaN/Inf escapes");
    CHECK (rms (out, 40000, 48000) > 1e-4, "the plugin keeps working afterwards (state was not poisoned)");
}

static void test_mutation_under_load()
{
    std::puts ("mutation while audio runs: 300 mutations, all styles, finite and bounded");
    auto p = makeProc (48000.0, 256); juce::MidiBuffer midi; bool ok = true; juce::Random rnd (4);
    setP (*p, "mut_intensity", 1.0f);
    for (int i = 0; i < 300; ++i)
    {
        setP (*p, "mut_style", (float) (i % 3));
        setP (*p, "mode", (float) (i % 3));
        p->mutate();
        if (i % 7 == 0) p->undoMutation();
        juce::AudioBuffer<float> buf (2, 256);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 256; ++k) buf.setSample (c, k, rnd.nextFloat() - 0.5f);
        for (int rep = 0; rep < 8; ++rep) { p->processBlock (buf, midi); for (int c = 0; c < 2; ++c) for (int k = 0; k < 256; ++k) { const float v = buf.getSample (c, k); ok &= std::isfinite (v) && std::fabs (v) <= 1.0001f; } }
    }
    CHECK (ok, "audio stays sane through 300 mutations");
}

static void test_threads()
{
    std::puts ("threading: audio thread runs while the UI thread mutates, loads presets and automates");
    auto p = makeProc (48000.0, 256);
    std::atomic<bool> stop { false }, bad { false }; std::atomic<long> blocks { 0 };
    std::thread audio ([&]
    {
        juce::Random rnd (1); juce::MidiBuffer midi;
        while (! stop.load())
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample (c, i, rnd.nextFloat() - 0.5f);
            p->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v) || std::fabs (v) > 1.0001f) bad = true; }
            ++blocks;
        }
    });
    juce::Random rnd (9); const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int ops = 0;
    while (juce::Time::getMillisecondCounterHiRes() - t0 < 3000.0)
    {
        switch (ops++ % 6)
        {
            case 0: p->mutate(); break;
            case 1: p->undoMutation(); break;
            case 2: p->loadPreset (rnd.nextInt (p->presets.size())); break;
            case 3: p->apvts.getParameter ("mode")->setValueNotifyingHost (rnd.nextFloat()); break;
            case 4: p->setParamLocked (rnd.nextInt (me::kNumSound), rnd.nextBool()); break;
            default: p->apvts.getParameter ("mut_morph")->setValueNotifyingHost (rnd.nextFloat()); break;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
    stop = true; audio.join();
    std::printf ("  %d UI operations while %ld audio blocks ran\n", ops, blocks.load());
    CHECK (! bad.load(), "no NaN / out-of-range sample during concurrent operations");
    CHECK (blocks.load() > 100 && ops > 100, "both threads made real progress");
}

static void test_cpu()
{
    std::puts ("CPU: real-time load of the heaviest settings (single core, this machine)");
    struct Cfg { const char* name; int mode; double sr; int block; };
    const Cfg cfgs[] = { { "Resonate 32 voices", 0, 48000.0, 512 }, { "Resonate 32 voices", 0, 96000.0, 64 }, { "Glitch + spectral", 1, 48000.0, 512 }, { "Phantom freeze", 2, 48000.0, 128 }, { "Resonate 32 voices", 0, 192000.0, 512 } };
    bool ok = true;
    // Sanitizer builds are several times slower; CI raises the limit with ME_STRESS_CPU_LIMIT (fraction of one core).
    double limit = 0.5; if (const char* e = std::getenv ("ME_STRESS_CPU_LIMIT")) limit = std::atof (e);
    for (const auto& c : cfgs)
    {
        auto p = makeProc (c.sr, c.block);
        setP (*p, "mode", (float) c.mode); setP (*p, "res_voices", 32.0f); setP (*p, "res_feedback", 0.7f); setP (*p, "mix", 1.0f); setP (*p, "formant_mix", 1.0f);
        setP (*p, "spec_freeze", c.mode == 2 ? 1.0f : 0.0f); setP (*p, "spec_scramble", 0.5f); setP (*p, "glitch_prob", 0.8f);
        setP (*p, "lfo1_rate", 5.0f); setP (*p, "mod1_src", 1.0f); setP (*p, "mod1_dst", 6.0f); setP (*p, "mod1_amt", 0.5f);
        const int seconds = 6; const auto in = noise ((int) c.sr * seconds, 0.5f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto out = run (*p, in, c.block);
        const double load = (juce::Time::getMillisecondCounterHiRes() - t0) / (1000.0 * seconds);
        std::printf ("  %-20s %6.0f Hz, block %4d : %5.1f %% of one core\n", c.name, c.sr, c.block, load * 100.0);
        ok &= load < limit && sane (out);
    }
    CHECK (ok, "every configuration runs well inside real time (default: < 50 % of one core)");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    test_latency(); test_matrix(); test_block_independence(); test_automation(); test_recall();
    test_safety(); test_mutation_under_load(); test_threads(); test_cpu();
    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

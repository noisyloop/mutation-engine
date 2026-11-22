// JUCE-free tests: preset text format, factory preset library, modulation engine.
#include "Presets/Preset.h"
#include "DSP/ModEngine.h"
#include "DSP/ResonatorBank.h"
#include "DSP/FilterBank.h"
#include "DSP/FormantEngine.h"
#include "DSP/Random.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <set>

using namespace me;
static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [line %d]\n", msg, __LINE__); } } while (0)

static void test_format()
{
    std::puts ("preset format: round trip, defaults, unknown ids, clamping, bad input");
    Preset p; p.name = "Test: with colon"; p.category = "Resonate"; p.author = "me";
    p.set (pid::ResRoot, 123.456f); p.set (pid::ResVoices, 7); p.set (pid::Mod1Amt, -0.5f); p.set (pid::FiltType, 3);
    Preset q;
    CHECK (Preset::fromText (p.toText(), q), "parses own output");
    CHECK (q.name == p.name && q.category == "Resonate" && q.author == "me", "metadata survives");
    CHECK (std::fabs (q.get (pid::ResRoot) - 123.456f) < 1e-3f && q.get (pid::ResVoices) == 7 && q.get (pid::Mod1Amt) == -0.5f && q.get (pid::FiltType) == 3, "values survive");
    CHECK (q.get (pid::ResFeed) == kParams[pid::ResFeed].def, "unmentioned parameter = default");

    Preset r;
    CHECK (Preset::fromText ("name: X\nnot_a_param = 5\nres_root = 99999\nres_voices = 2.6\nlock_glitch = 1\nmut_style = 2\n", r), "tolerates unknown ids");
    CHECK (r.get (pid::ResRoot) == 2000.0f, "out-of-range value is clamped");
    CHECK (r.get (pid::ResVoices) == 3.0f, "integer parameters are rounded");
    CHECK (! r.has (pid::LockGl) && ! r.has (pid::MutStyle), "lock/mutation controls are never part of a preset");
    CHECK (! Preset::fromText ("res_root = 5\n", r), "name is required");
    CHECK (! Preset::fromText ("name: X\nres_root = banana\n", r), "garbage value rejected");
    CHECK (! Preset::fromText ("name: X\nres_root = nan\n", r), "NaN rejected");
}

static std::vector<std::string> presetFiles()
{
    std::vector<std::string> f;
    if (DIR* d = opendir (ME_PRESET_DIR))
    {
        while (auto* e = readdir (d)) { std::string n = e->d_name; if (n.size() > 9 && n.substr (n.size() - 9) == ".mepreset") f.push_back (std::string (ME_PRESET_DIR) + "/" + n); }
        closedir (d);
    }
    std::sort (f.begin(), f.end());
    return f;
}

static void test_modengine()
{
    std::puts ("modulation: ranges, determinism, routing, safeguards");
    ModEngine m; m.prepare (48000.0); m.reset (5);
    m.setRates (2.0f, 0, 1.0f, 3, 4.0f);
    float lo = 9, hi = -9;
    for (int i = 0; i < 2000; ++i) { m.advance (256, 0.0f); lo = std::fmin (lo, m.source (ModEngine::Lfo1)); hi = std::fmax (hi, m.source (ModEngine::Lfo1)); }
    CHECK (lo < -0.99f && hi > 0.99f, "sine LFO spans -1..1");
    CHECK (ModEngine::wave (3, 0.25f) == 1.0f && ModEngine::wave (3, 0.75f) == -1.0f, "square wave");

    // LFO frequency: count zero crossings of a 2 Hz sine over 10 s
    ModEngine f; f.prepare (48000.0); f.reset (1); f.setRates (2.0f, 0, 1, 0, 1);
    int crossings = 0; float prev = f.source (ModEngine::Lfo1);
    for (int i = 0; i < 1875; ++i) { f.advance (256, 0.0f); float v = f.source (ModEngine::Lfo1); if ((v >= 0) != (prev >= 0)) ++crossings; prev = v; }
    CHECK (crossings >= 38 && crossings <= 42, "2 Hz LFO crosses zero ~40 times in 10 s");

    // envelope follows level, then releases
    ModEngine e; e.prepare (48000.0); e.reset (1);
    for (int i = 0; i < 50; ++i) e.advance (256, 0.5f);
    const float loud = e.source (ModEngine::Envelope);
    for (int i = 0; i < 200; ++i) e.advance (256, 0.0f);
    CHECK (loud > 0.8f && e.source (ModEngine::Envelope) < 0.05f, "envelope attacks and releases");

    // random: deterministic per seed
    auto run = [] (uint32_t seed) { ModEngine r; r.prepare (48000.0); r.reset (seed); r.setRates (1, 0, 1, 0, 8); std::vector<float> v; for (int i = 0; i < 500; ++i) { r.advance (256, 0); v.push_back (r.source (ModEngine::Random)); } return v; };
    CHECK (run (3) == run (3) && run (3) != run (4), "random source is seeded");

    // routing
    ModEngine r; r.prepare (48000.0); r.reset (1); r.setVelocity (1.0f);
    ModEngine::Slot s[4]; s[0] = { ModEngine::Velocity, 3, 0.5f }; s[1] = { ModEngine::Velocity, 3, 0.25f }; s[2] = { 0, 4, 1.0f }; s[3] = { ModEngine::Velocity, 0, 1.0f };
    r.setSlots (s); r.advance (256, 0);
    const float base = 0.2f;
    CHECK (std::fabs (toNorm (pid::ResDamp, r.modulate (pid::ResDamp, base)) - (toNorm (pid::ResDamp, base) + 0.75f)) < 1e-4f, "slots on the same target add");
    CHECK (r.modulate (pid::ResFeed, 0.3f) == 0.3f, "source None / dst None do nothing");
    s[0].amount = 5.0f; r.setSlots (s);
    CHECK (r.modulate (pid::ResDamp, base) == 1.0f, "result clamps to the parameter range");
    CHECK (r.isModulated (pid::ResDamp) && ! r.isModulated (pid::ResFeed), "isModulated");

    float fb = 1.0f, damp = 0.0f, res = 20.0f, cut = 20.0f, drop = 1.0f;
    ModEngine::safeguard (fb, damp, res, cut, 0, drop);
    SafetyLimits L;
    CHECK (fb <= L.maxFeedback && damp >= L.dampFloor && res <= L.lowCutoffMaxRes && cut >= L.minLowpassHz && drop <= L.maxDropout, "safeguard pulls modulated values into the safe region");
}

// Render a preset with its modulation running; return pre-clip peak.
static float renderPreset (const Preset& p, bool& finite)
{
    const double SR = 48000.0;
    ModEngine mod; mod.prepare (SR); mod.reset ((uint32_t) p.get (pid::Seed) + 1);
    mod.setRates (p.get (pid::Lfo1Rate), (int) p.get (pid::Lfo1Shape), p.get (pid::Lfo2Rate), (int) p.get (pid::Lfo2Shape), p.get (pid::RndRate));
    mod.setVelocity (0.8f);
    ModEngine::Slot slots[4];
    for (int i = 0; i < 4; ++i) slots[i] = { (int) p.get (modSlotParam (i, 0)), (int) p.get (modSlotParam (i, 1)), p.get (modSlotParam (i, 2)) };
    mod.setSlots (slots);

    ResonatorBank rb; rb.prepare (SR); FilterBank f; f.prepare (SR); FormantEngine fe; fe.prepare (SR);
    Rng r; r.seed (11); float peak = 0; finite = true;
    for (int blk = 0; blk < 400; ++blk)
    {
        float level = 0; float in[256];
        for (auto& x : in) { x = r.bipolar() * 0.9f; level += std::fabs (x); }
        mod.advance (256, level / 256.0f);
        auto M = [&] (int i) { return mod.modulate (i, p.get (i)); };
        float fb = M (pid::ResFeed), damp = M (pid::ResDamp), res = M (pid::FiltRes), cut = M (pid::FiltCutoff), drop = M (pid::SpDrop);
        ModEngine::safeguard (fb, damp, res, cut, (int) p.get (pid::FiltType), drop);
        ResonatorBank::Params rp; rp.rootHz = M (pid::ResRoot); rp.voices = (int) p.get (pid::ResVoices); rp.inharmonic = M (pid::ResInharm);
        rp.damping = damp; rp.feedback = fb; rp.spread = M (pid::ResSpread); rb.setParams (rp);
        f.set ((FilterBank::Type) (int) p.get (pid::FiltType), cut, res);
        fe.set (M (pid::Vowel), M (pid::FormShift));
        const float fm = M (pid::FormMix);
        for (float x : in)
        {
            float l, rr; rb.process (x, x, l, rr);
            float fl = l, fr = rr; fe.process (fl, fr); l += fm * (fl - l); rr += fm * (fr - rr);
            f.process (l, rr);
            finite &= std::isfinite (l) && std::isfinite (rr);
            peak = std::fmax (peak, std::fmax (std::fabs (l), std::fabs (rr)));
        }
    }
    return peak;
}

static void test_factory_presets()
{
    std::puts ("factory presets: parse, unique names, valid values, safe to render with modulation");
    const auto files = presetFiles();
    CHECK (files.size() >= 12, "at least 12 factory presets");
    std::set<std::string> names; std::set<std::string> cats; float worst = 0;
    bool parsed = true, unique = true, ranged = true, finite = true, safe = true, modes[3] = { false, false, false };
    for (auto& path : files)
    {
        std::ifstream in (path); std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        Preset p;
        if (! Preset::fromText (text, p)) { parsed = false; std::printf ("  cannot parse %s\n", path.c_str()); continue; }
        unique &= names.insert (p.name).second; cats.insert (p.category);
        for (auto& v : p.values) ranged &= v.second >= kParams[v.first].min && v.second <= kParams[v.first].max;
        modes[(int) p.get (pid::Mode)] = true;
        SafetyLimits L;
        safe &= p.get (pid::ResFeed) <= L.maxFeedback && ! (p.get (pid::ResFeed) > L.dampThreshold && p.get (pid::ResDamp) < L.dampFloor) && p.get (pid::FiltRes) <= L.maxFilterRes;
        Preset again; safe &= Preset::fromText (p.toText(), again) && again.name == p.name;
        bool f; const float pk = renderPreset (p, f); finite &= f; worst = std::fmax (worst, pk);
        if (pk >= 2.0f) std::printf ("  loud: %s peak %.2f\n", p.name.c_str(), pk);
    }
    std::printf ("  %zu presets, %zu categories, worst pre-clip peak %.3f\n", files.size(), cats.size(), worst);
    CHECK (parsed, "every file parses");
    CHECK (unique, "names are unique");
    CHECK (ranged, "all values in range");
    CHECK (safe, "feedback/damping/filter safe; text round-trips");
    CHECK (finite && worst < 2.0f, "renders finite with no dangerous peaks (modulation running)");
    CHECK (modes[0] && modes[1] && modes[2], "presets exist for Resonate, Glitch and Phantom");
}

int main()
{
    test_format(); test_modengine(); test_factory_presets();
    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

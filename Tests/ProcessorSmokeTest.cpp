// Headless integration test: real JUCE processor + APVTS, no host, no GUI.
#include "PluginProcessor.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [line %d]\n", msg, __LINE__); } } while (0)

static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }

static bool statesMatch (const me::MutationState& a, const me::MutationState& b, bool includeSeed = true)
{
    for (int i = 0; i < me::kNumSound; ++i) if (std::fabs (a.n[i] - b.n[i]) > 1e-4f) return false;
    return ! includeSeed || a.seed == b.seed;
}

static bool renderIsSane (MutationEngineProcessor& p)
{
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; juce::Random rnd (7); bool ok = true;
    for (int b = 0; b < 40; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, rnd.nextFloat() * 1.6f - 0.8f);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); ok &= std::isfinite (v) && std::fabs (v) <= 1.0001f; }
    }
    return ok;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    MutationEngineProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::puts ("every table parameter exists in the live processor");
    bool all = true;
    for (int i = 0; i < me::kNumParams; ++i) all &= p.apvts.getParameter (me::kParams[i].id) != nullptr;
    CHECK (all, "all parameters present");
    CHECK (p.apvts.processor.getParameters().size() == me::kNumParams, "no stray parameters");

    std::puts ("mutate / undo / redo through the real parameter tree");
    const auto s0 = p.readState();
    p.apvts.getParameter (me::ids::mutStyle)->setValueNotifyingHost (p.apvts.getParameter (me::ids::mutStyle)->convertTo0to1 (2.0f));   // Unstable
    p.apvts.getParameter (me::ids::mutIntensity)->setValueNotifyingHost (1.0f);
    p.mutate(); const auto s1 = p.readState();
    p.mutate(); const auto s2 = p.readState();
    CHECK (! statesMatch (s0, s1, false) && ! statesMatch (s1, s2, false), "each MUTATE changes the sound");
    CHECK (p.undoMutation() && statesMatch (p.readState(), s1), "undo -> previous sound and seed");
    CHECK (p.undoMutation() && statesMatch (p.readState(), s0), "undo -> original");
    CHECK (p.redoMutation() && statesMatch (p.readState(), s1), "redo -> first mutation");
    CHECK (p.redoMutation() && statesMatch (p.readState(), s2), "redo -> second mutation");

    std::puts ("same seed + same start = same mutation (reproducible)");
    p.undoMutation(); p.undoMutation();
    p.mutate(); CHECK (statesMatch (p.readState(), s1), "repeating the first MUTATE reproduces it exactly");

    std::puts ("locks");
    p.undoMutation();
    p.apvts.getParameter ("lock_resonator")->setValueNotifyingHost (1.0f);
    p.setParamLocked (me::pid::Vowel, true);
    const auto before = p.readState();
    for (int i = 0; i < 20; ++i) p.mutate();
    const auto after = p.readState(); bool held = true;
    for (int i = 0; i < me::kNumSound; ++i)
        if ((me::kParams[i].group == me::Group::Resonator || i == me::pid::Vowel) && std::fabs (before.n[i] - after.n[i]) > 1e-4f) held = false;
    CHECK (held, "group-locked resonator params and per-param locked vowel survive 20 mutations");
    CHECK (! statesMatch (before, after, false), "unlocked params did move");
    CHECK (renderIsSane (p), "audio finite and bounded after mutations");

    std::puts ("A/B morph");
    p.apvts.getParameter ("lock_resonator")->setValueNotifyingHost (0.0f); p.setParamLocked (me::pid::Vowel, false);
    p.mutate(); const auto B = p.readState();
    p.apvts.getParameter (me::ids::mutMorph)->setValueNotifyingHost (0.0f); pump();
    const auto atA = p.readState();
    CHECK (! statesMatch (atA, B, false), "morph at 0 moves back to A");
    p.apvts.getParameter (me::ids::mutMorph)->setValueNotifyingHost (1.0f); pump();
    CHECK (statesMatch (p.readState(), B, false), "morph at 1 returns to B");

    std::puts ("all modes render sanely with a mutated sound");
    for (int m = 0; m < 3; ++m)
    {
        p.apvts.getParameter (me::ids::mode)->setValueNotifyingHost (p.apvts.getParameter (me::ids::mode)->convertTo0to1 ((float) m));
        p.apvts.getParameter (me::ids::spFreeze)->setValueNotifyingHost (m == 2 ? 1.0f : 0.0f);
        CHECK (renderIsSane (p), "mode renders finite, |x| <= 1");
    }

    std::puts ("state save/restore keeps sound, seed, locks and A/B");
    p.setParamLocked (me::pid::FiltRes, true);
    juce::MemoryBlock blob; p.getStateInformation (blob);
    MutationEngineProcessor q; q.setPlayConfigDetails (2, 2, 48000.0, 512); q.prepareToPlay (48000.0, 512);
    q.setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (statesMatch (p.readState(), q.readState()), "sound + seed restored");
    CHECK (q.isParamLocked (me::pid::FiltRes) && ! q.isParamLocked (me::pid::Vowel), "per-param locks restored");
    // Morph both processors to A (same locks, same live state) - results must agree.
    p.apvts.getParameter (me::ids::mutMorph)->setValueNotifyingHost (0.0f); pump();
    q.apvts.getParameter (me::ids::mutMorph)->setValueNotifyingHost (0.0f); pump();
    CHECK (statesMatch (q.readState(), p.readState(), false), "A/B slots restored (morph works after reload)");
    CHECK (! statesMatch (p.readState(), B, false), "...and the restored morph actually moved the sound");

    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

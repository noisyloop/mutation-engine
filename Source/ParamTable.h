#pragma once
// Single source of truth for every parameter. JUCE-free so the mutation
// system can be unit-tested on its own; Parameters.h builds the JUCE layout
// from this table, so the two can never drift apart.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace me
{
enum class Kind  { Float, Int, Choice, Bool };
enum class Group { Global, Resonator, Filter, Formant, Glitch, Spectral, Control };

namespace pid   // indices into kParams
{
enum : int
{
    // ---- "sound" parameters: captured in snapshots, undo/redo and morphs
    Mode, Mix, Output, MidiTrack, Seed,
    ResRoot, ResVoices, ResInharm, ResDamp, ResFeed, ResSpread,
    FiltType, FiltCutoff, FiltRes,
    Vowel, FormShift, FormMix,
    GlDiv, GlProb, GlAmt,
    SpFreeze, SpDrift, SpShift, SpDrop, SpScram,
    // ---- control parameters: never part of a sound snapshot
    MutIntensity, MutStyle, MutMorph,
    LockRes, LockForm, LockFilt, LockSpec, LockGl,
    // ---- modulation: sources and four assignment slots (src, dst, amount)
    Lfo1Rate, Lfo1Shape, Lfo2Rate, Lfo2Shape, RndRate,
    Mod1Src, Mod1Dst, Mod1Amt, Mod2Src, Mod2Dst, Mod2Amt,
    Mod3Src, Mod3Dst, Mod3Amt, Mod4Src, Mod4Dst, Mod4Amt,
    kNumParams
};
}

inline constexpr int kNumSound  = pid::SpScram + 1;
inline constexpr int kNumParams = pid::kNumParams;

struct ParamInfo
{
    const char* id;
    const char* name;
    Kind  kind;
    float min, max, def, skew;
    const char* choices;   // "A|B|C" for Choice params
    const char* label;     // unit suffix
    Group group;
    bool  mutates;         // may be changed by MUTATE
};

#define MOD_SLOT(n) \
    { "mod" #n "_src", "Mod " #n " Source", Kind::Choice, 0, 5, 0, 1, "None|LFO 1|LFO 2|Envelope|Random|Velocity", "", Group::Control, false }, \
    { "mod" #n "_dst", "Mod " #n " Target", Kind::Choice, 0, 15, 0, 1, "None|Root|Inharmonic|Damping|Feedback|Spread|Cutoff|Filter Res|Vowel|Formant Shift|Formant Mix|Mix|Glitch Prob|Drift|Dropout|Scramble", "", Group::Control, false }, \
    { "mod" #n "_amt", "Mod " #n " Amount", Kind::Float, -1, 1, 0, 1, "", "", Group::Control, false },

inline constexpr ParamInfo kParams[kNumParams] = {
    { "mode",          "Mode",               Kind::Choice, 0, 2, 0, 1, "Resonate|Glitch|Phantom", "", Group::Global, false },
    { "mix",           "Mix",                Kind::Float,  0, 1, 0.7f, 1, "", "", Group::Global, false },
    { "output",        "Output",             Kind::Float, -24, 12, 0, 1, "", "dB", Group::Global, false },
    { "midi_track",    "MIDI Pitch",         Kind::Bool,   0, 1, 1, 1, "", "", Group::Global, false },
    { "seed",          "Seed",               Kind::Int,    0, 65535, 1, 1, "", "", Group::Global, false },

    { "res_root",      "Root",               Kind::Float,  20, 2000, 110, 0.35f, "", "Hz", Group::Resonator, true },
    { "res_voices",    "Voices",             Kind::Int,    1, 32, 16, 1, "", "", Group::Resonator, true },
    { "res_inharm",    "Inharmonic",         Kind::Float,  0, 1, 0, 1, "", "", Group::Resonator, true },
    { "res_damp",      "Damping",            Kind::Float,  0, 1, 0.5f, 1, "", "", Group::Resonator, true },
    { "res_feedback",  "Feedback",           Kind::Float,  0, 1, 0, 1, "", "", Group::Resonator, true },
    { "res_spread",    "Spread",             Kind::Float,  0, 1, 0.5f, 1, "", "", Group::Resonator, true },

    { "filt_type",     "Filter Type",        Kind::Choice, 0, 3, 0, 1, "Low-pass|High-pass|Band-pass|Notch", "", Group::Filter, true },
    { "filt_cutoff",   "Cutoff",             Kind::Float,  20, 20000, 12000, 0.3f, "", "Hz", Group::Filter, true },
    { "filt_res",      "Filter Res",         Kind::Float,  0.5f, 20, 0.707f, 0.5f, "", "", Group::Filter, true },

    { "vowel",         "Vowel",              Kind::Float,  0, 4, 0, 1, "", "", Group::Formant, true },
    { "formant_shift", "Formant Shift",      Kind::Float,  0.5f, 2, 1, 0.5f, "", "", Group::Formant, true },
    { "formant_mix",   "Formant Mix",        Kind::Float,  0, 1, 0, 1, "", "", Group::Formant, true },

    { "glitch_div",    "Glitch Division",    Kind::Choice, 0, 3, 2, 1, "1/4|1/8|1/16|1/32", "", Group::Glitch, true },
    { "glitch_prob",   "Glitch Probability", Kind::Float,  0, 1, 0.35f, 1, "", "", Group::Glitch, true },
    { "glitch_amount", "Glitch Amount",      Kind::Float,  0, 1, 0.5f, 1, "", "", Group::Glitch, true },

    { "spec_freeze",   "Freeze",             Kind::Bool,   0, 1, 0, 1, "", "", Group::Spectral, false },
    { "spec_drift",    "Drift",              Kind::Float,  0, 1, 0.25f, 1, "", "", Group::Spectral, true },
    { "spec_shift",    "Bin Shift",          Kind::Int,   -128, 128, 0, 1, "", "", Group::Spectral, true },
    { "spec_dropout",  "Bin Dropout",        Kind::Float,  0, 1, 0, 1, "", "", Group::Spectral, true },
    { "spec_scramble", "Bin Scramble",       Kind::Float,  0, 1, 0, 1, "", "", Group::Spectral, true },

    { "mut_intensity", "Mutation Intensity", Kind::Float,  0, 1, 0.5f, 1, "", "", Group::Control, false },
    { "mut_style",     "Mutation Style",     Kind::Choice, 0, 2, 0, 1, "Organic|Digital|Unstable", "", Group::Control, false },
    { "mut_morph",     "Morph A-B",          Kind::Float,  0, 1, 1, 1, "", "", Group::Control, false },
    { "lock_resonator","Lock Resonator",     Kind::Bool,   0, 1, 0, 1, "", "", Group::Control, false },
    { "lock_formant",  "Lock Formant",       Kind::Bool,   0, 1, 0, 1, "", "", Group::Control, false },
    { "lock_filter",   "Lock Filter",        Kind::Bool,   0, 1, 0, 1, "", "", Group::Control, false },
    { "lock_spectral", "Lock Spectral",      Kind::Bool,   0, 1, 0, 1, "", "", Group::Control, false },
    { "lock_glitch",   "Lock Glitch",        Kind::Bool,   0, 1, 0, 1, "", "", Group::Control, false },

    { "lfo1_rate",     "LFO 1 Rate",         Kind::Float,  0.05f, 20, 1.0f, 0.4f, "", "Hz", Group::Control, false },
    { "lfo1_shape",    "LFO 1 Shape",        Kind::Choice, 0, 3, 0, 1, "Sine|Triangle|Saw|Square", "", Group::Control, false },
    { "lfo2_rate",     "LFO 2 Rate",         Kind::Float,  0.05f, 20, 0.25f, 0.4f, "", "Hz", Group::Control, false },
    { "lfo2_shape",    "LFO 2 Shape",        Kind::Choice, 0, 3, 1, 1, "Sine|Triangle|Saw|Square", "", Group::Control, false },
    { "rnd_rate",      "Random Rate",        Kind::Float,  0.1f, 16, 2.0f, 0.4f, "", "Hz", Group::Control, false },
    MOD_SLOT (1) MOD_SLOT (2) MOD_SLOT (3) MOD_SLOT (4)
};

// Value <-> normalised (0..1), mirroring juce::NormalisableRange semantics.
inline float toNorm (int i, float v) noexcept
{
    const auto& p = kParams[i];
    float prop = (p.max > p.min) ? (v - p.min) / (p.max - p.min) : 0.0f;
    prop = std::fmax (0.0f, std::fmin (1.0f, prop));
    if (std::fabs (p.skew - 1.0f) > 1.0e-6f && prop > 0.0f) prop = std::pow (prop, p.skew);
    return prop;
}

inline float fromNorm (int i, float n) noexcept
{
    const auto& p = kParams[i];
    n = std::fmax (0.0f, std::fmin (1.0f, n));
    float prop = n;
    if (std::fabs (p.skew - 1.0f) > 1.0e-6f && prop > 0.0f) prop = std::exp (std::log (prop) / p.skew);
    float v = p.min + prop * (p.max - p.min);
    if (p.kind != Kind::Float) v = std::round (v);
    return v;
}

inline int findParam (const char* id) noexcept
{
    for (int i = 0; i < kNumParams; ++i) if (std::strcmp (kParams[i].id, id) == 0) return i;
    return -1;
}

// Modulation destinations, in the order of the "mod*_dst" choice list (index 0 = None).
inline constexpr int kNumModDsts = 16;
inline constexpr int kModDst[kNumModDsts] = {
    -1, pid::ResRoot, pid::ResInharm, pid::ResDamp, pid::ResFeed, pid::ResSpread, pid::FiltCutoff, pid::FiltRes,
    pid::Vowel, pid::FormShift, pid::FormMix, pid::Mix, pid::GlProb, pid::SpDrift, pid::SpDrop, pid::SpScram };
inline constexpr int kNumModSlots = 4;
inline constexpr int modSlotParam (int slot, int field) noexcept { return pid::Mod1Src + slot * 3 + field; }   // field 0 src, 1 dst, 2 amt

inline int numChoices (int i) noexcept { return (int) kParams[i].max + 1; }
} // namespace me

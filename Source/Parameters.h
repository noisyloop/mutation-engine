#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace me::ids
{
inline constexpr const char* mode       = "mode";
inline constexpr const char* mix        = "mix";
inline constexpr const char* output     = "output";
inline constexpr const char* resRoot    = "res_root";
inline constexpr const char* resVoices  = "res_voices";
inline constexpr const char* resInharm  = "res_inharm";
inline constexpr const char* resDamp    = "res_damp";
inline constexpr const char* resFeed    = "res_feedback";
inline constexpr const char* resSpread  = "res_spread";
inline constexpr const char* filtType   = "filt_type";
inline constexpr const char* filtCutoff = "filt_cutoff";
inline constexpr const char* filtRes    = "filt_res";
inline constexpr const char* vowel      = "vowel";
inline constexpr const char* formShift  = "formant_shift";
inline constexpr const char* formMix    = "formant_mix";
inline constexpr const char* midiTrack  = "midi_track";
}

namespace me
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P  = AudioParameterFloat;
    using PI = AudioParameterInt;
    using PC = AudioParameterChoice;
    using PB = AudioParameterBool;
    const auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };

    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<PC> (ParameterID { ids::mode, 1 }, "Mode",
                                 StringArray { "Resonate", "Glitch", "Phantom" }, 0));
    l.add (std::make_unique<P> (ParameterID { ids::mix, 1 }, "Mix", NormalisableRange<float> (0, 1), 0.7f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<P> (ParameterID { ids::output, 1 }, "Output",
                                NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                AudioParameterFloatAttributes().withLabel ("dB")));

    l.add (std::make_unique<P> (ParameterID { ids::resRoot, 1 }, "Root",
                                NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.35f), 110.0f,
                                AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<PI> (ParameterID { ids::resVoices, 1 }, "Voices", 1, 32, 16));
    l.add (std::make_unique<P> (ParameterID { ids::resInharm, 1 }, "Inharmonic", NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> (ParameterID { ids::resDamp, 1 }, "Damping", NormalisableRange<float> (0, 1), 0.5f));
    l.add (std::make_unique<P> (ParameterID { ids::resFeed, 1 }, "Feedback", NormalisableRange<float> (0, 1), 0.0f));
    l.add (std::make_unique<P> (ParameterID { ids::resSpread, 1 }, "Spread", NormalisableRange<float> (0, 1), 0.5f));

    l.add (std::make_unique<PC> (ParameterID { ids::filtType, 1 }, "Filter Type",
                                 StringArray { "Low-pass", "High-pass", "Band-pass", "Notch" }, 0));
    l.add (std::make_unique<P> (ParameterID { ids::filtCutoff, 1 }, "Cutoff",
                                NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.3f), 12000.0f,
                                AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<P> (ParameterID { ids::filtRes, 1 }, "Filter Res",
                                NormalisableRange<float> (0.5f, 20.0f, 0.01f, 0.5f), 0.707f));

    l.add (std::make_unique<P> (ParameterID { ids::vowel, 1 }, "Vowel", NormalisableRange<float> (0.0f, 4.0f), 0.0f));
    l.add (std::make_unique<P> (ParameterID { ids::formShift, 1 }, "Formant Shift",
                                NormalisableRange<float> (0.5f, 2.0f, 0.001f, 0.5f), 1.0f));
    l.add (std::make_unique<P> (ParameterID { ids::formMix, 1 }, "Formant Mix", NormalisableRange<float> (0, 1), 0.0f));

    l.add (std::make_unique<PB> (ParameterID { ids::midiTrack, 1 }, "MIDI Pitch", true));
    return l;
}
}

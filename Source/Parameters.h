#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamTable.h"

namespace me::ids
{
#define ME_ID(name, idx) inline constexpr const char* name = kParams[pid::idx].id;
ME_ID (mode, Mode)            ME_ID (mix, Mix)              ME_ID (output, Output)
ME_ID (midiTrack, MidiTrack)  ME_ID (seed, Seed)
ME_ID (resRoot, ResRoot)      ME_ID (resVoices, ResVoices)  ME_ID (resInharm, ResInharm)
ME_ID (resDamp, ResDamp)      ME_ID (resFeed, ResFeed)      ME_ID (resSpread, ResSpread)
ME_ID (filtType, FiltType)    ME_ID (filtCutoff, FiltCutoff) ME_ID (filtRes, FiltRes)
ME_ID (vowel, Vowel)          ME_ID (formShift, FormShift)  ME_ID (formMix, FormMix)
ME_ID (glDiv, GlDiv)          ME_ID (glProb, GlProb)        ME_ID (glAmt, GlAmt)
ME_ID (spFreeze, SpFreeze)    ME_ID (spDrift, SpDrift)      ME_ID (spShift, SpShift)
ME_ID (spDrop, SpDrop)        ME_ID (spScram, SpScram)
ME_ID (mutIntensity, MutIntensity) ME_ID (mutStyle, MutStyle) ME_ID (mutMorph, MutMorph)
#undef ME_ID
}

namespace me
{
// Every parameter is generated from kParams (ParamTable.h).
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kParams[i];
        const ParameterID id { p.id, 1 };
        switch (p.kind)
        {
            case Kind::Float:
                layout.add (std::make_unique<AudioParameterFloat> (id, p.name,
                    NormalisableRange<float> (p.min, p.max, 0.0f, p.skew), p.def,
                    AudioParameterFloatAttributes().withLabel (p.label)));
                break;
            case Kind::Int:
                layout.add (std::make_unique<AudioParameterInt> (id, p.name, (int) p.min, (int) p.max, (int) p.def));
                break;
            case Kind::Choice:
                layout.add (std::make_unique<AudioParameterChoice> (id, p.name, StringArray::fromTokens (p.choices, "|", ""), (int) p.def));
                break;
            case Kind::Bool:
                layout.add (std::make_unique<AudioParameterBool> (id, p.name, p.def > 0.5f));
                break;
        }
    }
    return layout;
}
}

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ResonatorBank.h"
#include "DSP/FilterBank.h"
#include "DSP/FormantEngine.h"
#include "DSP/SpectralEngine.h"
#include "DSP/GlitchEngine.h"
#include "Parameters.h"
#include "DSP/ModEngine.h"
#include "Presets/PresetManager.h"
#include "Mutation/Mutator.h"
#include "Mutation/UndoStack.h"
#include <atomic>

class MutationEngineProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    MutationEngineProcessor();
    ~MutationEngineProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;   // latency-compensated bypass

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mutation Engine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return std::max (1, presets.size()); }
    int getCurrentProgram() override { return std::max (0, currentPreset); }
    void setCurrentProgram (int i) override { loadPreset (i); }
    const juce::String getProgramName (int i) override { return i >= 0 && i < presets.size() ? juce::String (presets.get (i).preset.name) : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- mutation (message thread only) ------------------------------------
    void mutate();                          // new sound from the current one; pushes an undo snapshot
    bool undoMutation();
    bool redoMutation();
    void storeSlotA();                      // morph endpoints (MUTATE also sets A = before, B = after)
    void storeSlotB();
    bool canUndo() const { return undoStack.canUndo(); }
    bool canRedo() const { return undoStack.canRedo(); }
    uint32_t getMutationSeed() const { return mutSeed; }
    void setMutationSeed (uint32_t s) { mutSeed = s; }
    bool isParamLocked (int paramIndex) const { return (paramLocks.load() >> paramIndex) & 1u; }
    void setParamLocked (int paramIndex, bool locked);
    me::Mask currentLockMask() const;
    me::SafetyLimits currentSafetyLimits() const;
    me::MutationState readState() const;
    void applyState (const me::MutationState&);
    float getCpuLoad() const { return cpuLoad.load(); }

    // ---- presets (message thread) ------------------------------------------
    PresetManager presets;
    void loadPreset (int index);                       // undoable
    bool saveUserPreset (const juce::String& name, const juce::String& category);
    int  getCurrentPresetIndex() const { return currentPreset; }
    juce::String getCurrentPresetName() const { return currentPresetName; }

    // ---- live values for the UI (written on the audio thread) ---------------
    std::atomic<float> modSource[me::ModEngine::kNumSources] {};
    std::atomic<float> effRootHz { 110.0f }, effFeedback { 0.0f }, outPeak { 0.0f };
    double getSampleRate() const { return sampleRate; }

    // Lock-free tap of the output for the editor's scope/spectrum.
    static constexpr int kScopeSize = 4096;
    std::atomic<float> scope[kScopeSize] {};
    std::atomic<int> scopePos { 0 };

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void applyMorph();

    me::UndoStack undoStack;
    me::MutationState slotA, slotB;
    bool slotsValid = false, applyingState = false;
    uint32_t mutSeed = 1;
    std::atomic<std::uint64_t> paramLocks { 0 };
    std::atomic<float> cpuLoad { 0.0f };

    me::ResonatorBank resonator;
    me::FilterBank filter;
    me::FormantEngine formant;
    std::unique_ptr<me::SpectralEngine> spectral = std::make_unique<me::SpectralEngine>();
    me::GlitchEngine glitch;
    me::ModEngine mod;
    // Parameter smoothing: targets are set once per block, engines are updated every kSub samples.
    static constexpr int kSub = 32;
    struct Smoothers
    {
        juce::SmoothedValue<float> mix, out, formMix, damp, fb, spread, inharm, vowel, shift, res;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoff, root;
    } sm;
    bool smoothingPrimed = false;
    float lastVelocity = 0.0f;
    int currentPreset = -1;
    juce::String currentPresetName { "Init" };
    std::vector<float> dryDelay[2];     // aligns dry with the latency of the spectral stage
    int dryPos = 0, lastSeed = -1;
    double internalPpq = 0.0;           // free-running musical clock when the host is stopped
    double sampleRate = 44100.0;
    float midiRootHz = 0.0f;   // 0 = no note held
    int heldNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationEngineProcessor)
};

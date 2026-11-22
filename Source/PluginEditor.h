#pragma once
#include "PluginProcessor.h"

// Functional editor: live scope, mutation bar (MUTATE / undo / redo / style /
// intensity / A-B morph / seed / locks) and one rotary per parameter with an
// optional per-parameter lock. The full visual design (Assets/ui-preview.png)
// lands in step 5.
class MutationEngineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MutationEngineEditor (MutationEngineProcessor&);
    ~MutationEngineEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    void timerCallback() override;

    MutationEngineProcessor& proc;
    juce::Rectangle<int> scopeArea;

    struct Knob
    {
        int paramIndex = 0;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        juce::ToggleButton lock { "lock" };
        std::unique_ptr<APVTS::SliderAttachment> attach;
    };
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox modeBox, divBox, styleBox;
    juce::ToggleButton freezeBtn { "FREEZE" };
    juce::TextButton mutateBtn { "MUTATE" }, undoBtn { "UNDO" }, redoBtn { "REDO" }, storeABtn { "SET A" }, storeBBtn { "SET B" };
    juce::Slider intensity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider morphSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label intensityLabel, morphLabel, seedLabel;
    juce::ToggleButton groupLocks[me::kNumLockGroups];

    std::unique_ptr<APVTS::ComboBoxAttachment> modeAtt, divAtt, styleAtt;
    std::unique_ptr<APVTS::ButtonAttachment> freezeAtt, groupAtt[me::kNumLockGroups];
    std::unique_ptr<APVTS::SliderAttachment> intensityAtt, morphAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationEngineEditor)
};

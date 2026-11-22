#pragma once
#include "PluginProcessor.h"

// Foundation editor: dark theme, live output scope, one rotary per parameter.
// The full visual design lives in Assets/ui-preview.png and lands in step 5.
class MutationEngineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MutationEngineEditor (MutationEngineProcessor&);
    ~MutationEngineEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (scopeArea); }

    MutationEngineProcessor& proc;
    juce::Rectangle<int> scopeArea;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationEngineEditor)
};

#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/Widgets.h"

// Full dark interface. Laid out in a fixed 1600x1000 design space that is
// scaled to the window, so proportions never change when resizing.
class MutationEngineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MutationEngineEditor (MutationEngineProcessor&);
    ~MutationEngineEditor() override;
    void resized() override;
    juce::Image takeSnapshot (float scale) { return createComponentSnapshot (getLocalBounds(), true, scale); }

private:
    struct Content : juce::Component
    {
        explicit Content (MutationEngineEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintContent (g); }
        MutationEngineEditor& ed;
    };

    void paintContent (juce::Graphics&);
    void timerCallback() override;
    void layoutLeftPanel();
    void setChoice (int paramIndex, int value);

    MutationEngineProcessor& proc;
    me::ui::MutationLookAndFeel laf;
    Content content { *this };

    std::unique_ptr<me::ui::SpectrogramDisplay> spectro;
    std::unique_ptr<me::ui::ScopeDisplay> scope;
    std::unique_ptr<me::ui::VowelDisplay> vowel;
    std::unique_ptr<me::ui::FilterDisplay> filterDisplay;
    std::unique_ptr<me::ui::ResonanceBars> bars;
    std::unique_ptr<me::ui::ModulationPanel> modPanel;
    std::unique_ptr<me::ui::ModeTabs> tabs;
    std::unique_ptr<me::ui::PresetBar> presetBar;

    std::map<int, std::unique_ptr<me::ui::KnobComponent>> knobs;
    me::ui::SegmentedControl divSeg { { "1/4", "1/8", "1/16", "1/32" }, me::ui::cyan };
    me::ui::SegmentedControl filtSeg { { "LOW-PASS", "HIGH-PASS", "BAND-PASS", "NOTCH" }, me::ui::cyan };
    me::ui::SegmentedControl styleSeg { { "ORGANIC", "DIGITAL", "UNSTABLE" }, me::ui::magenta };
    me::ui::MutateButton mutateBtn;

    juce::TextButton freezeBtn { "FREEZE SPECTRUM" }, midiBtn { "MIDI PITCH" }, undoBtn { "UNDO" }, redoBtn { "REDO" },
                     storeABtn { "SET A" }, storeBBtn { "SET B" }, rerollBtn { "REROLL" };
    juce::Slider intensity { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }, morphSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label seedLabel;
    juce::ToggleButton groupLocks[me::kNumLockGroups];

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, midiAtt, groupAtt[me::kNumLockGroups];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAtt, morphAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationEngineEditor)
};

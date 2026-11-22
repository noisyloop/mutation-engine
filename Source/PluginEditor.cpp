#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff07080f), accent (0xff00e5ff), hot (0xffff2e93);
const char* const knobIds[][2] = {
    { me::ids::resRoot, "ROOT" }, { me::ids::resVoices, "VOICES" }, { me::ids::resInharm, "INHARM" },
    { me::ids::resDamp, "DAMP" }, { me::ids::resFeed, "FEEDBACK" }, { me::ids::resSpread, "SPREAD" },
    { me::ids::vowel, "VOWEL" }, { me::ids::formShift, "FORMANT" }, { me::ids::formMix, "F-MIX" },
    { me::ids::glProb, "GL PROB" }, { me::ids::glAmt, "GL AMT" }, { me::ids::spDrift, "DRIFT" },
    { me::ids::spShift, "BIN SHIFT" }, { me::ids::spDrop, "DROPOUT" }, { me::ids::spScram, "SCRAMBLE" },
    { me::ids::filtCutoff, "CUTOFF" }, { me::ids::filtRes, "RES" }, { me::ids::mix, "MIX" }, { me::ids::output, "OUT" } };
}

MutationEngineEditor::MutationEngineEditor (MutationEngineProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (auto& k : knobIds)
    {
        auto kn = std::make_unique<Knob>();
        kn->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        kn->slider.setColour (juce::Slider::thumbColourId, hot);
        kn->slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        kn->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        kn->label.setText (k[1], juce::dontSendNotification);
        kn->label.setJustificationType (juce::Justification::centred);
        kn->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        kn->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, k[0], kn->slider);
        addAndMakeVisible (kn->slider); addAndMakeVisible (kn->label);
        knobs.push_back (std::move (kn));
    }
    modeBox.addItemList ({ "Resonate", "Glitch", "Phantom" }, 1);
    divBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    modeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, me::ids::mode, modeBox);
    divAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, me::ids::glDiv, divBox);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, me::ids::spFreeze, freezeBtn);
    addAndMakeVisible (modeBox); addAndMakeVisible (divBox); addAndMakeVisible (freezeBtn);
    setResizable (true, true);
    setResizeLimits (800, 480, 1800, 1100);
    setSize (1100, 620);
    startTimerHz (30);
}

void MutationEngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("MUTATION ENGINE", 24, 12, 400, 30, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (scopeArea.toFloat(), 10.0f);

    juce::Path path;
    const int n = MutationEngineProcessor::kScopeSize;
    const int start = proc.scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float s = proc.scope[(start + i) % n].load (std::memory_order_relaxed);
        const float x = scopeArea.getX() + scopeArea.getWidth() * (float) i / (float) n;
        const float y = scopeArea.getCentreY() - s * scopeArea.getHeight() * 0.45f;
        i == 0 ? path.startNewSubPath (x, y) : path.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.25f)); g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (accent);                  g.strokePath (path, juce::PathStrokeType (1.5f));
}

void MutationEngineEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto top = r.removeFromTop (40);
    freezeBtn.setBounds (top.removeFromRight (90)); divBox.setBounds (top.removeFromRight (80).reduced (4)); modeBox.setBounds (top.removeFromRight (130).reduced (4));
    scopeArea = r.removeFromTop (r.getHeight() / 2).reduced (0, 6);
    const int cols = 10, rows = 2;
    const int w = r.getWidth() / cols, h = r.getHeight() / rows;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (r.getX() + (int) (i % cols) * w, r.getY() + (int) (i / cols) * h, w, h);
        knobs[i]->label.setBounds (cell.removeFromTop (18));
        knobs[i]->slider.setBounds (cell.reduced (6));
    }
}

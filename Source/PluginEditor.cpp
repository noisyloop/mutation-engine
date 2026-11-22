#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff07080f), accent (0xff00e5ff), hot (0xffff2e93), amber (0xffffb02e);

juce::StringArray StringArray_fromChoices (const char* c) { return juce::StringArray::fromTokens (c, "|", ""); }
bool kParamsMutates (int i) { return me::kParams[i].mutates; }

void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredRight);
    l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
}
}

MutationEngineEditor::MutationEngineEditor (MutationEngineProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    using namespace me;

    // Rotary per float/int sound parameter (seed has its own field).
    for (int i = 0; i < kNumSound; ++i)
    {
        const auto& info = kParams[i];
        if ((info.kind != Kind::Float && info.kind != Kind::Int) || i == pid::Seed) continue;
        auto k = std::make_unique<Knob>();
        k->paramIndex = i;
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        k->slider.setColour (juce::Slider::thumbColourId, hot);
        k->slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->label.setText (juce::String (info.name).toUpperCase(), juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        k->attach = std::make_unique<APVTS::SliderAttachment> (p.apvts, info.id, k->slider);
        addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
        if (info.mutates)
        {
            k->lock.setButtonText ("LOCK");
            k->lock.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.5f));
            k->lock.setColour (juce::ToggleButton::tickColourId, amber);
            k->lock.setToggleState (p.isParamLocked (i), juce::dontSendNotification);
            k->lock.onClick = [this, i, kp = k.get()] { proc.setParamLocked (i, kp->lock.getToggleState()); };
            addAndMakeVisible (k->lock);
        }
        knobs.push_back (std::move (k));
    }

    modeBox.addItemList (StringArray_fromChoices (kParams[pid::Mode].choices), 1);
    divBox.addItemList (StringArray_fromChoices (kParams[pid::GlDiv].choices), 1);
    styleBox.addItemList (StringArray_fromChoices (kParams[pid::MutStyle].choices), 1);
    modeAtt  = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, ids::mode, modeBox);
    divAtt   = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, ids::glDiv, divBox);
    styleAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, ids::mutStyle, styleBox);
    freezeAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::spFreeze, freezeBtn);

    intensity.setSliderStyle (juce::Slider::LinearHorizontal);
    intensityAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, ids::mutIntensity, intensity);
    morphAtt     = std::make_unique<APVTS::SliderAttachment> (p.apvts, ids::mutMorph, morphSlider);
    styleLabel (intensityLabel, "INTENSITY"); styleLabel (morphLabel, "MORPH A-B");

    for (int k = 0; k < kNumLockGroups; ++k)
    {
        static const char* names[] = { "LOCK RES", "LOCK FORM", "LOCK FILTER", "LOCK SPEC", "LOCK GLITCH" };
        groupLocks[k].setButtonText (names[k]);
        groupLocks[k].setColour (juce::ToggleButton::tickColourId, amber);
        groupAtt[k] = std::make_unique<APVTS::ButtonAttachment> (p.apvts, kParams[kLockGroups[k].paramIndex].id, groupLocks[k]);
        addAndMakeVisible (groupLocks[k]);
    }

    mutateBtn.setColour (juce::TextButton::buttonColourId, hot);
    mutateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    mutateBtn.onClick = [this] { proc.mutate(); };
    undoBtn.onClick   = [this] { proc.undoMutation(); };
    redoBtn.onClick   = [this] { proc.redoMutation(); };
    storeABtn.onClick = [this] { proc.storeSlotA(); };
    storeBBtn.onClick = [this] { proc.storeSlotB(); };

    seedLabel.setEditable (true);
    seedLabel.setColour (juce::Label::textColourId, accent);
    seedLabel.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    seedLabel.onTextChange = [this]
    {
        auto t = seedLabel.getText().trim();
        const auto v = t.startsWithIgnoreCase ("0x") ? (juce::uint32) t.substring (2).getHexValue32()
                                                     : (juce::uint32) t.getLargeIntValue();
        proc.setMutationSeed (v);
    };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &modeBox, &divBox, &styleBox, &freezeBtn, &mutateBtn, &undoBtn, &redoBtn,
                                                                         &storeABtn, &storeBBtn, &intensity, &morphSlider, &intensityLabel, &morphLabel, &seedLabel })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (960, 640, 1800, 1100);
    setSize (1200, 760);
    startTimerHz (30);
}

void MutationEngineEditor::timerCallback()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    if (! seedLabel.isBeingEdited())
        seedLabel.setText ("0x" + juce::String::toHexString ((int) proc.getMutationSeed()).paddedLeft ('0', 8).toUpperCase(), juce::dontSendNotification);
    for (auto& k : knobs)
        if (kParamsMutates (k->paramIndex) && k->lock.getToggleState() != proc.isParamLocked (k->paramIndex))
            k->lock.setToggleState (proc.isParamLocked (k->paramIndex), juce::dontSendNotification);
    repaint (scopeArea);
}

void MutationEngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("MUTATION ENGINE", 24, 10, 400, 30, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (scopeArea.toFloat(), 10.0f);

    juce::Path path;
    const int n = MutationEngineProcessor::kScopeSize;
    const int start = proc.scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float s = proc.scope[(start + i) % n].load (std::memory_order_relaxed);
        const float x = (float) scopeArea.getX() + (float) scopeArea.getWidth() * (float) i / (float) n;
        const float y = (float) scopeArea.getCentreY() - s * (float) scopeArea.getHeight() * 0.45f;
        i == 0 ? path.startNewSubPath (x, y) : path.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.25f)); g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (accent);                  g.strokePath (path, juce::PathStrokeType (1.5f));
}

void MutationEngineEditor::resized()
{
    auto r = getLocalBounds().reduced (20);

    auto top = r.removeFromTop (36);
    freezeBtn.setBounds (top.removeFromRight (90));
    divBox.setBounds (top.removeFromRight (80).reduced (4));
    modeBox.setBounds (top.removeFromRight (130).reduced (4));

    auto bar1 = r.removeFromTop (38);
    mutateBtn.setBounds (bar1.removeFromLeft (150).reduced (2));
    undoBtn.setBounds (bar1.removeFromLeft (70).reduced (2));
    redoBtn.setBounds (bar1.removeFromLeft (70).reduced (2));
    styleBox.setBounds (bar1.removeFromLeft (120).reduced (4));
    storeABtn.setBounds (bar1.removeFromLeft (70).reduced (2));
    storeBBtn.setBounds (bar1.removeFromLeft (70).reduced (2));
    seedLabel.setBounds (bar1.removeFromRight (150).reduced (4));

    auto bar2 = r.removeFromTop (34);
    intensityLabel.setBounds (bar2.removeFromLeft (90));
    intensity.setBounds (bar2.removeFromLeft (230));
    morphLabel.setBounds (bar2.removeFromLeft (100));
    morphSlider.setBounds (bar2.removeFromLeft (230));
    const int gw = std::max (60, bar2.getWidth() / me::kNumLockGroups);
    for (auto& b : groupLocks) b.setBounds (bar2.removeFromLeft (gw));

    scopeArea = r.removeFromTop (r.getHeight() * 2 / 5).reduced (0, 6);

    const int cols = 10, rows = 2;
    const int w = r.getWidth() / cols, h = r.getHeight() / rows;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (r.getX() + (int) (i % cols) * w, r.getY() + (int) (i / cols) * h, w, h);
        knobs[i]->label.setBounds (cell.removeFromTop (18));
        knobs[i]->lock.setBounds (cell.removeFromBottom (20));
        knobs[i]->slider.setBounds (cell.reduced (4));
    }
}

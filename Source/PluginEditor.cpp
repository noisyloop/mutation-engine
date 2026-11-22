#include "PluginEditor.h"

using namespace me;
using namespace me::ui;

namespace
{
constexpr int W = 1600, H = 1000;
const juce::Rectangle<int> leftR { 24, 128, 330, 640 }, specR { 368, 128, 864, 490 }, scopeR { 368, 632, 864, 136 },
    vowelR { 1246, 128, 330, 230 }, filterR { 1246, 372, 330, 140 }, modR { 1246, 526, 330, 242 },
    mutR { 24, 782, 330, 210 }, centerR { 368, 782, 864, 210 }, morphR { 1246, 782, 330, 210 };

int paramInt (MutationEngineProcessor& p, int idx) { return (int) rawParam (p, idx); }
}

MutationEngineEditor::MutationEngineEditor (MutationEngineProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (content);
    auto add = [this] (juce::Component& c) { content.addAndMakeVisible (c); };

    spectro = std::make_unique<SpectrogramDisplay> (p);   scope = std::make_unique<ScopeDisplay> (p);
    vowel = std::make_unique<VowelDisplay> (p);           filterDisplay = std::make_unique<FilterDisplay> (p);
    bars = std::make_unique<ResonanceBars> (p);           modPanel = std::make_unique<ModulationPanel> (p);
    tabs = std::make_unique<ModeTabs> (p);                presetBar = std::make_unique<PresetBar> (p);
    for (auto* c : std::initializer_list<juce::Component*> { spectro.get(), scope.get(), vowel.get(), filterDisplay.get(), bars.get(), modPanel.get(), tabs.get(), presetBar.get() }) content.addAndMakeVisible (c);

    // knobs (positioned per mode in layoutLeftPanel)
    const int knobIds[] = { pid::ResRoot, pid::ResVoices, pid::ResInharm, pid::ResDamp, pid::ResFeed, pid::ResSpread,
                            pid::GlProb, pid::GlAmt, pid::SpShift, pid::SpDrop, pid::SpScram, pid::SpDrift,
                            pid::Vowel, pid::FormShift, pid::FormMix, pid::FiltCutoff, pid::FiltRes, pid::Mix, pid::Output };
    for (int id : knobIds)
    {
        const auto accent = (id == pid::ResFeed) ? amber : (id == pid::ResInharm || id == pid::SpScram || id == pid::Vowel) ? violet : cyan;
        knobs[id] = std::make_unique<KnobComponent> (p, id, accent);
        content.addAndMakeVisible (*knobs[id]);
    }
    knobs[pid::Mix]->setName ("MIX"); knobs[pid::Output]->setName ("OUT"); knobs[pid::FiltRes]->setName ("RES");
    knobs[pid::FormMix]->setName ("F-MIX"); knobs[pid::FormShift]->setName ("SHIFT"); knobs[pid::ResFeed]->setName ("FEEDBACK");
    knobs[pid::GlProb]->setName ("PROB"); knobs[pid::GlAmt]->setName ("AMOUNT"); knobs[pid::SpDrop]->setName ("DROPOUT"); knobs[pid::SpScram]->setName ("SCRAMBLE");

    divSeg.onSelect = [this] (int i) { setChoice (pid::GlDiv, i); };
    filtSeg.onSelect = [this] (int i) { setChoice (pid::FiltType, i); };
    styleSeg.onSelect = [this] (int i) { setChoice (pid::MutStyle, i); };
    for (auto* c : std::initializer_list<juce::Component*> { &divSeg, &filtSeg, &styleSeg, &mutateBtn }) add (*c);

    freezeBtn.setClickingTogglesState (true); midiBtn.setClickingTogglesState (true);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::spFreeze, freezeBtn);
    midiAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::midiTrack, midiBtn);
    for (auto* c : std::initializer_list<juce::Component*> { &freezeBtn, &midiBtn, &undoBtn, &redoBtn, &storeABtn, &storeBBtn, &rerollBtn, &intensity, &morphSlider, &seedLabel }) add (*c);

    intensity.setColour (juce::Slider::trackColourId, magenta); morphSlider.setColour (juce::Slider::trackColourId, cyan);
    intensityAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids::mutIntensity, intensity);
    morphAtt     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids::mutMorph, morphSlider);

    for (int k = 0; k < kNumLockGroups; ++k)
    {
        static const char* names[] = { "RES", "FORM", "FILT", "SPEC", "GLCH" };
        groupLocks[k].setButtonText (names[k]);
        groupAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, kParams[kLockGroups[k].paramIndex].id, groupLocks[k]);
        add (groupLocks[k]);
    }

    mutateBtn.onClick = [this] { proc.mutate(); };
    undoBtn.onClick = [this] { proc.undoMutation(); };
    redoBtn.onClick = [this] { proc.redoMutation(); };
    storeABtn.onClick = [this] { proc.storeSlotA(); };
    storeBBtn.onClick = [this] { proc.storeSlotB(); };
    rerollBtn.onClick = [this] { proc.setMutationSeed ((uint32_t) juce::Random::getSystemRandom().nextInt()); };

    seedLabel.setEditable (true);
    seedLabel.setColour (juce::Label::textColourId, cyan);
    seedLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff090b16));
    seedLabel.setColour (juce::Label::outlineColourId, line);
    seedLabel.setFont (mono (12.0f)); seedLabel.setJustificationType (juce::Justification::centred);
    seedLabel.onTextChange = [this]
    {
        const auto t = seedLabel.getText().trim();
        proc.setMutationSeed (t.startsWithIgnoreCase ("0x") ? (juce::uint32) t.substring (2).getHexValue32() : (juce::uint32) t.getLargeIntValue());
    };

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) W / (double) H);
    setResizeLimits (800, 500, 2400, 1500);
    setSize (1280, 800);
    layoutLeftPanel();
    startTimerHz (30);
}

MutationEngineEditor::~MutationEngineEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MutationEngineEditor::setChoice (int idx, int value)
{
    if (auto* prm = proc.apvts.getParameter (kParams[idx].id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value)); prm->endChangeGesture(); }
}

void MutationEngineEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) W));
    content.setBounds (0, 0, W, H);

    tabs->setBounds (24, 76, 1000, 40);
    presetBar->setBounds (380, 12, 460, 38);
    midiBtn.setBounds (856, 14, 130, 34);
    spectro->setBounds (specR); scope->setBounds (scopeR);
    vowel->setBounds (vowelR); filterDisplay->setBounds (filterR); modPanel->setBounds (modR);

    // mutation panel
    styleSeg.setBounds (mutR.getX() + 14, mutR.getY() + 38, 302, 30);
    intensity.setBounds (mutR.getX() + 92, mutR.getY() + 76, 238, 26);
    seedLabel.setBounds (mutR.getX() + 62, mutR.getY() + 110, 160, 26); rerollBtn.setBounds (mutR.getX() + 232, mutR.getY() + 110, 84, 26);
    for (int k = 0; k < kNumLockGroups; ++k) groupLocks[k].setBounds (mutR.getX() + 14 + k * 61, mutR.getY() + 164, 58, 26);

    // centre footer
    mutateBtn.setBounds (centerR.getCentreX() - 130, centerR.getY(), 260, centerR.getHeight());
    undoBtn.setBounds (centerR.getX() + 24, centerR.getBottom() - 50, 84, 30); redoBtn.setBounds (centerR.getX() + 116, centerR.getBottom() - 50, 84, 30);

    // morph panel
    storeABtn.setBounds (morphR.getX() + 14, morphR.getY() + 36, 64, 28);
    morphSlider.setBounds (morphR.getX() + 84, morphR.getY() + 34, 162, 32);
    storeBBtn.setBounds (morphR.getRight() - 78, morphR.getY() + 36, 64, 28);
    bars->setBounds (morphR.getX() + 14, morphR.getY() + 108, 302, 84);
    layoutLeftPanel();
}

void MutationEngineEditor::layoutLeftPanel()
{
    const int mode = juce::jlimit (0, 2, paramInt (proc, pid::Mode));
    const int ox = leftR.getX() + 14, oy = leftR.getY();
    for (auto& k : knobs) k.second->setVisible (false);
    divSeg.setVisible (mode == 1); freezeBtn.setVisible (mode == 2);

    auto place3 = [&] (int id, int col, int y) { auto& k = *knobs[id]; k.setBounds (ox + col * 100, oy + y, 100, 108); k.setVisible (true); };
    if (mode == 0)
    {
        place3 (pid::ResRoot, 0, 40);  place3 (pid::ResVoices, 1, 40); place3 (pid::ResInharm, 2, 40);
        place3 (pid::ResDamp, 0, 148); place3 (pid::ResFeed, 1, 148);  place3 (pid::ResSpread, 2, 148);
    }
    else if (mode == 1)
    {
        divSeg.setBounds (ox, oy + 40, 302, 28);
        place3 (pid::GlProb, 0, 76);  place3 (pid::GlAmt, 1, 76);   place3 (pid::SpShift, 2, 76);
        place3 (pid::SpDrop, 0, 168); place3 (pid::SpScram, 1, 168);
    }
    else
    {
        freezeBtn.setBounds (ox, oy + 40, 302, 34);
        place3 (pid::SpDrift, 0, 86); place3 (pid::SpShift, 1, 86);
    }
    place3 (pid::Vowel, 0, 306); place3 (pid::FormShift, 1, 306); place3 (pid::FormMix, 2, 306);
    filtSeg.setBounds (ox, oy + 458, 302, 26);
    for (int i = 0; i < 4; ++i)
    {
        const int id = i == 0 ? pid::FiltCutoff : i == 1 ? pid::FiltRes : i == 2 ? pid::Mix : pid::Output;
        auto& k = *knobs[id]; k.setBounds (ox + i * 75, oy + 492, 75, 108); k.setVisible (true);
    }
}

void MutationEngineEditor::timerCallback()
{
    static int lastMode = -1;
    const int mode = paramInt (proc, pid::Mode);
    if (mode != lastMode) { lastMode = mode; layoutLeftPanel(); content.repaint (leftR); }

    spectro->tick(); scope->tick(); vowel->tick(); filterDisplay->tick(); bars->tick(); modPanel->repaint(); tabs->repaint();
    presetBar->refresh(); presetBar->repaint();
    for (auto& k : knobs) k.second->refresh();
    styleSeg.setSelected (paramInt (proc, pid::MutStyle)); filtSeg.setSelected (paramInt (proc, pid::FiltType)); divSeg.setSelected (paramInt (proc, pid::GlDiv));
    undoBtn.setEnabled (proc.canUndo()); redoBtn.setEnabled (proc.canRedo());
    if (! seedLabel.isBeingEdited()) seedLabel.setText ("0x" + juce::String::toHexString ((int) proc.getMutationSeed()).paddedLeft ('0', 8).toUpperCase(), juce::dontSendNotification);
    content.repaint (0, 0, W, 64); content.repaint (mutR); content.repaint (centerR); content.repaint (morphR); content.repaint (leftR.getX(), leftR.getY(), leftR.getWidth(), 36);
}

void MutationEngineEditor::paintContent (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141a3a), (float) W * 0.5f, -100.0f, juce::Colour (0xff04050a), (float) W * 0.5f, (float) H, false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1020), 0, 0, juce::Colour (0xff080a14), 0, 62, false)); g.fillRect (0, 0, W, 62);
    g.setColour (line); g.drawHorizontalLine (62, 0, (float) W);

    // logo
    juce::Path wave; wave.startNewSubPath (26, 31); wave.cubicTo (30, 8, 34, 8, 38, 31); wave.cubicTo (42, 54, 46, 54, 50, 31);
    g.setGradientFill (juce::ColourGradient (cyan, 24, 0, magenta, 52, 0, false)); g.strokePath (wave, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont (font (16.0f, true, 0.34f)); g.setColour (cyan); g.drawText ("MUTATION", 62, 16, 150, 30, juce::Justification::left, false);
    g.setColour (juce::Colours::white); g.drawText ("ENGINE", 190, 16, 130, 30, juce::Justification::left, false);

    // header meters
    const float cpu = juce::jlimit (0.0f, 1.0f, proc.getCpuLoad()), peak = proc.outPeak.load(), fb = proc.effFeedback.load();
    const float peakDb = juce::Decibels::gainToDecibels (peak, -60.0f);
    auto meter = [&] (int x, const juce::String& label, float frac, const juce::String& val, juce::Colour c)
    {
        g.setColour (dim); g.setFont (font (9.0f, true, 0.18f)); g.drawText (label, x, 20, 50, 22, juce::Justification::left, false);
        const juce::Rectangle<float> bar ((float) x + 40, 29.0f, 56.0f, 5.0f);
        g.setColour (juce::Colour (0xff12162a)); g.fillRoundedRectangle (bar, 2.5f);
        g.setGradientFill (juce::ColourGradient (ok, bar.getX(), 0, c, bar.getRight(), 0, false)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, frac)), 2.5f);
        g.setColour (text); g.setFont (mono (10.5f)); g.drawText (val, x + 102, 20, 56, 22, juce::Justification::left, false);
    };
    meter (1030, "CPU", cpu * 4.0f, juce::String (cpu * 100.0f, 1) + "%", cyan);
    meter (1210, "OUT", (peakDb + 48.0f) / 48.0f, juce::String (peakDb, 1), peakDb > -1.0f ? amber : cyan);
    g.setColour (dim); g.setFont (font (9.0f, true, 0.18f)); g.drawText ("LATENCY", 1390, 20, 60, 22, juce::Justification::left, false);
    g.setColour (text); g.setFont (mono (10.5f)); g.drawText (juce::String (proc.getLatencySamples()) + " smp", 1448, 20, 70, 22, juce::Justification::left, false);
    const bool safe = fb <= 0.851f;
    g.setColour (safe ? ok : amber); g.drawText (safe ? u8 ("● SAFE") : u8 ("● LIMIT"), 1514, 20, 70, 22, juce::Justification::left, false);

    // panels
    for (auto r : { leftR, mutR, centerR, morphR }) drawPanel (g, r.toFloat());

    // left panel headings
    const int mode = juce::jlimit (0, 2, paramInt (proc, pid::Mode));
    const juce::String titles[] = { "RESONATOR BANK", "GLITCH ENGINE", u8 ("PHANTOM · SPECTRAL FREEZE") };
    const juce::String right = mode == 0 ? juce::String (paramInt (proc, pid::ResVoices)) + " VOICES" : mode == 1 ? "STFT 1024" : "STFT 1024";
    drawHeading (g, leftR.withTrimmedLeft (14).withTrimmedRight (14).withHeight (36).withY (leftR.getY() + 12).withHeight (20), titles[mode], right);
    drawHeading (g, { leftR.getX() + 14, leftR.getY() + 284, 302, 20 }, "FORMANT", "THREE-PEAK VOWEL");
    drawHeading (g, { leftR.getX() + 14, leftR.getY() + 430, 302, 20 }, u8 ("FILTER · OUTPUT"));

    // mutation panel
    drawHeading (g, { mutR.getX() + 14, mutR.getY() + 12, 302, 20 }, "MUTATION", "INTENSITY " + juce::String (juce::roundToInt (rawParam (proc, pid::MutIntensity) * 100.0f)) + "%");
    g.setColour (dim); g.setFont (font (9.0f, true, 0.18f));
    g.drawText ("AMOUNT", mutR.getX() + 14, mutR.getY() + 76, 76, 26, juce::Justification::centredLeft, false);
    g.drawText ("SEED", mutR.getX() + 14, mutR.getY() + 110, 46, 26, juce::Justification::centredLeft, false);
    g.drawText ("LOCK GROUPS", mutR.getX() + 14, mutR.getY() + 146, 200, 16, juce::Justification::left, false);

    // centre footer: what is locked / what will mutate, and live safety readout
    const auto lock = proc.currentLockMask();
    juce::StringArray locked, free;
    static const char* gnames[] = { "RES", "FORM", "FILT", "SPEC", "GLITCH" };
    for (int k = 0; k < kNumLockGroups; ++k) ((lock & groupMask (kLockGroups[k].group)) == groupMask (kLockGroups[k].group) ? locked : free).add (gnames[k]);
    g.setFont (font (10.0f, true, 0.18f));
    const juce::Rectangle<int> lb (centerR.getX() + 20, centerR.getY() + 50, 260, 100), rb (centerR.getRight() - 280, centerR.getY() + 50, 260, 100);
    g.setColour (dim); g.drawText ("LOCKED", lb.withHeight (18), juce::Justification::centredRight, false);
    g.setColour (amber); g.drawText (locked.isEmpty() ? "-" : locked.joinIntoString (u8 (" · ")), lb.withTrimmedTop (18).withHeight (18), juce::Justification::centredRight, false);
    g.setColour (dim); g.drawText ("MUTATING", lb.withTrimmedTop (52).withHeight (18), juce::Justification::centredRight, false);
    g.setColour (text); g.setFont (font (9.5f, true, 0.1f)); g.drawText (free.isEmpty() ? "-" : free.joinIntoString (u8 (" · ")), lb.withTrimmedTop (70).withHeight (18), juce::Justification::centredRight, false);
    g.setFont (font (10.0f, true, 0.18f));
    g.setColour (dim); g.drawText ("PEAK", rb.withHeight (18), juce::Justification::centredLeft, false);
    g.setColour (peakDb > -1.0f ? amber : ok); g.setFont (mono (13.0f)); g.drawText (juce::String (peakDb, 1) + " dBFS", rb.withTrimmedTop (18).withHeight (20), juce::Justification::centredLeft, false);
    g.setFont (font (10.0f, true, 0.18f)); g.setColour (dim); g.drawText ("FEEDBACK LIMIT", rb.withTrimmedTop (52).withHeight (18), juce::Justification::centredLeft, false);
    g.setColour (fb <= 0.851f ? ok : amber); g.setFont (mono (13.0f)); g.drawText (juce::String (fb, 2) + u8 (" · ") + (fb <= 0.851f ? "SAFE" : "LIMITED"), rb.withTrimmedTop (70).withHeight (20), juce::Justification::centredLeft, false);

    // morph panel
    drawHeading (g, { morphR.getX() + 14, morphR.getY() + 12, 302, 20 }, "MORPH", "A / B");
    g.setColour (cyan); g.setFont (font (9.5f, true, 0.18f)); g.drawText (u8 ("A · BEFORE"), morphR.getX() + 14, morphR.getY() + 72, 140, 14, juce::Justification::left, false);
    g.setColour (magenta); g.drawText (u8 ("B · AFTER"), morphR.getRight() - 154, morphR.getY() + 72, 140, 14, juce::Justification::right, false);
    g.setColour (dim); g.setFont (font (9.0f, true, 0.2f)); g.drawText ("RESONANCE BANK", morphR.getX() + 14, morphR.getY() + 92, 200, 14, juce::Justification::left, false);
}

#pragma once
#include "Displays.h"

namespace me::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

inline juce::StringArray choicesOf (int paramIndex) { return juce::StringArray::fromTokens (kParams[paramIndex].choices, "|", ""); }

// ------------------------------------------------------------------------ knob
class KnobComponent : public juce::Component
{
public:
    KnobComponent (MutationEngineProcessor& p, int paramIndex, juce::Colour accent = cyan) : proc (p), idx (paramIndex)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (1.25f * juce::MathConstants<float>::pi, 2.75f * juce::MathConstants<float>::pi, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        attach = std::make_unique<APVTS::SliderAttachment> (p.apvts, kParams[idx].id, slider);
        slider.textFromValueFunction = [i = idx] (double v) { return formatValue (i, v); };
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        name = juce::String (kParams[idx].name).toUpperCase();
    }

    void setName (const juce::String& n) { name = n; repaint(); }
    int paramIndex() const { return idx; }

    void resized() override
    {
        const int s = std::min (getWidth() - 6, 70);
        slider.setBounds ((getWidth() - s) / 2, 8, s, s);
    }

    void refresh()
    {
        const bool l = (proc.currentLockMask() >> idx) & 1u;
        bool m = false;
        for (int k = 0; k < kNumModSlots; ++k)
        {
            const int dst = (int) rawParam (proc, modSlotParam (k, 1));
            m |= dst > 0 && kModDst[dst] == idx && (int) rawParam (proc, modSlotParam (k, 0)) > 0 && std::fabs (rawParam (proc, modSlotParam (k, 2))) > 1e-6f;
        }
        if (l != locked || m != modulated) { locked = l; modulated = m; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (dim); g.setFont (font (9.0f, true, 0.18f));
        g.drawText (name, 0, 80, getWidth(), 12, juce::Justification::centred, false);
        g.setColour (text); g.setFont (mono (11.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), 0, 93, getWidth(), 14, juce::Justification::centred, false);
        if (modulated) { g.setColour (violet); g.fillEllipse ((float) getWidth() * 0.5f + 28.0f, 82.0f, 6.0f, 6.0f); }
        if (kParams[idx].mutates)
        {
            const auto r = lockRect().toFloat();
            const auto c = locked ? amber : juce::Colour (0xff2a3354);
            g.setColour (c); g.fillRoundedRectangle (r.getX(), r.getY() + 4.0f, r.getWidth(), r.getHeight() - 4.0f, 2.0f);
            juce::Path sh; sh.addCentredArc (r.getCentreX(), r.getY() + 4.0f, 3.0f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi * 2.0f, 0.0f, true);
            g.strokePath (sh, juce::PathStrokeType (1.4f));
            if (locked) { g.setColour (amber.withAlpha (0.25f)); g.fillEllipse (r.expanded (4.0f)); }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (kParams[idx].mutates && lockRect().expanded (4).contains (e.getPosition())) { proc.setParamLocked (idx, ! proc.isParamLocked (idx)); refresh(); }
    }

private:
    juce::Rectangle<int> lockRect() const { return { getWidth() / 2 + 24, 6, 10, 10 }; }
    MutationEngineProcessor& proc; int idx;
    juce::Slider slider; std::unique_ptr<APVTS::SliderAttachment> attach;
    juce::String name; bool locked = false, modulated = false;
};

// ------------------------------------------------------------------- mode tabs
class ModeTabs : public juce::Component
{
public:
    explicit ModeTabs (MutationEngineProcessor& p) : proc (p) {}
    static constexpr const char* names[6] = { "RESONATE", "GLITCH", "PHANTOM", "FRACTAL", "LIQUEFY", "COLLAPSE" };
    static constexpr int widths[6] = { 168, 140, 150, 150, 150, 160 };

    juce::Rectangle<int> tab (int i) const { int x = 0; for (int k = 0; k < i; ++k) x += widths[k] + 10; return { x, 0, widths[i], getHeight() }; }

    void paint (juce::Graphics& g) override
    {
        const int cur = (int) rawParam (proc, pid::Mode);
        for (int i = 0; i < 6; ++i)
        {
            const auto r = tab (i).toFloat().reduced (0.5f); const bool on = i == cur, off = i >= 3;
            g.setColour (juce::Colour (0xff090b16)); g.fillRoundedRectangle (r, 10.0f);
            if (on)
            {
                g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.2f), 0, r.getY(), cyan.withAlpha (0.03f), 0, r.getBottom(), false)); g.fillRoundedRectangle (r, 10.0f);
                g.setColour (cyan.withAlpha (0.22f)); g.drawRoundedRectangle (r.expanded (2.0f), 12.0f, 3.0f);
            }
            g.setColour (on ? cyan : line); g.drawRoundedRectangle (r, 10.0f, 1.0f);
            g.setColour (on ? juce::Colours::white : (off ? dim.withAlpha (0.45f) : dim));
            g.setFont (font (11.0f, true, 0.2f));
            g.drawText (names[i], r.toNearestInt().withTrimmedRight (off ? 26 : 0), juce::Justification::centred, false);
            if (off)
            {
                const auto badge = juce::Rectangle<float> (r.getRight() - 30.0f, r.getCentreY() - 7.0f, 22.0f, 14.0f);
                g.setColour (juce::Colour (0xff2a3050)); g.drawRoundedRectangle (badge, 4.0f, 1.0f);
                g.setFont (font (8.0f, true, 0.1f)); g.setColour (dim.withAlpha (0.6f)); g.drawText ("P1", badge.toNearestInt(), juce::Justification::centred, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 3; ++i)   // P1 modes are not available yet
            if (tab (i).contains (e.getPosition()))
                if (auto* prm = proc.apvts.getParameter (ids::mode)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture(); }
    }
private:
    MutationEngineProcessor& proc;
};

// ------------------------------------------------------------ segmented control
class SegmentedControl : public juce::Component
{
public:
    SegmentedControl (juce::StringArray n, juce::Colour c = magenta) : items (std::move (n)), accent (c) {}
    std::function<void (int)> onSelect;
    void setSelected (int i) { if (i != sel) { sel = i; repaint(); } }
    int getSelected() const { return sel; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff090b16)); g.fillRoundedRectangle (b, 9.0f);
        const float w = b.getWidth() / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            const auto r = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
            if (i == sel)
            {
                g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.38f), 0, r.getY(), accent.withAlpha (0.1f), 0, r.getBottom(), false));
                g.fillRoundedRectangle (r.reduced (1.5f), 8.0f);
            }
            g.setColour (i == sel ? juce::Colours::white : dim); g.setFont (font (10.0f, true, 0.16f));
            g.drawText (items[i], r.toNearestInt(), juce::Justification::centred, false);
        }
        g.setColour (line); g.drawRoundedRectangle (b, 9.0f, 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, items.size() - 1, (int) ((float) e.x / ((float) getWidth() / (float) items.size())));
        if (onSelect) onSelect (i);
    }
private:
    juce::StringArray items; juce::Colour accent; int sel = 0;
};

// ----------------------------------------------------------------- MUTATE button
class MutateButton : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onClick;
    MutateButton() { startTimerHz (60); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat(); const auto c = b.getCentre(); const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - 34.0f;
        for (int i = 0; i < 2; ++i)
        {
            const float rr = r + 14.0f + 16.0f * (float) i + pulse * 26.0f * (float) (i + 1);
            g.setColour ((i ? violet : magenta).withAlpha ((i ? 0.25f : 0.45f) * (1.0f - pulse * 0.7f))); g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 1.0f);
        }
        g.setGradientFill (juce::ColourGradient (magenta.withAlpha (0.35f), c.x, c.y, magenta.withAlpha (0.0f), c.x + r * 2.0f, c.y, true)); g.fillEllipse (c.x - r * 2, c.y - r * 2, r * 4, r * 4);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff5fb0).brighter (hover ? 0.15f : 0.0f), c.x - r * 0.3f, c.y - r * 0.4f, juce::Colour (0xff8a1a9c), c.x + r * 0.6f, c.y + r * 0.8f, true));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::white.withAlpha (0.28f)); g.drawEllipse (c.x - r + 2, c.y - r + 2, r * 2 - 4, r * 2 - 4, 1.5f);
        g.setColour (juce::Colours::white); g.setFont (font (16.0f, true, 0.24f));
        g.drawText ("MUTATE", b.toNearestInt(), juce::Justification::centred, false);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { pulse = 1.0f; if (onClick) onClick(); }
    bool hitTest (int x, int y) override { return getLocalBounds().getCentre().getDistanceFrom ({ x, y }) < std::min (getWidth(), getHeight()) * 0.5f - 30.0f; }
private:
    void timerCallback() override { if (pulse > 0.0f) { pulse = std::max (0.0f, pulse - 0.04f); repaint(); } }
    float pulse = 0.0f; bool hover = false;
};

// -------------------------------------------------------------- preset browser
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (MutationEngineProcessor& p) : proc (p)
    {
        for (auto* b : { &prev, &next, &nameBtn, &save }) addAndMakeVisible (b);
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (+1); };
        nameBtn.onClick = [this] { showMenu(); };
        save.onClick = [this] { promptSave(); };
    }

    void refresh() { const auto n = proc.getCurrentPresetName(); if (n != nameBtn.getButtonText()) nameBtn.setButtonText (n); }
    void resized() override
    {
        auto r = getLocalBounds();
        prev.setBounds (r.removeFromLeft (34)); next.setBounds (r.removeFromRight (34).translated (-62, 0).withWidth (34));
        save.setBounds (getLocalBounds().removeFromRight (58));
        nameBtn.setBounds (getLocalBounds().withTrimmedLeft (36).withTrimmedRight (98));
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff090b16)); g.fillRoundedRectangle (b, 10.0f);
        g.setColour (line); g.drawRoundedRectangle (b, 10.0f, 1.0f);
        const int i = proc.getCurrentPresetIndex();
        if (i >= 0 && i < proc.presets.size())
        {
            g.setColour (dim); g.setFont (font (9.0f, true, 0.14f));
            g.drawText (juce::String (proc.presets.get (i).preset.category).toUpperCase() + (proc.presets.get (i).factory ? "" : u8 (" · USER")),
                        getLocalBounds().withTrimmedLeft (40).withTrimmedRight (100).removeFromRight (150).withTrimmedRight (4), juce::Justification::centredRight, false);
        }
    }

private:
    void step (int d) { const int n = proc.presets.size(); if (n > 0) proc.loadPreset (((std::max (0, proc.getCurrentPresetIndex()) + d) % n + n) % n); }

    void showMenu()
    {
        juce::PopupMenu menu; std::vector<juce::String> cats;
        for (int i = 0; i < proc.presets.size(); ++i)
        {
            const auto& e = proc.presets.get (i); const juce::String c = e.factory ? juce::String (e.preset.category) : juce::String ("User");
            if (std::find (cats.begin(), cats.end(), c) == cats.end()) cats.push_back (c);
        }
        for (auto& c : cats)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < proc.presets.size(); ++i)
            {
                const auto& e = proc.presets.get (i);
                if ((e.factory ? juce::String (e.preset.category) : juce::String ("User")) == c) sub.addItem (i + 1, juce::String (e.preset.name), true, i == proc.getCurrentPresetIndex());
            }
            menu.addSubMenu (c, sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameBtn), [this] (int r) { if (r > 0) proc.loadPreset (r - 1); });
    }

    void promptSave()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Saved to your user preset folder.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.getCurrentPresetName(), "Name");
        w->addComboBox ("cat", { "Resonate", "Glitch", "Phantom", "Vocal", "Mutant", "User" }, "Category");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1 && w->getTextEditorContents ("name").trim().isNotEmpty())
                proc.saveUserPreset (w->getTextEditorContents ("name").trim(), w->getComboBoxComponent ("cat")->getText());
        }), true);
    }

    MutationEngineProcessor& proc;
    juce::TextButton prev { "<" }, next { ">" }, nameBtn { "Init" }, save { "SAVE" };
};

// ------------------------------------------------------------ modulation panel
class ModulationPanel : public juce::Component
{
public:
    explicit ModulationPanel (MutationEngineProcessor& p) : proc (p)
    {
        for (int k = 0; k < kNumModSlots; ++k)
        {
            src[k].addItemList (choicesOf (modSlotParam (k, 0)), 1);
            dst[k].addItemList (choicesOf (modSlotParam (k, 1)), 1);
            srcAtt[k] = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, kParams[modSlotParam (k, 0)].id, src[k]);
            dstAtt[k] = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, kParams[modSlotParam (k, 1)].id, dst[k]);
            amt[k].setSliderStyle (juce::Slider::LinearHorizontal); amt[k].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            amt[k].setColour (juce::Slider::trackColourId, cyan);
            amtAtt[k] = std::make_unique<APVTS::SliderAttachment> (p.apvts, kParams[modSlotParam (k, 2)].id, amt[k]);
            addAndMakeVisible (src[k]); addAndMakeVisible (dst[k]); addAndMakeVisible (amt[k]);
        }
        const int rateIdx[3] = { pid::Lfo1Rate, pid::Lfo2Rate, pid::RndRate };
        for (int i = 0; i < 3; ++i)
        {
            rate[i].setSliderStyle (juce::Slider::LinearHorizontal); rate[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
            rate[i].setColour (juce::Slider::trackColourId, violet); rate[i].setColour (juce::Slider::textBoxTextColourId, text);
            rate[i].setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            rateAtt[i] = std::make_unique<APVTS::SliderAttachment> (p.apvts, kParams[rateIdx[i]].id, rate[i]);
            rate[i].textFromValueFunction = [] (double v) { return juce::String (v, 2) + " Hz"; };
            rate[i].updateText();
            addAndMakeVisible (rate[i]);
        }
        for (int i = 0; i < 2; ++i)
        {
            shape[i].addItemList (choicesOf (i == 0 ? pid::Lfo1Shape : pid::Lfo2Shape), 1);
            shapeAtt[i] = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, kParams[i == 0 ? pid::Lfo1Shape : pid::Lfo2Shape].id, shape[i]);
            addAndMakeVisible (shape[i]);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 0);
        for (int k = 0; k < kNumModSlots; ++k)
        {
            auto row = juce::Rectangle<int> (r.getX(), 42 + k * 30, r.getWidth(), 26);
            src[k].setBounds (row.removeFromLeft (76)); row.removeFromLeft (4);
            dst[k].setBounds (row.removeFromLeft (96)); row.removeFromLeft (4);
            amt[k].setBounds (row.removeFromLeft (88)); meterX = row.getX() + 6;
        }
        for (int i = 0; i < 3; ++i)
        {
            auto row = juce::Rectangle<int> (r.getX() + 46, 168 + i * 26, r.getWidth() - 46, 24);
            if (i < 2) { shape[i].setBounds (row.removeFromRight (84).reduced (0, 1)); row.removeFromRight (4); }
            rate[i].setBounds (row);
        }
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat());
        drawHeading (g, getLocalBounds().reduced (14, 10).removeFromTop (16), "MODULATION");
        for (int k = 0; k < kNumModSlots; ++k)
        {
            const int s = (int) rawParam (proc, modSlotParam (k, 0));
            const float v = s > 0 ? proc.modSource[s].load() : 0.0f, a = rawParam (proc, modSlotParam (k, 2));
            const auto m = juce::Rectangle<float> ((float) meterX, (float) (49 + k * 30), 24.0f, 12.0f);
            g.setColour (juce::Colour (0xff12162a)); g.fillRoundedRectangle (m, 3.0f);
            const float mid = m.getCentreX(), len = juce::jlimit (-1.0f, 1.0f, v * a) * m.getWidth() * 0.5f;
            g.setColour (s > 0 ? (a >= 0 ? cyan : magenta) : line); g.fillRect (std::min (mid, mid + len), m.getY() + 2.0f, std::max (1.0f, std::fabs (len)), m.getHeight() - 4.0f);
        }
        g.setColour (dim); g.setFont (font (9.0f, true, 0.18f));
        const char* names[3] = { "LFO 1", "LFO 2", "RAND" };
        for (int i = 0; i < 3; ++i) g.drawText (names[i], 14, 168 + i * 26, 44, 24, juce::Justification::centredLeft, false);
    }

private:
    MutationEngineProcessor& proc; int meterX = 0;
    juce::ComboBox src[kNumModSlots], dst[kNumModSlots], shape[2];
    juce::Slider amt[kNumModSlots], rate[3];
    std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt[kNumModSlots], dstAtt[kNumModSlots], shapeAtt[2];
    std::unique_ptr<APVTS::SliderAttachment> amtAtt[kNumModSlots], rateAtt[3];
};
} // namespace me::ui

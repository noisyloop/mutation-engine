#pragma once
#include "Theme.h"

namespace me::ui
{
class MutationLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MutationLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff090b16));
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0b0e1c));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff090b16));
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff0d1020));
        setColour (juce::AlertWindow::textColourId, text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startA, float endA, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = std::min (b.getWidth(), b.getHeight()), r = size * 0.5f;
        const auto c = b.getCentre();
        const float trackR = r - 3.0f;
        const float angle = startA + pos * (endA - startA);

        // body
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3156), c.x - r * 0.4f, c.y - r * 0.5f, juce::Colour (0xff0a0c18), c.x + r * 0.5f, c.y + r * 0.6f, true));
        g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
        g.setColour (juce::Colour (0xff262d52));
        g.drawEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f, 1.0f);

        // track + value arc
        juce::Path track; track.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, startA, endA, true);
        g.setColour (juce::Colour (0xff171c33));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        if (pos > 0.002f)
        {
            juce::Path val; val.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, startA, angle, true);
            juce::ColourGradient grad (accent, c.x - r, c.y + r, magenta, c.x + r, c.y - r, false);
            auto glow = grad; glow.multiplyOpacity (0.28f);
            g.setGradientFill (glow);
            g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (grad);
            g.strokePath (val, juce::PathStrokeType (3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // pointer
        const float pr = r * 0.52f;
        g.setColour (juce::Colours::white);
        g.drawLine (c.x, c.y, c.x + pr * std::sin (angle), c.y - pr * std::cos (angle), 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float cy = (float) y + (float) h * 0.5f, railH = 6.0f;
        const juce::Rectangle<float> rail ((float) x + 6.0f, cy - railH * 0.5f, (float) w - 12.0f, railH);
        g.setColour (juce::Colour (0xff12162a)); g.fillRoundedRectangle (rail, 3.0f);
        auto fill = rail.withRight (pos);
        const auto a = s.findColour (juce::Slider::trackColourId);
        g.setGradientFill (juce::ColourGradient (a, rail.getX(), 0, magenta, rail.getRight(), 0, false));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillEllipse (pos - 11.0f, cy - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colours::white);                  g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (juce::Colour (0xff090b16)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (box.hasKeyboardFocus (false) ? cyan : line); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        juce::Path arrow; const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (dim); g.fillPath (arrow);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (11.0f, false, 0.05f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override { l.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2); l.setFont (getComboBoxFont (box)); }
    juce::Font getPopupMenuFont() override { return font (12.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (juce::Colour (0xff090b16)); g.fillRoundedRectangle (r, 9.0f);
        if (over || down) { g.setColour (cyan.withAlpha (down ? 0.16f : 0.08f)); g.fillRoundedRectangle (r, 9.0f); }
        g.setColour (on ? cyan : line); g.drawRoundedRectangle (r, 9.0f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (font (10.5f, true, 0.12f));
        g.setColour (b.isEnabled() ? (b.getToggleState() ? cyan : text) : dim.withAlpha (0.5f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? amber.withAlpha (0.18f) : juce::Colour (0xff090b16)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? amber : (over ? dim : line)); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setFont (font (9.5f, true, 0.12f));
        g.setColour (on ? amber : dim);
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};
} // namespace me::ui

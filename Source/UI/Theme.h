#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../ParamTable.h"

namespace me::ui
{
inline const juce::Colour bg     { 0xff05060c }, panelTop { 0xff0d1020 }, panelBot { 0xff090b16 }, line { 0xff1b2036 },
                          text   { 0xffe8ecff }, dim      { 0xff6d7599 }, cyan     { 0xff00e5ff }, magenta { 0xffff2e93 },
                          violet { 0xff8a5cff }, amber    { 0xffffb02e }, ok       { 0xff34ffb0 };


// JUCE treats plain string literals as ASCII; anything with non-ASCII glyphs must go through here.
inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

inline juce::Font font (float height, bool bold = false, float kerning = 0.0f)
{
    juce::Font f (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    f.setExtraKerningFactor (kerning);
    return f;
}

inline juce::Font mono (float height)
{
    juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    return f;
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.0f)
{
    g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// Small caps heading with letter spacing, optionally with a right-aligned accent value.
inline void drawHeading (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& left, const juce::String& right = {})
{
    g.setFont (font (10.0f, true, 0.26f));
    g.setColour (dim);
    g.drawText (left, r, juce::Justification::centredLeft, false);
    if (right.isNotEmpty()) { g.setColour (cyan); g.drawText (right, r, juce::Justification::centredRight, false); }
}

inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width = 1.6f)
{
    g.setColour (c.withAlpha (0.18f)); g.strokePath (p, juce::PathStrokeType (width * 3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.35f)); g.strokePath (p, juce::PathStrokeType (width * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Human-readable parameter value for knob read-outs.
inline juce::String formatValue (int idx, double v)
{
    const auto& p = me::kParams[idx];
    const juce::String label (p.label);
    if (label == "Hz") return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (label == "dB") return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
    if (p.kind == me::Kind::Int) return (idx == me::pid::SpShift && v > 0 ? "+" : "") + juce::String (juce::roundToInt (v));
    if (std::fabs (p.min) < 1e-6f && std::fabs (p.max - 1.0f) < 1e-6f) return juce::String (juce::roundToInt (v * 100.0)) + "%";
    if (idx == me::pid::FormShift) return juce::String (v, 2) + "x";
    if (idx == me::pid::Vowel) { static const char* n[] = { "A", "E", "I", "O", "U" }; const int i = juce::jlimit (0, 3, (int) v); const double t = v - i;
                                 return t < 0.02 ? juce::String (n[i]) : t > 0.98 ? juce::String (n[i + 1]) : juce::String (n[i]) + "-" + n[i + 1]; }
    return juce::String (v, 2);
}

inline float logPos (float hz) { return std::log (std::fmax (hz, 20.0f) / 20.0f) / std::log (1000.0f); }   // 20 Hz..20 kHz -> 0..1
} // namespace me::ui

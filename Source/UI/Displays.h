#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include <juce_dsp/juce_dsp.h>

namespace me::ui
{
inline float rawParam (MutationEngineProcessor& p, int idx) { return p.apvts.getRawParameterValue (kParams[idx].id)->load(); }

// Sets a parameter from a UI drag (grouped into one host gesture).
struct ParamDrag
{
    juce::RangedAudioParameter* param = nullptr; int index = -1;
    void begin (MutationEngineProcessor& p, int idx) { index = idx; param = p.apvts.getParameter (kParams[idx].id); if (param) param->beginChangeGesture(); }
    void setActual (float v) { if (param) param->setValueNotifyingHost (param->convertTo0to1 (v)); }
    void setNormalised (float n) { if (param) param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n)); }
    void end() { if (param) param->endChangeGesture(); param = nullptr; }
};

// ------------------------------------------------------------------ spectrogram
class SpectrogramDisplay : public juce::Component
{
public:
    explicit SpectrogramDisplay (MutationEngineProcessor& p) : proc (p), fft (12), fftData (2 * kN, 0.0f), window (kN)
    {
        for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kN);
        struct Stop { float t; int r, g, b; };
        const Stop st[] = { { 0.0f, 4, 5, 14 }, { 0.18f, 40, 12, 90 }, { 0.4f, 150, 25, 150 }, { 0.62f, 255, 46, 120 }, { 0.82f, 255, 170, 60 }, { 1.0f, 255, 250, 230 } };
        for (int i = 0; i < 256; ++i)
        {
            const float t = (float) i / 255.0f; int k = 1; while (k < 5 && t > st[k].t) ++k;
            const float u = (t - st[k - 1].t) / (st[k].t - st[k - 1].t);
            lut[i] = juce::Colour ((juce::uint8) (st[k - 1].r + (st[k].r - st[k - 1].r) * u), (juce::uint8) (st[k - 1].g + (st[k].g - st[k - 1].g) * u),
                                   (juce::uint8) (st[k - 1].b + (st[k].b - st[k - 1].b) * u));
        }
        setOpaque (false);
    }

    void resized() override
    {
        const int w = std::max (1, getWidth()), h = std::max (1, getHeight());
        image = juce::Image (juce::Image::ARGB, w, h, true);
        { juce::Graphics g (image); g.fillAll (lut[0]); }
        lineLevel.assign ((size_t) w, 0.0f); levels.assign ((size_t) w, 0.0f);
    }

    void tick()
    {
        const int w = image.getWidth(), h = image.getHeight(); if (w < 2) return;
        const int pos = proc.scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < kN; ++i) fftData[(size_t) i] = proc.scope[(pos + i) % kN].load (std::memory_order_relaxed) * window[(size_t) i];
        std::fill (fftData.begin() + kN, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const float binHz = (float) proc.getSampleRate() / (float) kN;
        for (int x = 0; x < w; ++x)
        {
            const float f0 = 20.0f * std::pow (1000.0f, (float) x / (float) w), f1 = 20.0f * std::pow (1000.0f, (float) (x + 1) / (float) w);
            const float lo = f0 / binHz, hi = f1 / binHz;
            float mag;
            if (hi - lo < 1.0f) { const float c = 0.5f * (lo + hi); const int i = (int) c; const float t = c - (float) i; mag = fftData[(size_t) i] * (1 - t) + fftData[(size_t) std::min (i + 1, kN / 2)] * t; }
            else { mag = 0.0f; for (int i = (int) lo; i <= std::min ((int) hi, kN / 2); ++i) mag = std::max (mag, fftData[(size_t) i]); }
            const float db = 20.0f * std::log10 (mag * 4.0f / (float) kN + 1.0e-7f);
            const float lvl = juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 84.0f);
            levels[(size_t) x] = lvl;
            lineLevel[(size_t) x] = std::max (lvl, lineLevel[(size_t) x] * 0.86f);
        }

        const int step = 3;
        image.moveImageSection (0, 0, 0, step, w, h - step);
        juce::Image::BitmapData bd (image, 0, h - step, w, step, juce::Image::BitmapData::writeOnly);
        for (int x = 0; x < w; ++x)
        {
            const auto c = lut[(int) (std::pow (levels[(size_t) x], 0.85f) * 255.0f)];
            for (int y = 0; y < step; ++y) bd.setPixelColour (x, y, c);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth(), h = b.getHeight();
        drawPanel (g, b);
        juce::Path clip; clip.addRoundedRectangle (b.reduced (1.0f), 13.0f);
        g.saveState(); g.reduceClipRegion (clip);
        g.drawImageAt (image, 0, 0);
        g.setColour (bg.withAlpha (0.62f)); g.fillRect (0.0f, 0.0f, w, 34.0f);

        // grid
        g.setFont (mono (9.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = logPos (f) * w;
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawVerticalLine ((int) x, 34.0f, h);
            g.setColour (juce::Colour (0x88c8d2ff)); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x + 3, (int) h - 16, 40, 12, juce::Justification::left, false);
        }

        // live spectrum
        const float base = h - 22.0f, span = (h - 70.0f) * 0.62f;
        juce::Path fill, curve;
        for (int x = 0; x < (int) w; x += 2)
        {
            const float y = base - lineLevel[(size_t) std::min (x, (int) lineLevel.size() - 1)] * span;
            if (x < 0.5f) { fill.startNewSubPath (0.0f, base); fill.lineTo (0.0f, y); curve.startNewSubPath (0.0f, y); } else { fill.lineTo ((float) x, y); curve.lineTo ((float) x, y); }
        }
        fill.lineTo (w, base); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.5f), 0.0f, base - span, cyan.withAlpha (0.0f), 0.0f, base, false)); g.fillPath (fill);
        strokeGlow (g, curve, juce::Colour (0xff7ff3ff), 1.5f);

        // resonance markers (where the 32-voice bank actually sits)
        const double sr = proc.getSampleRate();
        const int voices = (int) rawParam (proc, pid::ResVoices);
        const double root = proc.effRootHz.load(), B = rawParam (proc, pid::ResInharm) * 0.02;
        for (int i = 0; i < voices; ++i)
        {
            const double n = i + 1, f = root * n * std::sqrt (1.0 + B * n * n);
            if (f >= sr * 0.45 || f > 20000.0) break;
            const float x = logPos ((float) f) * w;
            g.setColour (magenta.withAlpha (0.12f)); g.fillRect (x - 0.5f, 40.0f, 1.0f, h - 66.0f);
            juce::Path tri; tri.addTriangle (x - 3.5f, 36.0f, x + 3.5f, 36.0f, x, 43.0f);
            g.setColour (magenta.withAlpha (0.35f)); g.fillPath (tri.createPathWithRoundedCorners (1.0f));
            g.setColour (magenta); g.fillPath (tri);
        }

        // formant peaks
        if (rawParam (proc, pid::FormMix) > 0.02f)
        {
            const auto v = FormantEngine::vowelAt (rawParam (proc, pid::Vowel));
            const float shift = rawParam (proc, pid::FormShift);
            g.setFont (mono (9.0f));
            for (int k = 0; k < 3; ++k)
            {
                const float x = logPos (v.f[k] * shift) * w;
                const float dashes[] = { 3.0f, 4.0f };
                g.setColour (violet.withAlpha (0.9f)); g.drawDashedLine (juce::Line<float> (x, 50.0f, x, h - 26.0f), dashes, 2, 1.0f);
                g.setColour (juce::Colour (0xffb9a2ff)); g.drawText ("F" + juce::String (k + 1), (int) x + 4, 48, 24, 12, juce::Justification::left, false);
            }
        }
        g.restoreState();

        g.setColour (dim); g.setFont (font (10.0f, true, 0.2f));
        g.drawText ("LIVE SPECTROGRAM", 16, 10, 220, 14, juce::Justification::left, false);
        g.setColour (cyan); g.setFont (mono (10.0f)); g.drawText ("20 Hz - 20 kHz", 160, 10, 140, 14, juce::Justification::left, false);
        g.setFont (mono (10.0f));
        g.setColour (cyan);    g.drawText (u8 ("● OUTPUT"), (int) w - 300, 10, 100, 14, juce::Justification::right, false);
        g.setColour (magenta); g.drawText (u8 ("● RESONANCES ") + juce::String (voices), (int) w - 190, 10, 174, 14, juce::Justification::right, false);
    }

private:
    static constexpr int kN = MutationEngineProcessor::kScopeSize;
    MutationEngineProcessor& proc;
    juce::dsp::FFT fft;
    std::vector<float> fftData, window, lineLevel, levels;
    juce::Colour lut[256];
    juce::Image image;
};

// ------------------------------------------------------------------------ scope
class ScopeDisplay : public juce::Component
{
public:
    explicit ScopeDisplay (MutationEngineProcessor& p) : proc (p) {}
    void tick() { repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        drawPanel (g, b);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) b.getCentreY(), 14.0f, b.getRight() - 14.0f);
        constexpr int N = MutationEngineProcessor::kScopeSize, shown = 2048;
        const int start = proc.scopePos.load (std::memory_order_relaxed) + (N - shown);
        juce::Path p; const float x0 = 14.0f, ww = b.getWidth() - 28.0f;
        for (int i = 0; i < shown; i += 2)
        {
            const float s = juce::jlimit (-1.0f, 1.0f, proc.scope[(start + i) % N].load (std::memory_order_relaxed));
            const float x = x0 + ww * (float) i / (float) shown, y = b.getCentreY() - s * b.getHeight() * 0.42f;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        strokeGlow (g, p, cyan, 1.5f);
        g.setColour (dim); g.setFont (font (10.0f, true, 0.2f)); g.drawText ("OUTPUT", 16, 8, 120, 14, juce::Justification::left, false);
    }
private:
    MutationEngineProcessor& proc;
};

// ----------------------------------------------------------- 32-voice resonance bars
class ResonanceBars : public juce::Component
{
public:
    explicit ResonanceBars (MutationEngineProcessor& p) : proc (p) {}
    void tick() { repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const int voices = (int) rawParam (proc, pid::ResVoices);
        const float ring = 1.0f - 0.65f * rawParam (proc, pid::ResDamp);
        const double sr = proc.getSampleRate(), root = proc.effRootHz.load(), B = rawParam (proc, pid::ResInharm) * 0.02;
        const float bw = b.getWidth() / (float) ResonatorBank::kMaxVoices;
        for (int i = 0; i < ResonatorBank::kMaxVoices; ++i)
        {
            const double n = i + 1, f = root * n * std::sqrt (1.0 + B * n * n);
            const bool live = i < voices && f < sr * 0.45;
            const float gain = (float) (1.0 / std::pow (n, 0.35));
            const float bh = live ? std::max (3.0f, b.getHeight() * gain * (0.35f + 0.65f * ring)) : 2.0f;
            const juce::Rectangle<float> r (b.getX() + (float) i * bw + 1.5f, b.getBottom() - bh, bw - 3.0f, bh);
            if (live)
            {
                g.setGradientFill (juce::ColourGradient ((i & 1) ? magenta : violet, 0, r.getY(), cyan, 0, r.getBottom(), false));
                g.fillRoundedRectangle (r.expanded (1.0f, 0.0f), 2.0f);
            }
            else { g.setColour (juce::Colour (0xff171c33)); g.fillRoundedRectangle (r, 1.0f); }
        }
    }
private:
    MutationEngineProcessor& proc;
};

// ---------------------------------------------------------------- vowel chart
class VowelDisplay : public juce::Component
{
public:
    explicit VowelDisplay (MutationEngineProcessor& p) : proc (p) {}
    void tick() { repaint(); }

    juce::Point<float> pos (float f1, float f2) const
    {
        const auto b = getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (22.0f);
        const float x = b.getRight() - (juce::jlimit (500.0f, 2300.0f, f2) - 500.0f) / (2300.0f - 500.0f) * b.getWidth();
        const float y = b.getY() + (juce::jlimit (250.0f, 900.0f, f1) - 250.0f) / (900.0f - 250.0f) * b.getHeight();
        return { x, y };
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        drawPanel (g, b);
        drawHeading (g, getLocalBounds().reduced (14, 10).removeFromTop (16), "VOWEL SHAPE", vowelName (rawParam (proc, pid::Vowel)));
        const auto area = b.reduced (14.0f, 10.0f).withTrimmedTop (22.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (int i = 1; i < 5; ++i) { g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 5.0f), area.getY(), area.getBottom()); g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 5.0f), area.getX(), area.getRight()); }

        const float shift = rawParam (proc, pid::FormShift);
        juce::Path path;
        for (float t = 0.0f; t <= 4.001f; t += 0.05f) { const auto v = FormantEngine::vowelAt (t); const auto p = pos (v.f[0] * shift, v.f[1] * shift); if (t < 0.001f) path.startNewSubPath (p); else path.lineTo (p); }
        strokeGlow (g, path, violet, 1.8f);
        static const char* names[] = { "A", "E", "I", "O", "U" };
        for (int i = 0; i < 5; ++i)
        {
            const auto v = FormantEngine::vowelAt ((float) i); const auto p = pos (v.f[0] * shift, v.f[1] * shift);
            g.setColour (juce::Colour (0xff262d52)); g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
            g.setColour (juce::Colour (0xff9aa3cc)); g.setFont (font (11.0f, true)); g.drawText (names[i], (int) p.x + 8, (int) p.y - 8, 14, 16, juce::Justification::left, false);
        }
        const auto cur = FormantEngine::vowelAt (rawParam (proc, pid::Vowel)); const auto c = pos (cur.f[0] * shift, cur.f[1] * shift);
        const bool live = rawParam (proc, pid::FormMix) > 0.02f;
        g.setGradientFill (juce::ColourGradient (magenta.withAlpha (live ? 0.55f : 0.2f), c.x, c.y, magenta.withAlpha (0.0f), c.x + 36, c.y, true)); g.fillEllipse (c.x - 36, c.y - 36, 72, 72);
        g.setColour (juce::Colours::white.withAlpha (live ? 1.0f : 0.5f)); g.fillEllipse (c.x - 7, c.y - 7, 14, 14);
        g.setColour (dim); g.setFont (mono (9.0f));
        g.drawText ("F2", getLocalBounds().removeFromBottom (14).removeFromRight (60), juce::Justification::right, false);
        g.drawText ("F1", getLocalBounds().withTrimmedTop (30).removeFromTop (12).removeFromLeft (50).translated (4, 0), juce::Justification::left, false);
    }

    void mouseDown (const juce::MouseEvent& e) override { drag.begin (proc, pid::Vowel); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float shift = rawParam (proc, pid::FormShift); float best = 1e9f, bestT = 0.0f;
        for (float t = 0.0f; t <= 4.001f; t += 0.02f)
        {
            const auto v = FormantEngine::vowelAt (t); const auto p = pos (v.f[0] * shift, v.f[1] * shift);
            const float d = p.getDistanceSquaredFrom (e.position); if (d < best) { best = d; bestT = t; }
        }
        drag.setActual (bestT);
    }
    void mouseUp (const juce::MouseEvent&) override { drag.end(); }

private:
    static juce::String vowelName (float v)
    {
        static const char* n[] = { "A", "E", "I", "O", "U" };
        const int i = juce::jlimit (0, 3, (int) v); const float t = v - (float) i;
        if (t < 0.02f) return n[i];
        if (t > 0.98f) return n[i + 1];
        return juce::String (n[i]) + u8 (" > ") + n[i + 1] + u8 (" · ") + juce::String (juce::roundToInt (t * 100.0f)) + "%";
    }
    MutationEngineProcessor& proc; ParamDrag drag;
};

// ----------------------------------------------------------- filter response
class FilterDisplay : public juce::Component
{
public:
    explicit FilterDisplay (MutationEngineProcessor& p) : proc (p) {}
    void tick() { repaint(); }

    static float magnitude (int type, float x, float q)
    {
        const float re = 1.0f - x * x, im = x / q, den = std::sqrt (re * re + im * im) + 1.0e-9f;
        switch (type) { case 0: return 1.0f / den; case 1: return x * x / den; case 2: return (x / q) / den; default: return std::fabs (re) / den; }
    }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (22.0f); }
    float dbToY (float db) const { const auto a = plot(); return a.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 60.0f) * a.getHeight(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat(); drawPanel (g, b);
        static const char* types[] = { "LOW-PASS", "HIGH-PASS", "BAND-PASS", "NOTCH" };
        const int type = juce::jlimit (0, 3, (int) rawParam (proc, pid::FiltType));
        const float fc = rawParam (proc, pid::FiltCutoff), q = rawParam (proc, pid::FiltRes);
        drawHeading (g, getLocalBounds().reduced (14, 10).removeFromTop (16), "FILTER", juce::String (types[type]) + u8 (" · ") + (fc >= 1000 ? juce::String (fc / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (fc)) + " Hz"));
        const auto a = plot();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (float db : { -24.0f, -12.0f, 0.0f, 12.0f }) g.drawHorizontalLine ((int) dbToY (db), a.getX(), a.getRight());

        juce::Path curve, fill;
        for (float x = 0; x <= a.getWidth(); x += 2.0f)
        {
            const float f = 20.0f * std::pow (1000.0f, x / a.getWidth());
            const float db = 20.0f * std::log10 (magnitude (type, f / fc, q) + 1.0e-6f);
            const float y = dbToY (db);
            if (x < 0.5f) { curve.startNewSubPath (a.getX(), y); fill.startNewSubPath (a.getX(), a.getBottom()); fill.lineTo (a.getX(), y); } else { curve.lineTo (a.getX() + x, y); fill.lineTo (a.getX() + x, y); }
        }
        fill.lineTo (a.getRight(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.38f), 0, a.getY(), cyan.withAlpha (0.0f), 0, a.getBottom(), false)); g.fillPath (fill);
        strokeGlow (g, curve, juce::Colour (0xff7ff3ff), 1.8f);

        const float cx = a.getX() + logPos (fc) * a.getWidth(), cy = dbToY (20.0f * std::log10 (magnitude (type, 1.0f, q) + 1.0e-6f));
        g.setColour (cyan.withAlpha (0.3f)); g.fillEllipse (cx - 10, cy - 10, 20, 20);
        g.setColour (juce::Colours::white); g.fillEllipse (cx - 5, cy - 5, 10, 10);
    }

    void mouseDown (const juce::MouseEvent& e) override { dragF.begin (proc, pid::FiltCutoff); dragQ.begin (proc, pid::FiltRes); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto a = plot();
        const float xn = juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
        dragF.setActual (20.0f * std::pow (1000.0f, xn));
        dragQ.setNormalised (1.0f - (e.position.y - a.getY()) / a.getHeight());
    }
    void mouseUp (const juce::MouseEvent&) override { dragF.end(); dragQ.end(); }

private:
    MutationEngineProcessor& proc; ParamDrag dragF, dragQ;
};
} // namespace me::ui

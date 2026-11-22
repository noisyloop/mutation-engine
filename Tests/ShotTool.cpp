// Renders the real editor to a PNG (feeds a synthetic drum loop so the displays are live).
//   MutationEngineShot out.png ["Preset Name"] [mode 0-2] [mutate-count]
#include "PluginEditor.h"
#include <cmath>

static float drumSample (long n, double sr, juce::Random& rnd)
{
    const double step = sr * 0.125;                    // 16th at 120 BPM
    const long idx = (long) (n / step), pos = n - (long) (idx * step);
    const int s = (int) (idx % 16);
    const double t = (double) (n % (long) (step * 16)) / sr;
    float out = 0.0f;
    auto since = [&] (int startStep) { return (double) (n % (long) (step * 16) - (long) (startStep * step)) / sr; };
    for (int k : { 0, 6, 8, 11 }) { const double d = since (k); if (d >= 0 && d < 0.3) out += (float) (std::sin (2 * M_PI * (45.0 * d + 105.0 * (1 - std::exp (-d * 30)) / 30)) * std::exp (-d * 14) * 0.8); }
    for (int k : { 4, 12 })       { const double d = since (k); if (d >= 0 && d < 0.25) out += (float) ((rnd.nextFloat() * 2 - 1) * std::exp (-d * 22) * 0.45 + std::sin (2 * M_PI * 190 * d) * std::exp (-d * 30) * 0.3); }
    if (s % 2 == 0) { const double d = (double) pos / sr; if (d < 0.05) out += (float) ((rnd.nextFloat() * 2 - 1) * std::exp (-d * 90) * 0.18); }
    (void) t;
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : "shot.png");
    const juce::String presetName = argc > 2 ? argv[2] : "Glass Throat";
    const int mode = argc > 3 ? std::atoi (argv[3]) : -1, mutations = argc > 4 ? std::atoi (argv[4]) : 0;

    MutationEngineProcessor p;
    p.presets.setUserDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("me_shot_user_presets"));
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.loadPreset (std::max (0, p.presets.findByName (presetName)));
    if (mode >= 0) { auto* m = p.apvts.getParameter (me::ids::mode); m->setValueNotifyingHost (m->convertTo0to1 ((float) mode)); }
    p.apvts.getParameter (me::ids::mutIntensity)->setValueNotifyingHost (0.6f);
    for (int i = 0; i < mutations; ++i) p.mutate();

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1600, 1000);

    juce::Random rnd (3); long n = 0; juce::MidiBuffer midi;
    for (int frame = 0; frame < 420; ++frame)
    {
        for (int b = 0; b < 3; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i, ++n) { const float s = drumSample (n, 48000.0, rnd); buf.setSample (0, i, s); buf.setSample (1, i, s); }
            midi.clear(); p.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
    const auto img = static_cast<MutationEngineEditor*> (ed.get())->takeSnapshot (2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return ok ? 0 : 1;
}

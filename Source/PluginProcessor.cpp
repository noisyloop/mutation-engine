#include "PluginProcessor.h"
#include "PluginEditor.h"

MutationEngineProcessor::MutationEngineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", me::createParameterLayout())
{
}

bool MutationEngineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MutationEngineProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    resonator.prepare (sr);
    filter.prepare (sr);
    formant.prepare (sr);
    heldNote = -1; midiRootHz = 0.0f;
}

void MutationEngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // MIDI pitch control: last-note priority, resonator root follows the note.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       { heldNote = m.getNoteNumber(); midiRootHz = (float) juce::MidiMessage::getMidiNoteInHertz (heldNote); }
        else if (m.isNoteOff() && m.getNoteNumber() == heldNote) { /* keep last pitch: resonance rings on */ }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { heldNote = -1; midiRootHz = 0.0f; }
    }

    const auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    me::ResonatorBank::Params rp;
    rp.rootHz     = (get (me::ids::midiTrack) > 0.5f && midiRootHz > 0.0f) ? midiRootHz : get (me::ids::resRoot);
    rp.voices     = (int) get (me::ids::resVoices);
    rp.inharmonic = get (me::ids::resInharm);
    rp.damping    = get (me::ids::resDamp);
    rp.feedback   = get (me::ids::resFeed);
    rp.spread     = get (me::ids::resSpread);
    resonator.setParams (rp);
    filter.set ((me::FilterBank::Type) (int) get (me::ids::filtType), get (me::ids::filtCutoff), get (me::ids::filtRes));
    formant.set (get (me::ids::vowel), get (me::ids::formShift));

    const float mix     = get (me::ids::mix);
    const float formMix = get (me::ids::formMix);
    const float outGain = juce::Decibels::decibelsToGain (get (me::ids::output));

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float dl = L[i], dr = R[i];
        float wl, wr;
        resonator.process (dl, dr, wl, wr);

        float fl = wl, fr = wr;
        formant.process (fl, fr);
        wl += formMix * (fl - wl);
        wr += formMix * (fr - wr);

        filter.process (wl, wr);

        // Safety stage: soft-clip wet path, sanitise, then hard-limit the sum.
        wl = me::softClip (me::sanitize (wl));
        wr = me::softClip (me::sanitize (wr));
        const float ol = me::sanitize ((dl + mix * (wl - dl)) * outGain, 1.0f);
        const float or_ = me::sanitize ((dr + mix * (wr - dr)) * outGain, 1.0f);
        L[i] = ol; R[i] = or_;

        const int p = scopePos.load (std::memory_order_relaxed);
        scope[p].store (0.5f * (ol + or_), std::memory_order_relaxed);
        scopePos.store ((p + 1) % kScopeSize, std::memory_order_relaxed);
    }
}

void MutationEngineProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MutationEngineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MutationEngineProcessor::createEditor() { return new MutationEngineEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MutationEngineProcessor(); }

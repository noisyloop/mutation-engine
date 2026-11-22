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
    glitch.prepare (sr);
    spectral->reset (1);
    for (auto& d : dryDelay) d.assign ((size_t) me::SpectralEngine::latencySamples(), 0.0f);
    dryPos = 0; lastSeed = -1; internalPpq = 0.0;
    setLatencySamples (me::SpectralEngine::latencySamples());   // constant in every mode
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

    const int mode = (int) get (me::ids::mode);   // 0 Resonate, 1 Glitch, 2 Phantom

    const int seedNow = (int) get (me::ids::seed);
    if (seedNow != lastSeed) { lastSeed = seedNow; glitch.reseed ((uint32_t) seedNow * 2654435761u + 1u); spectral->reseed ((uint32_t) seedNow * 40503u + 7u); }

    // Tempo: follow the host when it provides one, otherwise free-run at 120 BPM.
    double bpm = 120.0, ppq = internalPpq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    internalPpq = ppq + buffer.getNumSamples() * bpm / (60.0 * sampleRate);

    me::SpectralEngine::Params sp;
    if (mode == 1) { sp.binShift = (int) get (me::ids::spShift); sp.dropout = get (me::ids::spDrop); sp.scramble = get (me::ids::spScram); }
    if (mode == 2) { sp.freeze = get (me::ids::spFreeze) > 0.5f; sp.drift = get (me::ids::spDrift); sp.binShift = (int) get (me::ids::spShift); }
    spectral->setParams (sp);

    me::GlitchEngine::Params gp;
    static const int stepsTable[] = { 1, 2, 4, 8 };
    gp.bpm = (float) bpm; gp.stepsPerBeat = stepsTable[std::max (0, std::min (3, (int) get (me::ids::glDiv)))];
    gp.probability = mode == 1 ? get (me::ids::glProb) : 0.0f;
    gp.intensity = get (me::ids::glAmt);
    glitch.setParams (gp);
    glitch.setPosition (ppq);

    const float mix     = get (me::ids::mix);
    const float formMix = get (me::ids::formMix);
    const float outGain = juce::Decibels::decibelsToGain (get (me::ids::output));

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float dl = L[i], dr = R[i];
        float wl = dl, wr = dr;
        if (mode == 0) resonator.process (dl, dr, wl, wr);
        else if (mode == 1) glitch.process (wl, wr);
        spectral->process (wl, wr, wl, wr);   // always in path -> constant latency

        // Dry path delayed by the same latency so the mix stays phase-aligned.
        const size_t dp = (size_t) dryPos;
        const float dDl = dryDelay[0][dp], dDr = dryDelay[1][dp];
        dryDelay[0][dp] = dl; dryDelay[1][dp] = dr;
        dryPos = (dryPos + 1) % (int) dryDelay[0].size();

        float fl = wl, fr = wr;
        formant.process (fl, fr);
        wl += formMix * (fl - wl);
        wr += formMix * (fr - wr);

        filter.process (wl, wr);

        // Safety stage: soft-clip wet path, sanitise, then hard-limit the sum.
        wl = me::softClip (me::sanitize (wl));
        wr = me::softClip (me::sanitize (wr));
        const float ol = me::sanitize ((dDl + mix * (wl - dDl)) * outGain, 1.0f);
        const float or_ = me::sanitize ((dDr + mix * (wr - dDr)) * outGain, 1.0f);
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

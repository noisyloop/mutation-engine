#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Preset.h"

// Factory presets are compiled into the plugin (Presets/*.mepreset via
// BinaryData); user presets are .mepreset files in the user data directory.
class PresetManager
{
public:
    struct Entry { me::Preset preset; bool factory = true; juce::File file; };

    PresetManager();

    static juce::File defaultUserDirectory();
    void setUserDirectory (const juce::File&);
    juce::File getUserDirectory() const { return userDir; }

    void rescan();
    int size() const { return (int) entries.size(); }
    const Entry& get (int i) const { return entries[(size_t) i]; }
    int findByName (const juce::String& name) const;

    // Reads / writes every preset parameter of the live parameter tree.
    static me::Preset capture (juce::AudioProcessorValueTreeState&, const juce::String& name, const juce::String& category);
    static void apply (juce::AudioProcessorValueTreeState&, const me::Preset&);

    // Saves into the user directory (overwrites a user preset of the same name).
    bool saveUser (const me::Preset&);

private:
    juce::File userDir;
    std::vector<Entry> entries;
};

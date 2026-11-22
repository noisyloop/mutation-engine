#include "PresetManager.h"
#include <BinaryData.h>
#include <algorithm>

PresetManager::PresetManager() : userDir (defaultUserDirectory()) { rescan(); }

juce::File PresetManager::defaultUserDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("NoisyLoop").getChildFile ("MutationEngine").getChildFile ("Presets");
}

void PresetManager::setUserDirectory (const juce::File& d) { userDir = d; rescan(); }

void PresetManager::rescan()
{
    entries.clear();

    std::vector<std::pair<juce::String, Entry>> factory;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        if (data == nullptr) continue;
        Entry e; e.factory = true;
        if (me::Preset::fromText (std::string (data, (size_t) size), e.preset))
            factory.emplace_back (BinaryData::originalFilenames[i], std::move (e));
    }
    std::sort (factory.begin(), factory.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });   // 01_..., 02_...
    for (auto& f : factory) entries.push_back (std::move (f.second));

    if (userDir.isDirectory())
    {
        auto files = userDir.findChildFiles (juce::File::findFiles, false, "*.mepreset");
        files.sort();
        for (auto& f : files)
        {
            Entry e; e.factory = false; e.file = f;
            if (me::Preset::fromText (f.loadFileAsString().toStdString(), e.preset)) entries.push_back (std::move (e));
        }
    }
}

int PresetManager::findByName (const juce::String& name) const
{
    for (int i = 0; i < size(); ++i) if (juce::String (entries[(size_t) i].preset.name) == name) return i;
    return -1;
}

me::Preset PresetManager::capture (juce::AudioProcessorValueTreeState& apvts, const juce::String& name, const juce::String& category)
{
    me::Preset p; p.name = name.toStdString(); p.category = category.toStdString(); p.author = "User";
    for (int i = 0; i < me::kNumParams; ++i)
        if (me::Preset::isPresetParam (i))
            if (auto* prm = apvts.getParameter (me::kParams[i].id)) p.set (i, me::fromNorm (i, prm->getValue()));
    return p;
}

void PresetManager::apply (juce::AudioProcessorValueTreeState& apvts, const me::Preset& preset)
{
    for (int i = 0; i < me::kNumParams; ++i)
    {
        if (! me::Preset::isPresetParam (i)) continue;
        if (auto* prm = apvts.getParameter (me::kParams[i].id))
        {
            const float n = me::toNorm (i, preset.get (i));       // unmentioned parameters load at their default
            prm->beginChangeGesture(); prm->setValueNotifyingHost (n); prm->endChangeGesture();
        }
    }
}

bool PresetManager::saveUser (const me::Preset& p)
{
    if (! userDir.createDirectory().wasOk()) return false;
    auto safe = juce::String (p.name).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-").trim();
    if (safe.isEmpty()) safe = "Preset";
    const auto file = userDir.getChildFile (safe + ".mepreset");
    if (! file.replaceWithText (juce::String (p.toText()))) return false;
    rescan();
    return true;
}

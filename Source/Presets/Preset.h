#pragma once
#include "../ParamTable.h"
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace me
{
// A preset is a named list of *actual* parameter values (Hz, dB, 0..1 ...),
// stored as plain text so files are readable, diffable and hand-editable:
//
//     # Mutation Engine preset
//     name: Glass Throat
//     category: Resonate
//     author: NoisyLoop
//     res_root = 220
//     res_voices = 20
//
// Parameters a preset does not mention load at their default. Unknown ids are
// ignored (forward compatible). Parsing is locale independent.
struct Preset
{
    std::string name = "Init", category = "User", author, description;
    std::vector<std::pair<int, float>> values;   // (parameter index, actual value)

    // Parameters that belong in a preset: sound + modulation, but not
    // mutation controls (intensity, style, morph) or lock switches.
    static bool isPresetParam (int i) noexcept
    {
        return i >= 0 && i < kNumParams && ! (i >= pid::MutIntensity && i <= pid::LockGl);
    }

    bool has (int i) const { for (auto& v : values) if (v.first == i) return true; return false; }

    float get (int i) const
    {
        for (auto& v : values) if (v.first == i) return v.second;
        return kParams[i].def;
    }

    void set (int i, float v)
    {
        for (auto& e : values) if (e.first == i) { e.second = v; return; }
        values.emplace_back (i, v);
    }

    std::string toText() const
    {
        std::ostringstream o; o.imbue (std::locale::classic()); o.precision (7);
        o << "# Mutation Engine preset\nname: " << name << "\ncategory: " << category << "\nauthor: " << author << "\n";
        if (! description.empty()) o << "description: " << description << "\n";
        for (int i = 0; i < kNumParams; ++i)
            if (isPresetParam (i) && has (i)) o << kParams[i].id << " = " << get (i) << "\n";
        return o.str();
    }

    static bool fromText (const std::string& text, Preset& out)
    {
        Preset p; p.name.clear(); p.category = "User";
        std::istringstream in (text);
        std::string line;
        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r') line.pop_back();
            line = trim (line);
            if (line.empty() || line[0] == '#') continue;

            const auto eq = line.find (" = ");
            if (eq != std::string::npos)
            {
                const int idx = findParam (trim (line.substr (0, eq)).c_str());
                if (idx < 0 || ! isPresetParam (idx)) continue;       // unknown id: ignore
                std::istringstream vs (line.substr (eq + 3)); vs.imbue (std::locale::classic());
                double v = 0; if (! (vs >> v) || ! std::isfinite (v)) return false;
                p.set (idx, clampToRange (idx, (float) v));
                continue;
            }
            const auto colon = line.find (':');
            if (colon == std::string::npos) continue;
            const auto key = trim (line.substr (0, colon)), val = trim (line.substr (colon + 1));
            if (key == "name") p.name = val; else if (key == "category") p.category = val;
            else if (key == "author") p.author = val; else if (key == "description") p.description = val;
        }
        if (p.name.empty()) return false;
        out = p;
        return true;
    }

    static float clampToRange (int i, float v) noexcept
    {
        v = std::fmax (kParams[i].min, std::fmin (kParams[i].max, v));
        return kParams[i].kind == Kind::Float ? v : std::round (v);
    }

private:
    static std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t");
        if (a == std::string::npos) return {};
        return s.substr (a, s.find_last_not_of (" \t") - a + 1);
    }
};
} // namespace me

# MUTATION ENGINE

Experimental spectral resonator: 32-voice resonator bank, vowel formants, spectral/glitch processing and a seeded, musically-constrained MUTATE system. VST3 (macOS also AU). Primary test host: Ableton Live (macOS); Linux builds for REAPER / Bitwig.

![UI preview](Assets/ui-preview.png)

*Design preview (HTML render in `Assets/ui-preview/`), not yet the JUCE editor. The current editor is the foundation version.*

## Status (step 1 + start of step 2)
- JUCE 8 / CMake project, stereo I/O, APVTS parameters, state save/restore, basic editor with live scope
- DSP (header-only, JUCE-free): `ResonatorBank`, `FilterBank`, `FormantEngine`
- MIDI pitch control of the resonator root
- Unit tests: impulse response, tuning, feedback stability, determinism, parameter-sweep safety, filter modes, formant peaks
- Not yet: spectral engine, glitch, mutation core, presets, full UI

## Build
```bash
# DSP tests only (no JUCE download)
cmake -S . -B build -DME_BUILD_PLUGIN=OFF && cmake --build build && ctest --test-dir build --output-on-failure
# Plugin (fetches JUCE 8.0.6)
cmake -S . -B build-plugin -DME_BUILD_TESTS=OFF && cmake --build build-plugin --target MutationEngine_VST3
```
Linux needs: `libasound2-dev libfreetype6-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev`.

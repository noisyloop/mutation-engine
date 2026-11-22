# MUTATION ENGINE

Experimental spectral resonator: 32-voice resonator bank, vowel formants, spectral/glitch processing and a seeded, musically-constrained MUTATE system. VST3 (macOS also AU). Primary test host: Ableton Live (macOS); Linux builds for REAPER / Bitwig.

![Mutation Engine UI](Assets/ui.png)

*The real JUCE editor rendered headlessly by `MutationEngineShot` while processing a synthetic drum loop. Glitch and Phantom mode panels: [`ui-glitch.png`](Assets/ui-glitch.png), [`ui-phantom.png`](Assets/ui-phantom.png). The original design mock-up is in `Assets/ui-preview/`.*

## Status (steps 1-3 core)
- JUCE 8 / CMake project, stereo I/O, APVTS parameters, state save/restore, basic editor with live scope
- DSP (header-only, JUCE-free): `ResonatorBank`, `FilterBank`, `FormantEngine`
- MIDI pitch control of the resonator root
- Unit tests: impulse response, tuning, feedback stability, determinism, parameter-sweep safety, filter modes, formant peaks
- `SpectralEngine`: 1024-pt STFT (75% overlap), freeze with drifting partials, bin shift, bin dropout, bin scramble (seeded, deterministic). Constant 1024-sample latency, reported to the host; dry path is delay-compensated
- `GlitchEngine`: tempo-synced events (stutter, reverse, crush, gate) decided per grid step from a seeded RNG; follows host tempo/position
- Modes wired: **Resonate**, **Glitch**, **Phantom** (P0)
- **Mutation core** (`Source/Mutation/`, JUCE-free, fully unit-tested): see below
- **Interface** (`Source/UI/`): dark theme with a custom look-and-feel, scaled from a fixed 1600x1000 design space. Live spectrogram waterfall with spectrum, the 32 resonance positions and the formant peaks overlaid; output scope; draggable vowel chart (drag along A-E-I-O-U) and filter response (drag for cutoff / resonance); resonance-bank bars; per-mode panels (Resonate / Glitch / Phantom); knobs with lock icons and a modulation badge; live CPU, output peak, latency and feedback-safety readouts
- **Modulation**: two LFOs (sine/triangle/saw/square), an input envelope follower, a seeded random source and MIDI velocity, routed through four assignment slots to 15 destinations. Modulated values are clamped by the same safety limits as mutation
- **Presets**: 16 factory presets (embedded in the binary) across Resonate, Vocal, Glitch, Phantom and Mutant; user presets save to `~/Library/Application Support/NoisyLoop/MutationEngine/Presets` (macOS) or the platform's user data folder. Loading a preset is undoable and the host sees them as programs
- Not yet: P1 modes (Fractal, Liquefy, Collapse), parameter smoothing

## Presets
Presets are plain text (`Presets/*.mepreset`), readable and hand-editable:
```
name: Glass Throat
category: Resonate
res_root = 220
res_voices = 20
mod1_src = 1
mod1_dst = 8
mod1_amt = 0.25
```
Unmentioned parameters load at their default; unknown ids are ignored. Mutation controls and lock switches are never stored in a preset. Every factory preset is checked in CI: it must parse, stay inside the safety limits and render finite, bounded audio with its modulation running.

## Mutation
- [x] **Deterministic seed**: result depends only on (state, options). The result carries the next seed, so repeated MUTATE walks a reproducible sequence; the seed is shown and editable (`0x...` hex or decimal) and is saved with the project
- [x] **Parameter groups and locks**: lock a whole group (Resonator / Formant / Filter / Spectral / Glitch) or any single knob. Locked parameters are never modified, and locking one parameter never changes what the others become (every parameter consumes a fixed number of random draws)
- [x] **Intensity**: 0 = nothing changes; higher = more parameters touched and moved further
- [x] **Styles**: *Organic* (gentle, centre-weighted, pitch snapped to a minor pentatonic), *Digital* (stepped values, power-of-two voice counts, vowels snap to A E I O U), *Unstable* (edge-weighted extremes, microtonal pitch, heavy feedback and inharmonicity)
- [x] **Undo / redo** snapshots (64 deep). Undo first commits manual tweaks, so hand-made changes are recoverable
- [x] **Morph** between two mutations: MUTATE sets A = before, B = after; `SET A` / `SET B` capture any sound. Only mutable, unlocked parameters morph
- [x] **Safety**: feedback ceiling with a damping floor, filter Q and low-pass cutoff limits, dropout never silences everything, bin-shift limit, and a **CPU budget** that lowers the maximum resonator voice count when the audio thread is busy. Safety adjusts only unlocked parameters; the DSP's own hard limits and the output soft-clipper remain the last line of defence

`Source/ParamTable.h` is the single source of truth for every parameter; the JUCE layout and the mutation rules are both generated from it.

## Screenshots
```bash
xvfb-run -a build/MutationEngineShot_artefacts/Release/MutationEngineShot out.png "Glass Throat" 0
```

## Tests
`me_dsp_tests` (DSP), `me_mutation_tests` (mutation core), `me_preset_tests` (preset format, modulation, factory library) and `MutationEngineSmoke` (real processor + parameter tree, headless) all run in CI.

## Build
```bash
# DSP tests only (no JUCE download)
cmake -S . -B build -DME_BUILD_PLUGIN=OFF && cmake --build build && ctest --test-dir build --output-on-failure
# Plugin (fetches JUCE 8.0.6)
cmake -S . -B build-plugin -DME_BUILD_TESTS=OFF && cmake --build build-plugin --target MutationEngine_VST3
```
Linux needs: `libasound2-dev libfreetype6-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev`.

# Testing

## Automated (runs in CI on every change)

| Suite | What it proves |
|---|---|
| `me_dsp_tests` | Impulse responses, tuning, stability, determinism, parameter-sweep safety, filters, formants, FFT/STFT reconstruction, freeze, bin processing, tempo-locked glitch events |
| `me_mutation_tests` | Seeded determinism, locks, intensity, styles, safety limits, undo/redo, morph, persistence |
| `me_preset_tests` | Preset text format, modulation engine, every factory preset renders safely with modulation running |
| `MutationEngineSmoke` | Real processor + parameter tree: all parameters present, mutate/undo/redo, locks, A/B morph, all modes, state save/restore, presets, modulation clamping |
| `MutationEngineStress` | **Step 6 beta suite** (below) |
| Sanitizer jobs | The smoke and stress suites under AddressSanitizer + UBSan and under ThreadSanitizer |
| Toolchains | GCC, Clang + libc++ (same standard library as Apple clang), Apple clang on macOS |

### `MutationEngineStress`
- **Latency reporting:** 1024 samples at 44.1 / 48 / 88.2 / 96 / 192 kHz; with *Mix = 0* the output is the input delayed by exactly the reported latency (bit exact); the wet path aligns with it within the filter's own group delay; bypass is delay-compensated.
- **Sample-rate and block-size matrix:** 5 rates × 8 block sizes (1 … 4096) × 3 modes = 120 combinations, all finite and within ±1; re-preparing at a new rate mid-session; resonator tuning identical at 44.1 / 48 / 96 kHz.
- **Block-size independence:** the same audio and parameters render identically with blocks of 1, 77, 512 and 2048 (difference 0).
- **Automation:** every parameter randomised every block for 1200 blocks; sudden gain / wet-dry jumps are smoothed (no click).
- **Preset recall:** all 16 presets saved and restored into a fresh instance render the same audio.
- **Feedback safety:** worst-case settings (feedback 1.0, damping 0, 32 voices, even when the user locks the dangerous values) ring down to silence and never grow; NaN / Inf / ±1e9 input does not poison the filters.
- **Mutation under load:** 300 mutations (all styles) with audio running.
- **Threading:** the audio thread runs while another thread mutates, loads presets, automates and toggles locks.
- **CPU:** heaviest settings must stay under 50 % of one core (`ME_STRESS_CPU_LIMIT` overrides); typical results on a developer machine: 1–2 % at 48 kHz, 6 % at 192 kHz.

## Manual (needs a real host) — *not yet done*

The automated suites cannot replace listening or using a real DAW. Run this on each platform and record the result.

### Setup
1. Install the plugin (`INSTALL.md`). Note the DAW version and OS version.
2. Make a project at 48 kHz with a drum loop on an audio track.

### Checklist (macOS: Ableton Live · Linux: REAPER and Bitwig)
| # | Step | Expect | Result |
|---|---|---|---|
| 1 | Rescan; plugin appears; add it after the drum loop | Editor opens, no console errors, UI scales when resized | |
| 2 | Play the loop, Mix 100 %, preset *Glass Throat* | Tuned resonances, live spectrogram moves, no clicks | |
| 3 | Tune with MIDI (REAPER/Bitwig: route a MIDI track to the plugin; Live: see note) | Resonance root follows notes | |
| 4 | Morph the vowel (drag in the vowel chart, or LFO 1 → Vowel) | Smooth formant movement, no zipper noise | |
| 5 | Filter: drag in the filter display, try all four types | Audible, stable, no pops | |
| 6 | Hit MUTATE ten times in each style; lock Filter + Vowel first | A new usable sound each time; locked values never move; no level spikes | |
| 7 | Undo ×5, redo ×5; SET A / SET B and sweep Morph | Exact recall; smooth morph | |
| 8 | Switch to Glitch, then Phantom (play audio, press FREEZE) | Tempo-locked events follow the host tempo; freeze holds a drone | |
| 9 | Latency: duplicate the audio track, put the plugin on the copy at Mix 0, invert the copy's polarity | Cancels to silence (PDC active). If not, note the host's PDC setting | |
| 10 | Change the project sample rate to 44.1 and 96 kHz and replay | Same sound, no glitches; spectrogram still correct | |
| 11 | Automate Mix, Root and Feedback; record and play the automation | Follows the lane smoothly | |
| 12 | Save the project, close, reopen | Sound, seed, locks, A/B, preset name all restored | |
| 13 | Bounce / render offline and compare with real-time playback | Same audio (within normal tolerance) | |
| 14 | Watch the CPU meter for 5 minutes with 32 voices and feedback high | Stable, no growth, no runaway level | |
| 15 | Bypass the plugin on and off while playing | No timing shift, no pop | |
| 16 | Remove / re-insert the plugin 20 times | No crash | |

### Known question to answer on the first Ableton run
*Does Live deliver MIDI to this audio-effect plugin?* If not (expected), the *Root* knob/automation is the only
pitch control in Live, and a "instrument with side-chain audio input" variant is the follow-up that would let
MIDI tune the resonances inside Live.

### Reporting
Open an issue titled `Beta test: <DAW> <version> on <OS>` containing this table filled in, the plugin version
(`BUILDINFO.txt`), sample rate / buffer size, and for any failure the project (or steps) and the console output.

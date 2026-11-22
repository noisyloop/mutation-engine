# Installing Mutation Engine

Mutation Engine is a stereo **VST3** effect (plus **AU** on macOS).

| | macOS | Linux |
|---|---|---|
| Formats | VST3, AU | VST3 |
| Architecture | Apple Silicon + Intel (universal) | x86-64 |
| Primary host | Ableton Live | REAPER, Bitwig |
| Verified here | **not yet** (see below) | builds and passes the full automated suite |

> **Status.** The Linux build is exercised by the automated suites on every change. The macOS build is
> configured and built by CI and by the release workflow, but at the time of writing no macOS run has
> completed (GitHub cancels the macOS jobs before they start) and nothing has been loaded in Ableton Live.
> Treat macOS as *untested* until `docs/TESTING.md` has been run there.

## 1. Pre-built archives

Tagged releases (and manual runs of the **release** workflow) produce:

- `MutationEngine-<version>-macOS-universal.zip`
- `MutationEngine-<version>-linux-x86_64.tar.gz`
- a `.sha256` file for each

Verify the download, unpack, then run the installer inside the archive:

```bash
shasum -a 256 -c MutationEngine-*.sha256        # macOS   (Linux: sha256sum -c)
unzip MutationEngine-*-macOS-universal.zip      # Linux: tar xzf MutationEngine-*-linux-x86_64.tar.gz
cd MutationEngine-*/
./scripts/install.sh
```

### macOS notes
- The installer copies the VST3 to `~/Library/Audio/Plug-Ins/VST3` and the AU to
  `~/Library/Audio/Plug-Ins/Components`, removes the download quarantine flag and ad-hoc signs the bundles.
- The archives are **ad-hoc signed, not notarised**. If you move the files by hand and macOS says the plugin
  "cannot be opened", run: `xattr -dr com.apple.quarantine "/path/to/Mutation Engine.vst3"`.
- **Ableton Live:** Settings → Plug-Ins → turn on *Use VST3 Plug-In System Folders*, then *Rescan*. The plugin
  appears under *Plug-ins → VST3 → NoisyLoop*.

### Linux notes
- The installer copies the VST3 to `~/.vst3` (system-wide with `--system`: `/usr/lib/vst3`).
- **REAPER:** Options → Preferences → Plug-ins → VST → *Re-scan*. **Bitwig:** Settings → Plug-ins → *Rescan*.
- Runtime libraries needed (usually already present): ALSA, FreeType, fontconfig, X11, Xrandr, GL.

## 2. Building from source

Requirements: CMake ≥ 3.22, a C++17 compiler, Git (JUCE 8.0.6 is downloaded by CMake), internet on first configure.

**macOS** — Xcode command line tools:
```bash
xcode-select --install
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
scripts/install.sh --build-dir build
```
The build is a universal binary (arm64 + x86_64), deployment target 11.0.

**Linux (Debian/Ubuntu)**:
```bash
sudo apt-get install build-essential cmake git libasound2-dev libfreetype6-dev libfontconfig1-dev \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
scripts/install.sh --build-dir build
```
`libfontconfig1-dev` matters: JUCE queries FreeType and fontconfig together, so a missing fontconfig
also hides FreeType and the build fails with `ft2build.h: No such file or directory`.

Useful options: `-DME_COPY_PLUGIN=ON` copies the plugin into your user plugin folder after every build;
`-DME_BUILD_PLUGIN=OFF` builds and runs only the JUCE-free DSP tests (no JUCE download).

### Run the tests
```bash
ctest --test-dir build --output-on-failure          # DSP, mutation and preset suites
build/MutationEngineSmoke_artefacts/Release/MutationEngineSmoke     # real processor, headless
build/MutationEngineStress_artefacts/Release/MutationEngineStress   # step 6 beta suite
```
(On macOS multi-config generators place binaries in `Release/`; the paths above are for Makefiles/Ninja.)

## 3. Reproducible builds

For a given commit, toolchain and JUCE tag the plugin binary does not depend on where the checkout lives
(source and build paths are stripped with `-ffile-prefix-map`, JUCE is pinned to a tag). Every release
archive contains `BUILDINFO.txt` (commit, compiler, CMake, JUCE tag) and has a published SHA-256. Archives are
packed with sorted entries and a fixed timestamp taken from the commit.

To check a Linux build yourself, build twice in two different directories and compare:
```bash
cmake -S . -B /tmp/a -DCMAKE_BUILD_TYPE=Release -DME_BUILD_TESTS=OFF && cmake --build /tmp/a --target MutationEngine_VST3 -j
cmake -S . -B /tmp/b -DCMAKE_BUILD_TYPE=Release -DME_BUILD_TESTS=OFF && cmake --build /tmp/b --target MutationEngine_VST3 -j
sha256sum /tmp/a/MutationEngine_artefacts/Release/VST3/*/Contents/*/*.so /tmp/b/MutationEngine_artefacts/Release/VST3/*/Contents/*/*.so
```
Bit-identical output across *different* machines additionally requires the same compiler version.

## 4. Host notes

- **MIDI pitch control.** The resonator root follows incoming MIDI notes (toggle *MIDI PITCH*). REAPER and Bitwig
  can route MIDI to an effect. **Ableton Live, to our knowledge, does not deliver MIDI to audio-effect plugins**,
  so inside Live the root has to be set (or automated) with the *Root* knob until a variant is built that Live
  treats as an instrument with a side-chain audio input. This has not been checked in Live yet; it is the main
  open item for the "tune resonances with MIDI" part of the milestone (see `docs/TESTING.md`).
- **Latency.** The plugin reports 1024 samples in every mode and delays its dry path to match, so *Mix* never
  comb-filters. Hosts with plugin delay compensation line it up automatically.
- **Presets.** 16 factory presets ship inside the plugin. User presets are saved to
  `~/Library/Application Support/NoisyLoop/MutationEngine/Presets` (macOS) or
  `~/.config/NoisyLoop/MutationEngine/Presets` (Linux).

## 5. Uninstall
```bash
scripts/install.sh --uninstall            # add --system if you installed system-wide
```

## 6. Troubleshooting
| Symptom | Fix |
|---|---|
| Not listed after install | Rescan in the DAW; on Live make sure the VST3 system folders option is on |
| macOS: "cannot be opened" / "damaged" | `xattr -dr com.apple.quarantine` on the bundle; the build is not notarised |
| macOS Apple Silicon: plugin fails to load | The bundle must be signed: `codesign --force --deep --sign - "Mutation Engine.vst3"` |
| Linux: scan fails with a missing `.so` | Install the runtime libraries listed above |
| Linux build: `ft2build.h` not found | Install `libfontconfig1-dev` |

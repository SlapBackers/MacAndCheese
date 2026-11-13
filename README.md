# MatchMaster

> **Based on [Matchering](https://github.com/sergree/matchering) by Sergree.** MatchMaster is a real-time C++ port of Matchering's algorithm, packaged as a VST3. It is a separate project, not affiliated with Matchering — see [NOTICE](NOTICE).

Reference mastering as a VST3 plugin: a semi-real-time version of Matchering, running live in your DAW.

> **Status: built in public.** macOS (Apple Silicon + Intel), VST3.

## Download

Get the latest `MatchMaster-vX.Y.Z-macOS-VST3.zip` from **[Releases](https://github.com/SlapBackers/MatchMaster/releases)** — what changed is in the [CHANGELOG](CHANGELOG.md).

1. Unzip and copy `MatchMaster.vst3` to `~/Library/Audio/Plug-Ins/VST3`
2. The plugin is not notarised by Apple yet. If your DAW doesn't show it, run once in Terminal:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MatchMaster.vst3`
3. Rescan plugins in your DAW.

## How it works

1. **References** – drop up to 3 well-mastered songs into slots **A / B / C**.
2. **Learn** – press Learn and play your track, loudest part included. The button tells you when it has heard enough (the sound of your track has stopped changing); then press Stop. One Learn matches all three profiles.
3. **Listen** – click A / B / C to compare. The match is applied live, non-destructively:
   - **Match** knob: 0 % = your original, 100 % = the full Matchering result
   - **Level match**: hear only the EQ match, at your track's own loudness — the fair way to judge the EQ (louder always sounds better)
   - **Clipper → Gain → Limiter**: **Clip** (0–6 dB, default 0) shaves the loudest peaks (Airwindows ClipOnly2), a clean gain sets the level, the limiter catches what's left. CLIP and LIM meters show each stage's work
   - **Limiter** (visible, always under your control): on/off, true peak (default on), ceiling (default −1 dBTP), gain-reduction readout. With the limiter off, the output keeps its headroom and the plugin tells you how much gain to push into your own limiter to match the reference.
   - **Advanced**: matching EQ curve, Mid EQ / Side EQ / Loudness amounts, EQ **Smoothing**, **Max correction**, EQ mode **Linear** (phase-exact, ~92 ms latency) or **Natural** (no pre-echo, ~7 ms latency)

Re-learn after big mix changes. References and the learned match are saved with your DAW project; after reopening, everything works, but loading a *new* reference needs a new Learn (the learned audio itself is not stored).

## What it does to the sound

A C++ port of Matchering's processing, run in real time:

| Matchering stage | MatchMaster |
|---|---|
| Loudness match on the loudest ~15 s pieces (mid channel) | same, measured during Learn |
| Matching EQ: averaged spectra of reference ÷ track, smoothed (cubic spline + LOWESS) | same, applied as a linear-phase FIR, separately on Mid and Side |
| RMS correction passes (×4) | same |
| — | optional clipper before the limiter: [ClipOnly2](https://github.com/airwindows/airwindows) by Airwindows |
| "Hyrax" brickwall limiter (non-causal) | real-time port with ~5 ms lookahead, 4× true-peak detection, adjustable ceiling, optional |
| Final level from the reference's peak | same |

Latency is reported to the host (≈ 95 ms at 48 kHz in Linear mode, ≈ 10 ms in Natural mode).

## Build

Requires CMake ≥ 3.22 and Xcode command line tools.

```bash
git clone --recurse-submodules https://github.com/SlapBackers/MatchMaster.git
cd MatchMaster
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MatchMaster_VST3 -j
```

The plugin is at `build/MatchMaster_artefacts/Release/VST3/MatchMaster.vst3` — copy it to `~/Library/Audio/Plug-Ins/VST3`.

### Tests

```bash
cmake --build build --target MatchMasterCLI MatchMasterFlowTest -j
./build/MatchMasterCLI_artefacts/Release/MatchMasterCLI selftest          # matching maths
./build/MatchMasterFlowTest_artefacts/Release/MatchMasterFlowTest.app/Contents/MacOS/MatchMasterFlowTest /tmp/mm   # full plugin flow + GUI renders
```

`MatchMasterCLI match <track> <reference> <out.wav>` renders a file offline with the same chain.

## Roadmap

Where MatchMaster is going (fix-the-mix section, Airwindows character stage, new GUI) is in **[ROADMAP.md](ROADMAP.md)**.

## Credits & license

- Matching algorithm: [Matchering](https://github.com/sergree/matchering) © 2016–2022 Sergree, GPLv3 — ported to C++ for real time (see [NOTICE](NOTICE))
- Clipper: [ClipOnly2](https://github.com/airwindows/airwindows) by Chris Johnson / Airwindows, MIT licence
- Framework: [JUCE](https://juce.com) (AGPLv3 / commercial)

MatchMaster is free software under the **GNU GPL v3** – see [LICENSE](LICENSE).

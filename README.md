# MatchMaster

Reference mastering as a VST3 plugin: a semi-real-time version of [Matchering](https://github.com/sergree/matchering), running live in your DAW.

> **Status: built in public.** macOS (Apple Silicon + Intel), VST3.

## Download

Get the latest `MatchMaster-vX.Y.Z-macOS-VST3.zip` from **[Releases](https://github.com/SlapBackers/MatchMaster/releases)** — what changed is in the [CHANGELOG](CHANGELOG.md).

1. Unzip and copy `MatchMaster.vst3` to `~/Library/Audio/Plug-Ins/VST3`
2. The plugin is not notarised by Apple yet. If your DAW doesn't show it, run once in Terminal:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MatchMaster.vst3`
3. Rescan plugins in your DAW.

## How it works

1. **Reference** – drop a well-mastered song onto the plugin. It's analysed in the background.
2. **Learn** – press Learn and play your track (at least 5 s, ideally the loudest part), then press Stop.
3. **Listen** – the match is locked and applied live, non-destructively:
   - **Match** knob: 0 % = your original, 100 % = the full Matchering result
   - **Advanced**: matching EQ curve, Mid EQ / Side EQ / Loudness amounts

Re-learn after big mix changes.

## What it does to the sound

A C++ port of Matchering's processing, run in real time:

| Matchering stage | MatchMaster |
|---|---|
| Loudness match on the loudest ~15 s pieces (mid channel) | same, measured during Learn |
| Matching EQ: averaged spectra of reference ÷ track, smoothed (cubic spline + LOWESS) | same, applied as a linear-phase FIR, separately on Mid and Side |
| RMS correction passes (×4) | same |
| "Hyrax" brickwall limiter (non-causal) | real-time port with ~5 ms lookahead |
| Final level from the reference's peak | same |

Latency is reported to the host (≈ 92 ms at 48 kHz).

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

- Several reference profiles to compare, tagged with a "flavour"
- Limiter and EQ phase improvements
- Airwindows stages (ToVinyl, Channel9, Point, Surge, Stonefire, Slew)

## Credits & license

- Matching algorithm: [Matchering](https://github.com/sergree/matchering) © 2016–2022 Sergree, GPLv3
- Framework: [JUCE](https://juce.com) (AGPLv3 / commercial)

MatchMaster is free software under the **GNU GPL v3** – see [LICENSE](LICENSE).

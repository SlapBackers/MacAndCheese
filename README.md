# Mac and Cheese

*Your mac, their cheese.* Your track is the mac; a great-sounding reference song is the cheese. Mac and Cheese pours the reference's tone and loudness over your track, live in your DAW. (Formerly MatchMaster.)

> **Based on [Matchering](https://github.com/sergree/matchering) by Sergree.** Mac and Cheese is a real-time C++ port of Matchering's algorithm, packaged as a VST3. It is a separate project, not affiliated with Matchering — see [NOTICE](NOTICE).

Reference mastering as a VST3 plugin: a semi-real-time version of Matchering, running live in your DAW.

> **Status: built in public.** macOS (Apple Silicon + Intel), VST3.

## Download

### [⬇ Download the VST3 plugin for macOS](https://github.com/SlapBackers/MacAndCheese/releases/latest/download/Mac-and-Cheese-VST3-plugin-macOS.zip)

Always the latest version (Apple Silicon and Intel). What changed: [CHANGELOG](CHANGELOG.md) · all versions: [Releases](https://github.com/SlapBackers/MacAndCheese/releases).

1. Unzip it: you get **`Mac and Cheese.vst3`**.
2. Move it to `~/Library/Audio/Plug-Ins/VST3` (in Finder: Go → Go to Folder…, paste the path).
3. Rescan plugins in your DAW.

If your DAW doesn't list it (macOS blocks plugins that aren't notarised by Apple yet), run once in Terminal:
`xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Mac and Cheese.vst3"`

## How it works

1. **References**: drop up to 3 great-sounding songs into **A / B / C**.
2. **Learn**: press Learn and play your track, loudest part included. The button says "enough" once the sound of your track has stopped changing; then press Stop. One Learn prepares all three references.
3. **Listen**: click A / B / C to compare. Everything is live and non-destructive, in three steps that follow the signal:
   - **1 Fix the mix**: **Unclump** evens out build-ups that come and go (mud blooming in a chorus, harsh cymbal hits) compared with the reference, in 5 bands (Bottom, Mud, Body, Bite, Fizz), mid and side separately. **Mono bass** makes the low end mono below a chosen frequency.
   - **2 Match**: the matching EQ and loudness, computed on the *fixed* mix. The **Match** knob goes from your mix (0 %) to fully matched (100 %).
   - **3 Loudness**: **Clip → Gain → Limiter**, three knobs, each **Auto** or manual (**Auto all** resets them). Auto clip shaves the peaks above the last ~1 dB of limiting (Airwindows ClipOnly2); auto gain matches the reference's loudness; auto limiter = −1 dBTP true peak. Limiter off: the output keeps its headroom and the plugin shows the gain to push into your own limiter.
   - **Level match**: hear the result at your track's own loudness: the fair comparison (louder always sounds better).
   - **Details**: EQ curve, Linear/Natural EQ, Mid/Side EQ, EQ smoothing, Max EQ, Unclump per band (50 % = match the reference, up to 2×), Allow boosts.

Re-learn after big mix changes. References and the learned match are saved with your DAW project; after reopening, everything works, but loading a *new* reference needs a new Learn (the learned audio itself is not stored).

## What it does to the sound

A C++ port of Matchering's processing, run in real time:

| Matchering stage | Mac and Cheese |
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
git clone --recurse-submodules https://github.com/SlapBackers/MacAndCheese.git
cd MacAndCheese
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MacAndCheese_VST3 -j
```

The plugin is at `build/MacAndCheese_artefacts/Release/VST3/Mac and Cheese.vst3` — copy it to `~/Library/Audio/Plug-Ins/VST3`.

### Tests

```bash
cmake --build build --target MacAndCheeseCLI MacAndCheeseFlowTest -j
./build/MacAndCheeseCLI_artefacts/Release/MacAndCheeseCLI selftest          # matching maths
./build/MacAndCheeseFlowTest_artefacts/Release/MacAndCheeseFlowTest.app/Contents/MacOS/MacAndCheeseFlowTest /tmp/mm   # full plugin flow + GUI renders
```

## Roadmap

Where Mac and Cheese is going (fix-the-mix section, Airwindows character stage, new GUI) is in **[ROADMAP.md](ROADMAP.md)**.

## Credits & license

- Matching algorithm: [Matchering](https://github.com/sergree/matchering) © 2016–2022 Sergree, GPLv3 — ported to C++ for real time (see [NOTICE](NOTICE))
- Clipper: [ClipOnly2](https://github.com/airwindows/airwindows) by Chris Johnson / Airwindows, MIT licence
- Framework: [JUCE](https://juce.com) (AGPLv3 / commercial)

Mac and Cheese is free software under the **GNU GPL v3** – see [LICENSE](LICENSE).

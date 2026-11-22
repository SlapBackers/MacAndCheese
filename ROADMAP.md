# Mac and Cheese roadmap

Mac and Cheese is built in public. This is where it's going and why. Releases and their notes are on the [Releases page](https://github.com/SlapBackers/MacAndCheese/releases); details of what shipped are in the [CHANGELOG](CHANGELOG.md).

**The idea:** pick a well-mastered reference song, let Mac and Cheese listen to your track once, and get a live, non-destructive master that sounds like the reference. Everything is driven by references, and nothing is a black box.

**The chain it is growing into:**

```
FIX (repair the mix)                       MASTER (match and polish)
Mono bass → Tame → Unclump          →     Match EQ → Character → Clipper → Gain → Limiter
```

- **FIX** is conservative: it only acts where your mix is clearly unhealthy compared with a reference (mud building up, harsh cymbals, boomy notes) and cuts by default.
- **MASTER** makes the repaired mix sound like the reference: tone, character, loudness.
- FIX can use a different reference than MASTER, e.g. a song with a similar arrangement to fix, and the sound you want to master.

---

## Done

| Version | What |
|---|---|
| v1.0 | Matchering's algorithm ported to C++ and running live as a VST3: reference → Learn → Match knob |
| v2.0 | Three reference profiles A/B/C, true-peak limiter with ceiling (−1 dBTP), limiter on/off, EQ smoothing, max correction, Linear/Natural EQ |
| v2.1 | Knobs respond while turning, ClipOnly2 clipper before the limiter, references and match saved with the project |
| v2.1.1 | Clipper → Gain → Limiter shown as separate stages with CLIP and LIM meters, **Level match** monitor (hear the EQ match at your track's loudness), **Learn quality** indicator (tells you when the spectrum is stable enough) |
| v2.1.x | Rename to Mac and Cheese, Learn quality on fixed slices, design template |
| **v2.2** | **Fix the mix**: Unclump (5-band dynamic match vs the reference) + Mono bass; three-column layout; Clip/Gain/Limiter with Auto; loudness and true-peak fixes |
| v2.2.1 | Per-band Unclump (50 % = match), band meters show boosts, signal levels checked stage by stage |
| v2.2.2 | Smaller window (fits laptop screens), one clearly named download, leaner code |

## Next

### v2.2.3: Universal
- **Windows and Linux builds** next to macOS, built and tested automatically by GitHub on every release. One clearly labelled zip per system, no installer. The Windows and Linux builds are **untested in a DAW**: feedback welcome.
- **Max boost** knob (0–6 dB) replaces the "Allow boosts" switch in Unclump.
- **Clip up to 9 dB** (knob and Auto).

### v2.3: Tame
- Soothe-style resonance taming, driven by the reference: only cuts where your track rings more than the reference does.
- Tame knob (50 % = match), listen to what is removed (delta), reduction display. Adds about 20–40 ms of latency.

### v2.4: Character
- **Character stage** (Airwindows), with each stage fed at the right level: **Point** (punch), **PowerSag**, **Channel9** (console colour, drive), **Acceleration2 / Slew** (tame harshness or add grit), **Stonefire** (Air / Fire / Stone tone after the match).

### v2.5: Presets and references
- **Your own presets** as starting points.
- **A+B+C average** profile: match the average of your three references, which is more robust than one song.
- **"Fix with" another reference** (A / B / C).
- **Unclump speed** (Tight / Loose = attack/release), learned at both speeds.
- **Vinyl preset**: ToVinyl4 with the limiter off.
- Load a new reference after reopening a project without re-learning (store a compact version of the learned track).

### v2.6: Metering
- Metering inspired by bx_meter: a moving dynamic-range readout that turns from green to red, plus LUFS and true peak.
- Spectrum overlay: your track vs the reference.

### v3.0: New GUI
- A designed interface (by the author). The spec is in [docs/GUI.md](docs/GUI.md). Resizable window.
- Fun names back as hover explanations on every control.

## Later / ideas
- Loudness target selector (match the reference, or "loudest sections at −10 LUFS"). Matching stays the default.
- Streaming loudness preview.
- Mix health check (hum, clipped source, DC offset, mono compatibility).
- Notarised macOS build, signed Windows build, AU format.

## Known limits today
- After reopening a project, loading a *new* reference needs a new Learn.
- Learn quality thresholds are first guesses and will be tuned on real songs.
- VST3 only. macOS build not notarised (see the README for the one-line install fix). Windows and Linux arrive in v2.2.3, untested in a DAW at first.

Suggestions are welcome: open an issue on GitHub.

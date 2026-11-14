# Changelog

Download the plugin from the [Releases page](https://github.com/SlapBackers/MacAndCheese/releases).

## v2.2.0 — Fix the mix, three-step layout, Auto loudness

**1 Fix the mix (new)**
- **Unclump**: evens out build-ups that come and go (mud blooming in a chorus, harsh cymbal hits) compared with the reference. 5 bands (Bottom, Mud, Body, Bite, Fizz), mid and side separately, cut-only by default. Per band: 50 % = match the reference, up to 2×. Band meters show each cut.
- **Mono bass**: mono low end below a chosen frequency.
- The match is computed on the *fixed* mix, so fix and match never correct the same thing twice.

**3 Loudness**
- **Clip → Gain → Limiter** as three knobs, each **Auto** or manual, plus **Auto all**. Auto clip leaves the limiter ~1 dB; auto gain = the reference's loudness (replaces the Loudness % knob); auto limiter = −1 dBTP.
- Numbers on the CLIP and LIM meters.

**Layout**: three columns in signal order (1 Fix the mix, 2 Match, 3 Loudness), plain names, **Details** side panel (fits laptop screens).

**Fixes**
- Loudness drifted when changing EQ smoothing or Max EQ: the loudness match now uses the same capped EQ as the sound and is recomputed when they change.
- The limiter could overshoot the true-peak ceiling by ~0.2 dB when working hard; it now holds it exactly.
- Learn says "enough" on real songs.

Note: projects saved with earlier versions open with Clip/Gain/Limiter on Auto, so Clip now acts by default.

## v2.1.4 — fixes

- Learn says "enough" more readily on real songs: the spectrum may change up to 1 dB on average (was 0.5), and the loudest part only counts as beaten when a later part is more than 1 dB louder (songs often end on their loudest chorus).
- Advanced panel shorter (780 px) so it fits on laptop screens inside the DAW window.

## v2.1.3 — Learn knows when it's enough, cheesier look

- **Learn quality fixed**: it used to never say "enough" on real songs, because the analysis re-sliced the whole capture at every check. It now works on fixed 15 s slices and says "enough" when the loudest part hasn't been beaten for 30 s, or when the spectrum (60 Hz–12 kHz) changes less than 0.5 dB on average.
- Mac and Cheese colours: baked crust, cream, melted cheddar.

## v2.1.2 — MatchMaster is now Mac and Cheese

- **New name: Mac and Cheese.** *Your mac, their cheese*: your track is the mac, the reference is the cheese. The plugin's internal ID is unchanged, so DAWs treat it as the same plugin; saved projects keep their settings.
- File is now `Mac and Cheese.vst3`: remove the old `MatchMaster.vst3` before installing, so the DAW doesn't see the same plugin twice.
- `design/template.svg` + workflow in `docs/GUI.md`: the starting point for designing the GUI.
- Leaner code: removed the outdated offline `match` command (it no longer matched the plugin's chain) and unused code.

## v2.1.1 — clearer output stages, level match, Learn quality

- **Clipper → Gain → Limiter**, shown as three stages: CLIP and LIM gain-reduction meters next to L/R, and a readout like "Clip 2.0 dB > Gain +4.2 dB > Limiter 0.3 dB". The sound is unchanged from v2.1.0.
- **Level match** button: hear only the EQ match at your track's own loudness (no loudness push, clipper or limiter), to judge the EQ fairly.
- **Learn quality**: while listening, MatchMaster re-analyses what it has heard every 4 s and tells you "keep playing" or "enough" once the spectrum of the loudest parts has stopped changing (under 0.5 dB, at least 30 s).
- Public [ROADMAP](ROADMAP.md).

## v2.1.0 — responsive knobs, clipper, saved with your project

- **Knobs respond while you turn them.** Before, the EQ only changed when you released the mouse (in some hosts not at all until then). The EQ now runs on a new engine with its own filter-building thread and click-free crossfades. Latency +128 samples (≈ 3 ms).
- **Clip** (0–6 dB, default 0 = off): a clipper before the limiter shaves that many dB off the loudest peaks; the limiter catches the rest and the true-peak ceiling still holds. Port of Airwindows ClipOnly2 (MIT). The readout shows clip and limiter reduction separately.
- **Saved with your DAW project**: references, file names and the learned match come back when you reopen. Known limit: the learned audio isn't stored, so loading a *new* reference after reopening needs a new Learn.
- Removed the flavour words.
- New `docs/GUI.md`: the spec for designing a new GUI.

## v2.0.1 — fix "damaged" plugin on macOS

- Fixed: macOS said "MatchMaster.vst3 is damaged" after downloading v2.0.0, and DAWs did not list it. The plugin was re-signed too early in the build; it is now signed as the last step.
- No change to the sound.
- If your DAW scanned the broken v2.0.0, replace it with this version and re-scan plugins (REAPER: Preferences → Plug-ins → VST → Re-scan, or "Clear cache/re-scan" if it is still missing).

## v2.0.0 — profiles, true-peak limiter, EQ controls

**Profiles**
- Three reference slots **A / B / C**, switchable live (automatable). One Learn matches all three.
- Automatic flavour word per reference (Bright, Warm, Heavy, Airy, Wide, Focused, Loud, Dynamic, Balanced), renamable by double-click.

**Limiter** (no longer a black box)
- True-peak detection (4× oversampled), on by default — fixes exports going over 0 dB.
- Ceiling control, default −1 dBTP.
- Limiter on/off. Off = the output keeps headroom under the ceiling and the plugin shows the gain to push into your own limiter to match the reference loudness.
- Gain-reduction readout (now / max).

**EQ**
- **Smoothing** knob: from Matchering's detailed curve to a broad tonal tilt.
- **Max correction** knob (default ±12 dB, was ±24 dB) — limits the boosts that cause pre-echo.
- **EQ mode**: Linear (as before) or **Natural** (minimum phase: no pre-echo, latency ≈ 7 ms instead of ≈ 92 ms).
- Side EQ no longer boosts near-mono content excessively (fixes the odd stereo image).

**Other**
- Clearer credit to Matchering (README + NOTICE).

Known: flavour words are an uncalibrated heuristic (rename freely); references and the learned match are not saved with the DAW project.

## v1.0.0 — first release

Semi-real-time Matchering as a VST3 for macOS (Apple Silicon and Intel).

- **Reference**: drop a well-mastered song; it is analysed in the background.
- **Learn**: play your track once; the match is computed and locked.
- **Live, non-destructive**: matching EQ (mid/side, linear phase), loudness match and Matchering's limiter, blended with the **Match** knob (0–100 %).
- **Advanced panel**: matching EQ curve, Mid EQ / Side EQ / Loudness amounts.
- Latency reported to the host (≈ 92 ms at 48 kHz).

Known issues: the limiter works on sample peaks only (exports can show true-peak overs); strong EQ boosts can cause pre-echo; the learned match is not saved with the DAW project.

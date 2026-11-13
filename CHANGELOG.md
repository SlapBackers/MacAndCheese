# Changelog

Download the plugin from the [Releases page](https://github.com/SlapBackers/MatchMaster/releases).

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

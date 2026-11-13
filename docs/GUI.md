# MatchMaster GUI spec (v2.1.1)

Everything a GUI designer needs: what the user can touch, what the plugin shows, and in which states.
The current GUI (`Source/PluginEditor.cpp`) is a functional placeholder; this spec is the contract a new design must cover.

## How a plugin GUI is built (for designers)

- **Fixed-size panel**, like a hardware faceplate. Design at 1× in pixels; deliver bitmaps at 2× for Retina. (Resizable is possible, but decide early.)
- **Static background + live controls on top.** The background is one image. Each knob, button, meter and readout is a separate layer the code moves or redraws.
- **Knobs**: either a filmstrip (64–128 frames, one image per position) or a fixed body + a pointer layer the code rotates. The second is easier and stays sharp.
- **Anything that moves is drawn by the code** (meters, EQ curve, numbers). You specify how it looks: colours, line weights, fonts, sizes.
- **Every state needs a design**, not only the hero shot (see "States").

## Controls (automatable parameters)

| ID | Name | Type / range | Default | Notes |
|---|---|---|---|---|
| `match` | Match | 0–100 % | 100 % | Main knob. Blends everything (EQ, loudness, clip) from dry to fully matched. |
| `profile` | Profile | A / B / C | A | Active reference slot. Clicking a slot card selects it. |
| `clip` | Clip | 0–6 dB, 0.1 steps | 0 dB | dB the clipper shaves off the loudest peaks before the limiter. 0 = off. |
| `limiter` | Limiter | on / off | on | Off: output kept under the ceiling, plugin shows the gain still needed. |
| `levelMatch` | Level match | on / off | off | Monitoring switch (not automatable): EQ match only, at the track's own loudness. |
| `truePeak` | True peak | on / off | on | Disabled (greyed) when the limiter is off. |
| `ceiling` | Ceiling | −6…0 dB, 0.1 steps | −1.0 dB | dBTP when True peak is on. |
| `eqMode` | EQ mode | Linear / Natural | Linear | Natural = no pre-echo, lower latency. Advanced. |
| `midEq` | Mid EQ | 0–100 % | 100 % | Advanced. |
| `sideEq` | Side EQ | 0–100 % | 100 % | Advanced. |
| `loudness` | Loudness | 0–100 % | 100 % | Advanced. How much of the loudness match is applied. |
| `smoothing` | Smoothing | 0–100 % | 0 % | Advanced. Detailed curve → broad tilt. Takes ~0.5 s to update. |
| `maxCorr` | Max correction | 3–24 dB, 0.1 steps | 12 dB | Advanced. Caps EQ boosts/cuts. |

## Actions (not parameters)

| Action | Where | Behaviour |
|---|---|---|
| Load reference | Slot card: drag & drop an audio file, or click an empty card / right-click any card to browse | Analysis takes a few seconds (card shows "Analysing…"). |
| Learn / Stop | Learn button | Toggle. Listening: plays your track through; Stop: analyses and matches all loaded slots. Auto-stops after ~3 min. Needs ≥ 5 s. |
| Advanced | Header button | Shows/hides the advanced panel. |

## Readouts and meters (drawn by the code)

| Item | Content | Update |
|---|---|---|
| Slot card ×3 | Letter (A/B/C), file name or "Drop or click to load a song", busy state "Analysing…", selected highlight | 30 Hz |
| Learn button | "Learn your track" / listening: "Stop  0:12" with a pulsing record icon / "Matching..." / "Re-learn your track" | 30 Hz |
| Message line | One sentence: next step or error (errors in the error colour). Examples below. | 30 Hz |
| Meters | Output L, R (peak, linear 0–1 with decay), CLIP (clipper reduction, 0–12 dB), LIM (limiter gain reduction, 0–12 dB) | 30 Hz |
| Output readout | One field per stage: "Clip 2.0 dB (max 2.2) > Gain +4.2 dB > Limiter 0.3 dB (max 0.8)". Limiter off: "... > your limiter: push +2.1 dB more, ceiling −1.0 dBTP". Level match: "Level match: EQ only, at your track's loudness". | 30 Hz |
| Learn quality | While listening: "keep playing" / "enough" on the Learn button (border turns green when enough) + message with how much the sound still changes (dB) | every 4 s |
| EQ curve (advanced) | Two curves, Mid and Side, gain in dB vs log frequency 20 Hz–20 kHz, ±18 dB scale, clamped to Max correction | on change |
| Info (advanced) | Loudness match (dB), reference level (dB), latency (ms) | on change |

Message examples: "Drop up to 3 mastered songs into A, B, C - then press Learn and play your track" · "Listening - play the loudest part of your track" · "Keep playing - the sound is still changing (1.4 dB). Include the loudest part" · "Enough - the sound of your track is stable, you can press Stop" · "Matching your track to the references..." · "Profile A matched - switch A / B / C to compare" · "Restored from the project - press Learn again before loading a new reference" · "Too short - play at least 5 seconds of your track".

## States to design

1. **First open**: all slots empty, Learn available, message = onboarding sentence.
2. **Slot loading**: one card "Analysing…".
3. **References loaded, not learned**.
4. **Listening**: Learn button active/recording look, elapsed time.
5. **Analysing** after Stop.
6. **Matched**: active slot highlighted, Match knob live, meters moving.
7. **Error**: message in error colour.
8. **Limiter off**: True peak greyed, readout shows the suggestion (accent colour).
9. **Clipping**: Clip > 0, CLIP meter moving.
10. **Level match on**: readout in the accent colour, clipper/limiter idle.
11. **Learn: keep playing / enough**.
12. **Advanced open**.
13. Interaction states for every control: normal, hover, pressed/dragging, disabled.

## Coming in v2.2 (leave room, don't detail yet)

Two clearly separated sections, both driven by the references:
- **FIX** (repair the mix, conservative): Dynamic match amount (+ 5 band amounts, per-band gain-reduction meters, Cut-only / Boost switch, Tight/Loose), Mono bass (Hz), "Fix from: Same / A / B / C".
- **MASTER** (match + polish): the current Match/EQ/loudness controls, then Character: Punch (bipolar, Tight/Loose), Sag, Drive + console type.

## Metering wish (v3.0)

Inspired by Brainworx bx_meter: a moving dynamic-range readout (peak-to-loudness) whose colour goes from green to red, plus LUFS (short-term / integrated) and true peak. Design room for it.

## What to deliver

- One mock-up per state above (Figma frames are ideal), at 1×, with layer names matching the control IDs.
- Knob/slider/button assets: SVG preferred, or PNG at 2× (filmstrip or body + pointer).
- Fonts (with a licence allowing embedding in software), colour tokens, spacing.
- Fixed size (e.g. 900 × 560) or resizable — say which.

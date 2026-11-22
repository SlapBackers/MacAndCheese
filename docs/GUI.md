# Mac and Cheese GUI spec (v2.2, for the v3.0 design)

Everything a GUI designer needs: what the user can touch, what the plugin shows, and in which states.
The current GUI (`Source/PluginEditor.cpp`) is a functional placeholder; this spec is the contract a new design must cover.

## How a plugin GUI is built (for designers)

- **Fixed-size panel**, like a hardware faceplate. Today: 756 × 470, or 1092 × 470 with Details open. Design at 1× in pixels; deliver bitmaps at 2× for Retina (3× leaves room for the resizable window planned for v3.0).
- **Static background + live controls on top.** The background is one image. Each knob, button, meter and readout is a separate layer the code moves or redraws.
- **Knobs**: either a filmstrip (64–128 frames, one image per position) or a fixed body + a pointer layer the code rotates. The second is easier and stays sharp.
- **Anything that moves is drawn by the code** (meters, EQ curve, numbers). You specify how it looks: colours, line weights, fonts, sizes.
- **Every state needs a design**, not only the hero shot (see "States").

## Controls (automatable parameters)

Plain names for now; fun names come back later as hover explanations.

| ID | Name | Range | Default | Section |
|---|---|---|---|---|
| `profile` | Reference | A / B / C | A | top row (slot cards) |
| `unclump` | Unclump | 0–100 % | 0 % | 1 Fix the mix |
| `monoBase` | Mono bass | Off, 20–300 Hz | Off | 1 Fix the mix |
| `match` | Match | 0–100 % | 100 % | 2 Match (hero knob) |
| `clip` / `clipAuto` | Clip | 0–6 dB / Auto | Auto | 3 Loudness |
| `gain` / `gainAuto` | Gain | −12…+36 dB / Auto | Auto | 3 Loudness |
| `ceiling` / `ceilingAuto` | Limiter (ceiling) | −6…0 dB / Auto (−1 dBTP) | Auto | 3 Loudness |
| `limiter` | Limiter on | on / off | on | 3 Loudness |
| `truePeak` | True peak | on / off | on | 3 Loudness |
| `levelMatch` | Level match | on / off (not automatable) | off | header |
| `eqMode` | EQ mode | Linear / Natural | Linear | Details |
| `midEq`, `sideEq` | Mid EQ, Side EQ | 0–100 % | 100 % | Details |
| `smoothing` | EQ smoothing | 0–100 % | 0 % | Details |
| `maxCorr` | Max EQ | 3–24 dB | 12 dB | Details |
| `bandBottom` … `bandFizz` | Unclump per band | 0–100 % (50 % = match) | 50 % | Details |
| `fluff` | Allow boosts (+3 dB) | on / off | off | Details |

Auto switches: switching one off leaves its knob at the value Auto was using. "Auto all" switches all three on.

## Actions (not parameters)

| Action | Where | Behaviour |
|---|---|---|
| Load reference | Slot card: drag & drop an audio file, or click an empty card / right-click any card to browse | Analysis takes a few seconds (card shows "Analysing…"). |
| Learn / Stop | Learn button | Toggle. Listening: plays your track through; Stop: analyses and matches all loaded slots. Auto-stops after ~3 min. Needs ≥ 5 s. |
| Level match | Header button | Hear the result at your track's loudness. |
| Details | Header button | Shows/hides the Details side panel. |

## Readouts and meters (drawn by the code)

| Item | Content | Update |
|---|---|---|
| Slot card ×3 | Letter (A/B/C), file name or "Drop or click to load a song", busy state "Analysing…", selected highlight | 30 Hz |
| Learn button | "Learn your track" / listening: "Stop  0:12" with a pulsing record icon / "Matching..." / "Re-learn your track" | 30 Hz |
| Message line | One sentence: next step or error (errors in the error colour). Examples below. | 30 Hz |
| Meters | Output L, R (peak, linear 0–1 with decay), CLIP (clipper reduction, 0–12 dB), LIM (limiter gain reduction, 0–12 dB) | 30 Hz |
| Output readout | One field per stage: "Clip 2.0 dB (max 2.2) > Gain +4.2 dB > Limiter 0.3 dB (max 0.8)". Limiter off: "... > your limiter: push +2.1 dB more, ceiling −1.0 dBTP". Level match: "Level match: EQ only, at your track's loudness". | 30 Hz |
| Learn quality | While listening: "keep playing" / "enough" on the Learn button (border turns green when enough) + message with how much the sound still changes (dB) | every 4 s |
| EQ curve (Details) | Two curves, Mid and Side, gain in dB vs log frequency 20 Hz–20 kHz, ±18 dB scale, clamped to Max correction | on change |
| Info (Details) | Loudness match (dB), reference level (dB), latency (ms) | on change |

Message examples: "Drop up to 3 mastered songs into A, B, C - then press Learn and play your track" · "Listening - play the loudest part of your track" · "Keep playing - the sound is still changing (1.4 dB). Include the loudest part" · "Enough - the sound of your track is stable, you can press Stop" · "Matching your track to the references..." · "Reference A matched - switch A / B / C to compare" · "Restored from the project - press Learn again before loading a new reference" · "Too short - play at least 5 seconds of your track".

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
12. **Details open**.
13. Interaction states for every control: normal, hover, pressed/dragging, disabled.

## Coming before v3.0 (leave room, don't detail yet)

- **v2.2.3**: "Allow boosts" becomes a **Max boost** knob (0–6 dB); Clip goes up to 9 dB.
- **v2.3 Tame**: knob (50 % = match), delta listen switch, reduction display, in 1 Fix the mix.
- **v2.4 Character**: a stage between Match and Loudness (Punch, Sag, Drive + console type, Tone).
- **v2.5**: presets, A+B+C average, "Fix with" reference.

## Metering wish (v2.6)

Inspired by Brainworx bx_meter: a moving dynamic-range readout (peak-to-loudness) whose colour goes from green to red, plus LUFS (short-term / integrated) and true peak. Design room for it.

## Workflow: from your design to the plugin

1. **Start from [`design/template.svg`](../design/template.svg)** (opens in Figma, Illustrator, Affinity). It holds every control as a placeholder; **layer name = control ID** from the tables above. The Details panel sits to the right of the main panel.
2. **Restyle freely**: move, resize, redraw. Two rules: keep the layer names, one layer (or group) per control.
3. **Export into `design/`**:
   - `background.png` and `background@2x.png` (and `background_details`), *without* the controls;
   - knobs: `knob_<id>_body.svg` + `knob_<id>_pointer.svg` (or a PNG filmstrip, 128 frames);
   - toggles/buttons: `<id>_on` / `<id>_off`;
   - fonts (licence allowing embedding) and a list of colours;
   - your layered source file, so positions and sizes can be read.
4. **I build it in code** and send back renders of every state from the test tool, next to your mock-ups.
5. **We iterate** on the differences.

## Designing in Blender (pre-rendered, no real-time 3D)

Everything is rendered once to images; the plugin only stacks them and picks frames.

- **Orthographic camera, straight on**; render resolution = panel size × 2 (or × 3). One scene and one lighting setup for everything.
- **Name each object after its control ID** (`match`, `unclump`, `clip`, `meter_lim`, ...). An export script renders all layers and writes `layout.json` with each control's pixel box.
- **Background**: the panel without knobs or buttons, knob shadows baked in. **Screens** (meters, EQ curve, band meters, text) stay empty: the code draws on them.
- **Knobs**: a filmstrip of 64 frames min → max on a transparent background (a rotated single image would turn the highlights with it). Knobs of the same style and size share one strip.
- **Buttons**: `off` / `on` (/ `pressed`) frames. Hover and disabled states are done in code.
- Budget: a few MB in total.

# How Mac and Cheese works

Matchering takes a finished track and a reference and returns one master. Mac and Cheese splits that process into parts you can hear and control live, and adds a "fix the mix" step in front.

```
your track ─► 1 FIX THE MIX ───────────► 2 MATCH ──────────────► 3 LOUDNESS ─────────────────► out
              Mono bass → Unclump        Match EQ (Mid / Side)   Clip → Gain → Limiter
              (live, per band)           + loudness match        (each Auto or manual)
                    ▲                          ▲                        ▲
                    └────── all driven by what Learn measured on your track and the reference ──┘
```

Everything runs in Mid / Side: **Mid** is what both speakers share (vocals, kick, bass), **Side** is what differs between them (width, reverb, wide guitars).

## Learn: measuring your track

The plugin can't see the whole song in advance, so it listens once.

- **Press Learn and play your track.** The audio is recorded in memory (up to about 3 minutes).
- **"Enough"** appears when the sound has stopped changing. The recording is cut into fixed 15 s slices and measured in 1/3-octave bands. It is enough once the loudest slice hasn't been beaten by more than 1 dB for 30 s, or the average spectrum has moved less than 1 dB twice in a row.
- **Press Stop.** Your track is analysed and matched against every loaded reference (A, B, C), so switching references afterwards is instant.

References are analysed the moment you drop them in, from the whole file.

## 1 Fix the mix

**Mono bass.** A 24 dB/octave high-pass on the Side signal: below the chosen frequency, the low end becomes mono. Off below 20 Hz.

**Unclump** (dynamic match, the part Matchering doesn't have). A static EQ can't fix mud that only blooms in the chorus, or cymbals that only bite on some hits. Unclump works on how each band *moves*:

1. Mid and Side are split into 5 bands with phase-aligned Linkwitz-Riley crossovers (they sum back flat): **Bottom** < 120 Hz < **Mud** < 500 Hz < **Body** < 2.5 kHz < **Bite** < 5 kHz < **Fizz**.
2. Each band's level is followed **relative to the rest of the mix**. When the whole song gets louder, nothing happens. When one band swells above the others, that is the build-up.
3. During Learn, the plugin stores how far each band usually climbs, in your track and in the reference: its typical level and its peaks (50th to 99th percentiles).
4. Live, when a band climbs further above its usual level than the reference's ever does, it is pulled down to the reference's spread. Normal moments are left alone.

- **Unclump** sets the overall amount (0 % = off).
- **Per band** (Details): 50 % = match the reference, 100 % = twice as strong, 0 % = leave that band alone. Cuts go down to −18 dB.
- **Max boost** (Details): at 0 dB, Unclump only cuts. Above it, a band that dips below the reference can be lifted, up to that amount.
- The band meters show each band's cut (and boost) live.

The match (step 2) is computed on the **fixed** track, so the EQ doesn't try to correct what Unclump already handled.

## 2 Match (Matchering's algorithm, live)

This is a C++ port of [Matchering](https://github.com/sergree/matchering)'s processing:

1. **Loudest parts.** Both songs are cut into pieces, and the loudest ones are kept, so quiet intros don't skew the match.
2. **Average spectra.** Averaged FFT spectra of those pieces are taken, separately for Mid and Side.
3. **EQ curve** = reference ÷ your track, smoothed (LOWESS) so it follows the tonal balance, not every detail.
4. **Correction passes.** The EQ is applied, the level is re-measured and corrected, several times, until the loudness matches.

Mac and Cheese turns the curve into an FIR filter and runs it live (partitioned FFT convolution, rebuilt in the background while you turn a knob).

- **Match**: 0 % = your mix, 100 % = the full EQ and loudness match.
- **Details**:
  - **Mid EQ / Side EQ**: how much of the curve applies to each.
  - **EQ smoothing**: from a detailed curve to a broad tilt.
  - **Max EQ**: the largest boost or cut allowed (the loudness is re-matched with the capped curve).
  - **Linear phase** (exact, about 95 ms latency) or **Natural** (minimum phase, no pre-echo, about 10 ms).

## 3 Loudness

Matchering ends with its "Hyrax" limiter. Mac and Cheese splits that end into three stages, each with **Auto** (or set by hand; **Auto all** turns all three back on).

| Stage | What it does | Auto |
|---|---|---|
| **Clip** (0–9 dB) | Shaves the sharpest peaks with Airwindows [ClipOnly2](https://github.com/airwindows/airwindows): clean, no knee, gentler on transients than a limiter pushed hard. | Takes everything above the last ~1 dB the limiter should do. |
| **Gain** | Plain gain into the limiter. | Brings the track to the reference's loudness. |
| **Limiter** | Real-time port of Hyrax: ~5 ms lookahead, true-peak detection (4× oversampled), adjustable ceiling. | Ceiling −1 dBTP. |

- **Limiter off:** the output keeps its headroom, and the readout tells you how much gain to push into your own limiter.
- **Level match** (header): hear the EQ match at your track's own loudness. That is the fair comparison, because louder always sounds better.

## What is saved with your project

The references' analysis, the learned match and Unclump's statistics are saved in the project, so everything sounds the same after reopening. The learned audio itself isn't stored, so loading a *new* reference needs a new Learn.

## Source map

| Part | File |
|---|---|
| Learn, slots, parameters, audio chain | [`Source/PluginProcessor.cpp`](../Source/PluginProcessor.cpp) |
| Matchering port (spectra, smoothing, EQ curve, correction passes) | [`Source/Core/Matcher.cpp`](../Source/Core/Matcher.cpp) |
| Mono bass, Unclump | [`Source/Core/Prep.h`](../Source/Core/Prep.h) |
| Live EQ engine | [`Source/Core/PartitionedConvolver.h`](../Source/Core/PartitionedConvolver.h) |
| Clipper, limiter, true peak | [`ClipOnly2.h`](../Source/Core/ClipOnly2.h), [`HyraxLimiter.h`](../Source/Core/HyraxLimiter.h), [`TruePeak.h`](../Source/Core/TruePeak.h) |
| Tests (synthetic songs through the whole plugin) | [`Tools/flowtest.cpp`](../Tools/flowtest.cpp) |

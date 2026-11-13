# Changelog

Download the plugin from the [Releases page](https://github.com/SlapBackers/MatchMaster/releases).

## v1.0.0 — first release

Semi-real-time Matchering as a VST3 for macOS (Apple Silicon and Intel).

- **Reference**: drop a well-mastered song; it is analysed in the background.
- **Learn**: play your track once; the match is computed and locked.
- **Live, non-destructive**: matching EQ (mid/side, linear phase), loudness match and Matchering's limiter, blended with the **Match** knob (0–100 %).
- **Advanced panel**: matching EQ curve, Mid EQ / Side EQ / Loudness amounts.
- Latency reported to the host (≈ 92 ms at 48 kHz).

Known issues: the limiter works on sample peaks only (exports can show true-peak overs); strong EQ boosts can cause pre-echo; the learned match is not saved with the DAW project.

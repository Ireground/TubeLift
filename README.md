# TubeLift

A one-style saturation VST3: a two-stage tube preamp in the spirit of a 1950s tape machine's
record/playback amplifier (the "A" flavour). It isn't a tape model: no tape compression, no hiss, no head bump.
It's just the tube preamp: warm 2nd-harmonic richness that makes sounds pop forward, and smoother highs as you push it.

## Controls
- **DRIVE** (0–10): how hard the signal hits the tubes. Drag the big number up/down or use the slider. Mouse wheel nudges, double-click resets to 3.0.
  Low settings add mostly 2nd harmonic (warmth, depth). Higher settings add grit and round off harsh highs.
  Level is matched at about −12 dBFS, so turning DRIVE up changes the colour without a big volume jump.
  Loud peaks get squashed, the way a real tube stage does.
- **ATTITUDE** meter: how hard you're hitting the first tube. Around 0 = saturation starts; in the orange = driving it.
- **LOW CUT** (OFF–1 kHz): removes lows *before* the tubes, so bass doesn't turn to mud when driven.
- **TONE** (DARK ↔ BRIGHT): tilt EQ *before* the tubes. Dark = smoother, bright = edgier.
- **HIGH CUT** (1 kHz–OFF): low-pass *after* the tubes, to tame fizz.
- **OUTPUT** (−24 to +12 dB) and **MIX** (dry/wet, latency-compensated for parallel saturation).

Sliders: drag (Shift = fine), mouse wheel nudge, double-click to reset. SIZE menu scales the window.

Under the hood: 4× oversampling (no aliasing), asymmetric triode curves, power-supply sag, interstage Miller roll-off
that softens the top end more as you add drive, and an output-transformer softening shelf.

## Build
GitHub Actions builds the Windows VST3 on every push (Actions tab → latest run → `TubeLift-VST3-Windows`).

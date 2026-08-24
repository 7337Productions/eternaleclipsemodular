# Changelog

## 2.9.0 (2026-08-20)

- New module: Litany Engine — looping sample player with a fixed bank of
  onboard loops (Buddha Machine ethos: no file loading, the shipped
  litany is the instrument) and a focused Morphagene-style granular
  layer: through-zero tape varispeed, grain size from whole-loop down
  to 15 ms, morphing overlap to 8x, scan offset and texture spray with
  stereo pan spread. Loop display with playhead, TURN/TRIG loop
  advance, EOC pulse output, CV with attenuverters on the five main
  controls. Loops decode once on a background thread and are shared by
  all instances. Ships with ten branded loops. Hybrid banks:
  "Load sample folder" swaps in a user folder of WAVs per instance
  (worker-thread decode, atomic swap, missing-folder states, path saved
  in the patch), "Restore onboard litany" returns; strict guardrails
  (99 files, 60 s/file truncation, 192 MB/bank, int16 storage) bound
  memory against arbitrary folders.
- Vendored dr_wav v0.14.6 under dep/ (public domain / MIT-0, see
  dep/dr_wav/PATCHES.md).

- Liminal Vast, the hugeness pass: diffusion allpasses inside the
  feedback loop (DENSITY now thickens the tail itself, not just the
  input), doubled maximum room size, a second slower LFO per line for
  a washier shimmer, DECAY that pins to infinity at the top of the
  knob, and a FREEZE button with gate input that holds the tail at
  unity feedback. Note: existing patches will sound larger at the same
  SIZE setting — the whole knob range grew upward.
- Liminal Vast display is now alive (GitHub #1): the eclipse's corona
  glows with the tail's energy and reaches with SIZE, and embers drift
  continuously from the rim, swelling with the incoming signal level —
  rate from DENSITY, travel from SIZE, lifetime from DECAY; frozen
  embers orbit the disc.
- Liminal Vast fixes: delay reads clamped for engine rates above
  192 kHz, DENSITY sweeps through zero without a click, reset now
  clears all modulation and smoothing state, and sample-rate changes
  clear the tail instead of warping it.

## 2.8.0 (2026-08-19)

- New module: Coronal Annihilator — stereo synth voice. CORE/FLARE dual
  oscillator (through-zero and exponential FM, soft/hard sync, shape
  morph, phase warp, wavefolding, mix and stereo spread) blended with
  the external stereo input under CV, into an onboard neural amp model player (.nam files,
  one network per channel, mono-network and loudness-normalization
  options, load from panel, menu or file drop), into a saturating
  4-pole lowpass with 12/24 dB slopes, resonance and key tracking.
- Vendored NeuralAmpModelerCore v0.5.4, Eigen and nlohmann/json under
  dep/ (see dep/NeuralAmpModelerCore/PATCHES.md).
- Performance pass: fast neural activations (default on, menu toggle,
  halves standard-WaveNet cost at 50+ dB accuracy), knob-derived
  gains/pitches memoized off the per-sample path, stress suite
  covering NaN/Inf CV, six engine rates, 600 model-swap cycles and
  a modulation soak with zero underruns or memory growth.
- Shared widgets: two-position horizontal toggle (eclipse::CKSSHorizontal).
- Neural stage: LEVEL replaced by a WET/DRY blend whose dry arm rides
  the same converter pipeline as the wet signal (latency-aligned, no
  comb filtering at partial blends); DRIVE and WET/DRY gain CV inputs.

## 2.7.1 (2026-07-10)

- Legibility pass at 100% zoom: dim ash text retired plugin-wide (it now
  colors glyphs and graphics only) — patch-bay rows, column headers, and
  small-knob labels all read at the standard 9px cream; label offsets now
  track knob radius.
- Elemental Revelator: DAEMON knob crowned with the Abyssal Harmonics
  seal (double red ring, stamp-red label), pentagram ribbons in title
  gold, patch bay recentered in its frame, output separator moved off
  the bottom jack row.
- Cosmic Clock zodiac rings and Sephirothic Modulator Tree of Life
  paths brightened to the same title gold.
- Moon Phase Distortion: phase name enlarged and centered between the
  moon and the controls; Liminal Vast corona raised clear of its frame.
- Elemental Revelator hardening: NaN CV from upstream modules can no
  longer crash Rack (two out-of-bounds paths fixed) or permanently
  silence the voice; wavetable edge-rounding guard; small CPU reduction.

## 2.7.0 (2026-07-09)

- Brand refresh: all panels move from amber-on-violet to the Church of
  the Eternal Eclipse copper-on-abyss palette (pure black panels,
  copper-bright titles, cream labels, ash secondary text).
- New ETERNAL ⊙ ECLIPSE brand mark flush against the bottom edge of
  every panel (eclipse sigil only on the 8HP Moon Phase Distortion).
- Each module gains a chamber glyph centered in a new fading header
  divider: Saros crescent, Cosmic Clock sun, Moon Phase half-disc,
  Liminal Vast dotted circle, Sephirothic Modulator concave diamond,
  Elemental Revelator triangle.
- The "ETERNAL ECLIPSE" header subtitle is retired — the brand name
  moved to the bottom mark, leaving a clean title with the glyph
  divider tucked beneath it.
- Corner brackets on section frames, echoing the Church card motif.

## 2.6.2 (2026-07-09)

- Elemental Revelator: pentagram drawn in gold (modifier corner-lines
  stay violet), WT knobs are now trimpots seated on the ring beside
  their elements, element glyphs redrawn in white inside the star's
  arms with SPIRIT titled above its knob, and OMEN/CUTOFF enlarged to
  match the oscillator pitch knobs.

## 2.6.1 (2026-07-09)

- Elemental Revelator: panel rework — all ten knobs on one ring around
  the pentagram (elements at the points, modifiers between them), the
  pentagram redrawn as a woven double-line star, modifier lines into
  the inner corners, spirit circle glyph, background frame removed.

## 2.6.0 (2026-07-09)

- New module: Elemental Revelator — four-element wavetable synth voice.
  AIR/WATER/EARTH/FIRE oscillators on a pentagram with per-element WT
  knobs, OFFERING scan character, SIGIL elemental-ring cross-modulation,
  RITE opposing-pair ring mod, VEIL detune/width, SPIRIT chord mode
  (24 chromatic voicings, AIR as live root), 72 DAEMON voice algorithms
  with CV scan, 64-step OMEN variation, 4-pole ladder lowpass filter,
  and stereo delay/diffusion — 16 CV inputs with attenuverters.

## 2.5.0 (2026-07-09)

Initial VCV Library release.

- Five modules: Moon Phase Distortion, Liminal Vast Reverb, Saros,
  Sephirothic Modulator, Cosmic Clock.

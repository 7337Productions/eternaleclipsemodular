# Litany Engine — loop bank

These WAV files ARE the Litany Engine's sample content. The module has no file
dialog and no drag-and-drop by design (Gristleism ethos): the shipped loops are
the instrument. The current files are synthesized placeholders from
`tools/gen_litany_placeholders.py` — replace them with the final branded loops.

## Drop-in contract

- **Replace the files in this folder; nothing else changes.** They ship
  automatically (`DISTRIBUTABLES += res` in the Makefile) and the module
  enumerates this folder at startup.
- **Naming:** `NN-name.wav`. `NN` (two digits) sets the loop order; `name`
  becomes the on-panel display text (uppercased, hyphens shown as spaces), e.g.
  `02-iron-psalm.wav` → `02 · IRON PSALM`.
- **Format:** stereo WAV, 16- or 24-bit PCM. 48 kHz preferred, but any sample
  rate works — the engine resamples at playback. Mono files are accepted and
  duplicated to both channels.
- **Loops must be seamless.** The engine crossfades the loop point (~10 ms), so
  an exact zero-crossing splice is not required, but the material should be
  loop-intentional (no hard cut mid-phrase).
- **Length:** recommended ≤ 20 s per loop. Loops are decoded to RAM once and
  shared across module instances; ~10 loops × 15 s stereo ≈ 45 MB, which is
  fine. If the final set gets much longer, `SampleBank.hpp` documents an int16
  storage fallback that halves memory.

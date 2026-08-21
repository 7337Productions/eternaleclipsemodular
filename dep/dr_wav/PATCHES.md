# dr_wav — local vendoring notes

- Upstream: https://github.com/mackron/dr_libs (`dr_wav.h` only)
- Version: v0.14.6, upstream commit `ee93752fec1274be42dda95baabcd9d9fb8efc29`
  (fetched 2026-08-20)
- License: public domain / MIT-0 (dual, our choice); the full license block from
  the end of the header is extracted into `LICENSE` here and shipped via
  `DISTRIBUTABLES`.

## Local patches

None. The header is vendored unmodified.

## Usage in this plugin

`#define DR_WAV_IMPLEMENTATION` appears exactly once, in
`src/LitanyEngine/LitanyEngine.cpp`. Decoding uses `drwav_init_memory` on a
buffer read with `rack::system::readFile`, so dr_wav never touches the
filesystem (avoids Windows fopen/UTF-8 path issues).

## Upgrading

Re-download `dr_wav.h` from upstream, update the version/commit above, re-extract
the license block into `LICENSE`, and rebuild.

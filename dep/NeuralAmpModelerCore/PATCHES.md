# Vendored NeuralAmpModelerCore

Source: https://github.com/sdatkinson/NeuralAmpModelerCore
Tag: v0.5.4 (commit 1f42f88535884450104b8711d7595019afa0495b), MIT license (see LICENSE).
Only `NAM/` is vendored. Companion headers live in `../eigen` (Eigen, MPL2, commit
bc3b39870ecb690a623a3f49149a358b95c5781d as pinned by NAM Core) and `../json`
(nlohmann/json 3.12.0 single header, MIT).

## Local patch (keep when upgrading)

The VCV Rack plugin toolchain builds macOS x64 with `-mmacosx-version-min=10.9`,
where `std::filesystem` is unavailable (introduced in 10.15). So:

- `NAM/convnet.cpp`, `NAM/convnet.h`, `NAM/dsp.cpp`, `NAM/dsp.h`: removed `#include <filesystem>`.
- `NAM/dsp.h`: removed the `get_dsp_legacy(const std::filesystem::path)` declaration
  (it had no definition in this tag).
- `NAM/get_dsp.h` / `NAM/get_dsp.cpp`: removed the two `get_dsp(const std::filesystem::path, ...)`
  overloads. The plugin reads the `.nam` file itself and calls `get_dsp(const nlohmann::json&, ...)`.

Nothing else is modified. Verify after any upgrade:

    grep -rn "filesystem" dep/NeuralAmpModelerCore/NAM   # must print nothing

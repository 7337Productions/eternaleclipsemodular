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

- `NAM/activations.cpp`, `NAM/get_dsp.cpp`: `std::optional::value()` replaced
  with `(*opt)` dereference (six sites, all `has_value()`-guarded). Apple's SDK
  marks `value()` unavailable before macOS 10.13 because its throwing path
  needs OS support; `operator*` has no availability gate.

- `NAM/get_dsp.h`: `#undef major` / `#undef minor` before `class Version`. glibc
  on the toolchain's Linux target defines these as function-like macros (via
  `<sys/types.h>`), which mangle the `major(major)` / `minor(minor)` member
  initializers into `gnu_dev_minor(...)` and break the lin-x64 build.

Nothing else is modified. Verify after any upgrade:

    grep -rn "filesystem" dep/NeuralAmpModelerCore/NAM        # must print nothing
    grep -rn "\.value()" dep/NeuralAmpModelerCore/NAM | grep -v value_or   # must print nothing
    grep -rn "undef major" dep/NeuralAmpModelerCore/NAM/get_dsp.h          # must print one line

(CI now builds all four Library platforms on every push — that is the
authoritative check; the greps are a quick local pre-flight.)

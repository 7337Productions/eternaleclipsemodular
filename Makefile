RACK_DIR ?= ./Rack-SDK

# dep/ holds vendored third-party sources (NAM Core, Eigen, nlohmann/json).
# Rack's dep.mk would otherwise `mkdir -p dep` and `cleandep` would rm -rf it.
DEP_LOCAL := build/dep

FLAGS += -Isrc
FLAGS += -std=c++17
# Neural amp model engine: float samples, MPL2-only Eigen; -isystem keeps
# third-party warnings out of our build output.
FLAGS += -DNAM_SAMPLE_FLOAT -DEIGEN_MPL2_ONLY -DNAM_ENABLE_A2_FAST
FLAGS += -isystem dep/eigen -isystem dep/json -isystem dep/NeuralAmpModelerCore
# Litany Engine sample decoding: single-header dr_wav (public domain / MIT-0)
FLAGS += -isystem dep/dr_wav
SOURCES += $(wildcard src/*.cpp) $(wildcard src/*/*.cpp)
SOURCES += $(wildcard dep/NeuralAmpModelerCore/NAM/*.cpp) $(wildcard dep/NeuralAmpModelerCore/NAM/*/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)
DISTRIBUTABLES += dep/NeuralAmpModelerCore/LICENSE dep/NeuralAmpModelerCore/PATCHES.md
DISTRIBUTABLES += dep/eigen/COPYING.MPL2 dep/eigen/COPYING.README dep/json/LICENSE.MIT
DISTRIBUTABLES += dep/dr_wav/LICENSE dep/dr_wav/PATCHES.md

include $(RACK_DIR)/plugin.mk

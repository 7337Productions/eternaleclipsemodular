RACK_DIR ?= ./Rack-SDK

FLAGS += -Isrc
FLAGS += -std=c++17
SOURCES += $(wildcard src/*.cpp) $(wildcard src/*/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

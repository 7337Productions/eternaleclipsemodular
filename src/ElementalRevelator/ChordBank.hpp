#pragma once
#include <rack.hpp>

// SPIRIT chord bank: 4-voice chromatic voicings, semitone offsets from the
// root in element order {AIR, WATER, EARTH, FIRE}. AIR is always the root.
// Ordered stable -> tense -> open so knob/CV sweeps feel musical.
struct SpiritChord {
	const char* name;
	float offsets[4]; // AIR, WATER, EARTH, FIRE
};

static const SpiritChord kChords[24] = {
	{"Octaves",     {0.f, 12.f, -12.f, 24.f}},
	{"Fifth Drone", {0.f, 7.f, 12.f, 19.f}},
	{"Major",       {0.f, 4.f, 7.f, 12.f}},
	{"Minor",       {0.f, 3.f, 7.f, 12.f}},
	{"Major Open",  {0.f, 7.f, 16.f, 24.f}},
	{"Minor Open",  {0.f, 7.f, 15.f, 24.f}},
	{"Major 6",     {0.f, 4.f, 7.f, 9.f}},
	{"Minor 6",     {0.f, 3.f, 7.f, 9.f}},
	{"Major 7",     {0.f, 4.f, 7.f, 11.f}},
	{"Minor 7",     {0.f, 3.f, 7.f, 10.f}},
	{"Dominant 7",  {0.f, 4.f, 7.f, 10.f}},
	{"Dom 7 Shell", {0.f, 10.f, 16.f, 24.f}},
	{"Sus2",        {0.f, 2.f, 7.f, 12.f}},
	{"Sus4",        {0.f, 5.f, 7.f, 12.f}},
	{"7Sus4",       {0.f, 5.f, 10.f, 17.f}},
	{"Add9",        {0.f, 4.f, 7.f, 14.f}},
	{"Minor Add9",  {0.f, 3.f, 7.f, 14.f}},
	{"Major 9",     {0.f, 4.f, 11.f, 14.f}},
	{"Minor 9",     {0.f, 3.f, 10.f, 14.f}},
	{"Half-Dim",    {0.f, 3.f, 6.f, 10.f}},
	{"Dim 7",       {0.f, 3.f, 6.f, 9.f}},
	{"Augmented",   {0.f, 4.f, 8.f, 12.f}},
	{"Quartal",     {0.f, 5.f, 10.f, 15.f}},
	{"Quintal",     {0.f, 7.f, 14.f, 21.f}},
};

struct SpiritQuantity : rack::ParamQuantity {
	std::string getDisplayValueString() override {
		int i = rack::math::clamp((int)std::round(getValue()), 0, 23);
		return rack::string::f("%d \xC2\xB7 %s", i, kChords[i].name);
	}
};

#pragma once
#include "WavetableBank.hpp"

namespace dsputil {

struct WavetableOsc {
	float phase = 0.f;

	// Process one sample
	// freq: frequency in Hz
	// morphPos: 0..1 wavetable position
	// bank: which wavetable bank (0-3)
	// sampleRate: current sample rate
	float process(float freq, float morphPos, int bank,
	              float sampleRate, const WavetableBank& wavetables) {
		// Clamp morph position
		morphPos = morphPos < 0.f ? 0.f : (morphPos > 1.f ? 1.f : morphPos);

		// Select mip level based on frequency
		int mip = WavetableBank::mipFromFreq(freq, sampleRate);

		// Read from wavetable
		float sample = wavetables.read(bank, morphPos, mip, phase);

		// Advance phase
		float phaseInc = freq / sampleRate;
		phase += phaseInc;
		phase -= std::floor(phase);

		return sample;
	}

	// Process with phase modulation applied externally
	float processWithPhaseMod(float freq, float morphPos, int bank,
	                          float sampleRate, const WavetableBank& wavetables,
	                          float phaseMod) {
		morphPos = morphPos < 0.f ? 0.f : (morphPos > 1.f ? 1.f : morphPos);
		int mip = WavetableBank::mipFromFreq(freq, sampleRate);

		// Apply phase modulation
		float readPhase = phase + phaseMod;
		readPhase -= std::floor(readPhase);

		float sample = wavetables.read(bank, morphPos, mip, readPhase);

		// Advance phase (unmodulated)
		float phaseInc = freq / sampleRate;
		phase += phaseInc;
		phase -= std::floor(phase);

		return sample;
	}

	void reset() {
		phase = 0.f;
	}
};

} // namespace dsputil

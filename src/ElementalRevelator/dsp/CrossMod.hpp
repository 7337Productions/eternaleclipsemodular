#pragma once
#include <cmath>
#include "../util/Interpolation.hpp"

namespace dsputil {

struct CrossMod {
	enum Mode {
		FM,          // Frequency modulation
		PM,          // Phase modulation
		WAVEFOLD_FM  // Wavefolding + FM combined
	};

	// Process frequency modulation
	// warpAmount: -1..+1 (controls depth)
	// modSource: the other oscillator's output (-1..+1)
	// baseFreq: base frequency of the modulated oscillator
	// Returns frequency offset to add to base frequency
	static float processFreqMod(float warpAmount, float modSource, float baseFreq) {
		if (warpAmount <= 0.f) return 0.f;
		// Exponential scaling for musical FM depth
		float depth = warpAmount * warpAmount * 4.f; // 0..4 octaves of FM
		return modSource * baseFreq * depth;
	}

	// Process phase modulation
	// warpAmount: -1..+1 (negative = PM active)
	// modSource: the other oscillator's output
	// Returns phase offset (0..1 range, wrapping handled by caller)
	static float processPhaseMod(float warpAmount, float modSource) {
		if (warpAmount >= 0.f) return 0.f;
		float depth = warpAmount * warpAmount * 2.f; // 0..2 radians (in 0..1 phase space)
		return modSource * depth;
	}

	// Wavefolding for deep modulation
	// input: audio signal
	// foldAmount: 0..1
	static float wavefold(float input, float foldAmount) {
		if (foldAmount <= 0.f) return input;

		float gain = 1.f + foldAmount * 4.f; // 1x to 5x gain into folder
		float x = input * gain;

		// Triangle fold: maps any value to -1..+1
		// using 4*abs(x/4 - floor(x/4 + 0.5)) - 1
		x = x * 0.25f;
		x = x - std::floor(x + 0.5f);
		x = std::fabs(x) * 4.f - 1.f;

		return x;
	}

	// Combined cross-modulation based on WARP knob position
	// warpAmount: -1..+1
	//   negative: phase modulation (deeper as more negative)
	//   zero: no modulation
	//   positive: FM (deeper as more positive)
	//   extreme values (>0.7 or <-0.7): adds wavefolding
	struct CrossModResult {
		float freqOffset;    // Add to base frequency for FM
		float phaseMod;      // Add to phase for PM
		float foldAmount;    // Amount of wavefolding to apply to output
	};

	static CrossModResult process(float warpAmount, float modSource, float baseFreq) {
		CrossModResult result = {0.f, 0.f, 0.f};

		if (warpAmount > 0.f) {
			// FM mode
			result.freqOffset = processFreqMod(warpAmount, modSource, baseFreq);
			// Add wavefolding at extreme settings
			if (warpAmount > 0.7f) {
				result.foldAmount = (warpAmount - 0.7f) / 0.3f;
			}
		} else if (warpAmount < 0.f) {
			// PM mode
			result.phaseMod = processPhaseMod(warpAmount, modSource);
			// Add wavefolding at extreme settings
			if (warpAmount < -0.7f) {
				result.foldAmount = (-warpAmount - 0.7f) / 0.3f;
			}
		}

		return result;
	}
};

} // namespace dsputil

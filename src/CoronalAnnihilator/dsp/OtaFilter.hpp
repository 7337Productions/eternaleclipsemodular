#pragma once
#include <cmath>
#include "Util.hpp"

namespace corona {

// Coefficients shared by the two stereo channels; computed once per sample.
struct OtaCoeffs {
	float G = 0.f;         // g/(1+g) at the 2x-oversampled rate
	float k = 0.f;         // resonance feedback gain
	float lastCutoff = -1.f;
	float lastSampleRate = -1.f;

	// cutoff in Hz; res 0..1
	void update(float cutoff, float res, float sampleRate) {
		cutoff = clampf(cutoff, 20.f, sampleRate * 0.45f);
		res = clampf(res, 0.f, 1.f);
		if (cutoff != lastCutoff || sampleRate != lastSampleRate) {
			lastCutoff = cutoff;
			lastSampleRate = sampleRate;
			float g = std::tan((float)M_PI * cutoff / (sampleRate * 2.f));
			G = g / (1.f + g);
		}
		k = 4.3f * res; // self-oscillation from ~res 0.93
	}
};

// 4-pole OTA-cascade lowpass: four TPT one-pole stages, each with a soft
// saturating differential input (the OTA transconductance curve), global
// resonance feedback around all four poles, 2x oversampled. Output is
// tapped after stage 2 (12 dB/oct) or stage 4 (24 dB/oct); the loop always
// runs around four poles so both slopes share the same resonance feel.
// Signals are in +/-1 units.
struct OtaFilter {
	float s[4] = {};
	float y2 = 0.f;
	float y4 = 0.f;

	float process(float input, const OtaCoeffs& c, bool fourPole) {
		input = sanitize(input);
		// Half passband-loss compensation: bass survives high resonance
		// while keeping the characteristic level squeeze
		float comp = 1.f + 0.5f * c.k;
		for (int os = 0; os < 2; os++) {
			// Loop-input saturation bounds self-oscillation; the 0.5/2 scaling
			// keeps a nominal +/-1 signal within ~0.6 dB of clean
			float u = 2.f * softClip(0.5f * (input * comp - c.k * y4));
			for (int i = 0; i < 4; i++) {
				float v = c.G * softClip(u - s[i]);
				float y = s[i] + v;
				s[i] = y + v;
				u = y;
				if (i == 1) y2 = y;
			}
			y4 = u;
		}
		if (!std::isfinite(y4)) {
			reset();
			return 0.f;
		}
		return fourPole ? y4 : y2;
	}

	void reset() {
		s[0] = s[1] = s[2] = s[3] = 0.f;
		y2 = y4 = 0.f;
	}
};

} // namespace corona

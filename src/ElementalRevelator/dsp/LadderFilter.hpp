#pragma once
#include <cmath>
#include "../util/Interpolation.hpp"

namespace dsputil {

// 4-pole transistor-ladder lowpass. TPT one-pole cascade with global
// resonance feedback (one-sample delay at the 2x-oversampled rate) and
// soft saturation at the input and between stages.
struct LadderFilter {
	float s[4] = {};              // stage states
	float y4 = 0.f;               // last stage output (feedback source)
	float lastCutoff = -1.f;
	float lastSampleRate = -1.f;
	float G = 0.f;                // g/(1+g) at the oversampled rate

	// cutoff in Hz; res 0..1.05 (>1 self-oscillates)
	float process(float input, float cutoff, float res, float sampleRate) {
		cutoff = clampf(cutoff, 20.f, sampleRate * 0.45f);
		res = clampf(res, 0.f, 1.05f);
		if (cutoff != lastCutoff || sampleRate != lastSampleRate) {
			lastCutoff = cutoff;
			lastSampleRate = sampleRate;
			float g = std::tan((float)M_PI * cutoff / (sampleRate * 2.f));
			G = g / (1.f + g);
		}
		float k = 4.f * res;
		// Half passband-loss compensation: bass survives high resonance
		// while keeping the characteristic level squeeze
		float comp = 1.f + 0.5f * k;

		float out = y4;
		for (int os = 0; os < 2; os++) {
			// The only nonlinearity sits at the loop input: enough to bound
			// self-oscillation without eating the passband
			float x = input * comp - k * y4;
			x = softClip(x * 0.8f) * 1.25f;
			x += 1e-12f; // denormal guard seeds the chain
			float y = x;
			for (int i = 0; i < 4; i++) {
				float v = (y - s[i]) * G;
				y = s[i] + v;
				s[i] = y + v;
			}
			y4 = y;
			out = y;
		}
		return out;
	}

	void reset() {
		s[0] = s[1] = s[2] = s[3] = 0.f;
		y4 = 0.f;
		lastCutoff = -1.f;
	}
};

} // namespace dsputil

#pragma once
#include <cmath>
#include <dsp/resampler.hpp>
#include "Util.hpp"

namespace corona {

// Dual oscillator engine. CORE (A) is a simple sine/triangle/saw that can
// frequency-modulate and sync FLARE (B). FLARE morphs sine->tri->saw->pulse
// (PLASMA), is phase-warped (ARC) and wavefolded (EJECTA). Everything runs
// 4x oversampled and is decimated per oscillator, which covers the aliasing
// from FM, sync, folding and phase distortion with one mechanism.
struct ComplexOsc {
	static constexpr int OS = 4;

	struct Params {
		float freqA = 110.f;   // Hz
		float freqB = 110.f;   // Hz
		int waveA = 0;         // 0 sine, 1 triangle, 2 saw
		float shape = 0.f;     // PLASMA 0..1
		float fold = 0.f;      // EJECTA 0..1
		float arc = 0.f;       // ARC 0..1
		float fm = 0.f;        // SURGE 0..1 (index of A -> B modulation)
		bool fmExp = false;    // false linear through-zero, true exponential
		int sync = 0;          // LOCK 0 off, 1 soft, 2 hard
	};

	float phaseA = 0.f;
	float phaseB = 0.f;
	float dirB = 1.f;          // soft sync flips FLARE's direction
	DCBlocker dcB;
	rack::dsp::Decimator<OS, 8> decA;
	rack::dsp::Decimator<OS, 8> decB;

	// Outputs are +/-1. sampleRate is the engine rate.
	void process(const Params& p, float sampleRate, float& outA, float& outB) {
		float osRate = sampleRate * OS;
		float fA = clampf(p.freqA, 0.1f, sampleRate * 0.45f);
		float fB = clampf(p.freqB, 0.1f, sampleRate * 0.45f);
		float shape = clampf(p.shape, 0.f, 1.f);
		float fold = clampf(p.fold, 0.f, 1.f);
		float arc = clampf(p.arc, 0.f, 1.f);
		float fm = clampf(p.fm, 0.f, 1.f);
		float incA = fA / osRate;
		// Phase-warp knee: 0.5 (none) -> 0.02 (extreme)
		float knee = 0.5f - 0.48f * arc;
		float foldGain = 1.f + 5.f * fold;

		float bufA[OS];
		float bufB[OS];
		for (int i = 0; i < OS; i++) {
			// ----- CORE -----
			float a = coreWave(phaseA, p.waveA);
			phaseA += incA;
			bool wrapped = false;
			float overshoot = 0.f;
			if (phaseA >= 1.f) {
				phaseA -= 1.f;
				wrapped = true;
				overshoot = phaseA / incA; // fraction of this step after the wrap
			}
			bufA[i] = a;

			// ----- FLARE frequency (FM from CORE) -----
			float fBeff = fB;
			if (fm > 0.f) {
				if (p.fmExp)
					fBeff = fB * std::exp2(3.f * fm * a);
				else
					fBeff = fB * (1.f + 4.f * fm * a); // through-zero
			}
			float incB = dirB * fBeff / osRate;
			incB = clampf(incB, -0.45f, 0.45f);

			// ----- LOCK (sync) -----
			if (wrapped && p.sync == 2) {
				phaseB = overshoot * incB;
			}
			else if (wrapped && p.sync == 1) {
				dirB = -dirB;
				incB = -incB;
			}
			phaseB += incB;
			phaseB -= std::floor(phaseB);

			// ----- ARC (phase warp) -----
			float ph = phaseB < knee
				? 0.5f * phaseB / knee
				: 0.5f + 0.5f * (phaseB - knee) / (1.f - knee);

			// ----- PLASMA (morph) + EJECTA (fold) -----
			float b = morph(ph, shape);
			if (fold > 0.f)
				b = triFold(b * foldGain);
			bufB[i] = b;
		}
		outA = decA.process(bufA);
		outB = dcB.process(decB.process(bufB));
	}

	void reset() {
		phaseA = phaseB = 0.f;
		dirB = 1.f;
		dcB.reset();
		decA.reset();
		decB.reset();
	}

private:
	static float coreWave(float ph, int wave) {
		switch (wave) {
			case 1: return 1.f - 4.f * std::fabs(ph - 0.5f);          // triangle
			case 2: return 2.f * ph - 1.f;                           // saw
			default: return std::sin(2.f * (float)M_PI * ph);        // sine
		}
	}

	// shape 0..1: sine -> triangle -> saw -> pulse
	static float morph(float ph, float shape) {
		float sine = std::sin(2.f * (float)M_PI * ph);
		float tri = 1.f - 4.f * std::fabs(ph - 0.5f);
		float saw = 2.f * ph - 1.f;
		float pulse = ph < 0.5f ? 1.f : -1.f;
		float t = shape * 3.f;
		if (t < 1.f) return lerp(sine, tri, t);
		if (t < 2.f) return lerp(tri, saw, t - 1.f);
		return lerp(saw, pulse, t - 2.f);
	}

	// Triangle fold: identity on [-1, 1], reflects anything beyond back
	// into range (period-4 triangle through (-1,-1), (1,1), (3,-1) ...).
	static float triFold(float x) {
		float t = (x + 1.f) * 0.25f;
		t = std::fabs(t - std::floor(t + 0.5f));
		return t * 4.f - 1.f;
	}
};

} // namespace corona

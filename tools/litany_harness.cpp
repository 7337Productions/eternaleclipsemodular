// Standalone sanity harness for the Litany Engine granular core.
// Build:  g++ -std=c++17 -O2 -Isrc tools/litany_harness.cpp -o litany_harness
// Sweeps speed/grain/morph/scan/texture and asserts the output stays finite
// and bounded, and that EOC fires when the tape moves.
#include "LitanyEngine/dsp/GranularEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
	const float RATE = 48000.f;
	const size_t FRAMES = 24000; // 0.5 s loop
	std::vector<float> l(FRAMES), r(FRAMES);
	for (size_t i = 0; i < FRAMES; i++) {
		l[i] = 0.7f * std::sin(2.0 * 3.14159265358979 * 220.0 * i / RATE);
		r[i] = 0.7f * std::sin(2.0 * 3.14159265358979 * 331.0 * i / RATE);
	}
	litany::LoopView view{l.data(), r.data(), FRAMES, RATE};

	litany::GranularEngine engine;
	const float speeds[] = {-2.f, -1.f, -0.005f, 0.f, 0.5f, 1.f, 2.f};
	const float knobs[] = {0.f, 0.3f, 0.7f, 1.f};

	int checked = 0;
	for (float speed : speeds)
		for (float grain : knobs)
			for (float morph : knobs)
				for (float scan : knobs)
					for (float texture : knobs) {
						litany::GranularParams p{speed, grain, morph, scan, texture};
						bool sawEoc = false;
						float peak = 0.f, peakLate = 0.f;
						for (int i = 0; i < 24000; i++) {
							auto out = engine.process(view, p, RATE);
							if (!std::isfinite(out.l) || !std::isfinite(out.r)) {
								std::printf("FAIL: non-finite at speed=%g grain=%g morph=%g scan=%g tex=%g\n",
								            speed, grain, morph, scan, texture);
								return 1;
							}
							float mag = std::fmax(std::fabs(out.l), std::fabs(out.r));
							peak = std::fmax(peak, mag);
							if (i >= 2400) // let grains from the previous combo fade out
								peakLate = std::fmax(peakLate, mag);
							sawEoc |= out.eoc;
						}
						// 16 windowed grains of +-0.7 material can stack; anything
						// past this is a normalization bug
						if (peak > 8.f) {
							std::printf("FAIL: peak %.2f at speed=%g grain=%g morph=%g scan=%g tex=%g\n",
							            peak, speed, grain, morph, scan, texture);
							return 1;
						}
						if (std::fabs(speed) >= 1.f && !sawEoc) {
							std::printf("FAIL: no EOC at speed=%g (0.5 s over 0.5 s loop)\n", speed);
							return 1;
						}
						if (std::fabs(speed) < 0.01f && peakLate > 0.05f) {
							std::printf("FAIL: stopped tape not silent (peak %.3f)\n", peakLate);
							return 1;
						}
						checked++;
					}

	// Through-zero regression (OMN-90 bug): sweep +1 -> -1 -> +1 continuously;
	// grains must follow the live speed, so output stays alive on both sides.
	{
		litany::GranularEngine e;
		// Returns the zero-crossing rate (Hz) of the last quarter of the segment:
		// the 220 Hz test tone gives ~440/s at |speed| = 1, and the old engine's
		// frozen-at-spawn grains crawl at ~0.02x after a zero crossing (~9/s).
		auto zcrOver = [&](float from, float to, int samples) {
			int tail = samples / 4, crossings = 0;
			float prev = 0.f;
			for (int i = 0; i < samples; i++) {
				float sp = from + (to - from) * (float)i / samples;
				auto out = e.process(view, {sp, 0.f, 0.f, 0.f, 0.f}, RATE);
				if (i >= samples - tail && (out.l > 0.f) != (prev > 0.f))
					crossings++;
				prev = out.l;
			}
			return crossings * RATE / tail;
		};
		zcrOver(1.f, 1.f, 24000);                 // settle at 1x
		zcrOver(1.f, -1.f, 96000);                // sweep down through zero over 2 s
		float b = zcrOver(-1.f, -1.f, 48000);     // hold reverse: expect ~440/s
		zcrOver(-1.f, 1.f, 96000);                // sweep back up
		float d = zcrOver(1.f, 1.f, 48000);       // hold forward: expect ~440/s
		if (b < 350.f || b > 530.f || d < 350.f || d > 530.f) {
			std::printf("FAIL: speed not followed through zero (zcr %.0f/s reverse, %.0f/s forward; want ~440)\n", b, d);
			return 1;
		}
		std::printf("through-zero sweep ok (zcr %.0f/s reverse, %.0f/s forward)\n", b, d);
	}

	// Empty/invalid view must be safe
	litany::LoopView bad;
	for (int i = 0; i < 1000; i++) {
		auto out = engine.process(bad, {1.f, 0.5f, 0.5f, 0.f, 0.f}, RATE);
		if (!std::isfinite(out.l) || !std::isfinite(out.r)) {
			std::printf("FAIL: non-finite on invalid view\n");
			return 1;
		}
	}

	std::printf("OK: %d param combinations clean\n", checked);
	return 0;
}

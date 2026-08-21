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

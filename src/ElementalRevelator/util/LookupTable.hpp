#pragma once
#include <cmath>

namespace dsputil {

struct SinTable {
	static constexpr int SIZE = 4096;
	float table[SIZE + 1];

	SinTable() {
		for (int i = 0; i <= SIZE; i++) {
			table[i] = std::sin(2.f * M_PI * i / SIZE);
		}
	}

	// phase: 0..1
	float lookup(float phase) const {
		phase -= std::floor(phase);
		float idx = phase * SIZE;
		int i0 = (int)idx;
		float frac = idx - i0;
		return table[i0] + frac * (table[i0 + 1] - table[i0]);
	}

	float cos(float phase) const {
		return lookup(phase + 0.25f);
	}
};

// Global instance
inline const SinTable sinTable;

} // namespace dsputil

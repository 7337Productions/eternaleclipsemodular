#pragma once
#include <cmath>

// Small numeric helpers for Coronal Annihilator. Local copy by convention
// (each module carries its own util header so modules stay self-contained).
namespace corona {

inline float lerp(float a, float b, float t) {
	return a + t * (b - a);
}

// Soft clipping using tanh approximation
inline float softClip(float x) {
	if (x > 3.f) return 1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Clamp value to range. NaN-safe: a NaN input lands on lo instead of
// passing through. clampf guards every CV entry point, so this is the
// firewall that keeps NaN from upstream modules out of phase/index/state
// math (where an (int) cast of NaN becomes INT_MIN and indexes wild).
inline float clampf(float x, float lo, float hi) {
	return x >= lo ? (x <= hi ? x : hi) : lo;
}

// Audio-path firewall: NaN/Inf becomes silence instead of a full-scale
// step (clampf) or a value that lodges in filter / neural-net state.
inline float sanitize(float x) {
	return std::isfinite(x) ? x : 0.f;
}

inline float dbToGain(float db) {
	return std::pow(10.f, db * 0.05f);
}

// DC blocker filter
struct DCBlocker {
	float xPrev = 0.f;
	float yPrev = 0.f;

	float process(float x) {
		float y = x - xPrev + 0.9995f * yPrev;
		xPrev = x;
		yPrev = y;
		return y;
	}
	void reset() { xPrev = yPrev = 0.f; }
};

} // namespace corona

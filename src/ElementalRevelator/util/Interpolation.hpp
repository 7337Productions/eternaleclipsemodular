#pragma once

namespace dsputil {

// Linear interpolation
inline float lerp(float a, float b, float t) {
	return a + t * (b - a);
}

// Hermite cubic interpolation for smoother wavetable reading
inline float hermite(float y0, float y1, float y2, float y3, float t) {
	float c0 = y1;
	float c1 = 0.5f * (y2 - y0);
	float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
	float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
	return ((c3 * t + c2) * t + c1) * t + c0;
}

// Soft clipping using tanh approximation
inline float softClip(float x) {
	if (x > 3.f) return 1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Clamp value to range
inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
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
};

} // namespace dsputil

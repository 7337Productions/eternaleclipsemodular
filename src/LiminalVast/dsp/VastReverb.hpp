#pragma once
#include <cmath>
#include <cstring>

namespace liminal {

inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

// Soft clipping using tanh approximation
inline float softClip(float x) {
	if (x > 3.f) return 1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Lush stereo reverb: input diffusion into an 8-line feedback delay network
// with a Householder feedback matrix and LFO-modulated read taps
// (chorused, shimmering tail).
struct VastReverb {
	static constexpr int NUM_LINES = 8;
	static constexpr int LINE_LEN = 32768; // 149ms base + mod headroom at 192kHz
	static constexpr int LINE_MASK = LINE_LEN - 1;
	static constexpr int PRE_LEN = 65536; // 250ms predelay at 192kHz
	static constexpr int PRE_MASK = PRE_LEN - 1;
	static constexpr int NUM_AP = 4;
	static constexpr int AP_LEN = 2048; // 8.3ms allpass at 192kHz
	static constexpr int AP_MASK = AP_LEN - 1;

	// Prime-ish base delay times (ms) at size = 1
	static constexpr float baseMs[NUM_LINES] = {41.f, 53.f, 67.f, 79.f, 97.f, 113.f, 131.f, 149.f};
	// Per-line LFO rate ratios for decorrelated modulation
	static constexpr float modRatios[NUM_LINES] = {1.f, 1.13f, 0.91f, 1.27f, 0.85f, 1.41f, 0.79f, 1.53f};
	// Input diffusion allpass times (ms)
	static constexpr float apMs[NUM_AP] = {2.1f, 3.7f, 5.9f, 8.3f};

	float* lines[NUM_LINES] = {};
	int writePos = 0;

	float* preL = nullptr;
	float* preR = nullptr;
	int preWritePos = 0;

	// Input diffusion allpasses (L uses [0], R uses [1])
	float* apBuf[2][NUM_AP] = {};
	int apWritePos[2][NUM_AP] = {};

	float lfoPhase[NUM_LINES] = {};
	float dampState[NUM_LINES] = {}; // one-pole LP per line (high cut)
	float lineGain[NUM_LINES] = {};
	float lowCutState[2] = {}; // one-pole HP on input

	// Smoothed to avoid zipper/pitch artifacts on knob sweeps
	float sizeSm = -1.f;
	float preSm = 0.f;
	int gainCounter = 0;

	// Smoothing coefficient, cached until the sample rate changes
	float smCoeff = 0.f;
	float smSampleRate = -1.f;

	VastReverb() {
		for (int i = 0; i < NUM_LINES; i++) {
			lines[i] = new float[LINE_LEN];
			std::memset(lines[i], 0, LINE_LEN * sizeof(float));
			lfoPhase[i] = (float)i / NUM_LINES;
		}
		preL = new float[PRE_LEN];
		preR = new float[PRE_LEN];
		std::memset(preL, 0, PRE_LEN * sizeof(float));
		std::memset(preR, 0, PRE_LEN * sizeof(float));
		for (int c = 0; c < 2; c++) {
			for (int i = 0; i < NUM_AP; i++) {
				apBuf[c][i] = new float[AP_LEN];
				std::memset(apBuf[c][i], 0, AP_LEN * sizeof(float));
			}
		}
	}

	~VastReverb() {
		for (int i = 0; i < NUM_LINES; i++)
			delete[] lines[i];
		delete[] preL;
		delete[] preR;
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < NUM_AP; i++)
				delete[] apBuf[c][i];
	}

	VastReverb(const VastReverb&) = delete;
	VastReverb& operator=(const VastReverb&) = delete;

	float processAllpass(int c, int i, float input, int delayLen, float coeff) {
		int readPos = (apWritePos[c][i] - delayLen) & AP_MASK;
		float delayed = apBuf[c][i][readPos];
		float v = input - coeff * delayed;
		apBuf[c][i][apWritePos[c][i]] = v;
		apWritePos[c][i] = (apWritePos[c][i] + 1) & AP_MASK;
		return delayed + coeff * v;
	}

	struct Frame {
		float L, R;
	};

	// Process one stereo sample; returns the wet signal only.
	// preDelaySec: 0..0.25
	// size: 0..1 (room scale)
	// rt60: decay time in seconds
	// density: 0..1 input diffusion
	// modRateHz, modDepth: tap modulation (depth 0..1)
	// lowCutHz: input highpass; highCutHz: loop damping lowpass
	Frame process(float inL, float inR,
	              float preDelaySec, float size, float rt60,
	              float density, float modRateHz, float modDepth,
	              float lowCutHz, float highCutHz, float sampleRate) {
		size = clampf(size, 0.f, 1.f);
		if (sampleRate != smSampleRate) {
			smSampleRate = sampleRate;
			smCoeff = 1.f - std::exp(-1.f / (0.03f * sampleRate));
		}
		float sm = smCoeff;
		if (sizeSm < 0.f)
			sizeSm = size; // first sample: no sweep from zero
		sizeSm += sm * (size - sizeSm);
		preSm += sm * (clampf(preDelaySec, 0.f, 0.25f) - preSm);

		// Predelay
		preL[preWritePos] = inL;
		preR[preWritePos] = inR;
		float preSamples = preSm * sampleRate;
		int pi0 = (int)preSamples;
		float pfrac = preSamples - pi0;
		int pr0 = (preWritePos - pi0) & PRE_MASK;
		int pr1 = (pr0 - 1) & PRE_MASK;
		float dL = preL[pr0] + pfrac * (preL[pr1] - preL[pr0]);
		float dR = preR[pr0] + pfrac * (preR[pr1] - preR[pr0]);
		preWritePos = (preWritePos + 1) & PRE_MASK;

		// Input low cut (one-pole highpass)
		float aLow = clampf(2.f * (float)M_PI * lowCutHz / sampleRate, 0.f, 0.99f);
		lowCutState[0] += aLow * (dL - lowCutState[0]);
		lowCutState[1] += aLow * (dR - lowCutState[1]);
		dL -= lowCutState[0];
		dR -= lowCutState[1];

		// Input diffusion
		float apCoeff = clampf(density, 0.f, 1.f) * 0.72f;
		if (apCoeff > 0.001f) {
			for (int i = 0; i < NUM_AP; i++) {
				int lenL = (int)(apMs[i] * 0.001f * sampleRate);
				int lenR = (int)(apMs[i] * 0.001f * sampleRate * 1.11f);
				if (lenL < 1) lenL = 1;
				if (lenR < 1) lenR = 1;
				if (lenL > AP_MASK) lenL = AP_MASK;
				if (lenR > AP_MASK) lenR = AP_MASK;
				float c = (i % 2 == 0) ? apCoeff : -apCoeff;
				dL = processAllpass(0, i, dL, lenL, c);
				dR = processAllpass(1, i, dR, lenR, c);
			}
		}

		// Line lengths from size (quadratic taper: small rooms get finer control)
		float scale = 0.05f + 0.95f * sizeSm * sizeSm;
		float lineSamples[NUM_LINES];
		for (int i = 0; i < NUM_LINES; i++)
			lineSamples[i] = baseMs[i] * 0.001f * sampleRate * scale;

		// Feedback gains from RT60 (refresh every 64 samples; powf is pricey)
		if (gainCounter <= 0) {
			gainCounter = 64;
			rt60 = clampf(rt60, 0.05f, 120.f);
			for (int i = 0; i < NUM_LINES; i++) {
				float lineSec = lineSamples[i] / sampleRate;
				lineGain[i] = std::pow(0.001f, lineSec / rt60);
			}
		}
		gainCounter--;

		// Modulated tap reads + loop damping
		float depthSamples = clampf(modDepth, 0.f, 1.f) * 0.0035f * sampleRate;
		float aHigh = clampf(2.f * (float)M_PI * highCutHz / sampleRate, 0.f, 0.99f);
		float v[NUM_LINES];
		for (int i = 0; i < NUM_LINES; i++) {
			lfoPhase[i] += modRateHz * modRatios[i] / sampleRate;
			lfoPhase[i] -= std::floor(lfoPhase[i]);
			float mod = depthSamples * std::sin(2.f * (float)M_PI * lfoPhase[i]);
			float readLen = lineSamples[i] + mod;
			if (readLen < 2.f) readLen = 2.f;
			int i0 = (int)readLen;
			float frac = readLen - i0;
			int r0 = (writePos - i0) & LINE_MASK;
			int r1 = (r0 - 1) & LINE_MASK;
			float tap = lines[i][r0] + frac * (lines[i][r1] - lines[i][r0]);
			dampState[i] += aHigh * (tap - dampState[i]);
			v[i] = dampState[i];
		}

		// Decorrelated stereo taps
		Frame out;
		out.L = 0.5f * (v[0] - v[2] + v[4] - v[6]);
		out.R = 0.5f * (v[1] - v[3] + v[5] - v[7]);

		// Householder feedback: y = w - (2/N) * sum(w)
		float w[NUM_LINES];
		float sum = 0.f;
		for (int i = 0; i < NUM_LINES; i++) {
			w[i] = v[i] * lineGain[i];
			sum += w[i];
		}
		float h = 2.f * sum / NUM_LINES;
		for (int i = 0; i < NUM_LINES; i++) {
			float in = (i % 2 == 0) ? dL : dR;
			lines[i][writePos] = softClip(in + w[i] - h);
		}
		writePos = (writePos + 1) & LINE_MASK;

		return out;
	}

	void clear() {
		for (int i = 0; i < NUM_LINES; i++) {
			std::memset(lines[i], 0, LINE_LEN * sizeof(float));
			dampState[i] = 0.f;
		}
		std::memset(preL, 0, PRE_LEN * sizeof(float));
		std::memset(preR, 0, PRE_LEN * sizeof(float));
		for (int c = 0; c < 2; c++) {
			for (int i = 0; i < NUM_AP; i++) {
				std::memset(apBuf[c][i], 0, AP_LEN * sizeof(float));
				apWritePos[c][i] = 0;
			}
		}
		lowCutState[0] = lowCutState[1] = 0.f;
		writePos = 0;
		preWritePos = 0;
		gainCounter = 0;
	}
};

constexpr float VastReverb::baseMs[];
constexpr float VastReverb::modRatios[];
constexpr float VastReverb::apMs[];

} // namespace liminal

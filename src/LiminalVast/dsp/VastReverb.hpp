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
// with a Householder feedback matrix, in-loop diffusion allpasses,
// dual-LFO-modulated read taps (chorused, shimmering tail), and freeze.
struct VastReverb {
	static constexpr int NUM_LINES = 8;
	static constexpr int LINE_LEN = 65536; // 298ms base + mod headroom at 192kHz
	static constexpr int LINE_MASK = LINE_LEN - 1;
	static constexpr int PRE_LEN = 65536; // 250ms predelay at 192kHz
	static constexpr int PRE_MASK = PRE_LEN - 1;
	static constexpr int NUM_AP = 4;
	static constexpr int AP_LEN = 2048; // 8.3ms allpass at 192kHz
	static constexpr int AP_MASK = AP_LEN - 1;
	static constexpr int LOOP_AP_LEN = 8192; // 19.7ms in-loop allpass at 192kHz
	static constexpr int LOOP_AP_MASK = LOOP_AP_LEN - 1;

	// Prime-ish base delay times (ms) at size = 1
	static constexpr float baseMs[NUM_LINES] = {82.f, 106.f, 134.f, 158.f, 194.f, 226.f, 262.f, 298.f};
	// Per-line LFO rate ratios for decorrelated modulation
	static constexpr float modRatios[NUM_LINES] = {1.f, 1.13f, 0.91f, 1.27f, 0.85f, 1.41f, 0.79f, 1.53f};
	// Second, slower LFO per line; incommensurate with modRatios so the
	// aggregate motion reads as shimmer rather than chorus warble
	static constexpr float modRatios2[NUM_LINES] = {0.62f, 0.71f, 0.53f, 0.83f, 0.47f, 0.67f, 0.59f, 0.77f};
	// Input diffusion allpass times (ms)
	static constexpr float apMs[NUM_AP] = {2.1f, 3.7f, 5.9f, 8.3f};
	// In-loop diffusion allpass times (ms); fixed lengths slightly floor the
	// minimum room size but keep size sweeps free of allpass chirp
	static constexpr float loopApMs[NUM_LINES] = {5.9f, 7.9f, 9.7f, 11.3f, 13.9f, 16.3f, 18.1f, 19.7f};

	float* lines[NUM_LINES] = {};
	int writePos = 0;

	float* preL = nullptr;
	float* preR = nullptr;
	int preWritePos = 0;

	// Input diffusion allpasses (L uses [0], R uses [1])
	float* apBuf[2][NUM_AP] = {};
	int apWritePos[2][NUM_AP] = {};

	// In-loop diffusion allpasses, one per FDN line
	float* loopAp[NUM_LINES] = {};
	int loopApPos[NUM_LINES] = {};

	float lfoPhase[NUM_LINES] = {};
	float lfoPhase2[NUM_LINES] = {};
	float dampState[NUM_LINES] = {}; // one-pole LP per line (high cut)
	float lineGain[NUM_LINES] = {};
	float lowCutState[2] = {}; // one-pole HP on input

	// Smoothed to avoid zipper/pitch artifacts on knob sweeps
	float sizeSm = -1.f;
	float preSm = 0.f;
	float frzSm = 0.f;
	int gainCounter = 0;

	// Smoothing coefficient, cached until the sample rate changes
	float smCoeff = 0.f;
	float smSampleRate = -1.f;

	VastReverb() {
		for (int i = 0; i < NUM_LINES; i++) {
			lines[i] = new float[LINE_LEN];
			std::memset(lines[i], 0, LINE_LEN * sizeof(float));
			loopAp[i] = new float[LOOP_AP_LEN];
			std::memset(loopAp[i], 0, LOOP_AP_LEN * sizeof(float));
			lfoPhase[i] = (float)i / NUM_LINES;
			lfoPhase2[i] = (float)((i + 3) % NUM_LINES) / NUM_LINES;
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
		for (int i = 0; i < NUM_LINES; i++) {
			delete[] lines[i];
			delete[] loopAp[i];
		}
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
	// density: 0..1 diffusion (input + in-loop)
	// modRateHz, modDepth: tap modulation (depth 0..1)
	// lowCutHz: input highpass; highCutHz: loop damping lowpass
	// freeze: 0..1 target; holds the tail at unity feedback with input muted
	Frame process(float inL, float inR,
	              float preDelaySec, float size, float rt60,
	              float density, float modRateHz, float modDepth,
	              float lowCutHz, float highCutHz, float freeze, float sampleRate) {
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
		frzSm += sm * (clampf(freeze, 0.f, 1.f) - frzSm);

		// Predelay
		preL[preWritePos] = inL;
		preR[preWritePos] = inR;
		float preSamples = preSm * sampleRate;
		if (preSamples > (float)(PRE_MASK - 2))
			preSamples = (float)(PRE_MASK - 2); // buffer sized for 192kHz; degrade gracefully above
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

		// Input diffusion; always run so density sweeps through zero keep a
		// constant topology (at coeff 0 the allpasses are plain short delays)
		float apCoeff = clampf(density, 0.f, 1.f) * 0.72f;
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

		// Line lengths from size (quadratic taper: small rooms get finer
		// control; 0.025 floor keeps the smallest room where it always was)
		float scale = 0.025f + 0.975f * sizeSm * sizeSm;
		float lineSamples[NUM_LINES];
		int loopApLen[NUM_LINES];
		for (int i = 0; i < NUM_LINES; i++) {
			lineSamples[i] = baseMs[i] * 0.001f * sampleRate * scale;
			int len = (int)(loopApMs[i] * 0.001f * sampleRate);
			if (len < 1) len = 1;
			if (len > LOOP_AP_MASK) len = LOOP_AP_MASK;
			loopApLen[i] = len;
		}

		// Feedback gains from RT60 (refresh every 64 samples; powf is pricey).
		// The 1e9 ceiling admits the "infinite" sentinel (gain -> 1.0).
		if (gainCounter <= 0) {
			gainCounter = 64;
			rt60 = clampf(rt60, 0.05f, 1e9f);
			for (int i = 0; i < NUM_LINES; i++) {
				float lineSec = (lineSamples[i] + loopApLen[i]) / sampleRate;
				lineGain[i] = std::pow(0.001f, lineSec / rt60);
			}
		}
		gainCounter--;

		// Modulated tap reads + loop damping + in-loop diffusion.
		// Two incommensurate sine LFOs per line: deep aggregate motion that
		// reads as shimmer, not chorus warble.
		float depthSamples = clampf(modDepth, 0.f, 1.f) * 0.0035f * sampleRate;
		float aHigh = clampf(2.f * (float)M_PI * highCutHz / sampleRate, 0.f, 0.99f);
		// Freeze eases damping open so the captured spectrum holds
		float aHighEff = aHigh + frzSm * (0.99f - aHigh);
		float loopCoeff = clampf(density, 0.f, 1.f) * 0.6f;
		float v[NUM_LINES];
		for (int i = 0; i < NUM_LINES; i++) {
			lfoPhase[i] += modRateHz * modRatios[i] / sampleRate;
			lfoPhase[i] -= std::floor(lfoPhase[i]);
			lfoPhase2[i] += 0.31f * modRateHz * modRatios2[i] / sampleRate;
			lfoPhase2[i] -= std::floor(lfoPhase2[i]);
			float mod = depthSamples * (std::sin(2.f * (float)M_PI * lfoPhase[i])
				+ 0.5f * std::sin(2.f * (float)M_PI * lfoPhase2[i]));
			float readLen = lineSamples[i] + mod;
			if (readLen < 2.f) readLen = 2.f;
			if (readLen > (float)(LINE_MASK - 2))
				readLen = (float)(LINE_MASK - 2); // buffer sized for 192kHz; degrade gracefully above
			int i0 = (int)readLen;
			float frac = readLen - i0;
			int r0 = (writePos - i0) & LINE_MASK;
			int r1 = (r0 - 1) & LINE_MASK;
			float tap = lines[i][r0] + frac * (lines[i][r1] - lines[i][r0]);
			dampState[i] += aHighEff * (tap - dampState[i]);
			// In-loop allpass: smears both the output taps and the feedback,
			// multiplying echo density every pass through the tank
			float c = (i % 2 == 0) ? loopCoeff : -loopCoeff;
			int rp = (loopApPos[i] - loopApLen[i]) & LOOP_AP_MASK;
			float delayed = loopAp[i][rp];
			float u = dampState[i] - c * delayed;
			loopAp[i][loopApPos[i]] = u;
			loopApPos[i] = (loopApPos[i] + 1) & LOOP_AP_MASK;
			v[i] = delayed + c * u;
		}

		// Decorrelated stereo taps
		Frame out;
		out.L = 0.5f * (v[0] - v[2] + v[4] - v[6]);
		out.R = 0.5f * (v[1] - v[3] + v[5] - v[7]);

		// Householder feedback: y = w - (2/N) * sum(w).
		// Freeze crossfades line gain to unity and mutes the input; the matrix
		// is unitary and softClip bounds every write, so this is stable.
		float inGain = 1.f - frzSm;
		float w[NUM_LINES];
		float sum = 0.f;
		for (int i = 0; i < NUM_LINES; i++) {
			float g = lineGain[i] + frzSm * (1.f - lineGain[i]);
			w[i] = v[i] * g;
			sum += w[i];
		}
		float h = 2.f * sum / NUM_LINES;
		for (int i = 0; i < NUM_LINES; i++) {
			float in = ((i % 2 == 0) ? dL : dR) * inGain;
			lines[i][writePos] = softClip(in + w[i] - h);
		}
		writePos = (writePos + 1) & LINE_MASK;

		return out;
	}

	void clear() {
		for (int i = 0; i < NUM_LINES; i++) {
			std::memset(lines[i], 0, LINE_LEN * sizeof(float));
			std::memset(loopAp[i], 0, LOOP_AP_LEN * sizeof(float));
			loopApPos[i] = 0;
			dampState[i] = 0.f;
			lineGain[i] = 0.f;
			lfoPhase[i] = (float)i / NUM_LINES;
			lfoPhase2[i] = (float)((i + 3) % NUM_LINES) / NUM_LINES;
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
		sizeSm = -1.f; // re-arm the first-sample seed
		preSm = 0.f;
		frzSm = 0.f;
		writePos = 0;
		preWritePos = 0;
		gainCounter = 0;
	}
};

constexpr float VastReverb::baseMs[];
constexpr float VastReverb::modRatios[];
constexpr float VastReverb::modRatios2[];
constexpr float VastReverb::apMs[];
constexpr float VastReverb::loopApMs[];

} // namespace liminal

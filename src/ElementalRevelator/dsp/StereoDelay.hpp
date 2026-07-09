#pragma once
#include <cmath>
#include <cstring>
#include "DiffusionNetwork.hpp"
#include "FreqShifter.hpp"
#include "../util/Interpolation.hpp"

namespace dsputil {

struct StereoDelay {
	// Max 4 seconds at 192kHz
	static constexpr int MAX_DELAY = 192000 * 4;

	float* bufferL = nullptr;
	float* bufferR = nullptr;
	int writePos = 0;

	DiffusionNetwork diffusionL;
	DiffusionNetwork diffusionR;
	FreqShifter shifterL;
	FreqShifter shifterR;

	// Feedback state
	float feedbackL = 0.f;
	float feedbackR = 0.f;

	// DC blockers for feedback path
	DCBlocker dcBlockL;
	DCBlocker dcBlockR;

	StereoDelay() {
		bufferL = new float[MAX_DELAY];
		bufferR = new float[MAX_DELAY];
		std::memset(bufferL, 0, MAX_DELAY * sizeof(float));
		std::memset(bufferR, 0, MAX_DELAY * sizeof(float));
	}

	~StereoDelay() {
		delete[] bufferL;
		delete[] bufferR;
	}

	StereoDelay(const StereoDelay&) = delete;
	StereoDelay& operator=(const StereoDelay&) = delete;

	struct StereoFrame {
		float L, R;
	};

	// Process one stereo sample
	// inputL, inputR: dry input
	// feedAmount: 0..1 (0 = clean delay, 1 = full diffusion/reverb)
	// timeSeconds: delay time in seconds (0.01 to 4.0)
	// feedback: feedback amount (0..0.95)
	// shiftHz: frequency shift in Hz for feedback path
	// dryWet: 0..1 mix (0 = all dry, 1 = all wet)
	// sampleRate: current sample rate
	StereoFrame process(float inputL, float inputR,
	                    float feedAmount, float timeSeconds,
	                    float feedback, float shiftHz,
	                    float dryWet, float sampleRate) {
		// Clamp parameters
		timeSeconds = clampf(timeSeconds, 0.01f, 3.99f);
		feedback = clampf(feedback, 0.f, 0.95f);
		feedAmount = clampf(feedAmount, 0.f, 1.f);
		dryWet = clampf(dryWet, 0.f, 1.f);

		// Calculate delay lengths (slightly different L/R for width)
		int delaySamplesL = (int)(timeSeconds * sampleRate);
		int delaySamplesR = (int)(timeSeconds * sampleRate * 1.037f); // ~3.7% offset
		if (delaySamplesL >= MAX_DELAY) delaySamplesL = MAX_DELAY - 1;
		if (delaySamplesR >= MAX_DELAY) delaySamplesR = MAX_DELAY - 1;

		// Read from delay lines
		int readPosL = writePos - delaySamplesL;
		if (readPosL < 0) readPosL += MAX_DELAY;
		int readPosR = writePos - delaySamplesR;
		if (readPosR < 0) readPosR += MAX_DELAY;

		float delayedL = bufferL[readPosL];
		float delayedR = bufferR[readPosR];

		// Configure diffusion based on FEED amount
		// Longer diffusion times as feedAmount increases
		float diffTimeMs = 10.f + feedAmount * 80.f; // 10ms to 90ms
		diffusionL.setTimes(diffTimeMs, sampleRate);
		diffusionR.setTimes(diffTimeMs * 1.12f, sampleRate); // offset for stereo
		diffusionL.setCoefficient(0.5f + feedAmount * 0.35f);
		diffusionR.setCoefficient(0.5f + feedAmount * 0.35f);

		// Apply diffusion to delayed signal
		float processedL = diffusionL.process(delayedL, feedAmount);
		float processedR = diffusionR.process(delayedR, feedAmount);

		// Frequency shift in feedback path
		float shiftedL = processedL;
		float shiftedR = processedR;
		if (std::fabs(shiftHz) > 0.01f) {
			shiftedL = shifterL.process(processedL, shiftHz, sampleRate);
			shiftedR = shifterR.process(processedR, -shiftHz, sampleRate); // opposite shift for stereo
		}

		// Feedback with DC blocking
		feedbackL = dcBlockL.process(shiftedL * feedback);
		feedbackR = dcBlockR.process(shiftedR * feedback);

		// Cross-feed for ping-pong character (subtle)
		float crossFeed = 0.15f;
		float fbL = feedbackL + feedbackR * crossFeed;
		float fbR = feedbackR + feedbackL * crossFeed;

		// Write to delay lines (input + feedback)
		bufferL[writePos] = softClip(inputL + fbL);
		bufferR[writePos] = softClip(inputR + fbR);

		writePos = (writePos + 1) % MAX_DELAY;

		// Mix dry/wet
		StereoFrame out;
		out.L = lerp(inputL, processedL, dryWet);
		out.R = lerp(inputR, processedR, dryWet);

		return out;
	}

	void clear() {
		std::memset(bufferL, 0, MAX_DELAY * sizeof(float));
		std::memset(bufferR, 0, MAX_DELAY * sizeof(float));
		writePos = 0;
		feedbackL = feedbackR = 0.f;
		diffusionL.clear();
		diffusionR.clear();
		shifterL.reset();
		shifterR.reset();
	}
};

} // namespace dsputil

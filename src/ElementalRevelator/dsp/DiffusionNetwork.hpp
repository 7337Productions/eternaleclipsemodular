#pragma once
#include <cmath>
#include <cstring>

namespace dsputil {

struct DiffusionNetwork {
	static constexpr int NUM_STAGES = 4;
	static constexpr int MAX_STAGE_LEN = 8192;

	struct AllpassStage {
		float* buffer = nullptr;
		int writePos = 0;
		int delayLen = 1;
		float coeff = 0.5f;

		AllpassStage() {
			buffer = new float[MAX_STAGE_LEN];
			std::memset(buffer, 0, MAX_STAGE_LEN * sizeof(float));
		}

		~AllpassStage() {
			delete[] buffer;
		}

		AllpassStage(const AllpassStage&) = delete;
		AllpassStage& operator=(const AllpassStage&) = delete;

		float process(float input) {
			int readPos = writePos - delayLen;
			if (readPos < 0) readPos += MAX_STAGE_LEN;

			float delayed = buffer[readPos];
			float v = input - coeff * delayed;
			buffer[writePos] = v;
			writePos = (writePos + 1) % MAX_STAGE_LEN;

			return delayed + coeff * v;
		}

		void clear() {
			std::memset(buffer, 0, MAX_STAGE_LEN * sizeof(float));
			writePos = 0;
		}
	};

	AllpassStage stages[NUM_STAGES];

	// Prime-ratio delay lengths for natural diffusion
	// These ratios avoid metallic coloring
	static constexpr float delayRatios[NUM_STAGES] = {1.0f, 1.347f, 1.693f, 2.239f};

	// Set delay times based on a base time
	// baseTimeMs: base delay time in milliseconds
	void setTimes(float baseTimeMs, float sampleRate) {
		for (int i = 0; i < NUM_STAGES; i++) {
			int len = (int)(baseTimeMs * 0.001f * sampleRate * delayRatios[i]);
			if (len < 1) len = 1;
			if (len >= MAX_STAGE_LEN) len = MAX_STAGE_LEN - 1;
			stages[i].delayLen = len;
		}
	}

	// Set diffusion coefficient (how much each stage smears)
	void setCoefficient(float coeff) {
		coeff = coeff < -0.9f ? -0.9f : (coeff > 0.9f ? 0.9f : coeff);
		for (int i = 0; i < NUM_STAGES; i++) {
			// Alternate sign for better diffusion
			stages[i].coeff = (i % 2 == 0) ? coeff : -coeff;
		}
	}

	// Process one sample through all stages
	// diffusionAmount: 0..1 (0 = bypass, 1 = full diffusion)
	float process(float input, float diffusionAmount) {
		if (diffusionAmount <= 0.001f) return input;

		float diffused = input;
		for (int i = 0; i < NUM_STAGES; i++) {
			diffused = stages[i].process(diffused);
		}

		// Crossfade between dry and diffused
		return input + diffusionAmount * (diffused - input);
	}

	void clear() {
		for (int i = 0; i < NUM_STAGES; i++) {
			stages[i].clear();
		}
	}
};

constexpr float DiffusionNetwork::delayRatios[];

} // namespace dsputil

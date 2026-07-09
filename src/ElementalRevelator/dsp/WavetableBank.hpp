#pragma once
#include <cmath>
#include <cstring>
#include <cstdint>
#include "../util/Interpolation.hpp"

namespace dsputil {

struct WavetableBank {
	static constexpr int TABLE_SIZE = 2048;
	static constexpr int NUM_WAVES = 16;
	static constexpr int NUM_BANKS = 4;
	static constexpr int MIP_LEVELS = 8;

	// [bank][wave][mip_level][sample]
	// Heap-allocated to avoid stack overflow (~4MB total)
	float* data = nullptr;

	WavetableBank() {
		data = new float[NUM_BANKS * NUM_WAVES * MIP_LEVELS * TABLE_SIZE];
		std::memset(data, 0, NUM_BANKS * NUM_WAVES * MIP_LEVELS * TABLE_SIZE * sizeof(float));
		generate();
	}

	~WavetableBank() {
		delete[] data;
	}

	// Non-copyable
	WavetableBank(const WavetableBank&) = delete;
	WavetableBank& operator=(const WavetableBank&) = delete;

	float* getTable(int bank, int wave, int mip) {
		return data + ((bank * NUM_WAVES + wave) * MIP_LEVELS + mip) * TABLE_SIZE;
	}

	const float* getTable(int bank, int wave, int mip) const {
		return data + ((bank * NUM_WAVES + wave) * MIP_LEVELS + mip) * TABLE_SIZE;
	}

	// Read with morphing between waves and phase interpolation
	float read(int bank, float morphPos, int mipLevel, float phase) const {
		bank = clampInt(bank, 0, NUM_BANKS - 1);
		mipLevel = clampInt(mipLevel, 0, MIP_LEVELS - 1);

		float waveFloat = morphPos * (NUM_WAVES - 1);
		int waveA = clampInt((int)waveFloat, 0, NUM_WAVES - 2);
		int waveB = waveA + 1;
		float waveFrac = waveFloat - waveA;

		float sA = readSingle(bank, waveA, mipLevel, phase);
		float sB = readSingle(bank, waveB, mipLevel, phase);
		return lerp(sA, sB, waveFrac);
	}

	float readSingle(int bank, int wave, int mip, float phase) const {
		const float* table = getTable(bank, wave, mip);
		int size = TABLE_SIZE >> mip;

		phase -= std::floor(phase);
		float idx = phase * size;
		int i0 = (int)idx;
		int i1 = (i0 + 1) % size;
		float frac = idx - i0;

		return lerp(table[i0], table[i1], frac);
	}

	// Compute mip level from frequency
	static int mipFromFreq(float freq, float sampleRate) {
		if (freq <= 0.f || sampleRate <= 0.f) return 0;
		float ratio = freq * TABLE_SIZE / sampleRate;
		if (ratio <= 1.f) return 0;
		int mip = (int)std::floor(std::log2(ratio));
		return clampInt(mip, 0, MIP_LEVELS - 1);
	}

private:
	static int clampInt(int x, int lo, int hi) {
		return x < lo ? lo : (x > hi ? hi : x);
	}

	void generate() {
		generateBank0_Classic();
		generateBank1_Complex();
		generateBank2_Spectral();
		generateBank3_Shaped();
	}

	// Helper: add harmonic to a mip-0 table
	void addHarmonic(float* table, int harmonic, float amplitude, float phaseOffset = 0.f) {
		for (int i = 0; i < TABLE_SIZE; i++) {
			float phase = (float)i / TABLE_SIZE;
			table[i] += amplitude * std::sin(2.f * M_PI * harmonic * phase + phaseOffset);
		}
	}

	// Generate mip-mapped versions from mip-0 by filtering harmonics
	void generateMips(int bank, int wave) {
		// Mip 0 is already filled - generate from additive scratch for each level
		// For mip level m, max harmonic = TABLE_SIZE / (2^(m+1))
		// We regenerate additively for accuracy

		// First, analyze mip-0 to get the spectrum (or just regenerate from the same params)
		// Simpler approach: downsample by averaging pairs
		for (int mip = 1; mip < MIP_LEVELS; mip++) {
			float* prev = getTable(bank, wave, mip - 1);
			float* curr = getTable(bank, wave, mip);
			int currSize = TABLE_SIZE >> mip;

			for (int i = 0; i < currSize; i++) {
				// Simple 2-point average (basic low-pass)
				int j = i * 2;
				curr[i] = 0.5f * (prev[j] + prev[j + 1]);
			}
		}
	}

	// Normalize a mip-0 table to peak amplitude 1.0
	void normalize(float* table, int size = TABLE_SIZE) {
		float peak = 0.f;
		for (int i = 0; i < size; i++) {
			float a = std::fabs(table[i]);
			if (a > peak) peak = a;
		}
		if (peak > 0.0001f) {
			float scale = 1.f / peak;
			for (int i = 0; i < size; i++) {
				table[i] *= scale;
			}
		}
	}

	// Bank 0: Classic waveforms
	void generateBank0_Classic() {
		int bank = 0;
		int maxH = TABLE_SIZE / 2;

		// 0: Sine
		{
			float* t = getTable(bank, 0, 0);
			addHarmonic(t, 1, 1.f);
		}

		// 1: Triangle
		{
			float* t = getTable(bank, 1, 0);
			for (int h = 1; h <= maxH; h += 2) {
				float amp = 1.f / (float)(h * h);
				float sign = ((h / 2) % 2 == 0) ? 1.f : -1.f;
				addHarmonic(t, h, sign * amp);
			}
			normalize(t);
		}

		// 2: Saw (band-limited)
		{
			float* t = getTable(bank, 2, 0);
			for (int h = 1; h <= maxH; h++) {
				float amp = 1.f / (float)h;
				float sign = (h % 2 == 0) ? 1.f : -1.f;
				addHarmonic(t, h, sign * amp);
			}
			normalize(t);
		}

		// 3: Square (band-limited)
		{
			float* t = getTable(bank, 3, 0);
			for (int h = 1; h <= maxH; h += 2) {
				addHarmonic(t, h, 1.f / (float)h);
			}
			normalize(t);
		}

		// 4: Pulse 25%
		{
			float* t = getTable(bank, 4, 0);
			for (int h = 1; h <= maxH; h++) {
				float amp = std::sin(M_PI * h * 0.25f) * 2.f / (M_PI * h);
				addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// 5: Pulse 10%
		{
			float* t = getTable(bank, 5, 0);
			for (int h = 1; h <= maxH; h++) {
				float amp = std::sin(M_PI * h * 0.1f) * 2.f / (M_PI * h);
				addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// 6: Half-rectified sine
		{
			float* t = getTable(bank, 6, 0);
			// DC + fundamental + even harmonics
			addHarmonic(t, 1, 0.5f);
			for (int h = 2; h <= maxH; h += 2) {
				float amp = 2.f / (M_PI * (1.f - (float)(h * h)));
				if (std::fabs(amp) > 0.0001f)
					addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// 7: Full-rectified sine
		{
			float* t = getTable(bank, 7, 0);
			for (int h = 2; h <= maxH; h += 2) {
				float amp = -2.f / (M_PI * ((float)(h * h) - 1.f));
				if (std::fabs(amp) > 0.0001f)
					addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// 8: Staircase (4-step)
		{
			float* t = getTable(bank, 8, 0);
			for (int i = 0; i < TABLE_SIZE; i++) {
				float phase = (float)i / TABLE_SIZE;
				int step = (int)(phase * 4);
				t[i] = (step / 2.f) - 0.75f;
			}
			normalize(t);
		}

		// 9: Odd harmonics (clarinet-like)
		{
			float* t = getTable(bank, 9, 0);
			for (int h = 1; h <= maxH; h += 2) {
				addHarmonic(t, h, 1.f / std::sqrt((float)h));
			}
			normalize(t);
		}

		// 10: Even harmonics (octave-rich)
		{
			float* t = getTable(bank, 10, 0);
			addHarmonic(t, 1, 1.f);
			for (int h = 2; h <= maxH; h += 2) {
				addHarmonic(t, h, 1.f / (float)h);
			}
			normalize(t);
		}

		// 11: Prime harmonics
		{
			float* t = getTable(bank, 11, 0);
			int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47};
			addHarmonic(t, 1, 1.f);
			for (int p : primes) {
				if (p > maxH) break;
				addHarmonic(t, p, 1.f / std::sqrt((float)p));
			}
			normalize(t);
		}

		// 12: Warm (1/n^2 rolloff)
		{
			float* t = getTable(bank, 12, 0);
			for (int h = 1; h <= maxH; h++) {
				addHarmonic(t, h, 1.f / (float)(h * h));
			}
			normalize(t);
		}

		// 13: Random harmonics (deterministic seed)
		{
			float* t = getTable(bank, 13, 0);
			uint32_t seed = 12345;
			for (int h = 1; h <= 32 && h <= maxH; h++) {
				seed = seed * 1664525u + 1013904223u;
				float amp = (float)(seed & 0xFFFF) / 65535.f;
				seed = seed * 1664525u + 1013904223u;
				float ph = (float)(seed & 0xFFFF) / 65535.f * 2.f * M_PI;
				addHarmonic(t, h, amp / (float)h, ph);
			}
			normalize(t);
		}

		// 14: Formant A (three peaks at ~700, 1200, 2500 Hz-ish in harmonic space)
		{
			float* t = getTable(bank, 14, 0);
			for (int h = 1; h <= maxH; h++) {
				float f = (float)h;
				float amp = std::exp(-0.5f * (f - 3.f) * (f - 3.f) / 1.f)
				          + std::exp(-0.5f * (f - 6.f) * (f - 6.f) / 2.f)
				          + std::exp(-0.5f * (f - 12.f) * (f - 12.f) / 4.f);
				addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// 15: Formant O (two peaks)
		{
			float* t = getTable(bank, 15, 0);
			for (int h = 1; h <= maxH; h++) {
				float f = (float)h;
				float amp = std::exp(-0.5f * (f - 2.f) * (f - 2.f) / 0.8f)
				          + std::exp(-0.5f * (f - 8.f) * (f - 8.f) / 3.f);
				addHarmonic(t, h, amp);
			}
			normalize(t);
		}

		// Generate mip levels for all waves
		for (int w = 0; w < NUM_WAVES; w++) {
			generateMips(bank, w);
		}
	}

	// Bank 1: FM-synthesized waveforms with increasing mod index
	void generateBank1_Complex() {
		int bank = 1;

		struct FMConfig {
			float ratio;    // carrier:modulator frequency ratio
			float index;    // modulation index
		};

		FMConfig configs[NUM_WAVES] = {
			{1.f, 0.0f},   // 0: Pure sine (no modulation)
			{1.f, 0.5f},   // 1: Gentle FM 1:1
			{1.f, 1.5f},   // 2: Moderate FM 1:1
			{1.f, 3.0f},   // 3: Heavy FM 1:1
			{2.f, 0.5f},   // 4: Gentle FM 1:2
			{2.f, 1.5f},   // 5: Moderate FM 1:2
			{2.f, 3.0f},   // 6: Heavy FM 1:2
			{1.5f, 1.0f},  // 7: Inharmonic FM 2:3
			{1.5f, 2.5f},  // 8: Heavy inharmonic
			{3.f, 0.5f},   // 9: Gentle FM 1:3
			{3.f, 2.0f},   // 10: Heavy FM 1:3
			{0.5f, 1.0f},  // 11: Sub-ratio FM 2:1
			{0.5f, 3.0f},  // 12: Heavy sub FM
			{4.f, 1.0f},   // 13: FM 1:4
			{7.f, 0.5f},   // 14: FM 1:7 (metallic)
			{3.14159f, 2.f} // 15: Irrational ratio (very inharmonic)
		};

		for (int w = 0; w < NUM_WAVES; w++) {
			float* t = getTable(bank, w, 0);
			for (int i = 0; i < TABLE_SIZE; i++) {
				float phase = 2.f * M_PI * i / TABLE_SIZE;
				t[i] = std::sin(phase + configs[w].index * std::sin(phase * configs[w].ratio));
			}
			normalize(t);
			generateMips(bank, w);
		}
	}

	// Bank 2: Additive with varying harmonic density (sparse to dense)
	void generateBank2_Spectral() {
		int bank = 2;

		for (int w = 0; w < NUM_WAVES; w++) {
			float* t = getTable(bank, w, 0);
			// Wave 0 = 1 harmonic, Wave 15 = 64 harmonics
			int numHarmonics = 1 + (int)(w * 63.f / 15.f);
			int maxH = TABLE_SIZE / 2;
			if (numHarmonics > maxH) numHarmonics = maxH;

			// Spectral tilt varies: wave 0 = flat, wave 15 = -6dB/oct
			float tiltFactor = w / 15.f;

			for (int h = 1; h <= numHarmonics; h++) {
				float amp = 1.f / std::pow((float)h, tiltFactor);
				// Alternate phase for variety
				float ph = (h % 3 == 0) ? M_PI * 0.5f : 0.f;
				addHarmonic(t, h, amp, ph);
			}
			normalize(t);
			generateMips(bank, w);
		}
	}

	// Bank 3: Chebyshev waveshaped sine (increasing distortion)
	void generateBank3_Shaped() {
		int bank = 3;

		for (int w = 0; w < NUM_WAVES; w++) {
			float* t = getTable(bank, w, 0);

			// Chebyshev polynomial order: 1 (sine) to 16 (heavily shaped)
			int order = w + 1;

			for (int i = 0; i < TABLE_SIZE; i++) {
				float phase = (float)i / TABLE_SIZE;
				float x = std::sin(2.f * M_PI * phase);

				// Chebyshev polynomial of the first kind
				float result = chebyshev(x, order);

				// Blend with original sine for smoother morphing
				float blend = (float)w / (NUM_WAVES - 1);
				t[i] = lerp(x, result, blend * 0.7f + 0.3f);
			}
			normalize(t);
			generateMips(bank, w);
		}
	}

	// Chebyshev polynomial T_n(x) computed iteratively
	static float chebyshev(float x, int n) {
		if (n == 0) return 1.f;
		if (n == 1) return x;
		float t0 = 1.f;
		float t1 = x;
		for (int i = 2; i <= n; i++) {
			float t2 = 2.f * x * t1 - t0;
			t0 = t1;
			t1 = t2;
		}
		return t1;
	}
};

} // namespace dsputil

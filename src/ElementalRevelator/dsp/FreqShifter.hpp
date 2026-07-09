#pragma once
#include <cmath>
#include "../util/LookupTable.hpp"

namespace dsputil {

// Frequency shifter using Hilbert transform (cascaded allpass)
// Shifts spectrum by a small amount (0.1-5 Hz) for spectral movement in feedback
struct FreqShifter {
	// Hilbert transform allpass coefficients (approximate 90-degree phase split)
	// Using 4-stage allpass pairs for I and Q channels
	static constexpr int NUM_STAGES = 4;

	// Allpass coefficients for ~20Hz-20kHz range Hilbert approximation
	// These produce approximately 90 degrees phase difference between I and Q
	float coeffsI[NUM_STAGES] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
	float coeffsQ[NUM_STAGES] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};

	// Allpass state
	float stateI[NUM_STAGES][2] = {};  // [stage][0=x_prev, 1=y_prev]
	float stateQ[NUM_STAGES][2] = {};

	// Shift oscillator phase
	float shiftPhase = 0.f;

	// One-pole smoothing for the shift output
	float prevI = 0.f;
	float prevQ = 0.f;

	// Process first-order allpass: y[n] = a * (x[n] - y[n-1]) + x[n-1]
	static float allpass1(float input, float coeff, float state[2]) {
		float output = coeff * (input - state[1]) + state[0];
		state[0] = input;
		state[1] = output;
		return output;
	}

	// Process one sample
	// input: audio signal
	// shiftHz: frequency shift amount in Hz (typically 0.1 to 5.0)
	// sampleRate: current sample rate
	float process(float input, float shiftHz, float sampleRate) {
		// Run input through two parallel allpass chains (Hilbert pair)
		float i_signal = input;
		float q_signal = input;

		for (int s = 0; s < NUM_STAGES; s++) {
			i_signal = allpass1(i_signal, coeffsI[s], stateI[s]);
			q_signal = allpass1(q_signal, coeffsQ[s], stateQ[s]);
		}

		// Generate shift oscillator
		float phaseInc = shiftHz / sampleRate;
		shiftPhase += phaseInc;
		shiftPhase -= std::floor(shiftPhase);

		float cosShift = sinTable.cos(shiftPhase);
		float sinShift = sinTable.lookup(shiftPhase);

		// Complex multiplication for frequency shift (upshift)
		float output = i_signal * cosShift - q_signal * sinShift;

		return output;
	}

	void reset() {
		for (int s = 0; s < NUM_STAGES; s++) {
			stateI[s][0] = stateI[s][1] = 0.f;
			stateQ[s][0] = stateQ[s][1] = 0.f;
		}
		shiftPhase = 0.f;
		prevI = prevQ = 0.f;
	}
};

} // namespace dsputil

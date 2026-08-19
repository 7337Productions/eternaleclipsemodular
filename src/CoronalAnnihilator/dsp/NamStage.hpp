#pragma once
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <dsp/resampler.hpp>
#include <dsp/common.hpp>
#include "NAM/get_dsp.h"
#include "json.hpp"
#include "Util.hpp"

// The only file in the module that touches the neural-amp-model library.
namespace corona {

// One loaded .nam model: two independent networks (L/R) sharing the same
// weights. Immutable after load; owned by exactly one thread at a time.
struct NamModelSet {
	std::unique_ptr<nam::DSP> net[2];
	double expectedRate = -1.0;   // <= 0: "unknown" -> run at the engine rate
	bool hasLoudness = false;
	float loudness = 0.f;         // dB (model metadata)
	std::string path;
	std::string name;             // file stem for the panel display

	static constexpr int MAX_FRAMES = 1024; // max frames per net->process() call

	// Returns nullptr and fills `error` on failure. Safe to call from any
	// thread (not the audio thread: parses JSON, allocates, prewarms).
	// `fallbackRate` is used for models that don't declare a sample rate.
	static NamModelSet* load(const std::string& path, double fallbackRate, std::string& error) {
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			error = "missing";
			return nullptr;
		}
		nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
		if (j.is_discarded()) {
			error = "bad model";
			return nullptr;
		}
		std::unique_ptr<NamModelSet> m(new NamModelSet);
		try {
			for (int c = 0; c < 2; c++) {
				nam::DspLoadOptions opts;
				opts.prewarm = false; // we prewarm below at the rate we run at
				m->net[c] = nam::get_dsp(j, opts);
				if (!m->net[c] || m->net[c]->NumInputChannels() != 1 || m->net[c]->NumOutputChannels() != 1) {
					error = "bad model";
					return nullptr;
				}
			}
		}
		catch (const std::exception&) {
			error = "bad model";
			return nullptr;
		}
		m->expectedRate = m->net[0]->GetExpectedSampleRate();
		double rate = m->expectedRate > 0 ? m->expectedRate : fallbackRate;
		for (int c = 0; c < 2; c++)
			m->net[c]->ResetAndPrewarm(rate, MAX_FRAMES);
		if (m->net[0]->HasLoudness()) {
			m->hasLoudness = true;
			m->loudness = (float)m->net[0]->GetLoudness();
		}
		m->path = path;
		// file stem
		size_t slash = path.find_last_of("/\\");
		std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
		size_t dot = base.find_last_of('.');
		m->name = dot == std::string::npos ? base : base.substr(0, dot);
		return m.release();
	}
};

// Block-based stereo wrapper around a NamModelSet: engine-rate frames go
// through a sample-rate converter to the model's rate, through the nets in
// blocks of BLOCK frames, back through a converter, and out of a FIFO one
// frame per process() call. Latency: BLOCK frames (+ converter latency when
// rates differ). All buffers are fixed; configure() is the only allocator.
struct NamStage {
	static constexpr int BLOCK = 32;
	static constexpr int MAX_FRAMES = NamModelSet::MAX_FRAMES;
	static constexpr int FIFO_SIZE = 4096; // power of two
	static constexpr int PRIME_SRC = 64;   // extra zero frames when converting rates (covers converter latency + jitter)

	rack::dsp::SampleRateConverter<2> up;    // engine -> model
	rack::dsp::SampleRateConverter<2> down;  // model -> engine
	bool converting = false;

	rack::dsp::Frame<2> inBuf[BLOCK];
	int inCount = 0;
	rack::dsp::Frame<2> srcBuf[MAX_FRAMES];  // model-rate frames
	rack::dsp::Frame<2> backBuf[MAX_FRAMES]; // engine-rate frames after down-conversion
	float namIn[2][MAX_FRAMES];
	float namOut[2][MAX_FRAMES];

	rack::dsp::Frame<2> fifo[FIFO_SIZE];
	int fifoHead = 0; // read
	int fifoTail = 0; // write
	int underruns = 0;

	int engineRate = 0;
	int modelRate = 0;

	NamStage() {
		up.setQuality(4);
		down.setQuality(4);
		std::memset(namIn, 0, sizeof(namIn));
		std::memset(namOut, 0, sizeof(namOut));
	}

	// Not real-time safe (speex state allocation). Call when the engine rate
	// or the loaded model changes.
	void configure(int engineRate_, int modelRate_) {
		engineRate = engineRate_ > 0 ? engineRate_ : 48000;
		modelRate = modelRate_ > 0 ? modelRate_ : engineRate;
		converting = engineRate != modelRate;
		up.setRates(engineRate, modelRate);
		down.setRates(modelRate, engineRate);
		reset();
	}

	void reset() {
		inCount = 0;
		fifoHead = fifoTail = 0;
		underruns = 0;
		int prime = converting ? PRIME_SRC : 0;
		rack::dsp::Frame<2> z = {};
		for (int i = 0; i < prime; i++)
			fifoPush(z);
	}

	int fifoSize() const {
		return (fifoTail - fifoHead) & (FIFO_SIZE - 1);
	}

	// One stereo frame in, one out (+/-1 units, pre-scaled by the caller).
	void process(float inL, float inR, NamModelSet* m, bool mono, float& outL, float& outR) {
		inBuf[inCount].samples[0] = sanitize(inL);
		inBuf[inCount].samples[1] = sanitize(inR);
		inCount++;
		if (inCount >= BLOCK) {
			inCount = 0;
			runBlock(m, mono);
		}
		if (fifoSize() > 0) {
			rack::dsp::Frame<2> f = fifo[fifoHead];
			fifoHead = (fifoHead + 1) & (FIFO_SIZE - 1);
			outL = f.samples[0];
			outR = f.samples[1];
		}
		else {
			underruns++;
			outL = outR = 0.f;
		}
	}

private:
	void fifoPush(const rack::dsp::Frame<2>& f) {
		int next = (fifoTail + 1) & (FIFO_SIZE - 1);
		if (next == fifoHead)
			return; // full: drop (cannot happen with sane rates)
		fifo[fifoTail] = f;
		fifoTail = next;
	}

	void runBlock(NamModelSet* m, bool mono) {
		// engine -> model rate
		int inFrames = BLOCK;
		int srcFrames = MAX_FRAMES;
		up.process(inBuf, &inFrames, srcBuf, &srcFrames);
		if (srcFrames <= 0)
			return;

		// nets (mono-in, mono-out each; L and R are independent instances)
		for (int i = 0; i < srcFrames; i++) {
			if (mono) {
				namIn[0][i] = 0.5f * (srcBuf[i].samples[0] + srcBuf[i].samples[1]);
			}
			else {
				namIn[0][i] = srcBuf[i].samples[0];
				namIn[1][i] = srcBuf[i].samples[1];
			}
		}
		int channels = mono ? 1 : 2;
		for (int c = 0; c < channels; c++) {
			float* ip[1] = {namIn[c]};
			float* op[1] = {namOut[c]};
			m->net[c]->process(ip, op, srcFrames);
		}
		for (int i = 0; i < srcFrames; i++) {
			float l = sanitize(namOut[0][i]);
			float r = mono ? l : sanitize(namOut[1][i]);
			srcBuf[i].samples[0] = l;
			srcBuf[i].samples[1] = r;
		}

		// model -> engine rate, into the FIFO
		int backIn = srcFrames;
		int backOut = MAX_FRAMES;
		down.process(srcBuf, &backIn, backBuf, &backOut);
		for (int i = 0; i < backOut; i++)
			fifoPush(backBuf[i]);
	}
};

} // namespace corona

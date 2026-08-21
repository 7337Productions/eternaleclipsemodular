#pragma once
#include "plugin.hpp"
#include <dr_wav.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Fixed loop bank for the Litany Engine. The shipped WAVs in res/litany/ are
// the instrument (Gristleism ethos -- no user loading). One bank is shared by
// every module instance: filenames are scanned synchronously at first access
// (so loop count and names are available for configSwitch), audio is decoded
// once on a background thread, and buffers are immutable and live until plugin
// unload -- so the audio thread and lingering grains may hold pointers freely.
namespace litany {

struct Loop {
	std::string name;      // display text, e.g. "01 · IRON PSALM"
	std::string path;
	float fileRate = 48000.f;
	size_t frames = 0;
	std::vector<float> l, r;  // deinterleaved. If Nick's final set gets long,
	                          // switch these to int16 with scale-on-read to
	                          // halve memory; nothing outside this file cares.
	std::atomic<bool> ready{false};
};

class SampleBank {
public:
	static SampleBank& get() {
		static SampleBank bank;
		return bank;
	}

	// Start the background decode (idempotent).
	void ensureLoaded() {
		std::call_once(loadOnce, [this] {
			loader = std::thread([this] { decodeAll(); });
		});
	}

	size_t count() const { return loops.size(); }

	const Loop* loop(size_t i) const {
		return i < loops.size() ? loops[i].get() : nullptr;
	}

	std::vector<std::string> names() const {
		std::vector<std::string> out;
		for (const auto& lp : loops)
			out.push_back(lp->name);
		return out;
	}

	~SampleBank() {
		if (loader.joinable())
			loader.join();
	}

private:
	std::vector<std::unique_ptr<Loop>> loops;
	std::thread loader;
	std::once_flag loadOnce;

	SampleBank() {
		// Synchronous scan: names only, no decoding.
		std::string dir = rack::asset::plugin(pluginInstance, "res/litany");
		std::vector<std::string> entries;
		if (rack::system::isDirectory(dir))
			entries = rack::system::getEntries(dir);
		std::sort(entries.begin(), entries.end());
		for (const std::string& path : entries) {
			std::string ext = rack::string::lowercase(rack::system::getExtension(path));
			if (ext != ".wav")
				continue;
			auto lp = std::make_unique<Loop>();
			lp->path = path;
			lp->name = displayName(path);
			loops.push_back(std::move(lp));
		}
	}

	// "02-iron-psalm.wav" -> "02 · IRON PSALM"
	static std::string displayName(const std::string& path) {
		std::string stem = rack::system::getStem(path);
		std::string num, rest = stem;
		if (stem.size() > 3 && isdigit(stem[0]) && isdigit(stem[1]) && stem[2] == '-') {
			num = stem.substr(0, 2);
			rest = stem.substr(3);
		}
		// Hyphens/underscores become spaces; CamelCase splits on lower->upper
		// ("MorsEstRequies" -> "MORS EST REQUIES"); everything uppercased.
		std::string out;
		for (size_t i = 0; i < rest.size(); i++) {
			char c = rest[i];
			if (c == '-' || c == '_') {
				out += ' ';
				continue;
			}
			if (i > 0 && isupper((unsigned char)c) && islower((unsigned char)rest[i - 1]))
				out += ' ';
			out += (char)toupper((unsigned char)c);
		}
		return num.empty() ? out : num + " · " + out;
	}

	void decodeAll() {
		rack::system::setThreadName("LitanySampleBank");
		for (auto& lp : loops) {
			decode(*lp);
			lp->ready.store(true, std::memory_order_release);
		}
	}

	static void decode(Loop& lp) {
		// readFile + init_memory keeps dr_wav off the filesystem (Windows
		// UTF-8 path safety). Failures leave an empty loop: ready but silent.
		std::vector<uint8_t> data;
		try {
			data = rack::system::readFile(lp.path);
		}
		catch (...) {
			WARN("Litany Engine: could not read %s", lp.path.c_str());
			return;
		}

		drwav wav;
		if (!drwav_init_memory(&wav, data.data(), data.size(), NULL)) {
			WARN("Litany Engine: could not decode %s", lp.path.c_str());
			return;
		}
		size_t frames = (size_t)wav.totalPCMFrameCount;
		unsigned channels = wav.channels;
		float fileRate = (float)wav.sampleRate;
		if (frames < 64 || channels < 1) {
			drwav_uninit(&wav);
			return;
		}
		std::vector<float> interleaved(frames * channels);
		size_t got = (size_t)drwav_read_pcm_frames_f32(&wav, frames, interleaved.data());
		drwav_uninit(&wav);
		if (got < 64)
			return;

		lp.fileRate = fileRate;
		lp.l.resize(got);
		lp.r.resize(got);
		for (size_t i = 0; i < got; i++) {
			float a = interleaved[i * channels];
			float b = channels > 1 ? interleaved[i * channels + 1] : a;
			lp.l[i] = a;
			lp.r[i] = b;
		}

		// Bake a ~10 ms end->start crossfade into the buffer so the loop point
		// is seamless for every reader (grains wrap mid-buffer with no runtime
		// splice handling).
		size_t fade = std::min((size_t)(0.010f * lp.fileRate), got / 4);
		for (size_t i = 0; i < fade; i++) {
			float t = (float)i / (float)fade;
			size_t tail = got - fade + i;
			lp.l[i] = lp.l[tail] * (1.f - t) + lp.l[i] * t;
			lp.r[i] = lp.r[tail] * (1.f - t) + lp.r[i] * t;
		}
		lp.l.resize(got - fade);
		lp.r.resize(got - fade);
		lp.frames = got - fade;
	}
};

} // namespace litany

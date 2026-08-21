#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

// Granular playback engine for the Litany Engine. Deliberately rack-free so it
// can be compiled and exercised standalone (tools/litany_harness.cpp).
//
// Design (see plan): tape-style through-zero varispeed with speed/pitch
// coupled; GRAIN geometrically interpolates grain length from the whole loop
// (0 = literal looped playback, the Gristleism case) down to 15 ms; MORPH is
// grain overlap 1x..8x; SCAN offsets grain spawn position; TEXTURE adds
// position spray and stereo pan spread. Grains use a Tukey window whose taper
// is clamped to ~10 ms, so a whole-loop grain is plain playback with a
// crossfaded loop point rather than a special code path.
namespace litany {

// Read-only view of a decoded loop. The underlying buffers are owned by the
// SampleBank and live for the plugin's lifetime, so grains may safely keep a
// view of a loop the module has already switched away from.
struct LoopView {
	const float* l = nullptr;
	const float* r = nullptr;
	size_t frames = 0;
	float fileRate = 48000.f;

	bool valid() const { return l && r && frames > 1; }
};

static constexpr float PI_F = 3.14159265358979f;

struct GranularParams {
	float speed = 1.f;    // -2..2, through-zero; |speed| < 0.01 = stopped tape
	float grain = 0.f;    // 0..1; 0 = whole loop, 1 = 15 ms
	float morph = 0.f;    // 0..1 -> overlap 1x..8x (exponential)
	float scan = 0.f;     // 0..1 position offset in loop fractions
	float texture = 0.f;  // 0..1 spray + pan spread
};

class GranularEngine {
public:
	static constexpr int MAX_GRAINS = 16;
	static constexpr float MIN_SPEED = 0.01f;      // below this the tape is stopped
	static constexpr float MIN_GRAIN_SEC = 0.015f; // grain length at GRAIN = 1
	static constexpr float TAPER_SEC = 0.010f;     // max window fade time
	static constexpr float RETIRE_SEC = 0.015f;    // fade-out for retired grains

	struct Out {
		float l = 0.f;
		float r = 0.f;
		bool eoc = false;
	};

	void reset() {
		head = 0.0;
		countdown = 0.0;
		lastScan = -1.f;
		curData = nullptr;
		for (Grain& g : grains)
			g.active = false;
	}

	// Playhead position 0..1 for the panel display.
	float phase01() const { return (float)headPhase; }

	Out process(const LoopView& loop, const GranularParams& p, float engineRate) {
		Out out;
		if (!loop.valid() || engineRate <= 0.f) {
			renderGrains(out, engineRate); // let stragglers from an old loop fade out
			return out;
		}

		// Loop switch: fade out old-loop grains, restart the playhead.
		if (loop.l != curData) {
			curData = loop.l;
			retireAll();
			head = 0.0;
			countdown = 0.0;
			lastScan = -1.f;
		}

		const double frames = (double)loop.frames;
		const double srcRatio = (double)loop.fileRate / (double)engineRate;
		const bool stopped = std::fabs(p.speed) < MIN_SPEED;
		const double inc = stopped ? 0.0 : (double)p.speed * srcRatio;

		// ----- Playhead (tape) -----
		head += inc;
		if (head >= frames) {
			head -= frames;
			out.eoc = true;
		}
		else if (head < 0.0) {
			head += frames;
			out.eoc = true;
		}
		headPhase = head / frames;

		// ----- Grain geometry -----
		// Geometric interpolation loop length -> MIN_GRAIN_SEC: continuous, and
		// GRAIN = 0 is exactly the whole loop.
		const double loopSec = frames / (double)loop.fileRate;
		const double grainSec = std::exp((1.0 - (double)p.grain) * std::log(loopSec)
		                                 + (double)p.grain * std::log((double)MIN_GRAIN_SEC));
		const double grainSrcFrames = grainSec * (double)loop.fileRate;
		const double overlap = std::exp2(3.0 * (double)p.morph); // 1x..8x
		const bool wholeLoopish = p.grain < 0.1f;

		// Scrubbing SCAN in whole-loop mode: crossfade to the new position
		// instead of letting a huge grain keep playing stale material.
		if (lastScan >= 0.f && wholeLoopish && std::fabs(p.scan - lastScan) > 0.01f) {
			retireAll();
			countdown = 0.0;
		}
		lastScan = p.scan;

		// ----- Scheduler -----
		if (stopped) {
			retireAll();
			countdown = 0.0; // spawn immediately on resume
		}
		else {
			countdown -= 1.0;
			if (countdown <= 0.0) {
				spawn(loop, p, engineRate, inc, grainSrcFrames, frames);
				// Successive grains overlap by the window taper so the
				// crossfade sums to unity (no dip at overlap = 1x).
				const double durEngine = grainSrcFrames / std::fabs(inc);
				const double taperEngine = std::fmin(durEngine * 0.5, (double)(TAPER_SEC * engineRate));
				countdown = std::fmax(1.0, (durEngine - taperEngine) / overlap);
			}
		}

		renderGrains(out, engineRate);
		const float norm = 1.f / std::sqrt((float)overlap);
		out.l *= norm;
		out.r *= norm;
		return out;
	}

private:
	struct Grain {
		LoopView src;
		double pos = 0.0;        // source frames, wrapped
		double inc = 0.0;        // source frames per engine sample (signed)
		double age = 0.0;        // engine samples since spawn
		double dur = 0.0;        // total engine samples
		double taper = 0.0;      // window fade, engine samples
		float gainL = 1.f;
		float gainR = 1.f;
		bool active = false;
		bool retiring = false;
		float retireGain = 1.f;
	};

	Grain grains[MAX_GRAINS];
	double head = 0.0;
	double headPhase = 0.0;
	double countdown = 0.0;
	float lastScan = -1.f;
	const float* curData = nullptr;
	uint32_t rngState = 0x9e3779b9u;

	float bipolarRand() {
		// xorshift32 -- deterministic, allocation-free
		rngState ^= rngState << 13;
		rngState ^= rngState >> 17;
		rngState ^= rngState << 5;
		return (float)(int32_t)rngState * (1.f / 2147483648.f);
	}

	void retireAll() {
		for (Grain& g : grains)
			if (g.active)
				g.retiring = true;
	}

	void spawn(const LoopView& loop, const GranularParams& p, float engineRate,
	           double inc, double grainSrcFrames, double frames) {
		// Reuse a free slot, else steal the oldest grain.
		Grain* slot = nullptr;
		double oldestAge = -1.0;
		for (Grain& g : grains) {
			if (!g.active) {
				slot = &g;
				break;
			}
			if (g.age > oldestAge) {
				oldestAge = g.age;
				slot = &g;
			}
		}

		const double spray = (double)(p.texture * p.texture) * 0.5 * frames * (double)bipolarRand();
		double start = head + (double)p.scan * frames + spray;
		start = std::fmod(start, frames);
		if (start < 0.0)
			start += frames;

		// Equal-power pan offset, normalized so center = unity per channel.
		const float panOff = p.texture * 0.25f * bipolarRand();
		const float theta = (0.5f + panOff) * (PI_F * 0.5f);
		const float panNorm = 1.41421356f;

		slot->src = loop;
		slot->pos = start;
		slot->inc = inc;
		slot->age = 0.0;
		slot->dur = grainSrcFrames / std::fabs(inc);
		slot->taper = std::fmin(slot->dur * 0.5, (double)(TAPER_SEC * engineRate));
		slot->gainL = std::cos(theta) * panNorm;
		slot->gainR = std::sin(theta) * panNorm;
		slot->active = true;
		slot->retiring = false;
		slot->retireGain = 1.f;
	}

	void renderGrains(Out& out, float engineRate) {
		const float retireStep = engineRate > 0.f ? 1.f / (RETIRE_SEC * engineRate) : 1.f;
		for (Grain& g : grains) {
			if (!g.active)
				continue;
			if (!g.src.valid() || g.age >= g.dur) {
				g.active = false;
				continue;
			}

			// Tukey window: raised-cosine edges, flat top.
			float w = 1.f;
			if (g.taper > 0.0) {
				if (g.age < g.taper)
					w = 0.5f - 0.5f * std::cos((float)(g.age / g.taper) * PI_F);
				else if (g.age > g.dur - g.taper)
					w = 0.5f - 0.5f * std::cos((float)((g.dur - g.age) / g.taper) * PI_F);
			}
			if (g.retiring) {
				g.retireGain -= retireStep;
				if (g.retireGain <= 0.f) {
					g.active = false;
					continue;
				}
				w *= g.retireGain;
			}

			// Linear-interpolated wrapped stereo read
			const double frames = (double)g.src.frames;
			double pos = g.pos;
			size_t i0 = (size_t)pos;
			if (i0 >= g.src.frames)
				i0 = g.src.frames - 1;
			const size_t i1 = (i0 + 1 < g.src.frames) ? i0 + 1 : 0;
			const float frac = (float)(pos - (double)i0);
			const float sl = g.src.l[i0] + (g.src.l[i1] - g.src.l[i0]) * frac;
			const float sr = g.src.r[i0] + (g.src.r[i1] - g.src.r[i0]) * frac;

			out.l += sl * w * g.gainL;
			out.r += sr * w * g.gainR;

			g.pos += g.inc;
			if (g.pos >= frames)
				g.pos -= frames;
			else if (g.pos < 0.0)
				g.pos += frames;
			g.age += 1.0;
		}
	}
};

} // namespace litany

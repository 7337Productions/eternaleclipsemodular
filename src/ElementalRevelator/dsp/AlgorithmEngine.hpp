#pragma once

namespace dsputil {

struct AlgorithmEngine {
	static constexpr int NUM_BANKS = 8;
	static constexpr int ALGOS_PER_BANK = 9;
	static constexpr int TOTAL_ALGOS = NUM_BANKS * ALGOS_PER_BANK;

	struct AlgorithmConfig {
		// Oscillator config
		int wavebank[4];            // wavetable bank per element: AIR, WATER, EARTH, FIRE (0-3)
		float detuneScale;          // multiplier for VEIL detune spread
		float morphSpread;          // per-element wavetable position spread
		// Cross-modulation
		float sigilScale;           // multiplier for SIGIL ring depth
		bool sigilAlternate;        // invert SIGIL direction on WATER and EARTH
		// Filter config
		float cutoffOffset;         // octaves added to the CUTOFF exponent
		float resBias;              // added to RES
		float keyTrack;             // root-pitch tracking of the cutoff (0..1)
		// FX config
		float delayTimeBase;        // base delay time in seconds
		float delayFeedback;        // base feedback amount
		float diffusionBase;        // base diffusion amount (added to FEED)
		float freqShiftRange;       // max frequency shift in Hz
		// Mix config
		float elementTilt;          // -1..+1 (AIR/FIRE vs WATER/EARTH balance, 0 = equal)
		float riteScale;            // multiplier for RITE (ring mod) parameter
		float outputGain;           // output level
	};

	int currentBank = 0;
	int currentAlgo = 0;

	// Smooth transition state
	int prevBank = 0;
	int prevAlgo = 0;
	float crossfade = 1.f;         // 1.0 = fully on new algo
	float crossfadeRate = 0.f;     // per-sample increment

	void setBank(int bank) {
		bank = bank < 0 ? 0 : (bank > NUM_BANKS - 1 ? NUM_BANKS - 1 : bank);
		if (bank != currentBank) {
			prevBank = currentBank;
			prevAlgo = currentAlgo;
			currentBank = bank;
			crossfade = 0.f;
		}
	}

	void setAlgo(int algo) {
		algo = algo < 0 ? 0 : (algo > ALGOS_PER_BANK - 1 ? ALGOS_PER_BANK - 1 : algo);
		if (algo != currentAlgo) {
			prevBank = currentBank;
			prevAlgo = currentAlgo;
			currentAlgo = algo;
			crossfade = 0.f;
		}
	}

	// Select a daemon by flat index 0..71
	void setIndex(int idx) {
		idx = idx < 0 ? 0 : (idx > TOTAL_ALGOS - 1 ? TOTAL_ALGOS - 1 : idx);
		setBank(idx / ALGOS_PER_BANK);
		setAlgo(idx % ALGOS_PER_BANK);
	}

	// Call once when sample rate changes
	void setSampleRate(float sampleRate) {
		// ~50ms crossfade
		crossfadeRate = 1.f / (0.05f * sampleRate);
	}

	// Returns true if currently crossfading
	bool isCrossfading() const {
		return crossfade < 1.f;
	}

	// Advance crossfade and return blend amount (0..1, 1 = fully new)
	float advanceCrossfade() {
		if (crossfade < 1.f) {
			crossfade += crossfadeRate;
			if (crossfade > 1.f) crossfade = 1.f;
		}
		return crossfade;
	}

	const AlgorithmConfig& getConfig() const {
		int idx = currentBank * ALGOS_PER_BANK + currentAlgo;
		return configs[idx];
	}

	const AlgorithmConfig& getPrevConfig() const {
		int idx = prevBank * ALGOS_PER_BANK + prevAlgo;
		return configs[idx];
	}

	// ===== 72 Daemon Configurations =====
	static const AlgorithmConfig configs[TOTAL_ALGOS];
};

// =====================================================
// Bank I:    BREATH    (airy pads, organs, vocal drift)
// Bank II:   TIDE      (watery, chorused, slow shift)
// Bank III:  STONE     (subs, drones, dark weight)
// Bank IV:   EMBER     (FM bells, brass, struck metal)
// Bank V:    STORM     (aggressive, folded, industrial)
// Bank VI:   TEMPLE    (choral, chordal, SPIRIT-friendly)
// Bank VII:  VOID      (vast diffused space)
// Bank VIII: LIGHTNING (glassy, shimmering, bright)
// =====================================================
// Fields: {wavebank[4]}, detuneScale, morphSpread, sigilScale, sigilAlternate,
//         cutoffOffset, resBias, keyTrack,
//         delayTimeBase, delayFeedback, diffusionBase, freqShiftRange,
//         elementTilt, riteScale, outputGain

inline const AlgorithmEngine::AlgorithmConfig AlgorithmEngine::configs[TOTAL_ALGOS] = {
	// ===== BANK I: BREATH =====
	// 1 Bael - pure spread sines, open filter, gentle hall
	{{0, 0, 0, 0}, 0.4f, 0.05f, 0.0f, false, +0.5f, 0.00f, 0.6f, 1.8f, 0.55f, 0.70f, 0.4f, 0.0f, 0.0f, 1.00f},
	// 2 Agares - additive organ, bright and steady
	{{2, 2, 2, 2}, 0.3f, 0.10f, 0.0f, false, +0.3f, 0.05f, 0.7f, 0.8f, 0.40f, 0.60f, 0.2f, 0.0f, 0.0f, 1.00f},
	// 3 Vassago - soft PM shimmer pad
	{{0, 2, 0, 2}, 0.8f, 0.15f, 0.4f, true, 0.0f, 0.10f, 0.5f, 2.0f, 0.50f, 0.70f, 0.6f, 0.1f, 0.1f, 0.95f},
	// 4 Samigina - vocal formant drift
	{{0, 0, 2, 2}, 0.6f, 0.35f, 0.5f, true, -0.5f, 0.15f, 0.4f, 2.0f, 0.50f, 0.80f, 0.3f, 0.0f, 0.15f, 1.00f},
	// 5 Marbas - breathing cluster
	{{2, 2, 2, 2}, 1.2f, 0.30f, 0.6f, true, -0.3f, 0.10f, 0.3f, 1.8f, 0.50f, 0.70f, 0.5f, 0.0f, 0.25f, 0.95f},
	// 6 Valefor - glassy air, light FM
	{{0, 2, 2, 0}, 0.9f, 0.20f, 0.5f, false, +0.8f, 0.10f, 0.6f, 2.5f, 0.55f, 0.85f, 1.5f, 0.2f, 0.1f, 0.90f},
	// 7 Amon - warm hollow reeds
	{{0, 0, 0, 0}, 0.7f, 0.25f, 0.3f, false, -0.8f, 0.20f, 0.5f, 0.6f, 0.40f, 0.40f, 0.3f, -0.2f, 0.1f, 1.00f},
	// 8 Barbatos - wind through strings
	{{2, 3, 2, 3}, 0.5f, 0.15f, 0.4f, false, -0.4f, 0.25f, 0.4f, 1.2f, 0.45f, 0.75f, 0.8f, 0.0f, 0.2f, 0.90f},
	// 9 Paimon - full airy choir swell
	{{2, 2, 2, 2}, 1.0f, 0.40f, 0.5f, true, -0.2f, 0.15f, 0.5f, 3.0f, 0.60f, 0.85f, 0.5f, 0.0f, 0.2f, 0.95f},

	// ===== BANK II: TIDE =====
	// 10 Buer - slow chorus wash
	{{0, 0, 0, 0}, 1.5f, 0.15f, 0.2f, false, -0.3f, 0.05f, 0.3f, 0.03f, 0.30f, 0.50f, 1.0f, -0.2f, 0.1f, 0.95f},
	// 11 Gusion - deep current
	{{0, 3, 0, 3}, 1.0f, 0.20f, 0.3f, true, -1.0f, 0.10f, 0.2f, 0.5f, 0.50f, 0.60f, 0.7f, -0.3f, 0.2f, 1.00f},
	// 12 Sitri - rippling phase water
	{{0, 0, 2, 2}, 1.2f, 0.25f, 0.6f, true, -0.2f, 0.15f, 0.3f, 0.4f, 0.45f, 0.55f, 1.2f, 0.0f, 0.25f, 0.95f},
	// 13 Beleth - tidal swell
	{{0, 2, 0, 2}, 1.8f, 0.20f, 0.4f, false, -0.6f, 0.10f, 0.2f, 2.0f, 0.60f, 0.70f, 0.5f, -0.2f, 0.15f, 0.95f},
	// 14 Leraje - undertow
	{{3, 0, 3, 0}, 0.8f, 0.30f, 0.5f, true, -1.2f, 0.20f, 0.1f, 3.5f, 0.85f, 0.80f, 0.3f, 0.2f, 0.2f, 0.90f},
	// 15 Eligos - spray and foam
	{{3, 3, 0, 0}, 2.0f, 0.30f, 0.7f, false, 0.0f, 0.20f, 0.3f, 0.8f, 0.50f, 0.75f, 2.0f, 0.0f, 0.35f, 0.85f},
	// 16 Zepar - moon pull
	{{0, 0, 0, 0}, 1.4f, 0.10f, 0.3f, true, -0.8f, 0.10f, 0.4f, 1.5f, 0.55f, 0.65f, 0.8f, -0.1f, 0.1f, 1.00f},
	// 17 Botis - drowned bells
	{{1, 0, 1, 0}, 1.0f, 0.20f, 0.6f, false, -0.5f, 0.15f, 0.3f, 2.2f, 0.60f, 0.80f, 1.5f, 0.1f, 0.3f, 0.90f},
	// 18 Bathin - abyssal drift
	{{3, 0, 0, 3}, 0.6f, 0.15f, 0.3f, false, -1.8f, 0.10f, 0.1f, 3.5f, 0.80f, 0.90f, 0.4f, 0.0f, 0.15f, 0.95f},

	// ===== BANK III: STONE =====
	// 19 Sallos - bedrock sub
	{{0, 0, 0, 0}, 0.2f, 0.00f, 0.0f, false, -2.5f, 0.10f, 0.0f, 0.7f, 0.55f, 0.30f, 0.1f, 0.4f, 0.0f, 1.10f},
	// 20 Purson - granite drone
	{{0, 0, 0, 0}, 0.5f, 0.05f, 0.2f, false, -1.8f, 0.15f, 0.1f, 1.5f, 0.60f, 0.50f, 0.3f, 0.2f, 0.1f, 1.05f},
	// 21 Marax - slow tectonic beat
	{{0, 0, 3, 3}, 0.35f, 0.10f, 0.3f, true, -2.0f, 0.20f, 0.0f, 2.5f, 0.70f, 0.60f, 0.2f, 0.3f, 0.2f, 1.05f},
	// 22 Ipos - cavern resonance
	{{0, 2, 0, 2}, 0.4f, 0.15f, 0.2f, false, -1.5f, 0.35f, 0.2f, 2.8f, 0.70f, 0.80f, 0.3f, 0.0f, 0.1f, 1.00f},
	// 23 Aim - molten core
	{{3, 0, 3, 0}, 0.5f, 0.20f, 0.5f, false, -1.6f, 0.25f, 0.0f, 1.0f, 0.60f, 0.50f, 0.5f, 0.2f, 0.4f, 1.05f},
	// 24 Naberius - obsidian edge
	{{0, 3, 0, 3}, 0.7f, 0.15f, 0.6f, true, -1.0f, 0.30f, 0.1f, 0.6f, 0.55f, 0.40f, 0.6f, 0.0f, 0.5f, 1.00f},
	// 25 Glasya-Labolas - buried machine
	{{0, 0, 0, 0}, 0.3f, 0.30f, 0.7f, true, -2.2f, 0.20f, 0.0f, 0.15f, 0.80f, 0.20f, 0.5f, 0.3f, 0.5f, 1.00f},
	// 26 Bune - dust and ash
	{{3, 3, 3, 3}, 0.4f, 0.25f, 0.4f, false, -1.4f, 0.15f, 0.1f, 2.0f, 0.65f, 0.85f, 0.8f, 0.0f, 0.3f, 0.90f},
	// 27 Ronove - doom knell
	{{0, 3, 0, 3}, 0.25f, 0.10f, 0.5f, false, -2.4f, 0.20f, 0.0f, 3.5f, 0.80f, 0.90f, 0.3f, 0.3f, 0.6f, 1.15f},

	// ===== BANK IV: EMBER =====
	// 28 Berith - small bright bell
	{{1, 1, 1, 1}, 0.6f, 0.10f, 0.8f, false, +0.8f, 0.15f, 0.6f, 1.5f, 0.45f, 0.60f, 1.5f, 0.0f, 0.2f, 0.90f},
	// 29 Astaroth - brass glow
	{{1, 0, 1, 0}, 0.8f, 0.20f, 1.0f, false, 0.0f, 0.20f, 0.5f, 0.5f, 0.50f, 0.40f, 0.8f, -0.1f, 0.3f, 0.95f},
	// 30 Forneus - struck bronze
	{{1, 1, 1, 1}, 1.0f, 0.25f, 1.3f, true, +0.3f, 0.25f, 0.4f, 2.0f, 0.50f, 0.60f, 2.0f, 0.0f, 0.4f, 0.90f},
	// 31 Foras - gamelan court
	{{1, 2, 1, 2}, 1.2f, 0.30f, 1.1f, false, +0.5f, 0.20f, 0.5f, 0.8f, 0.45f, 0.50f, 1.2f, 0.1f, 0.5f, 0.90f},
	// 32 Asmoday - furnace choir
	{{1, 1, 2, 2}, 1.5f, 0.35f, 1.5f, true, -0.3f, 0.30f, 0.3f, 1.8f, 0.55f, 0.70f, 1.5f, 0.0f, 0.4f, 0.90f},
	// 33 Gaap - anvils afar
	{{1, 3, 1, 3}, 1.0f, 0.20f, 1.6f, false, -0.6f, 0.25f, 0.2f, 2.5f, 0.60f, 0.75f, 2.5f, 0.2f, 0.5f, 0.85f},
	// 34 Furfur - spark shower
	{{1, 1, 3, 3}, 2.0f, 0.30f, 1.8f, true, +0.6f, 0.30f, 0.4f, 0.3f, 0.50f, 0.30f, 3.0f, 0.0f, 0.45f, 0.85f},
	// 35 Marchosias - white heat
	{{1, 1, 1, 1}, 1.4f, 0.15f, 2.0f, false, +0.4f, 0.35f, 0.3f, 0.12f, 0.40f, 0.45f, 2.0f, 0.0f, 0.4f, 0.90f},
	// 36 Stolas - cooling iron song
	{{1, 0, 0, 1}, 0.7f, 0.25f, 1.2f, true, -0.9f, 0.20f, 0.3f, 2.8f, 0.65f, 0.80f, 1.0f, -0.2f, 0.3f, 0.95f},

	// ===== BANK V: STORM =====
	// 37 Phenex - first thunderhead
	{{0, 0, 0, 0}, 1.5f, 0.20f, 1.8f, false, 0.0f, 0.30f, 0.3f, 0.3f, 0.65f, 0.20f, 2.5f, 0.0f, 0.6f, 1.10f},
	// 38 Halphas - folded scream
	{{3, 3, 3, 3}, 1.2f, 0.10f, 1.0f, false, -0.2f, 0.40f, 0.3f, 0.4f, 0.30f, 0.30f, 1.0f, 0.0f, 0.8f, 1.00f},
	// 39 Malphas - strafing rain
	{{1, 3, 1, 3}, 2.0f, 0.15f, 0.9f, false, +0.3f, 0.10f, 0.4f, 0.25f, 0.50f, 0.10f, 0.5f, 0.0f, 0.4f, 0.95f},
	// 40 Raum - sirens crossed
	{{0, 0, 0, 0}, 2.2f, 0.00f, 1.5f, true, -0.1f, 0.35f, 0.5f, 0.2f, 0.50f, 0.15f, 1.5f, -0.3f, 0.3f, 0.95f},
	// 41 Focalor - engine room
	{{0, 0, 0, 0}, 0.3f, 0.30f, 0.7f, true, -1.9f, 0.25f, 0.1f, 0.15f, 0.80f, 0.20f, 0.5f, 0.0f, 0.5f, 1.00f},
	// 42 Vepar - acid squall
	{{0, 0, 0, 0}, 0.5f, 0.10f, 0.0f, false, -1.1f, 0.50f, 0.7f, 0.35f, 0.55f, 0.20f, 0.3f, 0.0f, 0.0f, 1.05f},
	// 43 Sabnock - razor hail
	{{0, 0, 0, 0}, 3.0f, 0.15f, 0.3f, false, +0.4f, 0.20f, 0.3f, 0.03f, 0.25f, 0.40f, 1.0f, 0.0f, 0.9f, 0.95f},
	// 44 Shax - sheared metal
	{{1, 1, 1, 1}, 1.0f, 0.35f, 2.0f, false, +0.2f, 0.25f, 0.4f, 0.12f, 0.40f, 0.45f, 2.0f, 0.0f, 0.4f, 0.90f},
	// 45 Vine - black vortex
	{{0, 3, 0, 3}, 0.2f, 0.00f, 0.5f, false, -2.3f, 0.20f, 0.0f, 3.5f, 0.80f, 0.90f, 0.3f, 0.3f, 0.7f, 1.15f},

	// ===== BANK VI: TEMPLE =====
	// 46 Bifrons - plainchant
	{{0, 0, 0, 0}, 0.10f, 0.00f, 0.0f, false, -0.4f, 0.05f, 0.5f, 1.8f, 0.50f, 0.70f, 0.2f, 0.0f, 0.0f, 1.00f},
	// 47 Uvall - cathedral voices
	{{0, 0, 0, 0}, 0.15f, 0.05f, 0.4f, true, -0.5f, 0.10f, 0.4f, 3.0f, 0.55f, 0.85f, 0.5f, 0.0f, 0.2f, 0.90f},
	// 48 Haagenti - organ mass
	{{2, 2, 2, 2}, 0.10f, 0.05f, 0.0f, false, +0.2f, 0.10f, 0.7f, 0.8f, 0.40f, 0.60f, 0.2f, 0.0f, 0.0f, 1.00f},
	// 49 Crocell - glass choir
	{{2, 2, 2, 2}, 0.20f, 0.10f, 0.4f, true, +0.6f, 0.15f, 0.6f, 2.5f, 0.50f, 0.85f, 3.5f, 0.0f, 0.1f, 0.85f},
	// 50 Furcas - monastery bells
	{{1, 2, 1, 2}, 0.15f, 0.10f, 0.8f, false, +0.4f, 0.20f, 0.5f, 1.5f, 0.45f, 0.60f, 1.5f, 0.0f, 0.3f, 0.90f},
	// 51 Balam - censer smoke
	{{2, 3, 2, 3}, 0.20f, 0.15f, 0.3f, false, -0.7f, 0.10f, 0.3f, 2.2f, 0.60f, 0.80f, 0.8f, 0.0f, 0.15f, 0.90f},
	// 52 Alloces - crypt harmonics
	{{2, 0, 2, 0}, 0.10f, 0.00f, 0.2f, false, -1.3f, 0.25f, 0.2f, 2.8f, 0.70f, 0.80f, 0.3f, 0.1f, 0.1f, 0.95f},
	// 53 Caim - antiphon answer
	{{0, 2, 2, 0}, 0.20f, 0.10f, 0.5f, true, -0.1f, 0.15f, 0.5f, 1.2f, 0.50f, 0.70f, 0.6f, 0.2f, 0.2f, 0.95f},
	// 54 Murmur - requiem swell
	{{2, 2, 0, 0}, 0.15f, 0.05f, 0.3f, false, -0.6f, 0.10f, 0.4f, 3.2f, 0.65f, 0.90f, 0.4f, 0.0f, 0.1f, 0.90f},

	// ===== BANK VII: VOID =====
	// 55 Orobas - event horizon
	{{0, 0, 0, 0}, 0.01f, 0.00f, 0.5f, false, +0.7f, 0.05f, 0.3f, 3.0f, 0.60f, 0.90f, 4.0f, 0.0f, 0.1f, 0.85f},
	// 56 Gremory - frozen spectra
	{{2, 2, 2, 2}, 0.05f, 0.50f, 0.0f, false, -0.2f, 0.00f, 0.2f, 3.5f, 0.90f, 0.95f, 0.5f, 0.0f, 0.0f, 0.80f},
	// 57 Ose - nebula pad
	{{0, 0, 0, 0}, 1.5f, 0.20f, 0.3f, false, -0.4f, 0.10f, 0.3f, 2.0f, 0.50f, 0.70f, 0.3f, 0.0f, 0.1f, 1.00f},
	// 58 Amy - starfield
	{{2, 2, 2, 2}, 0.5f, 0.40f, 0.4f, true, +0.3f, 0.25f, 0.5f, 2.5f, 0.50f, 0.85f, 3.5f, 0.0f, 0.1f, 0.85f},
	// 59 Oriax - solar wind
	{{3, 3, 3, 3}, 1.0f, 0.30f, 0.6f, false, -0.9f, 0.10f, 0.1f, 2.5f, 0.50f, 0.90f, 2.0f, 0.0f, 0.5f, 0.75f},
	// 60 Vapula - deep ocean of night
	{{3, 0, 3, 0}, 0.3f, 0.10f, 0.3f, true, -1.7f, 0.10f, 0.0f, 3.5f, 0.85f, 0.80f, 0.3f, 0.2f, 0.2f, 0.90f},
	// 61 Zagan - gravity bloom
	{{0, 3, 0, 3}, 0.15f, 0.00f, 0.0f, false, -2.6f, 0.15f, 0.0f, 3.8f, 0.75f, 0.95f, 0.2f, 0.4f, 0.0f, 1.10f},
	// 62 Valac - comet tail
	{{2, 1, 2, 1}, 0.8f, 0.15f, 0.7f, false, +0.5f, 0.15f, 0.6f, 1.5f, 0.45f, 0.60f, 1.5f, 0.0f, 0.3f, 0.90f},
	// 63 Andras - collapsed star
	{{3, 3, 0, 0}, 0.4f, 0.20f, 0.8f, true, -1.2f, 0.30f, 0.1f, 3.6f, 0.80f, 0.95f, 1.0f, 0.2f, 0.3f, 0.90f},

	// ===== BANK VIII: LIGHTNING =====
	// 64 Flauros - arc flash
	{{0, 0, 0, 0}, 1.0f, 0.05f, 0.4f, false, +1.2f, 0.20f, 0.6f, 2.5f, 0.55f, 0.90f, 3.0f, 0.0f, 0.1f, 0.90f},
	// 65 Andrealphus - prism strike
	{{2, 1, 2, 1}, 0.8f, 0.15f, 0.7f, false, +1.0f, 0.30f, 0.7f, 1.5f, 0.45f, 0.60f, 1.5f, 0.0f, 0.3f, 0.90f},
	// 66 Kimaris - ion shimmer
	{{2, 2, 2, 2}, 0.5f, 0.40f, 0.4f, true, +0.8f, 0.35f, 0.5f, 2.5f, 0.50f, 0.85f, 3.5f, 0.0f, 0.1f, 0.85f},
	// 67 Amdusias - thunder chord
	{{0, 1, 0, 1}, 1.2f, 0.20f, 1.2f, true, +0.3f, 0.30f, 0.5f, 0.6f, 0.35f, 0.40f, 0.5f, -0.2f, 0.3f, 0.95f},
	// 68 Belial - white lattice
	{{2, 2, 2, 2}, 0.5f, 0.50f, 0.0f, false, +0.9f, 0.20f, 0.4f, 2.5f, 0.55f, 0.80f, 0.3f, 0.0f, 0.8f, 0.90f},
	// 69 Decarabia - static field
	{{3, 3, 3, 3}, 1.0f, 0.30f, 0.6f, false, +0.6f, 0.25f, 0.3f, 2.5f, 0.50f, 0.90f, 2.0f, 0.0f, 0.5f, 0.75f},
	// 70 Seere - glass storm
	{{3, 2, 3, 2}, 1.5f, 0.25f, 0.9f, true, +1.1f, 0.40f, 0.4f, 0.8f, 0.50f, 0.75f, 2.5f, 0.0f, 0.35f, 0.85f},
	// 71 Dantalion - many faces
	{{1, 2, 3, 0}, 1.0f, 0.30f, 1.0f, true, +0.4f, 0.30f, 0.4f, 1.0f, 0.50f, 0.85f, 1.5f, 0.0f, 0.4f, 0.85f},
	// 72 Andromalius - final revelation
	{{3, 1, 2, 0}, 2.0f, 0.35f, 1.5f, true, +0.7f, 0.45f, 0.5f, 3.0f, 0.70f, 0.90f, 4.0f, 0.0f, 0.6f, 0.90f},
};

} // namespace dsputil

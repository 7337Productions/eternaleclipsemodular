#include <chrono>
#include "plugin.hpp"
#include "EclipseWidgets.hpp"
#include "Orbits.hpp"

// Sephirothic Modulator (OMN-67): ten CV outputs laid out as the Tree of
// Life, each a computed astrological signal for that sephirah's planetary
// correspondence, plus a Da'at matrix mixer blending them into two busses.
// All positions run from the system clock (REAL) or a time-warped clock
// (WARP, up to x10^8) -- no network, just Kepler.

static const int NUM_NODES = 10;

static const char* NODE_NAMES[NUM_NODES] = {
	"KETHER", "CHOKMAH", "BINAH", "CHESED", "GEBURAH",
	"TIPHARETH", "NETZACH", "HOD", "YESOD", "MALKUTH"
};

struct SephirothicModulator : Module {
	enum ParamId {
		WARP_PARAM,
		MODE_PARAM,
		MALKUTH_PARAM,
		ENUMS(MIX_PARAMS, 2 * NUM_NODES), // [bus][node]
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		KETHER_OUTPUT,
		CHOKMAH_OUTPUT,
		BINAH_OUTPUT,
		CHESED_OUTPUT,
		GEBURAH_OUTPUT,
		TIPHARETH_OUTPUT,
		NETZACHX_OUTPUT,
		NETZACHY_OUTPUT,
		HOD_OUTPUT,
		HODTRIG_OUTPUT,
		YESOD_OUTPUT,
		MALKUTH_OUTPUT,
		BUS1_OUTPUT,
		BUS2_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	orbits::Sky sky;
	double simUnix = 0.0;
	float node[NUM_NODES] = {};  // primary CV per sephirah (Netzach = X)
	float netzachY = 0.f;
	float glow[NUM_NODES] = {};  // panel halo brightness, read by the widget

	// Mercury retrograde station detection
	double lastMercLon = 0.0;
	int lastMercDir = 0;
	dsp::PulseGenerator stationPulse;

	dsp::ClockDivider skyDivider;

	static double realNow() {
		using namespace std::chrono;
		return duration<double>(system_clock::now().time_since_epoch()).count();
	}

	SephirothicModulator() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(WARP_PARAM, 0.f, 8.f, 5.f, "Time warp", "x", 10.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Time source", {"Real (true sky)", "Warp"});
		configParam(MALKUTH_PARAM, -5.f, 5.f, 0.f, "Malkuth voltage", " V");
		for (int b = 0; b < 2; b++)
			for (int n = 0; n < NUM_NODES; n++)
				configParam(MIX_PARAMS + b * NUM_NODES + n, -1.f, 1.f, 0.f,
					string::f("Da'at %s: %s", b ? "II" : "I", NODE_NAMES[n]), "%", 0.f, 100.f);

		configOutput(KETHER_OUTPUT, "Kether - Neptune drift (+-5V)");
		configOutput(CHOKMAH_OUTPUT, "Chokmah - Uranus zodiac, 12 steps of 1/12 V");
		configOutput(BINAH_OUTPUT, "Binah - Saturn clock (10V square)");
		configOutput(CHESED_OUTPUT, "Chesed - Jupiter clock (10V square)");
		configOutput(GEBURAH_OUTPUT, "Geburah - Mars eccentric LFO (+-5V)");
		configOutput(TIPHARETH_OUTPUT, "Tiphareth - solar year ramp (0-10V)");
		configOutput(NETZACHX_OUTPUT, "Netzach - Venus pentagram X (+-5V)");
		configOutput(NETZACHY_OUTPUT, "Netzach - Venus pentagram Y (+-5V)");
		configOutput(HOD_OUTPUT, "Hod - Mercury elongation (+-5V)");
		configOutput(HODTRIG_OUTPUT, "Hod - Mercury retrograde station trigger");
		configOutput(YESOD_OUTPUT, "Yesod - lunar phase illumination (0-10V)");
		configOutput(MALKUTH_OUTPUT, "Malkuth - manual tonic");
		configOutput(BUS1_OUTPUT, "Da'at bus I");
		configOutput(BUS2_OUTPUT, "Da'at bus II");

		skyDivider.setDivision(32);
		simUnix = realNow();
		sky.compute(simUnix);
		lastMercLon = sky.geoLon[orbits::MERCURY];
	}

	void process(const ProcessArgs& args) override {
		if (skyDivider.process()) {
			double dt = args.sampleTime * skyDivider.getDivision();
			if (params[MODE_PARAM].getValue() > 0.5f) {
				simUnix += dt * std::pow(10.0, (double)params[WARP_PARAM].getValue());
				// Keep within the +-500 century range the element set tolerates
				simUnix = std::fmin(std::fmax(simUnix, 946727935.816 - 1.578e12), 946727935.816 + 1.578e12);
			}
			else {
				simUnix = realNow();
			}
			sky.compute(simUnix);

			using namespace orbits;
			double sunDeg = sky.sunLon / DEG;
			if (sunDeg < 0.0) sunDeg += 360.0;
			double uraDeg = sky.geoLon[URANUS] / DEG;
			if (uraDeg < 0.0) uraDeg += 360.0;
			double satDeg = sky.helioLon[SATURN] / DEG;
			if (satDeg < 0.0) satDeg += 360.0;
			double jupDeg = sky.helioLon[JUPITER] / DEG;
			if (jupDeg < 0.0) jupDeg += 360.0;

			// Kether/Neptune: near-DC source drift
			node[0] = 5.f * (float)std::sin(sky.helioLon[NEPTUNE]);
			// Chokmah/Uranus: current zodiac sign as 12 chromatic steps (1V/oct)
			int sign = clamp((int)(uraDeg / 30.0), 0, 11);
			node[1] = sign / 12.f;
			// Binah/Saturn + Chesed/Jupiter: structural clocks, one cycle per 30 deg
			node[2] = std::fmod(satDeg, 30.0) < 15.0 ? 10.f : 0.f;
			node[3] = std::fmod(jupDeg, 30.0) < 15.0 ? 10.f : 0.f;
			// Geburah/Mars: true longitude sine, eccentricity skews the speed
			node[4] = 5.f * (float)std::sin(sky.helioLon[MARS]);
			// Tiphareth/Sun: 0-10V ramp over the solar year
			node[5] = (float)(sunDeg / 36.0);
			// Netzach/Venus: geocentric position pair -> 8-year pentagram
			node[6] = 5.f * clamp((float)(sky.gx[VENUS] / 1.75), -1.f, 1.f);
			netzachY = 5.f * clamp((float)(sky.gy[VENUS] / 1.75), -1.f, 1.f);
			// Hod/Mercury: solar elongation, reverses through retrogrades
			double elong = std::remainder(sky.geoLon[MERCURY] - sky.sunLon, 2.0 * M_PI) / DEG;
			node[7] = 5.f * clamp((float)(elong / 30.0), -1.f, 1.f);
			// Yesod/Moon: illumination of the synodic phase
			node[8] = 10.f * 0.5f * (1.f - (float)std::cos(2.0 * M_PI * sky.moonPhase));
			// Malkuth: the manually set tonic the tree resolves into
			node[9] = params[MALKUTH_PARAM].getValue();

			// Mercury station: apparent geocentric motion reverses direction.
			// Only judge direction once at least 0.002 deg has accumulated, so
			// real-time (near-zero per-tick motion) stays noise-immune.
			double dLon = std::remainder(sky.geoLon[MERCURY] - lastMercLon, 2.0 * M_PI);
			if (std::fabs(dLon) > 0.002 * DEG) {
				int dir = dLon > 0.0 ? 1 : -1;
				if (lastMercDir != 0 && dir != lastMercDir)
					stationPulse.trigger(1e-3f);
				lastMercDir = dir;
				lastMercLon = sky.geoLon[MERCURY];
			}

			outputs[KETHER_OUTPUT].setVoltage(node[0]);
			outputs[CHOKMAH_OUTPUT].setVoltage(node[1]);
			outputs[BINAH_OUTPUT].setVoltage(node[2]);
			outputs[CHESED_OUTPUT].setVoltage(node[3]);
			outputs[GEBURAH_OUTPUT].setVoltage(node[4]);
			outputs[TIPHARETH_OUTPUT].setVoltage(node[5]);
			outputs[NETZACHX_OUTPUT].setVoltage(node[6]);
			outputs[NETZACHY_OUTPUT].setVoltage(netzachY);
			outputs[HOD_OUTPUT].setVoltage(node[7]);
			outputs[YESOD_OUTPUT].setVoltage(node[8]);
			outputs[MALKUTH_OUTPUT].setVoltage(node[9]);

			for (int b = 0; b < 2; b++) {
				float sum = 0.f;
				for (int n = 0; n < NUM_NODES; n++)
					sum += params[MIX_PARAMS + b * NUM_NODES + n].getValue() * node[n];
				outputs[BUS1_OUTPUT + b].setVoltage(clamp(sum, -10.f, 10.f));
			}

			for (int n = 0; n < NUM_NODES; n++)
				glow[n] = clamp(std::fabs(node[n]) / 7.f, 0.f, 1.f);
		}

		outputs[HODTRIG_OUTPUT].setVoltage(stationPulse.process(args.sampleTime) ? 10.f : 0.f);
	}

	void onReset() override {
		simUnix = realNow();
		lastMercDir = 0;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "simUnix", json_real(simUnix));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* simJ = json_object_get(rootJ, "simUnix");
		if (simJ)
			simUnix = json_real_value(simJ);
	}
};

// Sephirah centers [mm], shared by jacks, halos, and the panel SVG geometry
static const Vec NODE_POS[NUM_NODES] = {
	Vec(32, 24),  // Kether
	Vec(52, 34),  // Chokmah
	Vec(12, 34),  // Binah
	Vec(52, 52),  // Chesed
	Vec(12, 52),  // Geburah
	Vec(32, 61),  // Tiphareth
	Vec(52, 79),  // Netzach
	Vec(12, 79),  // Hod
	Vec(32, 88),  // Yesod
	Vec(32, 102), // Malkuth
};

// Amber halos behind the sephirah jacks, brightness tracking each output
struct TreeGlow : TransparentWidget {
	SephirothicModulator* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module)
			return;
		for (int n = 0; n < NUM_NODES; n++) {
			float g = module->glow[n];
			if (g < 0.01f)
				continue;
			Vec c = mm2px(NODE_POS[n]);
			float r = mm2px(7.f);
			NVGpaint paint = nvgRadialGradient(args.vg, c.x, c.y, mm2px(2.f), r,
				nvgRGBAf(1.f, 0.77f, 0.39f, 0.45f * g), nvgRGBAf(1.f, 0.77f, 0.39f, 0.f));
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, r);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}
	}
};

// Simulated ephemeris date readout (UTC)
struct DateDisplay : TransparentWidget {
	SephirothicModulator* module = NULL;

	// Howard Hinnant's civil_from_days: works far outside gmtime's range
	static void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
		z += 719468;
		int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		unsigned doe = (unsigned)(z - era * 146097);
		unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		int64_t yy = (int64_t)yoe + era * 400;
		unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		unsigned mp = (5 * doy + 2) / 153;
		d = doy - (153 * mp + 2) / 5 + 1;
		m = mp < 10 ? mp + 3 : mp - 9;
		y = (int)(yy + (m <= 2));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::string text = "2026-07-07";
		if (module) {
			int y;
			unsigned m, d;
			civilFromDays((int64_t)std::floor(module->simUnix / 86400.0), y, m, d);
			text = string::f("%d-%02u-%02u", y, m, d);
		}
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, eclipse::TITLE_SIZE);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, eclipse::ACCENT_COLOR);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};

struct SephirothicModulatorWidget : ModuleWidget {
	SephirothicModulatorWidget(SephirothicModulator* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SephirothicModulator.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		eclipse::addHeader(this, 55.88f, "S E P H I R O T H I C   M O D U L A T O R");

		TreeGlow* treeGlow = new TreeGlow;
		treeGlow->module = module;
		treeGlow->box.pos = Vec(0, 0);
		treeGlow->box.size = box.size;
		addChild(treeGlow);

		// Tree of Life jacks
		static const int NODE_OUTPUT[NUM_NODES] = {
			SephirothicModulator::KETHER_OUTPUT, SephirothicModulator::CHOKMAH_OUTPUT,
			SephirothicModulator::BINAH_OUTPUT, SephirothicModulator::CHESED_OUTPUT,
			SephirothicModulator::GEBURAH_OUTPUT, SephirothicModulator::TIPHARETH_OUTPUT,
			SephirothicModulator::NETZACHX_OUTPUT, SephirothicModulator::HOD_OUTPUT,
			SephirothicModulator::YESOD_OUTPUT, SephirothicModulator::MALKUTH_OUTPUT,
		};
		// The sephirah jacks are deliberately unlabeled: the tree geometry is
		// the label. Tooltips carry the correspondences for the uninitiated.
		for (int n = 0; n < NUM_NODES; n++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(NODE_POS[n]), module, NODE_OUTPUT[n]));

		// Netzach Y companion + Mercury station trigger
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(61, 79)), module, SephirothicModulator::NETZACHY_OUTPUT));
		eclipse::addLabel(this, Vec(52, 84.5f), "X", eclipse::FINE_SIZE);
		eclipse::addLabel(this, Vec(61, 84.5f), "Y", eclipse::FINE_SIZE);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(21, 79)), module, SephirothicModulator::HODTRIG_OUTPUT));
		eclipse::addLabel(this, Vec(21, 84.5f), "RX", eclipse::FINE_SIZE);

		// Malkuth level
		addParam(createParamCentered<Trimpot>(mm2px(Vec(43, 102)), module, SephirothicModulator::MALKUTH_PARAM));

		// Time controls + ephemeris date
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 115)), module, SephirothicModulator::WARP_PARAM));
		eclipse::addLabel(this, Vec(10, 121.5f), "WARP", eclipse::LABEL_SIZE);
		addParam(createParamCentered<CKSS>(mm2px(Vec(22, 115)), module, SephirothicModulator::MODE_PARAM));
		eclipse::addLabel(this, Vec(22, 121.5f), "TIME", eclipse::LABEL_SIZE);

		DateDisplay* date = new DateDisplay;
		date->module = module;
		date->box.size = mm2px(Vec(34, 8));
		date->box.pos = mm2px(Vec(28, 111));
		addChild(date);

		// Da'at matrix: 10 rows x 2 busses, full sephirah names beside the knobs
		eclipse::addLabel(this, Vec(88.5f, 22.f), "DA'AT - THE GATE", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		eclipse::addLabel(this, Vec(93.5f, 27.f), "I", eclipse::FINE_SIZE, eclipse::DIM_COLOR);
		eclipse::addLabel(this, Vec(103.5f, 27.f), "II", eclipse::FINE_SIZE, eclipse::DIM_COLOR);
		for (int n = 0; n < NUM_NODES; n++) {
			float y = 33.f + 8.f * n;
			eclipse::addLabel(this, Vec(77.5f, y), NODE_NAMES[n], eclipse::LABEL_SIZE);
			addParam(createParamCentered<Trimpot>(mm2px(Vec(93.5f, y)), module, SephirothicModulator::MIX_PARAMS + n));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(103.5f, y)), module, SephirothicModulator::MIX_PARAMS + NUM_NODES + n));
		}
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(93.5f, 114)), module, SephirothicModulator::BUS1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(103.5f, 114)), module, SephirothicModulator::BUS2_OUTPUT));
		eclipse::addLabel(this, Vec(93.5f, 120.5f), "I", eclipse::FINE_SIZE, eclipse::ACCENT_COLOR);
		eclipse::addLabel(this, Vec(103.5f, 120.5f), "II", eclipse::FINE_SIZE, eclipse::ACCENT_COLOR);
	}

	void appendContextMenu(Menu* menu) override {
		SephirothicModulator* module = getModule<SephirothicModulator>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Set time to now", "", [=]() {
			module->simUnix = SephirothicModulator::realNow();
		}));
	}
};

Model* modelSephirothicModulator = createModel<SephirothicModulator, SephirothicModulatorWidget>("SephirothicModulator");

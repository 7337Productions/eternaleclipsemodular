#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <osdialog.h>
#include "plugin.hpp"
#include "EclipseWidgets.hpp"
#include "dsp/Util.hpp"
#include "dsp/ComplexOsc.hpp"
#include "dsp/OtaFilter.hpp"
#include "dsp/NamStage.hpp"

using namespace corona;

// Coronal Annihilator: a stereo voice. Either the external stereo input or
// the internal CORE/FLARE dual oscillator feeds the NEURAL stage (a loaded
// .nam neural amp model, one network per channel), then the UMBRA 4-pole
// saturating lowpass, then the stereo outputs.
struct CoronalAnnihilator : Module {
	enum ParamId {
		CORE_PITCH_PARAM,
		CORE_WAVE_PARAM,
		FLARE_PITCH_PARAM,
		FLARE_FINE_PARAM,
		TRACK_PARAM,
		PLASMA_PARAM,
		EJECTA_PARAM,
		ARC_PARAM,
		SURGE_PARAM,
		FM_MODE_PARAM,
		LOCK_PARAM,
		MIX_PARAM,
		SPREAD_PARAM,
		SOURCE_PARAM,
		DRIVE_PARAM,
		LEVEL_PARAM,
		NEURAL_ACTIVE_PARAM,
		CUTOFF_PARAM,
		RES_PARAM,
		KEY_PARAM,
		SLOPE_PARAM,
		// CV attenuverters
		PLASMA_ATT_PARAM,
		EJECTA_ATT_PARAM,
		ARC_ATT_PARAM,
		SURGE_ATT_PARAM,
		MIX_ATT_PARAM,
		SPREAD_ATT_PARAM,
		CUTOFF_ATT_PARAM,
		RES_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INL_INPUT,
		INR_INPUT,
		CORE_VOCT_INPUT,
		FLARE_VOCT_INPUT,
		PLASMA_INPUT,
		EJECTA_INPUT,
		ARC_INPUT,
		SURGE_INPUT,
		MIX_INPUT,
		SPREAD_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		NEURAL_LIGHT,
		LIGHTS_LEN
	};

	// Model display states
	enum LoadState { LOAD_NONE, LOAD_LOADING, LOAD_OK, LOAD_MISSING, LOAD_BAD };

	ComplexOsc osc;
	OtaCoeffs otaCoeffs;
	OtaFilter fltL, fltR;
	NamStage nam;

	// ----- model ownership -----
	// activeModel is owned by the audio thread. The loader thread parks a
	// freshly built set in pendingModel; process() swaps it in and parks the
	// previous one in retiredModel for the UI thread (widget step) to free.
	NamModelSet* activeModel = NULL;
	std::atomic<NamModelSet*> pendingModel{NULL};
	std::atomic<NamModelSet*> retiredModel{NULL};
	std::atomic<bool> clearRequested{false};
	std::thread loader;
	std::atomic<int> loadState{LOAD_NONE};
	std::atomic<int> engineRate{48000};
	// UI-thread state
	std::string modelPath;
	std::mutex nameMutex;
	std::string displayName;   // panel display
	std::string displayInfo;   // context-menu summary (name, rate, loudness)
	// Menu options (read by the audio thread; bool writes are atomic enough)
	bool monoModel = false;
	bool normalizeLoudness = true;

	CoronalAnnihilator() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CORE_PITCH_PARAM, -54.f, 54.f, -12.f, "Core pitch", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configSwitch(CORE_WAVE_PARAM, 0.f, 2.f, 0.f, "Core wave", {"Sine", "Triangle", "Saw"});
		configParam(FLARE_PITCH_PARAM, -54.f, 54.f, -12.f, "Flare pitch", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FLARE_FINE_PARAM, -1.f, 1.f, 0.f, "Flare fine tune", " semitones");
		configSwitch(TRACK_PARAM, 0.f, 1.f, 1.f, "Track (Flare follows Core V/oct)", {"Off", "On"});
		configParam(PLASMA_PARAM, 0.f, 1.f, 0.f, "Plasma (Flare shape: sine \xE2\x86\x92 tri \xE2\x86\x92 saw \xE2\x86\x92 pulse)", "%", 0.f, 100.f);
		configParam(EJECTA_PARAM, 0.f, 1.f, 0.f, "Ejecta (Flare wavefold)", "%", 0.f, 100.f);
		configParam(ARC_PARAM, 0.f, 1.f, 0.f, "Arc (Flare phase warp)", "%", 0.f, 100.f);
		configParam(SURGE_PARAM, 0.f, 1.f, 0.f, "Surge (Core \xE2\x86\x92 Flare FM index)", "%", 0.f, 100.f);
		configSwitch(FM_MODE_PARAM, 0.f, 1.f, 0.f, "Surge mode", {"Linear (through-zero)", "Exponential"});
		configSwitch(LOCK_PARAM, 0.f, 2.f, 0.f, "Lock (Flare sync to Core)", {"Off", "Soft", "Hard"});
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix (Core \xE2\x86\x90 / Flare \xE2\x86\x92)", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Spread (Core left / Flare right)", "%", 0.f, 100.f);
		configSwitch(SOURCE_PARAM, 0.f, 1.f, 1.f, "Source", {"Stereo input", "Oscillators"});
		configParam(DRIVE_PARAM, -30.f, 18.f, -12.f, "Drive (neural model input)", " dB");
		configParam(LEVEL_PARAM, -24.f, 24.f, 0.f, "Level (neural model output)", " dB");
		configSwitch(NEURAL_ACTIVE_PARAM, 0.f, 1.f, 1.f, "Neural stage", {"Bypassed", "Active"});
		configParam(CUTOFF_PARAM, 0.f, 1.f, 1.f, "Umbra cutoff", " Hz", 1024.f, 20.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Umbra resonance", "%", 0.f, 100.f);
		configParam(KEY_PARAM, 0.f, 1.f, 0.f, "Umbra key tracking (Core V/oct)", "%", 0.f, 100.f);
		configSwitch(SLOPE_PARAM, 0.f, 1.f, 1.f, "Umbra slope", {"12 dB/oct", "24 dB/oct"});
		configParam(PLASMA_ATT_PARAM, -1.f, 1.f, 0.f, "Plasma CV amount", "%", 0.f, 100.f);
		configParam(EJECTA_ATT_PARAM, -1.f, 1.f, 0.f, "Ejecta CV amount", "%", 0.f, 100.f);
		configParam(ARC_ATT_PARAM, -1.f, 1.f, 0.f, "Arc CV amount", "%", 0.f, 100.f);
		configParam(SURGE_ATT_PARAM, -1.f, 1.f, 0.f, "Surge CV amount", "%", 0.f, 100.f);
		configParam(MIX_ATT_PARAM, -1.f, 1.f, 0.f, "Mix CV amount", "%", 0.f, 100.f);
		configParam(SPREAD_ATT_PARAM, -1.f, 1.f, 0.f, "Spread CV amount", "%", 0.f, 100.f);
		configParam(CUTOFF_ATT_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount", "%", 0.f, 100.f);
		configParam(RES_ATT_PARAM, -1.f, 1.f, 0.f, "Resonance CV amount", "%", 0.f, 100.f);

		configInput(INL_INPUT, "Left audio");
		configInput(INR_INPUT, "Right audio (normalled to left)");
		configInput(CORE_VOCT_INPUT, "Core V/oct");
		configInput(FLARE_VOCT_INPUT, "Flare V/oct");
		configInput(PLASMA_INPUT, "Plasma CV");
		configInput(EJECTA_INPUT, "Ejecta CV");
		configInput(ARC_INPUT, "Arc CV");
		configInput(SURGE_INPUT, "Surge CV");
		configInput(MIX_INPUT, "Mix CV");
		configInput(SPREAD_INPUT, "Spread CV");
		configInput(CUTOFF_INPUT, "Cutoff CV (1 V/oct)");
		configInput(RES_INPUT, "Resonance CV");
		configOutput(OUTL_OUTPUT, "Left audio");
		configOutput(OUTR_OUTPUT, "Right audio");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);

		engineRate = (int)APP->engine->getSampleRate();
		nam.configure(engineRate, engineRate);
	}

	~CoronalAnnihilator() override {
		if (loader.joinable())
			loader.join();
		delete pendingModel.exchange(NULL);
		delete retiredModel.exchange(NULL);
		delete activeModel;
	}

	// ----- model loading (UI / patch-load thread) -----
	void requestLoad(const std::string& path) {
		if (loader.joinable())
			loader.join();
		modelPath = path;
		if (path.empty()) {
			delete pendingModel.exchange(NULL); // an unconsumed load must not resurrect after the clear
			clearRequested = true;
			loadState = LOAD_NONE;
			setDisplayName("");
			return;
		}
		loadState = LOAD_LOADING;
		setDisplayName(system::getStem(path));
		int rate = engineRate.load();
		loader = std::thread([this, path, rate]() {
			std::string error;
			NamModelSet* m = NamModelSet::load(path, (double)rate, error);
			if (!m) {
				loadState = (error == "missing") ? LOAD_MISSING : LOAD_BAD;
				return;
			}
			std::string info = m->name;
			info += m->expectedRate > 0 ? string::f(" \xC2\xB7 %d Hz", (int)m->expectedRate) : " \xC2\xB7 any rate";
			if (m->hasLoudness)
				info += string::f(" \xC2\xB7 %.1f dB", m->loudness);
			setDisplayName(m->name, info);
			delete pendingModel.exchange(m); // drop a still-unconsumed earlier load
			loadState = LOAD_OK;
		});
	}

	void setDisplayName(const std::string& name, const std::string& info = "") {
		std::lock_guard<std::mutex> lock(nameMutex);
		displayName = name;
		displayInfo = info;
	}

	std::string getDisplayName() {
		std::lock_guard<std::mutex> lock(nameMutex);
		return displayName;
	}

	std::string getDisplayInfo() {
		std::lock_guard<std::mutex> lock(nameMutex);
		return displayInfo;
	}

	void retire(NamModelSet* m) {
		if (!m)
			return;
		NamModelSet* expected = NULL;
		if (!retiredModel.compare_exchange_strong(expected, m))
			delete m; // slot still occupied (no widget to drain it): free inline
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engineRate = (int)e.sampleRate;
		int modelRate = engineRate;
		if (activeModel) {
			if (activeModel->expectedRate > 0)
				modelRate = (int)activeModel->expectedRate;
			else
				for (int c = 0; c < 2; c++)
					activeModel->net[c]->ResetAndPrewarm(e.sampleRate, NamModelSet::MAX_FRAMES);
		}
		nam.configure(engineRate, modelRate);
		osc.reset();
		fltL.reset();
		fltR.reset();
	}

	void onReset() override {
		osc.reset();
		fltL.reset();
		fltR.reset();
		nam.reset();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "modelPath", json_string(modelPath.c_str()));
		json_object_set_new(rootJ, "monoModel", json_boolean(monoModel));
		json_object_set_new(rootJ, "normalizeLoudness", json_boolean(normalizeLoudness));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* monoJ = json_object_get(rootJ, "monoModel");
		if (monoJ)
			monoModel = json_boolean_value(monoJ);
		json_t* normJ = json_object_get(rootJ, "normalizeLoudness");
		if (normJ)
			normalizeLoudness = json_boolean_value(normJ);
		json_t* pathJ = json_object_get(rootJ, "modelPath");
		if (pathJ) {
			std::string path = json_string_value(pathJ);
			if (!path.empty())
				requestLoad(path);
		}
	}

	void process(const ProcessArgs& args) override {
		// ===== Model hand-off =====
		if (clearRequested.exchange(false)) {
			retire(activeModel);
			activeModel = NULL;
		}
		if (NamModelSet* m = pendingModel.exchange(NULL)) {
			retire(activeModel);
			activeModel = m;
			int modelRate = m->expectedRate > 0 ? (int)m->expectedRate : (int)args.sampleRate;
			nam.configure((int)args.sampleRate, modelRate);
		}

		// ===== Source =====
		float coreVoct = clampf(inputs[CORE_VOCT_INPUT].getVoltage(), -10.f, 10.f);
		float sigL, sigR; // +/-1 units
		if (params[SOURCE_PARAM].getValue() > 0.5f) {
			float flareVoct = clampf(inputs[FLARE_VOCT_INPUT].getVoltage(), -10.f, 10.f);
			if (params[TRACK_PARAM].getValue() > 0.5f)
				flareVoct += coreVoct;
			ComplexOsc::Params p;
			p.freqA = dsp::FREQ_C4 * std::exp2(params[CORE_PITCH_PARAM].getValue() / 12.f + coreVoct);
			p.freqB = dsp::FREQ_C4 * std::exp2((params[FLARE_PITCH_PARAM].getValue() + params[FLARE_FINE_PARAM].getValue()) / 12.f + flareVoct);
			p.waveA = (int)std::round(params[CORE_WAVE_PARAM].getValue());
			p.shape = cvParam(PLASMA_PARAM, PLASMA_INPUT, PLASMA_ATT_PARAM);
			p.fold = cvParam(EJECTA_PARAM, EJECTA_INPUT, EJECTA_ATT_PARAM);
			p.arc = cvParam(ARC_PARAM, ARC_INPUT, ARC_ATT_PARAM);
			p.fm = cvParam(SURGE_PARAM, SURGE_INPUT, SURGE_ATT_PARAM);
			p.fmExp = params[FM_MODE_PARAM].getValue() > 0.5f;
			p.sync = (int)std::round(params[LOCK_PARAM].getValue());
			float a, b;
			osc.process(p, args.sampleRate, a, b);

			// Equal-power mix, then spread Core left / Flare right
			float mix = cvParam(MIX_PARAM, MIX_INPUT, MIX_ATT_PARAM);
			float spread = cvParam(SPREAD_PARAM, SPREAD_INPUT, SPREAD_ATT_PARAM);
			float gA = std::cos(mix * (float)M_PI_2);
			float gB = std::sin(mix * (float)M_PI_2);
			float ca = gA * a, cb = gB * b;
			sigL = 0.5f * (ca * (1.f + spread) + cb * (1.f - spread));
			sigR = 0.5f * (ca * (1.f - spread) + cb * (1.f + spread));
		}
		else {
			sigL = sanitize(inputs[INL_INPUT].getVoltage()) * 0.2f;
			sigR = inputs[INR_INPUT].isConnected() ? sanitize(inputs[INR_INPUT].getVoltage()) * 0.2f : sigL;
		}

		// ===== Neural stage =====
		bool neuralActive = params[NEURAL_ACTIVE_PARAM].getValue() > 0.5f;
		if (neuralActive && activeModel) {
			float drive = dbToGain(params[DRIVE_PARAM].getValue());
			float level = dbToGain(params[LEVEL_PARAM].getValue());
			if (normalizeLoudness && activeModel->hasLoudness)
				level *= dbToGain(-18.f - activeModel->loudness);
			float l, r;
			nam.process(sigL * drive, sigR * drive, activeModel, monoModel, l, r);
			sigL = l * level;
			sigR = r * level;
		}
		lights[NEURAL_LIGHT].setBrightness(neuralActive && activeModel ? 1.f : (neuralActive ? 0.25f : 0.f));

		// ===== Umbra filter =====
		float cutoffExp = params[CUTOFF_PARAM].getValue() * 10.f
			+ clampf(inputs[CUTOFF_INPUT].getVoltage(), -10.f, 10.f) * params[CUTOFF_ATT_PARAM].getValue()
			+ params[KEY_PARAM].getValue() * coreVoct;
		float cutoff = clampf(20.f * std::exp2(cutoffExp), 20.f, std::min(20000.f, args.sampleRate * 0.45f));
		float res = cvParam(RES_PARAM, RES_INPUT, RES_ATT_PARAM);
		otaCoeffs.update(cutoff, res, args.sampleRate);
		bool fourPole = params[SLOPE_PARAM].getValue() > 0.5f;
		sigL = fltL.process(sigL, otaCoeffs, fourPole);
		sigR = fltR.process(sigR, otaCoeffs, fourPole);

		// ===== Output =====
		float outL = sigL * 5.f;
		float outR = sigR * 5.f;
		if (outputs[OUTL_OUTPUT].isConnected() && !outputs[OUTR_OUTPUT].isConnected())
			outputs[OUTL_OUTPUT].setVoltage(0.5f * (outL + outR));
		else
			outputs[OUTL_OUTPUT].setVoltage(outL);
		outputs[OUTR_OUTPUT].setVoltage(outR);
	}

	// Unipolar knob + CV (10 V = full range) through its attenuverter, NaN-safe
	float cvParam(int paramId, int inputId, int attId) {
		return clampf(params[paramId].getValue()
			+ inputs[inputId].getVoltage() / 10.f * params[attId].getValue(), 0.f, 1.f);
	}
};

// ----- Model name display (over the NEURAL bezel); click to load -----
struct NamModelDisplay : TransparentWidget {
	CoronalAnnihilator* module = NULL;

	static void openModelDialog(CoronalAnnihilator* module) {
		std::string dir = module->modelPath.empty() ? asset::user("") : system::getDirectory(module->modelPath);
		osdialog_filters* filters = osdialog_filters_parse("Neural amp model:nam");
		char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
		osdialog_filters_free(filters);
		if (!pathC)
			return;
		std::string path = pathC;
		std::free(pathC);
		module->requestLoad(path);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::string text = "NO MODEL";
		NVGcolor color = eclipse::LABEL_COLOR;
		if (module) {
			int state = module->loadState.load();
			std::string name = module->getDisplayName();
			switch (state) {
				case CoronalAnnihilator::LOAD_LOADING: text = "LOADING " + name; break;
				case CoronalAnnihilator::LOAD_OK: text = name; color = eclipse::ACCENT_COLOR; break;
				case CoronalAnnihilator::LOAD_MISSING: text = "MISSING " + name; break;
				case CoronalAnnihilator::LOAD_BAD: text = "BAD MODEL " + name; break;
				default: break;
			}
		}
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, eclipse::LABEL_SIZE);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, color);
		// Ellipsize to the bezel width
		float maxW = box.size.x - mm2px(2.f);
		float bounds[4];
		nvgTextBounds(args.vg, 0, 0, text.c_str(), NULL, bounds);
		if (bounds[2] - bounds[0] > maxW) {
			while (text.size() > 2) {
				text.pop_back();
				std::string t = text + "\xE2\x80\xA6";
				nvgTextBounds(args.vg, 0, 0, t.c_str(), NULL, bounds);
				if (bounds[2] - bounds[0] <= maxW) {
					text = t;
					break;
				}
			}
		}
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			openModelDialog(module);
		}
	}
};

// LOAD button: momentary, opens the file dialog on release
struct LoadButton : VCVButton {
	CoronalAnnihilator* module = NULL;
	void onDragEnd(const DragEndEvent& e) override {
		VCVButton::onDragEnd(e);
		if (module)
			NamModelDisplay::openModelDialog(module);
	}
};

struct CoronalAnnihilatorWidget : ModuleWidget {
	// Sigil anchor nodes inside the eclipse disc (disc center 56.5,70 r=51
	// in the panel SVG): the top bar's endpoints and the vertical axis
	static constexpr float SIG_LX = 27.5f, SIG_RX = 87.5f, SIG_TY = 39.7f;
	static constexpr float SIG_CX = 57.5f;
	// Neural / Umbra column
	static constexpr float NX_L = 128.5f, NX_R = 147.5f;
	// Patch bay column centers and jack/attenuverter pair offsets
	static constexpr float BAY_XL = 171.5f, BAY_XR = 190.9f;
	static constexpr float JACK_DX = -4.8f, ATT_DX = 5.2f;

	void addLabel(Vec mmPos, const std::string& text, float fontSize = eclipse::LABEL_SIZE,
	              NVGcolor color = eclipse::LABEL_COLOR) {
		eclipse::addLabel(this, mmPos, text, fontSize, color);
	}

	void addCvPair(float x, float y, int inputId, int attParamId, CoronalAnnihilator* module) {
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + JACK_DX, y)), module, inputId));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(x + ATT_DX, y)), module, attParamId));
	}

	CoronalAnnihilatorWidget(CoronalAnnihilator* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/CoronalAnnihilator.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		eclipse::addHeader(this, 101.6f, "C O R O N A L   A N N I H I L A T O R");

		// ===== Oscillators: the sigil inside the eclipse disc =====
		// Nick's sketch layout (geometry mirrored in the panel SVG): CORE
		// and FLARE end the top bar with SURGE at its midpoint; the two
		// long arms cross at SPREAD and land on PLASMA and EJECTA; the
		// sides converge on ARC, below which the diamond-and-curls
		// ornament descends to MIX at the sigil's foot. V/oct jacks float
		// inside the sides; WAVE and LOCK ride above the bar; the LIN/EXP
		// mode and TRACK flank SPREAD.
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(SIG_LX, SIG_TY)), module, CoronalAnnihilator::CORE_PITCH_PARAM));
		addLabel(Vec(SIG_LX, SIG_TY + 10.f), "CORE");
		addLabel(Vec(44.f, 27.8f), "SIN  TRI  SAW", eclipse::FINE_SIZE);
		addParam(createParamCentered<CKSSThreeHorizontal>(mm2px(Vec(44.f, 32.3f)), module, CoronalAnnihilator::CORE_WAVE_PARAM));
		addLabel(Vec(44.f, 36.7f), "WAVE");
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(SIG_RX, SIG_TY)), module, CoronalAnnihilator::FLARE_PITCH_PARAM));
		addLabel(Vec(SIG_RX, SIG_TY + 10.f), "FLARE");
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(92.5f, 55.4f)), module, CoronalAnnihilator::FLARE_FINE_PARAM));
		addLabel(Vec(92.5f, 61.6f), "FINE", eclipse::FINE_SIZE);

		addLabel(Vec(71.f, 27.8f), "OFF SOFT HARD", eclipse::FINE_SIZE);
		addParam(createParamCentered<CKSSThreeHorizontal>(mm2px(Vec(71.f, 32.3f)), module, CoronalAnnihilator::LOCK_PARAM));
		addLabel(Vec(71.f, 36.7f), "LOCK");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(SIG_CX, SIG_TY)), module, CoronalAnnihilator::SURGE_PARAM));
		addLabel(Vec(SIG_CX, 46.9f), "SURGE");
		addLabel(Vec(40.f, 66.7f), "EXP", eclipse::FINE_SIZE);
		addParam(createParamCentered<CKSS>(mm2px(Vec(40.f, 72.9f)), module, CoronalAnnihilator::FM_MODE_PARAM));
		addLabel(Vec(40.f, 79.1f), "LIN", eclipse::FINE_SIZE);
		addParam(createParamCentered<CKSS>(mm2px(Vec(75.f, 72.9f)), module, CoronalAnnihilator::TRACK_PARAM));
		addLabel(Vec(75.f, 79.5f), "TRACK");

		addLabel(Vec(31.f, 64.6f), "V/OCT");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(31.f, 58.4f)), module, CoronalAnnihilator::CORE_VOCT_INPUT));
		addLabel(Vec(84.f, 64.6f), "V/OCT");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(84.f, 58.4f)), module, CoronalAnnihilator::FLARE_VOCT_INPUT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(SIG_CX, 68.f)), module, CoronalAnnihilator::SPREAD_PARAM));
		addLabel(Vec(SIG_CX, 75.2f), "SPREAD");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(37.f, 87.4f)), module, CoronalAnnihilator::PLASMA_PARAM));
		addLabel(Vec(37.f, 94.6f), "PLASMA");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(78.f, 87.4f)), module, CoronalAnnihilator::EJECTA_PARAM));
		addLabel(Vec(78.f, 94.6f), "EJECTA");
		addLabel(Vec(SIG_CX, 82.8f), "ARC");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(SIG_CX, 89.4f)), module, CoronalAnnihilator::ARC_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(SIG_CX, 111.4f)), module, CoronalAnnihilator::MIX_PARAM));
		addLabel(Vec(SIG_CX, 118.6f), "MIX");

		// ===== Neural =====
		NamModelDisplay* display = new NamModelDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(121.f, 20.5f));
		display->box.size = mm2px(Vec(34.f, 8.f));
		addChild(display);
		LoadButton* load = createWidgetCentered<LoadButton>(mm2px(Vec(NX_L, 37.f)));
		load->module = module;
		addChild(load);
		addLabel(Vec(NX_L, 42.4f), "LOAD");
		addParam(createLightParamCentered<VCVLightBezelLatch<RedLight>>(mm2px(Vec(NX_R, 37.f)), module,
			CoronalAnnihilator::NEURAL_ACTIVE_PARAM, CoronalAnnihilator::NEURAL_LIGHT));
		addLabel(Vec(NX_R, 42.4f), "NEURAL");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(NX_L, 55.f)), module, CoronalAnnihilator::DRIVE_PARAM));
		addLabel(Vec(NX_L, 62.2f), "DRIVE");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(NX_R, 55.f)), module, CoronalAnnihilator::LEVEL_PARAM));
		addLabel(Vec(NX_R, 62.2f), "LEVEL");

		// ===== Umbra =====
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(132.5f, 87.f)), module, CoronalAnnihilator::CUTOFF_PARAM));
		addLabel(Vec(132.5f, 97.f), "CUTOFF", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(150.5f, 82.f)), module, CoronalAnnihilator::RES_PARAM));
		addLabel(Vec(150.5f, 89.2f), "RES");
		addParam(createParamCentered<Trimpot>(mm2px(Vec(150.5f, 100.f)), module, CoronalAnnihilator::KEY_PARAM));
		addLabel(Vec(150.5f, 105.4f), "KEY");
		addLabel(Vec(126.f, 108.f), "12", eclipse::FINE_SIZE);
		addParam(createParamCentered<eclipse::CKSSHorizontal>(mm2px(Vec(132.5f, 108.f)), module, CoronalAnnihilator::SLOPE_PARAM));
		addLabel(Vec(139.f, 108.f), "24", eclipse::FINE_SIZE);
		addLabel(Vec(132.5f, 113.f), "SLOPE");

		// ===== Patch bay =====
		addLabel(Vec(BAY_XL, 20.3f), "IN L");
		addLabel(Vec(BAY_XR, 20.3f), "IN R");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(BAY_XL, 26.f)), module, CoronalAnnihilator::INL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(BAY_XR, 26.f)), module, CoronalAnnihilator::INR_INPUT));
		addLabel(Vec(181.2f, 32.6f), "SOURCE");
		addLabel(Vec(173.7f, 38.f), "IN", eclipse::FINE_SIZE);
		addParam(createParamCentered<eclipse::CKSSHorizontal>(mm2px(Vec(181.2f, 38.f)), module, CoronalAnnihilator::SOURCE_PARAM));
		addLabel(Vec(189.f, 38.f), "OSC", eclipse::FINE_SIZE);

		static const float ROW_Y[4] = {52.f, 66.f, 80.f, 94.f};
		struct BayEntry {
			const char* label;
			int inputId;
			int attId;
		};
		static const BayEntry LEFT[4] = {
			{"PLASMA", CoronalAnnihilator::PLASMA_INPUT, CoronalAnnihilator::PLASMA_ATT_PARAM},
			{"ARC", CoronalAnnihilator::ARC_INPUT, CoronalAnnihilator::ARC_ATT_PARAM},
			{"MIX", CoronalAnnihilator::MIX_INPUT, CoronalAnnihilator::MIX_ATT_PARAM},
			{"CUTOFF", CoronalAnnihilator::CUTOFF_INPUT, CoronalAnnihilator::CUTOFF_ATT_PARAM},
		};
		static const BayEntry RIGHT[4] = {
			{"EJECTA", CoronalAnnihilator::EJECTA_INPUT, CoronalAnnihilator::EJECTA_ATT_PARAM},
			{"SURGE", CoronalAnnihilator::SURGE_INPUT, CoronalAnnihilator::SURGE_ATT_PARAM},
			{"SPREAD", CoronalAnnihilator::SPREAD_INPUT, CoronalAnnihilator::SPREAD_ATT_PARAM},
			{"RES", CoronalAnnihilator::RES_INPUT, CoronalAnnihilator::RES_ATT_PARAM},
		};
		for (int i = 0; i < 4; i++) {
			addLabel(Vec(BAY_XL, ROW_Y[i] - 5.7f), LEFT[i].label);
			addCvPair(BAY_XL, ROW_Y[i], LEFT[i].inputId, LEFT[i].attId, module);
			addLabel(Vec(BAY_XR, ROW_Y[i] - 5.7f), RIGHT[i].label);
			addCvPair(BAY_XR, ROW_Y[i], RIGHT[i].inputId, RIGHT[i].attId, module);
		}

		addLabel(Vec(BAY_XL, 111.6f), "OUT L", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addLabel(Vec(BAY_XR, 111.6f), "OUT R", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(BAY_XL, 117.6f)), module, CoronalAnnihilator::OUTL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(BAY_XR, 117.6f)), module, CoronalAnnihilator::OUTR_OUTPUT));
	}

	void step() override {
		ModuleWidget::step();
		CoronalAnnihilator* m = getModule<CoronalAnnihilator>();
		if (m)
			delete m->retiredModel.exchange(NULL);
	}

	void onPathDrop(const PathDropEvent& e) override {
		CoronalAnnihilator* m = getModule<CoronalAnnihilator>();
		if (m && !e.paths.empty() && string::lowercase(system::getExtension(e.paths[0])) == ".nam") {
			m->requestLoad(e.paths[0]);
			e.consume(this);
			return;
		}
		ModuleWidget::onPathDrop(e);
	}

	void appendContextMenu(Menu* menu) override {
		CoronalAnnihilator* module = getModule<CoronalAnnihilator>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Neural amp model"));
		menu->addChild(createMenuItem("Load model (.nam)\xE2\x80\xA6", "", [module]() {
			NamModelDisplay::openModelDialog(module);
		}));
		menu->addChild(createMenuItem("Clear model", "", [module]() {
			module->requestLoad("");
		}, module->modelPath.empty()));
		std::string info = module->getDisplayInfo();
		if (!info.empty())
			menu->addChild(createMenuLabel(info));
		menu->addChild(createBoolPtrMenuItem("Mono model (sum L+R through one network)", "", &module->monoModel));
		menu->addChild(createBoolPtrMenuItem("Normalize model loudness (-18 dB)", "", &module->normalizeLoudness));
	}
};

Model* modelCoronalAnnihilator = createModel<CoronalAnnihilator, CoronalAnnihilatorWidget>("CoronalAnnihilator");

#include <ctime>
#include "plugin.hpp"
#include "EclipseWidgets.hpp"

struct MoonPhaseDistortion : Module {
	enum ParamId {
		PHASE_PARAM,
		DRIVE_PARAM,
		MIX_PARAM,
		AUTO_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		PHASE_INPUT,
		INL_INPUT,
		INR_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Per-channel DC blocker state (asymmetric shaping adds DC)
	float dcXL[16] = {};
	float dcYL[16] = {};
	float dcXR[16] = {};
	float dcYR[16] = {};

	dsp::ClockDivider realPhaseDivider;
	float realPhase = 0.f;
	float displayPhase = 0.f; // effective phase, read by the panel display

	MoonPhaseDistortion() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PHASE_PARAM, 0.f, 1.f, 0.5f, "Moon phase", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.5f, "Drive", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
		configSwitch(AUTO_PARAM, 0.f, 1.f, 1.f, "Phase source", {"Manual", "Auto (real moon)"});
		configInput(PHASE_INPUT, "Phase CV (10V = full cycle)");
		configInput(INL_INPUT, "Left audio");
		configInput(INR_INPUT, "Right audio (normalled to left)");
		configOutput(OUTL_OUTPUT, "Left audio");
		configOutput(OUTR_OUTPUT, "Right audio");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);

		realPhaseDivider.setDivision(65536); // recheck the sky every ~1.4s
		realPhase = computeRealPhase();
	}

	// 0 = new moon, 0.5 = full moon. Synodic month, epoch: new moon
	// 2000-01-06 18:14 UTC (unix 947182440).
	static float computeRealPhase() {
		double days = ((double)std::time(NULL) - 947182440.0) / 86400.0;
		double phase = std::fmod(days / 29.53058867, 1.0);
		if (phase < 0.0)
			phase += 1.0;
		return (float)phase;
	}

	static float softClip(float x) {
		if (x > 3.f) return 1.f;
		if (x < -3.f) return -1.f;
		float x2 = x * x;
		return x * (27.f + x2) / (27.f + 9.f * x2);
	}

	static float triFold(float x) {
		x = x * 0.25f;
		x = x - std::floor(x + 0.5f);
		return std::fabs(x) * 4.f - 1.f;
	}

	void process(const ProcessArgs& args) override {
		if (realPhaseDivider.process())
			realPhase = computeRealPhase();

		float phase;
		if (params[AUTO_PARAM].getValue() > 0.5f) {
			phase = realPhase;
		}
		else {
			phase = params[PHASE_PARAM].getValue() + inputs[PHASE_INPUT].getVoltage() / 10.f;
			phase -= std::floor(phase); // phase is cyclic
		}
		displayPhase = phase;

		float illum = 0.5f * (1.f - std::cos(2.f * M_PI * phase));
		float drive = params[DRIVE_PARAM].getValue();
		float mix = params[MIX_PARAM].getValue();

		// Illumination sets how hard the moon leans on the signal
		float gain = 1.f + drive * (0.5f + 19.5f * illum);
		// Waxing pushes positive bias, waning negative (even harmonics), none at new/full
		float bias = 0.4f * std::sin(2.f * M_PI * phase) * drive;
		// Wavefold only emerges near the full moon
		float foldAmt = clamp((illum - 0.7f) / 0.3f, 0.f, 1.f) * 0.6f * drive;

		int channels = std::max(inputs[INL_INPUT].getChannels(), 1);
		outputs[OUTL_OUTPUT].setChannels(channels);
		outputs[OUTR_OUTPUT].setChannels(channels);
		bool rConnected = inputs[INR_INPUT].isConnected();
		bool monoOut = outputs[OUTL_OUTPUT].isConnected() && !outputs[OUTR_OUTPUT].isConnected();

		for (int c = 0; c < channels; c++) {
			float dryL = inputs[INL_INPUT].getVoltage(c) / 5.f;
			float dryR = rConnected ? inputs[INR_INPUT].getVoltage(c) / 5.f : dryL;

			float wetL = softClip(dryL * gain + bias) - softClip(bias);
			float wetR = softClip(dryR * gain + bias) - softClip(bias);
			if (foldAmt > 0.f) {
				wetL = wetL + foldAmt * (triFold(wetL * 1.5f) - wetL);
				wetR = wetR + foldAmt * (triFold(wetR * 1.5f) - wetR);
			}
			// DC blockers
			float yL = wetL - dcXL[c] + 0.9995f * dcYL[c];
			dcXL[c] = wetL;
			dcYL[c] = yL;
			float yR = wetR - dcXR[c] + 0.9995f * dcYR[c];
			dcXR[c] = wetR;
			dcYR[c] = yR;

			float outL = (dryL + mix * (yL - dryL)) * 5.f;
			float outR = (dryR + mix * (yR - dryR)) * 5.f;
			if (monoOut)
				outL = 0.5f * (outL + outR);
			outputs[OUTL_OUTPUT].setVoltage(outL, c);
			outputs[OUTR_OUTPUT].setVoltage(outR, c);
		}
	}

	void onReset() override {
		for (int c = 0; c < 16; c++)
			dcXL[c] = dcYL[c] = dcXR[c] = dcYR[c] = 0.f;
	}
};

static const char* PHASE_NAMES[8] = {
	"NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
	"FULL MOON", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"
};

// Runtime-drawn moon: lit side is a semicircle closed by a bezier terminator
// whose bulge tracks cos(2*pi*phase).
struct MoonDisplay : TransparentWidget {
	MoonPhaseDistortion* module = NULL;

	float getPhase() {
		return module ? module->displayPhase : 0.62f; // waxing gibbous in the browser
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		float phase = getPhase();
		Vec c = box.size.div(2);
		float R = std::min(c.x, c.y) - mm2px(1.f);
		float t = std::cos(2.f * M_PI * phase);
		bool waxing = phase < 0.5f;

		// Shadow disc
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, R);
		nvgFillColor(args.vg, nvgRGB(0x1c, 0x12, 0x08));
		nvgFill(args.vg);

		// Lit region
		float illum = 0.5f * (1.f - t);
		if (illum > 0.005f) {
			float side = waxing ? 1.f : -1.f;
			float ctrlX = c.x + (waxing ? t : -t) * R * (4.f / 3.f);
			float semiX = side * R * (4.f / 3.f);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, c.x, c.y - R);
			// Lit-side semicircle, top to bottom
			nvgBezierTo(args.vg, c.x + semiX, c.y - R, c.x + semiX, c.y + R, c.x, c.y + R);
			// Terminator, bottom back to top
			nvgBezierTo(args.vg, ctrlX, c.y + R, ctrlX, c.y - R, c.x, c.y - R);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGB(0xff, 0xee, 0xb8));
			nvgFill(args.vg);
		}

		// Copper rim
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, R);
		nvgStrokeWidth(args.vg, mm2px(0.35f));
		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x91, 0x29, 0xb0));
		nvgStroke(args.vg);
	}
};

// Label that tracks the current phase name
struct PhaseNameLabel : TransparentWidget {
	MoonPhaseDistortion* module = NULL;

	void draw(const DrawArgs& args) override {
		float phase = module ? module->displayPhase : 0.62f;
		int idx = (int)std::floor(phase * 8.f + 0.5f) % 8;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, eclipse::FINE_SIZE);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, eclipse::DIM_COLOR);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, PHASE_NAMES[idx], NULL);
	}
};

struct MoonPhaseDistortionWidget : ModuleWidget {
	static constexpr float XC = 20.32f, X1 = 12.7f, X2 = 27.94f;

	MoonPhaseDistortionWidget(MoonPhaseDistortion* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MoonPhaseDistortion.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		eclipse::addHeader(this, XC, "M O O N");

		MoonDisplay* display = new MoonDisplay;
		display->module = module;
		display->box.size = mm2px(Vec(24.f, 24.f));
		display->box.pos = mm2px(Vec(XC - 12.f, 19.f));
		addChild(display);

		PhaseNameLabel* phaseName = new PhaseNameLabel;
		phaseName->module = module;
		phaseName->box.size = mm2px(Vec(38.f, 5.f));
		phaseName->box.pos = mm2px(Vec(XC - 19.f, 44.f)).minus(Vec(0, mm2px(2.5f)));
		addChild(phaseName);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X1, 56.f)), module, MoonPhaseDistortion::PHASE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X2, 56.f)), module, MoonPhaseDistortion::DRIVE_PARAM));
		eclipse::addLabel(this, Vec(X1, 64.f), "PHASE", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 64.f), "DRIVE", eclipse::LABEL_SIZE);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X1, 76.f)), module, MoonPhaseDistortion::MIX_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(X2, 76.f)), module, MoonPhaseDistortion::AUTO_PARAM));
		eclipse::addLabel(this, Vec(X1, 84.f), "MIX", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 84.f), "AUTO", eclipse::LABEL_SIZE);

		eclipse::addLabel(this, Vec(XC, 88.9f), "PHASE CV", eclipse::LABEL_SIZE);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(XC, 94.7f)), module, MoonPhaseDistortion::PHASE_INPUT));

		eclipse::addLabel(this, Vec(X1, 100.7f), "IN L", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 100.7f), "IN R", eclipse::LABEL_SIZE);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X1, 106.5f)), module, MoonPhaseDistortion::INL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X2, 106.5f)), module, MoonPhaseDistortion::INR_INPUT));

		eclipse::addLabel(this, Vec(X1, 112.5f), "OUT L", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		eclipse::addLabel(this, Vec(X2, 112.5f), "OUT R", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X1, 118.3f)), module, MoonPhaseDistortion::OUTL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X2, 118.3f)), module, MoonPhaseDistortion::OUTR_OUTPUT));
	}
};

Model* modelMoonPhaseDistortion = createModel<MoonPhaseDistortion, MoonPhaseDistortionWidget>("MoonPhaseDistortion");

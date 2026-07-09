#include "plugin.hpp"
#include "EclipseWidgets.hpp"
#include "dsp/VastReverb.hpp"

struct LiminalVast : Module {
	enum ParamId {
		SIZE_PARAM,
		DECAY_PARAM,
		DENSITY_PARAM,
		RATE_PARAM,
		DEPTH_PARAM,
		PREDELAY_PARAM,
		LOWCUT_PARAM,
		HIGHCUT_PARAM,
		MIX_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		SIZE_INPUT,
		DECAY_INPUT,
		MIX_INPUT,
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

	liminal::VastReverb reverb;

	// Knob mappings need pow(); refresh at block rate instead of every sample
	dsp::ClockDivider paramDivider;
	float rt60 = 3.7f;
	float modRate = 0.3f;
	float lowCut = 10.f;
	float highCut = 9500.f;

	LiminalVast() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.5f, "Size", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " s", 350.f, 0.2f);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);
		configParam(RATE_PARAM, 0.f, 1.f, 0.35f, "Mod rate", " Hz", 160.f, 0.05f);
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.3f, "Mod depth", "%", 0.f, 100.f);
		configParam(PREDELAY_PARAM, 0.f, 250.f, 0.f, "Pre-delay", " ms");
		configParam(LOWCUT_PARAM, 0.f, 1.f, 0.f, "Low cut", " Hz", 50.f, 10.f);
		configParam(HIGHCUT_PARAM, 0.f, 1.f, 0.8f, "High cut", " Hz", 40.f, 500.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		configInput(SIZE_INPUT, "Size CV (10V = full range)");
		configInput(DECAY_INPUT, "Decay CV (10V = full range)");
		configInput(MIX_INPUT, "Mix CV (10V = full range)");
		configInput(INL_INPUT, "Left audio");
		configInput(INR_INPUT, "Right audio (normalled to left)");
		configOutput(OUTL_OUTPUT, "Left audio");
		configOutput(OUTR_OUTPUT, "Right audio");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);
		paramDivider.setDivision(16);
	}

	void process(const ProcessArgs& args) override {
		float size = clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float decay = clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() / 10.f, 0.f, 1.f);

		if (paramDivider.process()) {
			rt60 = 0.2f * std::pow(350.f, decay);
			modRate = 0.05f * std::pow(160.f, params[RATE_PARAM].getValue());
			lowCut = 10.f * std::pow(50.f, params[LOWCUT_PARAM].getValue());
			highCut = 500.f * std::pow(40.f, params[HIGHCUT_PARAM].getValue());
		}

		float dryL = inputs[INL_INPUT].getVoltageSum() / 5.f;
		float dryR = inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltageSum() / 5.f : dryL;

		liminal::VastReverb::Frame wet = reverb.process(dryL, dryR,
			params[PREDELAY_PARAM].getValue() / 1000.f, size, rt60,
			params[DENSITY_PARAM].getValue(), modRate, params[DEPTH_PARAM].getValue(),
			lowCut, highCut, args.sampleRate);

		float outL = (dryL + mix * (wet.L - dryL)) * 5.f;
		float outR = (dryR + mix * (wet.R - dryR)) * 5.f;
		if (outputs[OUTL_OUTPUT].isConnected() && !outputs[OUTR_OUTPUT].isConnected())
			outL = 0.5f * (outL + outR);
		outputs[OUTL_OUTPUT].setVoltage(outL);
		outputs[OUTR_OUTPUT].setVoltage(outR);
	}

	void onReset() override {
		reverb.clear();
	}
};

struct LiminalVastWidget : ModuleWidget {
	static constexpr float XC = 30.48f, X1 = 13.5f, X2 = 47.46f;

	LiminalVastWidget(LiminalVast* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/LiminalVast.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		eclipse::addHeader(this, XC, "L I M I N A L  V A S T", "WHAT LINGERS BETWEEN");

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X1, 46.f)), module, LiminalVast::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 46.f)), module, LiminalVast::DECAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X2, 46.f)), module, LiminalVast::DENSITY_PARAM));
		eclipse::addLabel(this, Vec(X1, 54.f), "SIZE", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(XC, 54.f), "DECAY", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 54.f), "DENSITY", eclipse::LABEL_SIZE);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X1, 64.f)), module, LiminalVast::RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 64.f)), module, LiminalVast::DEPTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X2, 64.f)), module, LiminalVast::PREDELAY_PARAM));
		eclipse::addLabel(this, Vec(X1, 72.f), "RATE", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(XC, 72.f), "DEPTH", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 72.f), "PRE-DLY", eclipse::LABEL_SIZE);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X1, 82.f)), module, LiminalVast::LOWCUT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 82.f)), module, LiminalVast::HIGHCUT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X2, 82.f)), module, LiminalVast::MIX_PARAM));
		eclipse::addLabel(this, Vec(X1, 90.f), "LO CUT", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(XC, 90.f), "HI CUT", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 90.f), "MIX", eclipse::LABEL_SIZE);

		eclipse::addLabel(this, Vec(X1, 97.5f), "SIZE", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(XC, 97.5f), "DECAY", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(X2, 97.5f), "MIX", eclipse::LABEL_SIZE);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X1, 103.5f)), module, LiminalVast::SIZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(XC, 103.5f)), module, LiminalVast::DECAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X2, 103.5f)), module, LiminalVast::MIX_INPUT));

		eclipse::addLabel(this, Vec(9.5f, 112.5f), "IN L", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(23.5f, 112.5f), "IN R", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(37.46f, 112.5f), "OUT L", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		eclipse::addLabel(this, Vec(51.46f, 112.5f), "OUT R", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.5f, 118.3f)), module, LiminalVast::INL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.5f, 118.3f)), module, LiminalVast::INR_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(37.46f, 118.3f)), module, LiminalVast::OUTL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.46f, 118.3f)), module, LiminalVast::OUTR_OUTPUT));
	}
};

Model* modelLiminalVast = createModel<LiminalVast, LiminalVastWidget>("LiminalVast");

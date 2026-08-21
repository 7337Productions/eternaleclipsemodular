// Litany Engine (OMN-90): fixed-bank looping sample player with a focused
// granular layer. Gristleism / Buddha Machine ethos -- the loops shipped in
// res/litany/ are the instrument (no user loading) -- crossed with a
// Morphagene-style tape/microsound voice. Stereo companion to the Coronal
// Annihilator's external input.
#define DR_WAV_IMPLEMENTATION
#include "plugin.hpp"
#include "EclipseWidgets.hpp"
#include "LitanyEngine/dsp/SampleBank.hpp"
#include "LitanyEngine/dsp/GranularEngine.hpp"

struct LitanyEngine : Module {
	enum ParamId {
		LOOP_PARAM,
		SPEED_PARAM,
		GRAIN_PARAM,
		MORPH_PARAM,
		SCAN_PARAM,
		TEXTURE_PARAM,
		LEVEL_PARAM,
		ADVANCE_PARAM,
		SPEED_ATT_PARAM,
		GRAIN_ATT_PARAM,
		MORPH_ATT_PARAM,
		SCAN_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT,
		SPEED_INPUT,
		GRAIN_INPUT,
		MORPH_INPUT,
		SCAN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ADVANCE_LIGHT,
		LIGHTS_LEN
	};

	litany::GranularEngine engine;
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger buttonTrigger;
	dsp::PulseGenerator eocPulse;
	std::atomic<float> loopPhase{0.f};

	LitanyEngine() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		litany::SampleBank& bank = litany::SampleBank::get();
		bank.ensureLoaded();
		int count = (int)bank.count();
		if (count > 0)
			configSwitch(LOOP_PARAM, 0.f, count - 1, 0.f, "Loop", bank.names());
		else
			configSwitch(LOOP_PARAM, 0.f, 0.f, 0.f, "Loop", {"(no loops)"});

		configParam(SPEED_PARAM, -2.f, 2.f, 1.f, "Speed", "x");
		configParam(GRAIN_PARAM, 0.f, 1.f, 0.f, "Grain size");
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph (grain overlap)");
		configParam(SCAN_PARAM, 0.f, 1.f, 0.f, "Scan (position offset)");
		configParam(TEXTURE_PARAM, 0.f, 1.f, 0.f, "Texture (spray + spread)");
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", "%", 0.f, 100.f);
		configButton(ADVANCE_PARAM, "Advance loop");
		configParam(SPEED_ATT_PARAM, -1.f, 1.f, 0.f, "Speed CV amount");
		configParam(GRAIN_ATT_PARAM, -1.f, 1.f, 0.f, "Grain CV amount");
		configParam(MORPH_ATT_PARAM, -1.f, 1.f, 0.f, "Morph CV amount");
		configParam(SCAN_ATT_PARAM, -1.f, 1.f, 0.f, "Scan CV amount");

		configInput(TRIG_INPUT, "Advance loop trigger");
		configInput(SPEED_INPUT, "Speed CV");
		configInput(GRAIN_INPUT, "Grain CV");
		configInput(MORPH_INPUT, "Morph CV");
		configInput(SCAN_INPUT, "Scan CV");
		configOutput(OUTL_OUTPUT, "Left audio");
		configOutput(OUTR_OUTPUT, "Right audio");
		configOutput(EOC_OUTPUT, "End of loop");
	}

	// Unipolar knob + CV (10 V = full range) through its attenuverter
	float cvParam(int paramId, int inputId, int attId, float lo = 0.f, float hi = 1.f) {
		float v = params[paramId].getValue()
			+ inputs[inputId].getVoltage() / 10.f * params[attId].getValue() * (hi - lo);
		if (!std::isfinite(v))
			v = lo;
		return clamp(v, lo, hi);
	}

	int loopCount() {
		return (int)litany::SampleBank::get().count();
	}

	int currentLoop() {
		return (int)std::round(params[LOOP_PARAM].getValue());
	}

	void advanceLoop() {
		int count = loopCount();
		if (count > 0)
			params[LOOP_PARAM].setValue((currentLoop() + 1) % count);
	}

	void process(const ProcessArgs& args) override {
		// Loop advance: button, trigger input, or the display (UI thread)
		bool adv = false;
		adv |= buttonTrigger.process(params[ADVANCE_PARAM].getValue() > 0.5f);
		adv |= trigTrigger.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		if (adv)
			advanceLoop();

		litany::SampleBank& bank = litany::SampleBank::get();
		litany::LoopView view;
		const litany::Loop* lp = bank.loop(currentLoop());
		if (lp && lp->ready.load(std::memory_order_acquire) && lp->frames > 1) {
			view.l = lp->l.data();
			view.r = lp->r.data();
			view.frames = lp->frames;
			view.fileRate = lp->fileRate;
		}

		litany::GranularParams p;
		p.speed = cvParam(SPEED_PARAM, SPEED_INPUT, SPEED_ATT_PARAM, -2.f, 2.f);
		p.grain = cvParam(GRAIN_PARAM, GRAIN_INPUT, GRAIN_ATT_PARAM);
		p.morph = cvParam(MORPH_PARAM, MORPH_INPUT, MORPH_ATT_PARAM);
		p.scan = cvParam(SCAN_PARAM, SCAN_INPUT, SCAN_ATT_PARAM);
		p.texture = params[TEXTURE_PARAM].getValue();

		litany::GranularEngine::Out out = engine.process(view, p, args.sampleRate);
		loopPhase.store(engine.phase01(), std::memory_order_relaxed);
		if (out.eoc)
			eocPulse.trigger(1e-3f);

		float level = params[LEVEL_PARAM].getValue();
		float outL = out.l * 5.f * level;
		float outR = out.r * 5.f * level;
		if (outputs[OUTL_OUTPUT].isConnected() && !outputs[OUTR_OUTPUT].isConnected())
			outputs[OUTL_OUTPUT].setVoltage(0.5f * (outL + outR));
		else
			outputs[OUTL_OUTPUT].setVoltage(outL);
		outputs[OUTR_OUTPUT].setVoltage(outR);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);

		lights[ADVANCE_LIGHT].setBrightnessSmooth(out.eoc || adv ? 1.f : 0.f, args.sampleTime * 0.5f);
	}

	void onReset() override {
		engine.reset();
	}
};

// ----- Loop name display: current loop + playhead hairline; click advances -----
struct LoopDisplay : TransparentWidget {
	LitanyEngine* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::string text = "01 \xC2\xB7 FIRST UTTERANCE";
		NVGcolor color = eclipse::ACCENT_COLOR;
		float phase = 0.f;
		if (module) {
			litany::SampleBank& bank = litany::SampleBank::get();
			const litany::Loop* lp = bank.loop(module->currentLoop());
			if (!lp) {
				text = "NO LOOPS";
				color = eclipse::LABEL_COLOR;
			}
			else if (!lp->ready.load(std::memory_order_acquire)) {
				text = "LOADING";
				color = eclipse::LABEL_COLOR;
			}
			else {
				text = lp->name;
				phase = module->loopPhase.load(std::memory_order_relaxed);
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

		// Playhead hairline along the bottom edge
		if (phase > 0.f) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, mm2px(1.f), box.size.y - mm2px(0.7f));
			nvgLineTo(args.vg, mm2px(1.f) + (box.size.x - mm2px(2.f)) * phase, box.size.y - mm2px(0.7f));
			nvgStrokeColor(args.vg, eclipse::DIM_COLOR);
			nvgStrokeWidth(args.vg, mm2px(0.35f));
			nvgStroke(args.vg);
		}
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			module->advanceLoop();
		}
	}
};

struct LitanyEngineWidget : ModuleWidget {
	static constexpr float CX = 40.64f;
	// Patch bay column centers and jack/attenuverter pair offsets
	static constexpr float BAY_XL = 22.f, BAY_XR = 59.f;
	static constexpr float JACK_DX = -4.8f, ATT_DX = 5.2f;

	void addLabel(Vec mmPos, const std::string& text, float fontSize = eclipse::LABEL_SIZE,
	              NVGcolor color = eclipse::LABEL_COLOR) {
		eclipse::addLabel(this, mmPos, text, fontSize, color);
	}

	void addCvPair(float x, float y, int inputId, int attParamId, LitanyEngine* module) {
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + JACK_DX, y)), module, inputId));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(x + ATT_DX, y)), module, attParamId));
	}

	LitanyEngineWidget(LitanyEngine* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/LitanyEngine.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		eclipse::addHeader(this, CX, "L I T A N Y   E N G I N E");

		// Loop display over the bezel
		LoopDisplay* display = new LoopDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(8.f, 15.5f));
		display->box.size = mm2px(Vec(65.28f, 8.f));
		addChild(display);

		// Liturgy row: loop select / advance / level
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(20.f, 36.f)), module, LitanyEngine::LOOP_PARAM));
		addLabel(Vec(20.f, 46.f), "LOOP");
		addParam(createLightParamCentered<VCVLightBezel<RedLight>>(mm2px(Vec(44.f, 36.f)), module,
			LitanyEngine::ADVANCE_PARAM, LitanyEngine::ADVANCE_LIGHT));
		addLabel(Vec(44.f, 43.4f), "TURN");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(65.f, 36.f)), module, LitanyEngine::LEVEL_PARAM));
		addLabel(Vec(65.f, 43.4f), "LEVEL");

		// Centerpiece: through-zero varispeed inside the litany-wheel sigil
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(CX, 57.f)), module, LitanyEngine::SPEED_PARAM));
		addLabel(Vec(CX, 71.f), "SPEED", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);

		// Grain row
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.f, 78.f)), module, LitanyEngine::GRAIN_PARAM));
		addLabel(Vec(13.f, 85.2f), "GRAIN");
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(31.f, 78.f)), module, LitanyEngine::MORPH_PARAM));
		addLabel(Vec(31.f, 85.2f), "MORPH");
		addParam(createParamCentered<Trimpot>(mm2px(Vec(50.f, 78.f)), module, LitanyEngine::TEXTURE_PARAM));
		addLabel(Vec(50.f, 84.6f), "TEX", eclipse::FINE_SIZE);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(68.f, 78.f)), module, LitanyEngine::SCAN_PARAM));
		addLabel(Vec(68.f, 85.2f), "SCAN");

		// Patch bay
		addLabel(Vec(BAY_XL, 99.3f - 5.2f), "SPEED", eclipse::FINE_SIZE);
		addCvPair(BAY_XL, 99.3f, LitanyEngine::SPEED_INPUT, LitanyEngine::SPEED_ATT_PARAM, module);
		addLabel(Vec(BAY_XR, 99.3f - 5.2f), "GRAIN", eclipse::FINE_SIZE);
		addCvPair(BAY_XR, 99.3f, LitanyEngine::GRAIN_INPUT, LitanyEngine::GRAIN_ATT_PARAM, module);
		addLabel(Vec(BAY_XL, 109.3f - 5.2f), "MORPH", eclipse::FINE_SIZE);
		addCvPair(BAY_XL, 109.3f, LitanyEngine::MORPH_INPUT, LitanyEngine::MORPH_ATT_PARAM, module);
		addLabel(Vec(BAY_XR, 109.3f - 5.2f), "SCAN", eclipse::FINE_SIZE);
		addCvPair(BAY_XR, 109.3f, LitanyEngine::SCAN_INPUT, LitanyEngine::SCAN_ATT_PARAM, module);

		// Bottom row: trig / eoc / stereo out
		addLabel(Vec(12.f, 113.1f), "TRIG", eclipse::FINE_SIZE);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.f, 118.6f)), module, LitanyEngine::TRIG_INPUT));
		addLabel(Vec(30.5f, 113.1f), "EOC", eclipse::FINE_SIZE);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.5f, 118.6f)), module, LitanyEngine::EOC_OUTPUT));
		addLabel(Vec(51.f, 113.1f), "OUT L", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.f, 118.6f)), module, LitanyEngine::OUTL_OUTPUT));
		addLabel(Vec(69.5f, 113.1f), "OUT R", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(69.5f, 118.6f)), module, LitanyEngine::OUTR_OUTPUT));
	}
};

Model* modelLitanyEngine = createModel<LitanyEngine, LitanyEngineWidget>("LitanyEngine");

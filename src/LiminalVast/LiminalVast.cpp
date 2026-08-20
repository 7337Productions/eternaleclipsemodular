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
		FREEZE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		SIZE_INPUT,
		DECAY_INPUT,
		MIX_INPUT,
		INL_INPUT,
		INR_INPUT,
		FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		FREEZE_LIGHT,
		LIGHTS_LEN
	};

	// The top 0.5% of the knob pins the tail to infinity
	struct DecayQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() > 0.995f)
				return "∞";
			return ParamQuantity::getDisplayValueString();
		}
	};

	liminal::VastReverb reverb;

	// Knob mappings need pow(); refresh at block rate instead of every sample
	dsp::ClockDivider paramDivider;
	float rt60 = 3.7f;
	float modRate = 0.3f;
	float lowCut = 10.f;
	float highCut = 9500.f;

	dsp::SchmittTrigger freezeGate;

	// Display state: written here, read lock-free by VastDisplay
	float dispEnergy = 0.f;
	float dispSize = 0.5f;
	float dispDecay = 0.5f;
	float dispDensity = 0.7f;
	float dispFreeze = 0.f;
	int dispBurst = 0;

	// Wet-tail energy + input-onset followers, refreshed every 256 samples
	dsp::ClockDivider envDivider;
	float energyAcc = 0.f;
	float onsetPeak = 0.f;
	float onsetFast = 0.f;
	float onsetSlow = 0.f;
	bool onsetArmed = true;

	LiminalVast() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.5f, "Size", "%", 0.f, 100.f);
		configParam<DecayQuantity>(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " s", 350.f, 0.2f);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);
		configParam(RATE_PARAM, 0.f, 1.f, 0.35f, "Mod rate", " Hz", 160.f, 0.05f);
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.3f, "Mod depth", "%", 0.f, 100.f);
		configParam(PREDELAY_PARAM, 0.f, 250.f, 0.f, "Pre-delay", " ms");
		configParam(LOWCUT_PARAM, 0.f, 1.f, 0.f, "Low cut", " Hz", 50.f, 10.f);
		configParam(HIGHCUT_PARAM, 0.f, 1.f, 0.8f, "High cut", " Hz", 40.f, 500.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configInput(SIZE_INPUT, "Size CV (10V = full range)");
		configInput(DECAY_INPUT, "Decay CV (10V = full range)");
		configInput(MIX_INPUT, "Mix CV (10V = full range)");
		configInput(INL_INPUT, "Left audio");
		configInput(INR_INPUT, "Right audio (normalled to left)");
		configInput(FREEZE_INPUT, "Freeze gate");
		configOutput(OUTL_OUTPUT, "Left audio");
		configOutput(OUTR_OUTPUT, "Right audio");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);
		paramDivider.setDivision(16);
		envDivider.setDivision(256);
	}

	void process(const ProcessArgs& args) override {
		float size = clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float decay = clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() / 10.f, 0.f, 1.f);

		freezeGate.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = params[FREEZE_PARAM].getValue() > 0.5f || freezeGate.isHigh();

		if (paramDivider.process()) {
			rt60 = decay > 0.995f ? 1e9f : 0.2f * std::pow(350.f, decay);
			modRate = 0.05f * std::pow(160.f, params[RATE_PARAM].getValue());
			lowCut = 10.f * std::pow(50.f, params[LOWCUT_PARAM].getValue());
			highCut = 500.f * std::pow(40.f, params[HIGHCUT_PARAM].getValue());
			dispSize = size;
			dispDecay = decay;
			dispDensity = params[DENSITY_PARAM].getValue();
		}

		float dryL = inputs[INL_INPUT].getVoltageSum() / 5.f;
		float dryR = inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltageSum() / 5.f : dryL;

		liminal::VastReverb::Frame wet = reverb.process(dryL, dryR,
			params[PREDELAY_PARAM].getValue() / 1000.f, size, rt60,
			params[DENSITY_PARAM].getValue(), modRate, params[DEPTH_PARAM].getValue(),
			lowCut, highCut, frozen ? 1.f : 0.f, args.sampleRate);

		// Display followers: tail RMS for the corona, fast/slow input envelope
		// pair for transient-triggered particle bursts
		energyAcc += wet.L * wet.L + wet.R * wet.R;
		float dryAbs = std::fabs(dryL) + std::fabs(dryR);
		if (dryAbs > onsetPeak)
			onsetPeak = dryAbs;
		if (envDivider.process()) {
			float dt = 256.f * args.sampleTime;
			float rms = std::sqrt(energyAcc / 256.f);
			energyAcc = 0.f;
			float e = clamp(rms * 1.5f, 0.f, 1.f);
			e = e / (e + 0.3f);
			float tau = (e > dispEnergy) ? 0.03f : 0.4f;
			dispEnergy += (1.f - std::exp(-dt / tau)) * (e - dispEnergy);

			float fastTau = (onsetPeak > onsetFast) ? 0.005f : 0.08f;
			onsetFast += (1.f - std::exp(-dt / fastTau)) * (onsetPeak - onsetFast);
			onsetSlow += (1.f - std::exp(-dt / 0.3f)) * (onsetPeak - onsetSlow);
			onsetPeak = 0.f;
			if (onsetArmed && onsetFast > 1.6f * onsetSlow + 0.02f) {
				dispBurst++;
				onsetArmed = false;
			}
			else if (!onsetArmed && onsetFast < 1.2f * onsetSlow + 0.02f) {
				onsetArmed = true;
			}

			dispFreeze += (1.f - std::exp(-dt / 0.1f)) * ((frozen ? 1.f : 0.f) - dispFreeze);
			lights[FREEZE_LIGHT].setBrightness(dispFreeze);
		}

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

	void onSampleRateChange() override {
		// Buffers hold samples at the old rate; a clean clear beats a warped tail
		reverb.clear();
	}
};

// Live overlay on the eclipse art: corona glow riding the tail's energy, and
// particles ejected from the rim on input transients. Simulation lives
// entirely here (never on the audio thread), driven by the module's disp*
// fields.
struct VastDisplay : TransparentWidget {
	LiminalVast* module = nullptr;

	struct Particle {
		Vec pos;
		Vec vel;
		float age = 0.f;
		float life = 0.f;
		float radius = 0.f;
	};
	static constexpr int MAX_PARTICLES = 48;
	Particle particles[MAX_PARTICLES];
	int nextParticle = 0;
	int lastBurst = 0;

	// Eclipse center in local coords: panel mm(30.48, 26) minus box origin
	static Vec center() {
		return mm2px(Vec(27.98f, 13.5f));
	}

	VastDisplay() {
		box.pos = mm2px(Vec(2.5f, 12.5f));
		box.size = mm2px(Vec(55.96f, 26.f));
	}

	void spawnBurst(float size, float decay, float density, float freeze) {
		int count = (int)(6.f + 10.f * density);
		float rim = mm2px(7.3f);
		float life = 0.4f + 2.2f * ((freeze > 0.5f) ? 1.f : decay);
		for (int n = 0; n < count; n++) {
			Particle& p = particles[nextParticle];
			nextParticle = (nextParticle + 1) % MAX_PARTICLES;
			float ang = 2.f * M_PI * random::uniform();
			Vec dir = Vec(std::cos(ang), std::sin(ang));
			p.pos = center().plus(dir.mult(rim));
			float speed = mm2px(6.f) * (0.5f + size) * (0.6f + 0.8f * random::uniform());
			p.vel = dir.mult(speed);
			p.age = 0.f;
			p.life = life * (0.6f + 0.8f * random::uniform());
			p.radius = mm2px(0.25f + 0.35f * random::uniform());
		}
	}

	void step() override {
		TransparentWidget::step();
		if (!module)
			return;
		float dt = APP->window->getLastFrameDuration();
		if (dt > 0.05f)
			dt = 0.05f;
		float freeze = module->dispFreeze;

		if (module->dispBurst != lastBurst) {
			lastBurst = module->dispBurst;
			spawnBurst(module->dispSize, module->dispDecay, module->dispDensity, freeze);
		}

		for (Particle& p : particles) {
			if (p.age >= p.life)
				continue;
			p.age += dt;
			if (freeze > 0.5f) {
				// Frozen embers pick up a tangential curl and orbit the disc
				Vec r = p.pos.minus(center());
				float len = r.norm();
				if (len > 1e-3f) {
					Vec tang = Vec(-r.y / len, r.x / len);
					p.vel = p.vel.plus(tang.mult(mm2px(4.f) * dt));
				}
			}
			p.vel = p.vel.mult(std::pow(0.5f, dt / 0.8f));
			p.pos = p.pos.plus(p.vel.mult(dt));
		}
	}

	void drawParticle(const DrawArgs& args, Vec pos, float radius, float t) {
		// Cream #ffeeb8 cooling to copper #cd762b over the particle's life
		NVGcolor c = nvgLerpRGBA(nvgRGBf(1.f, 0.93f, 0.72f), nvgRGBf(0.80f, 0.46f, 0.17f), t);
		c.a = (1.f - t) * (1.f - t) * 0.85f;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, pos.x, pos.y, radius);
		nvgFillColor(args.vg, c);
		nvgFill(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);

		float energy = module ? module->dispEnergy : 0.45f;
		float size = module ? module->dispSize : 0.5f;
		float freeze = module ? module->dispFreeze : 0.f;

		// Frozen corona holds a steady floor and breathes slowly
		if (freeze > 0.01f) {
			float breathe = 1.f + 0.1f * std::sin(2.f * M_PI * 0.15f * (float)system::getTime());
			float floor = 0.35f * freeze * breathe;
			if (energy < floor)
				energy = floor;
		}

		// Corona glow: annulus from the disc edge outward; SIZE sets reach,
		// tail energy sets intensity. Max reach stays inside the art region's
		// half-height so the gradient dies out before the scissor edge.
		Vec c = center();
		float inner = mm2px(7.3f);
		float outer = mm2px(7.3f + 1.5f + 4.f * size);
		float alpha = 0.08f + 0.45f * energy;
		NVGpaint paint = nvgRadialGradient(args.vg, c.x, c.y, inner, outer,
			nvgRGBAf(1.f, 0.77f, 0.39f, alpha), nvgRGBAf(1.f, 0.77f, 0.39f, 0.f));
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, outer);
		nvgCircle(args.vg, c.x, c.y, inner);
		nvgPathWinding(args.vg, NVG_HOLE);
		nvgFillPaint(args.vg, paint);
		nvgFill(args.vg);

		if (module) {
			for (Particle& p : particles) {
				if (p.age >= p.life || p.life <= 0.f)
					continue;
				drawParticle(args, p.pos, p.radius, p.age / p.life);
			}
		}
		else {
			// Module browser: a still frame of drifting embers
			for (int n = 0; n < 8; n++) {
				float ang = 2.f * M_PI * (n + 0.5f) / 8.f;
				float dist = inner + mm2px(1.5f + 0.9f * n);
				Vec pos = c.plus(Vec(std::cos(ang), std::sin(ang)).mult(dist));
				drawParticle(args, pos, mm2px(0.45f - 0.02f * n), n / 8.f);
			}
		}

		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
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

		eclipse::addHeader(this, XC, "L I M I N A L  V A S T");

		VastDisplay* display = new VastDisplay;
		display->module = module;
		addChild(display);

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(
			mm2px(Vec(53.5f, 16.5f)), module, LiminalVast::FREEZE_PARAM, LiminalVast::FREEZE_LIGHT));
		eclipse::addLabel(this, Vec(53.5f, 20.5f), "FREEZE", eclipse::FINE_SIZE);

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

		// CV row on the IO row's 4-column grid to make room for the freeze gate
		static constexpr float C1 = 9.5f, C2 = 23.5f, C3 = 37.46f, C4 = 51.46f;
		eclipse::addLabel(this, Vec(C1, 97.5f), "SIZE", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(C2, 97.5f), "DECAY", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(C3, 97.5f), "MIX", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(C4, 97.5f), "FREEZE", eclipse::LABEL_SIZE);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C1, 103.5f)), module, LiminalVast::SIZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, 103.5f)), module, LiminalVast::DECAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C3, 103.5f)), module, LiminalVast::MIX_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C4, 103.5f)), module, LiminalVast::FREEZE_INPUT));

		eclipse::addLabel(this, Vec(C1, 112.5f), "IN L", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(C2, 112.5f), "IN R", eclipse::LABEL_SIZE);
		eclipse::addLabel(this, Vec(C3, 112.5f), "OUT L", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		eclipse::addLabel(this, Vec(C4, 112.5f), "OUT R", eclipse::LABEL_SIZE, eclipse::ACCENT_COLOR);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C1, 118.3f)), module, LiminalVast::INL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, 118.3f)), module, LiminalVast::INR_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(C3, 118.3f)), module, LiminalVast::OUTL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(C4, 118.3f)), module, LiminalVast::OUTR_OUTPUT));
	}
};

Model* modelLiminalVast = createModel<LiminalVast, LiminalVastWidget>("LiminalVast");

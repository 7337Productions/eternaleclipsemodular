#pragma once
#include <rack.hpp>

using namespace rack;

// Shared Eternal Eclipse panel style: dark violet panels (#131019), amber
// accents, runtime-drawn labels (Rack's SVG renderer ignores <text> elements).
namespace eclipse {

static const NVGcolor LABEL_COLOR = nvgRGB(0xf2, 0xee, 0xfb);
static const NVGcolor ACCENT_COLOR = nvgRGB(0xe8, 0xa3, 0x3d);
static const NVGcolor DIM_COLOR = nvgRGB(0xa8, 0x9f, 0xbf);

// Type scale (px at 100% zoom). VCV Library review requires text readable at
// 100% on a normal-DPI monitor; these sizes track VCV Fundamental's density.
// FINE_SIZE is the floor -- nothing on a panel goes smaller.
static const float TITLE_SIZE = 12.f;
static const float LABEL_SIZE = 9.f;
static const float SUBTITLE_SIZE = 9.f;
static const float FINE_SIZE = 7.f;

struct PanelLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, color);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};

inline void addLabel(Widget* parent, Vec mmPos, const std::string& text,
                     float fontSize = LABEL_SIZE, NVGcolor color = LABEL_COLOR) {
	PanelLabel* label = new PanelLabel;
	label->box.size = mm2px(Vec(30.f, 5.f));
	label->box.pos = mm2px(mmPos).minus(label->box.size.div(2));
	label->text = text;
	label->fontSize = fontSize;
	label->color = color;
	parent->addChild(label);
}

// Standard header block: title at y=7.3, subtitle at y=11.8, sized for the
// accent line at y=14.0 in the panel SVG. Every module uses this geometry.
inline void addHeader(Widget* parent, float centerX, const std::string& title,
                      const std::string& subtitle = "ETERNAL ECLIPSE") {
	addLabel(parent, Vec(centerX, 7.3f), title, TITLE_SIZE, ACCENT_COLOR);
	addLabel(parent, Vec(centerX, 11.8f), subtitle, SUBTITLE_SIZE, DIM_COLOR);
}

} // namespace eclipse

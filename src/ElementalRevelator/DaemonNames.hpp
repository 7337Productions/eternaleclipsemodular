#pragma once
#include <rack.hpp>

// The 72 daemons, named in the traditional Goetic order.
static const char* kDaemonNames[72] = {
	"Bael", "Agares", "Vassago", "Samigina", "Marbas", "Valefor",
	"Amon", "Barbatos", "Paimon", "Buer", "Gusion", "Sitri",
	"Beleth", "Leraje", "Eligos", "Zepar", "Botis", "Bathin",
	"Sallos", "Purson", "Marax", "Ipos", "Aim", "Naberius",
	"Glasya-Labolas", "Bune", "Ronove", "Berith", "Astaroth", "Forneus",
	"Foras", "Asmoday", "Gaap", "Furfur", "Marchosias", "Stolas",
	"Phenex", "Halphas", "Malphas", "Raum", "Focalor", "Vepar",
	"Sabnock", "Shax", "Vine", "Bifrons", "Uvall", "Haagenti",
	"Crocell", "Furcas", "Balam", "Alloces", "Caim", "Murmur",
	"Orobas", "Gremory", "Ose", "Amy", "Oriax", "Vapula",
	"Zagan", "Valac", "Andras", "Flauros", "Andrealphus", "Kimaris",
	"Amdusias", "Belial", "Decarabia", "Seere", "Dantalion", "Andromalius"
};

struct DaemonQuantity : rack::ParamQuantity {
	std::string getDisplayValueString() override {
		int i = rack::math::clamp((int)std::round(getValue()), 0, 71);
		return rack::string::f("%d \xC2\xB7 %s", i + 1, kDaemonNames[i]);
	}
};

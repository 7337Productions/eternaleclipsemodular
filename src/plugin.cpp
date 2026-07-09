#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMoonPhaseDistortion);
	p->addModel(modelLiminalVast);
	p->addModel(modelSaros);
	p->addModel(modelSephirothicModulator);
	p->addModel(modelCosmicClock);
}

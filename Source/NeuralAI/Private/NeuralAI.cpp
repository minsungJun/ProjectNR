// Neural Enemy AI prototype module.

#include "NeuralAI.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultGameModuleImpl, NeuralAI);

DEFINE_LOG_CATEGORY(LogNeuralAI);

DEFINE_STAT(STAT_NeuralAI_BrainStep);
DEFINE_STAT(STAT_NeuralAI_SensorUpdate);
DEFINE_STAT(STAT_NeuralAI_ProjectileThreat);
DEFINE_STAT(STAT_NeuralAI_StepsPerFrame);
DEFINE_STAT(STAT_NeuralAI_LiveProjectiles);

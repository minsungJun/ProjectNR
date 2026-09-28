// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/** Log category for the neural enemy AI prototype. */
DECLARE_LOG_CATEGORY_EXTERN(LogNeuralAI, Log, All);

/**
 * Profiling group for the AI, visible through "stat NeuralAI".
 *
 * The brain and the sensor are counted separately on purpose. They run at the same rate
 * but scale differently: the brain is a fixed cost per enemy, while sensing walks the
 * live projectile list and traces for line of sight, so it is the side that grows with
 * what is happening in the level rather than with how many enemies there are.
 */
DECLARE_STATS_GROUP(TEXT("NeuralAI"), STATGROUP_NeuralAI, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Brain Step"), STAT_NeuralAI_BrainStep, STATGROUP_NeuralAI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Sensor Update"), STAT_NeuralAI_SensorUpdate, STATGROUP_NeuralAI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Projectile Threat"), STAT_NeuralAI_ProjectileThreat, STATGROUP_NeuralAI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Brain Steps"), STAT_NeuralAI_StepsPerFrame, STATGROUP_NeuralAI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Live Projectiles"), STAT_NeuralAI_LiveProjectiles, STATGROUP_NeuralAI, );

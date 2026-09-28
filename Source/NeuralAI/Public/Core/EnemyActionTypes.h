// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "EnemyActionTypes.generated.h"

/**
 * What the enemy is currently committed to doing.
 *
 * The brain has no idea this exists. It only ever produces competing desires; deciding
 * that one of them has become "the thing being done", and for how long, belongs to the
 * controller. Phase 8 attaches minimum commit times to these.
 */
UENUM(BlueprintType)
enum class EEnemyActionType : uint8
{
	None,
	Move,
	Dodge,
	Attack
};

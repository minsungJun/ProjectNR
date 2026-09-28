// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AITestGameMode.generated.h"

/**
 * Bare game mode for the neural AI test map: it only picks the test pawn.
 * Set this as the World Settings game mode override so the project's own
 * default game mode is left untouched.
 */
UCLASS()
class NEURALAI_API AAITestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAITestGameMode();
};

// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AttackTokenSettings.generated.h"

/**
 * Attack tokens: how many enemies may strike the same target at once, and in what order.
 * Separate from where they stand (UPositionalSlotSettings). Project Settings > Game > Attack
 * Tokens. @see UAttackTokenSubsystem
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Attack Tokens"))
class NEURALAI_API UAttackTokenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Off: every enemy strikes whenever the target is in range. */
	UPROPERTY(Config, EditAnywhere, Category = "Tokens")
	bool bEnabled = true;

	/** Enemies allowed to strike the same target at the same time. */
	UPROPERTY(Config, EditAnywhere, Category = "Tokens", meta = (ClampMin = "1"))
	int32 MaxAttackers = 3;

	/**
	 * Seconds a token is kept after a strike - one enemy's turn - so the next one waits.
	 * With 3 tokens, 2 s holds a group to about 1.5 strikes a second; at 1 s it hardly slowed
	 * five elites down (3 a second against their own 3.3).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tokens", meta = (ClampMin = "0.1"))
	float TokenHold = 2.0f;
};

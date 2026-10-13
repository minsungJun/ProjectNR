// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AttackTokenSubsystem.generated.h"

/**
 * Attack tokens per target: which of the enemies in reach may actually strike. Knows nothing
 * of where they stand - that is UPositionalSlotSubsystem.
 *
 * An enemy that wants to strike asks TryTakeToken. At most MaxAttackers hold one per target, each
 * for TokenHold seconds; among those kept waiting, the one waiting longest goes next. Settings
 * in UAttackTokenSettings; NeuralAI.AttackTokens.Debug 1 logs each token taken.
 */
UCLASS()
class NEURALAI_API UAttackTokenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Tokens are handed out; when false every strike is allowed. */
	bool IsEnabled() const;

	/**
	 * Enemy wants to strike Target. True - and a token held for TokenHold - when fewer than
	 * MaxAttackers hold one for that target and no enemy has been kept waiting longer.
	 */
	bool TryTakeToken(AActor* Enemy, AActor* Target);

	/** Enemy stops fighting: any token or place in the queue goes back. */
	void Release(AActor* Enemy);

	/** Logs every target's tokens out and who is waiting. */
	void LogStatus() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FToken
	{
		TWeakObjectPtr<AActor> Holder;
		float Until = 0.0f;
	};

	struct FWaiting
	{
		TWeakObjectPtr<AActor> Enemy;
		float Since = 0.0f;
		float LastAsked = 0.0f;
	};

	struct FTargetTokens
	{
		TWeakObjectPtr<AActor> Target;
		TArray<FToken> Tokens;
		TArray<FWaiting> Waiting;
	};

	TArray<FTargetTokens> Targets;
};

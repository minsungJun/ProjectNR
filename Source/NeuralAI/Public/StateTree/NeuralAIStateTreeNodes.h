// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "Tasks/StateTreeAITask.h"
#include "NeuralAIStateTreeNodes.generated.h"

class AAIController;

/**
 * Which fact about the enemy a condition should test.
 *
 * These are all game-state questions, never behavioural ones. Nothing here asks whether
 * the enemy wants to attack or dodge: that lives in the network, and letting the state
 * tree peek at it would be exactly the mixing of roles this layer exists to prevent.
 */
UENUM()
enum class EEnemyStateQuery : uint8
{
	IsDead,
	IsStunned,
	HasTarget
};

USTRUCT()
struct FSTCondition_EnemyStateInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EEnemyStateQuery Query = EEnemyStateQuery::HasTarget;

	/** Flips the result, so one node covers both "has target" and "has no target". */
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;
};

/** Tests a single game-state fact about the possessed enemy. */
USTRUCT(meta = (DisplayName = "Enemy State", Category = "Neural AI"))
struct NEURALAI_API FSTCondition_EnemyState : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_EnemyStateInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT()
struct FSTTask_NeuralCombatInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAIController> AIController = nullptr;
};

/**
 * Runs the neural pipeline for as long as this state is active.
 *
 * The task itself contains no behaviour. It takes ownership of the pipeline on enter,
 * pumps it once per tick, and hands control back on exit. Every decision about what to
 * do is still made by the network; this only decides that fighting is happening at all.
 */
USTRUCT(meta = (DisplayName = "Neural Combat", Category = "Neural AI"))
struct NEURALAI_API FSTTask_NeuralCombat : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_NeuralCombatInstanceData;

	FSTTask_NeuralCombat();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FSTTask_EnemyHaltInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAIController> AIController = nullptr;
};

/** Stops the pawn and clears any commitment. Used by Idle, Stunned and Dead. */
USTRUCT(meta = (DisplayName = "Enemy Halt", Category = "Neural AI"))
struct NEURALAI_API FSTTask_EnemyHalt : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_EnemyHaltInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

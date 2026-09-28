// Neural Enemy AI prototype module.

#include "StateTree/NeuralAIStateTreeNodes.h"

#include "Characters/EnemyCharacter.h"
#include "Components/EnemySensorComponent.h"
#include "Controllers/EnemyAIController.h"
#include "NeuralAI.h"
#include "StateTreeExecutionContext.h"

namespace
{
	/** Every node here runs on an AIController the schema guarantees; this narrows it. */
	AEnemyAIController* ResolveController(AAIController* Controller)
	{
		return Cast<AEnemyAIController>(Controller);
	}
}

bool FSTCondition_EnemyState::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const AEnemyAIController* Controller = ResolveController(InstanceData.AIController);
	const AEnemyCharacter* Enemy = Controller ? Controller->GetEnemyCharacter() : nullptr;
	if (!Enemy)
	{
		return InstanceData.bInvert;
	}

	bool bResult = false;
	switch (InstanceData.Query)
	{
	case EEnemyStateQuery::IsDead:
		bResult = Enemy->IsDead();
		break;

	case EEnemyStateQuery::IsStunned:
		bResult = Enemy->IsStunned();
		break;

	case EEnemyStateQuery::HasTarget:
		if (const UEnemySensorComponent* Sensor = Enemy->GetSensorComponent())
		{
			bResult = Sensor->GetSensorData().TargetDetected > 0.0f;
		}
		break;
	}

	return InstanceData.bInvert ? !bResult : bResult;
}

FSTTask_NeuralCombat::FSTTask_NeuralCombat()
{
	// Without this the task would enter once and never be pumped again.
	bShouldCallTick = true;
}

EStateTreeRunStatus FSTTask_NeuralCombat::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AEnemyAIController* Controller = ResolveController(InstanceData.AIController);
	if (!Controller)
	{
		UE_LOG(LogNeuralAI, Warning, TEXT("Neural Combat task is not running on an AEnemyAIController"));
		return EStateTreeRunStatus::Failed;
	}

	Controller->SetDrivenByStateTree(true);
	UE_LOG(LogNeuralAI, Verbose, TEXT("%s entered Combat"), *GetNameSafe(Controller->GetEnemyCharacter()));

	// Running, never Succeeded: combat ends because a transition fires, not because the
	// task decided it was finished.
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_NeuralCombat::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AEnemyAIController* Controller = ResolveController(InstanceData.AIController);
	if (!Controller)
	{
		return EStateTreeRunStatus::Failed;
	}

	Controller->RunNeuralPipeline(DeltaTime);
	return EStateTreeRunStatus::Running;
}

void FSTTask_NeuralCombat::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AEnemyAIController* Controller = ResolveController(InstanceData.AIController))
	{
		// Hand the pipeline back so the controller keeps working if the tree stops.
		Controller->SetDrivenByStateTree(false);
		Controller->HaltActions();
	}
}

EStateTreeRunStatus FSTTask_EnemyHalt::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AEnemyAIController* Controller = ResolveController(InstanceData.AIController))
	{
		Controller->SetDrivenByStateTree(true);
		Controller->HaltActions();
	}

	return EStateTreeRunStatus::Running;
}

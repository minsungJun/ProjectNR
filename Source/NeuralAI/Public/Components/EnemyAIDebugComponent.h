// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyAIDebugComponent.generated.h"

class UEnemySensorComponent;
class UNeuralBrainComponent;

/**
 * Live readout of what the enemy senses, thinks and does.
 *
 * Tuning a hand-wired neural circuit is guesswork without this, so it exists from the
 * first sensing phase rather than being bolted on at the end. Neural and action sections
 * light up as those phases land.
 */
UCLASS(ClassGroup = (NeuralAI), meta = (BlueprintSpawnableComponent))
class NEURALAI_API UEnemyAIDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyAIDebugComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	void DrawOnScreenValues();
	void DrawWorldShapes();

	/** Text block in the top-left corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowOnScreenValues = true;

	/** Detection radius, facing, and the bearing to the current target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDrawWorldShapes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	FColor TextColor = FColor::Green;

private:
	UPROPERTY(Transient)
	TObjectPtr<UEnemySensorComponent> Sensor;

	UPROPERTY(Transient)
	TObjectPtr<UNeuralBrainComponent> Brain;

	/** Stable on-screen message keys so multiple enemies do not overwrite each other. */
	int32 DebugMessageKeyBase = 0;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NRStaminaComponent.generated.h"

/**
 * The gauge rolls are paid from. Refills at a steady rate; a roll that cannot be paid for does
 * not start (UNRRollComponent asks HasStamina first and pays with Consume once it has begun).
 *
 * MaxStamina is the base; UNRStatsComponent raises it with SetMaxStamina.
 */
UCLASS(ClassGroup = (ProjectNR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRStaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRStaminaComponent();

	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool HasStamina(float Amount) const { return CurrentStamina + KINDA_SMALL_NUMBER >= Amount; }

	/** Takes Amount if there is that much. False, and nothing taken, otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Stamina")
	bool Consume(float Amount);

	/** Changes the cap. Raising it adds the difference, so a bigger gauge arrives filled. */
	UFUNCTION(BlueprintCallable, Category = "Stamina")
	void SetMaxStamina(float NewMaxStamina);

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetCurrentStamina() const { return CurrentStamina; }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetMaxStamina() const { return MaxStamina; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "1.0"))
	float MaxStamina = 50.0f;

	/** Stamina regained per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RegenPerSecond = 5.0f;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Stamina", Transient)
	float CurrentStamina = 0.0f;
};

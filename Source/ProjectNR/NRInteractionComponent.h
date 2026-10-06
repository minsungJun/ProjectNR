// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "NRInteractionComponent.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;

/**
 * Lets the player use INRInteractable actors with one key.
 *
 * Several times a second it picks the nearest interactable within InteractRange that will
 * accept the player; the HUD draws that target's prompt, and the key interacts with it.
 *
 * The key goes through a runtime mapping context above the sample's: IMC_Sandbox already maps
 * E to its own IA_Interact, and the higher context consumes E so only this one fires.
 */
UCLASS(ClassGroup = (ProjectNR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRInteractionComponent();

	/** The interactable the key would use right now, or null. */
	AActor* GetTarget() const { return Target.Get(); }

	const FKey& GetInteractKey() const { return InteractKey; }

	/** Uses the current target. False when there is none or it refused. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool TryInteract();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Same reason as UNRWeaponComponent: a GameMode-spawned pawn has no InputComponent
	 *  during BeginPlay, so binding waits for the restart. */
	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Input")
	FKey InteractKey = EKeys::E;

	/** Must be above IMC_Sandbox's 0 so the key stops reaching the sample's IA_Interact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Input")
	int32 InputPriority = 10;

	/** Farthest an interactable's location may be from the player, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float InteractRange = 220.0f;

	/** Seconds between target searches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float ScanInterval = 0.1f;

private:
	void BindInput();
	void UpdateInputContext();
	void OnInteractInput() { TryInteract(); }
	void FindTarget();

	/** No interacting while dead or while a menu has the cursor. */
	bool IsInteractionBlocked() const;

	TWeakObjectPtr<AActor> Target;
	float NextScanTime = 0.0f;
	bool bInputBound = false;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	/** Removal must find the subsystem it was added to even after the controller is gone. */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ContextSubsystem;
};

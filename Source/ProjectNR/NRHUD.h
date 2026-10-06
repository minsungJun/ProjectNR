// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "NRRunSubsystem.h"
#include "NRHUD.generated.h"

class UHealthComponent;
class UNRLoadoutWidget;
class UNRWeaponComponent;

/**
 * Canvas crosshair, enemy health bars and floating damage numbers.
 *
 * Deliberately a thin renderer: every value it draws with comes from
 * UNRWeaponComponent's getters, and nothing is decided here. Replacing this with a UMG
 * widget means writing the widget and clearing HUDClass - the spread, the hostile test
 * and the visibility rule all stay where they are and the widget binds to the same
 * getters.
 *
 * Draws at the canvas centre, which is the same screen point UNRWeaponComponent traces
 * through, so the crosshair and the shots agree by construction rather than by tuning.
 *
 * Health bars and damage numbers read UHealthComponent only: every living component in
 * this world except the possessed pawn's gets a bar, and every hit it reports through
 * UHealthComponent::OnAnyDamaged becomes a number. Nothing on the enemy side needs to
 * know the HUD exists.
 */
UCLASS()
class PROJECTNR_API ANRHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/**
	 * Opens or closes the part loadout screen. Lives on the HUD rather than the player
	 * controller because GM_Sandbox uses the sample's PC_Sandbox, which is not ours; the HUD
	 * is the one piece of UI the game mode already hands us.
	 */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void ToggleLoadoutMenu();

	UFUNCTION(BlueprintPure, Category = "Loadout")
	bool IsLoadoutMenuOpen() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void OpenLoadoutMenu();
	void CloseLoadoutMenu();

	UPROPERTY(EditDefaultsOnly, Category = "Loadout")
	TSubclassOf<UNRLoadoutWidget> LoadoutWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Loadout")
	FKey LoadoutMenuKey = EKeys::Tab;

	/** Restarts the run once it has ended. Enter, because R reloads and Space jumps. */
	UPROPERTY(EditDefaultsOnly, Category = "Run")
	FKey RestartKey = EKeys::Enter;

	/** Seconds after the run ends before the restart prompt shows and the key works, so the
	 *  ending is seen and a key still held from the fight cannot skip it. */
	UPROPERTY(EditDefaultsOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float RestartPromptDelay = 1.0f;

	void OnRestartPressed();

	UFUNCTION()
	void HandleRunStateChanged(ENRRunState NewState);

	/** Bars farther from the camera than this are not drawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar", meta = (ClampMin = "0.0"))
	float HealthBarMaxDistance = 4000.0f;

	/** Hides bars whose actor is behind level geometry. Other pawns never hide a bar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar")
	bool bHealthBarRequiresLineOfSight = true;

	/** World distance between the top of the actor's collision and the bar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar")
	float HealthBarWorldOffset = 35.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar", meta = (ClampMin = "1.0"))
	float HealthBarWidth = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar", meta = (ClampMin = "1.0"))
	float HealthBarHeight = 7.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar")
	FLinearColor HealthBarBackColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.6f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar")
	FLinearColor HealthBarFillColor = FLinearColor(0.85f, 0.12f, 0.08f, 0.95f);

	/** The just-lost chunk, which lingers and then drains so the size of a hit is readable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar")
	FLinearColor HealthBarTrailColor = FLinearColor(1.0f, 0.85f, 0.55f, 0.9f);

	/** Seconds the lost chunk holds before it starts draining. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar", meta = (ClampMin = "0.0"))
	float HealthBarTrailDelay = 0.4f;

	/** Bar fractions per second the lost chunk drains at. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health Bar", meta = (ClampMin = "0.01"))
	float HealthBarTrailDrainRate = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers", meta = (ClampMin = "0.1"))
	float DamageNumberLifetime = 0.9f;

	/** Pixels a number rises over its lifetime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers")
	float DamageNumberRise = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers", meta = (ClampMin = "0.1"))
	float DamageNumberScale = 1.3f;

	/**
	 * Hits on the same target this close together add into one number instead of
	 * stacking - a shotgun blast reads as one total rather than twelve overlapping 8s.
	 * 0 shows every hit separately.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers", meta = (ClampMin = "0.0"))
	float DamageNumberMergeWindow = 0.12f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers")
	FLinearColor DamageNumberColor = FLinearColor(1.0f, 0.95f, 0.7f, 1.0f);

	/** Numbers at or above this are drawn larger and in BigHitColor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers", meta = (ClampMin = "0.0"))
	float BigHitThreshold = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers")
	FLinearColor BigHitColor = FLinearColor(1.0f, 0.5f, 0.1f, 1.0f);

	/** Used for the hit that killed its target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Damage Numbers")
	FLinearColor KillingHitColor = FLinearColor(1.0f, 0.2f, 0.15f, 1.0f);

	/** Gap between the centre and the inner end of each line, before spread. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.0"))
	float BaseGap = 8.0f;

	/** Pixels the gap grows by at full spread. @see UNRWeaponComponent::GetCrosshairSpread */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.0"))
	float MaxSpreadPixels = 26.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1.0"))
	float LineLength = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1.0"))
	float LineThickness = 2.0f;

	/** Drawn when the aim point is on nothing in particular. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor NeutralColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.75f);

	/** Drawn when the aim point is on something that can take damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor HostileColor = FLinearColor(1.0f, 0.25f, 0.2f, 0.9f);

	/** A single pixel at the exact centre. Useful while checking that shots converge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	bool bDrawCenterDot = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Label", meta = (ClampMin = "0.1"))
	float WeaponLabelScale = 1.5f;

	/** Pixels between the equipped-weapon label and the bottom of the screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Label", meta = (ClampMin = "0.0"))
	float WeaponLabelBottomMargin = 40.0f;

private:
	struct FHealthBarState
	{
		/** Where the lingering "just lost" chunk ends, 0..1. */
		float TrailFraction = 1.0f;
		float LastDamageTime = -1.0e6f;
	};

	struct FDamageNumber
	{
		TWeakObjectPtr<UHealthComponent> Target;
		FVector WorldLocation = FVector::ZeroVector;
		float Amount = 0.0f;
		/** Restarted by merged hits so the number pops again. */
		float StartTime = 0.0f;
		/** Merging is measured from here, so steady fire can't chain into one endless number. */
		float FirstHitTime = 0.0f;
		/** Screen-space sideways offset so back-to-back numbers don't sit on each other. */
		float JitterX = 0.0f;
		bool bKillingHit = false;
	};

	/** The weapon on the currently possessed pawn, or null. */
	UNRWeaponComponent* FindWeapon() const;

	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	/** World point just above the actor's collision, where its bar and numbers sit. */
	FVector GetOverheadLocation(const AActor* Actor) const;

	/** Screen position of WorldLocation, or false when it is behind the camera. */
	bool ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreen) const;

	void DrawHealthBars();
	void DrawDamageNumbers();

	/** Top centre: enemies left while the player's room is fighting, a banner once it is cleared. */
	void DrawRoomStatus();

	/** Top left: run time, rooms and kills; a centred banner once the run has ended. */
	void DrawRunStatus();

	/** Kept after closing so reopening is instant; the loadout itself lives on the pawn. */
	UPROPERTY(Transient)
	TObjectPtr<UNRLoadoutWidget> LoadoutWidget;

	TMap<TWeakObjectPtr<UHealthComponent>, FHealthBarState> BarStates;
	TArray<FDamageNumber> DamageNumbers;
	FDelegateHandle AnyDamagedHandle;
};

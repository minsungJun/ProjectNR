// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "NRWeaponComponent.generated.h"

class AAITestProjectile;
class UAnimMontage;
class UInputAction;

UENUM(BlueprintType)
enum class ENRWeaponKind : uint8
{
	/** Fires projectiles from the muzzle at the crosshair. */
	Ranged,
	/** Swings. The attacks themselves live on UNRMeleeComponent; this slot only selects it. */
	Melee
};

/**
 * Everything that makes one weapon different from another.
 *
 * Aiming, the muzzle and the crosshair are shared by every weapon, so they stay on the
 * component. What is here is only what changes when the player switches.
 */
USTRUCT(BlueprintType)
struct FNRWeaponDefinition
{
	GENERATED_BODY()

	/** Shown on the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FText DisplayName;

	/** Key that equips this weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Input")
	FKey EquipKey;

	/** Melee slots ignore every field below except DisplayName and EquipKey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	ENRWeaponKind Kind = ENRWeaponKind::Ranged;

	/** What to spawn. Range, speed and damage belong to the projectile, not the weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AAITestProjectile> ProjectileClass;

	/** Projectiles spawned per trigger pull. 1 for a rifle, the pellet count for a shotgun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "1"))
	int32 ProjectilesPerShot = 1;

	/**
	 * Half-angle, in degrees, of the cone projectiles scatter in around the aim direction.
	 * 0 fires every projectile dead on the crosshair.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SpreadHalfAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0"))
	float FireCooldown = 0.2f;

	/** Crosshair bloom added by one shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Crosshair", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireSpread = 0.5f;

	/** Played on the mesh when firing. Optional - leave empty to fire with no animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

	/** Slot-relative play rate for @FireMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Animation", meta = (ClampMin = "0.01"))
	float FireMontagePlayRate = 1.0f;
};

/**
 * Fires AAITestProjectile at whatever the crosshair is over, and owns the state a
 * crosshair needs to draw itself.
 *
 * Deliberately a component and not pawn code: it attaches to any pawn, whether that pawn
 * is our C++ ANRPlayerPawn or a Blueprint child of the animation sample's character, and
 * it needs no Blueprint wiring beyond being added to the actor.
 *
 * The aiming is the part worth reading. Firing straight down the camera's forward vector
 * looks right until you notice the shots never hit what the crosshair is on: the muzzle
 * sits to the side of and below the camera, so a parallel ray stays parallel and misses
 * by exactly that offset. @see GetAimPoint for what this does instead.
 *
 * Crosshair *state* lives here rather than in whatever draws it. A canvas HUD and a UMG
 * widget are both thin readers of these getters, so replacing one with the other costs
 * only the drawing code.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRWeaponComponent();

	/** Fires the equipped weapon from the muzzle towards the crosshair. Respects the
	 *  cooldown and returns the number of projectiles spawned, 0 when it declines to fire. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	int32 Fire();

	/** Switches to the weapon at Index in @Weapons. Ignores invalid indices and the weapon
	 *  already in hand. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EquipWeapon(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetCurrentWeaponIndex() const { return CurrentWeaponIndex; }

	/** The equipped weapon, or null if @Weapons is empty. */
	const FNRWeaponDefinition* GetCurrentWeapon() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FText GetCurrentWeaponName() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsMeleeEquipped() const;

	/**
	 * World point under the centre of the screen.
	 *
	 * Traced from the player's actual view point, which is whatever the Gameplay Camera
	 * resolved to this frame rather than any particular camera component, so it keeps
	 * working through camera mode changes and while aiming.
	 *
	 * Falls back to a point far down the view ray when the trace hits nothing, which is
	 * what makes shots at the skybox travel away from the camera instead of collapsing
	 * onto the muzzle.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Aim")
	FVector GetAimPoint() const;

	/** Actor under the centre of the screen, or null. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Aim")
	AActor* GetAimActor() const;

	/** Where projectiles leave the character. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleLocation() const;

	/** True while the cooldown has not elapsed. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsOnCooldown() const;

	// --- Crosshair state -----------------------------------------------------------
	//
	// Normalised and renderer-agnostic on purpose. Whoever draws decides what a spread of
	// 1.0 means in pixels; this only says how wide the crosshair should currently be.

	/** 0 when the weapon is at rest, 1 at maximum bloom. Grows with movement speed and
	 *  with each shot, and decays back over @SpreadRecoveryTime. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Crosshair")
	float GetCrosshairSpread() const;

	/** True when the aim point is on something that can take damage and is still alive. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Crosshair")
	bool IsAimingAtHostile() const;

	/** False while there is no player looking through this pawn, while it is dead, and
	 *  while a montage owns the body - a vault or a climb is not an aiming pose. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Crosshair")
	bool ShouldShowCrosshair() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** A GameMode-spawned pawn has no InputComponent during BeginPlay - it is created when
	 *  the controller possesses it - so binding waits for the restart notification. */
	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	void BindInput();

	void OnFireInput();

	/**
	 * Tells everything that depends on the weapon kind that it changed: the melee component
	 * takes or releases the mouse buttons, and the animation blueprint's rifle upper-body
	 * layer is switched off while swinging.
	 */
	void ApplyWeaponKind();

	/**
	 * Weapons the player carries, in slot order. Defaults to a rifle on 1 and a shotgun on
	 * 2, so the component works with no setup.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TArray<FNRWeaponDefinition> Weapons;

	/** Slot equipped at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	int32 CurrentWeaponIndex = 0;

	/**
	 * Fire action. Optional: with no action assigned the component binds the left mouse
	 * button directly, which keeps it working on pawns whose mapping context has no
	 * fire mapping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Input")
	TObjectPtr<UInputAction> FireAction;

	/** Bone or socket the muzzle is measured from. Leave as None to use the actor's own
	 *  transform, which is what you want until there is a weapon mesh to hang this on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Muzzle")
	FName MuzzleSocketName;

	/** Muzzle position relative to the socket, or to the actor when no socket is set:
	 *  forward, right, up. Chest height and slightly to the right by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Muzzle")
	FVector MuzzleOffset = FVector(30.0f, 15.0f, 45.0f);

	/** How far the crosshair ray reaches before the aim point is treated as "far away". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Aim", meta = (ClampMin = "100.0"))
	float MaxAimDistance = 20000.0f;

	/**
	 * Closest the aim point may be to the camera.
	 *
	 * Without this, standing against a wall puts the aim point behind the muzzle and the
	 * shot fires backwards through the character. Clamping the point forward along the
	 * view ray keeps the direction sane in the one case where convergence cannot work.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Aim", meta = (ClampMin = "0.0"))
	float MinAimDistance = 200.0f;

	// --- Spread tuning -------------------------------------------------------------

	/** Speed at which movement alone contributes @MoveSpread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Crosshair", meta = (ClampMin = "1.0"))
	float SpreadReferenceSpeed = 500.0f;

	/** Spread contributed by running at @SpreadReferenceSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Crosshair", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MoveSpread = 0.5f;

	/** Seconds for a shot's contribution to decay away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Crosshair", meta = (ClampMin = "0.01"))
	float SpreadRecoveryTime = 0.35f;

private:
	/**
	 * Runs the crosshair trace at most once per frame.
	 *
	 * GetAimPoint, GetAimActor and IsAimingAtHostile are all called during the same frame
	 * - the HUD reads two of them and firing reads a third - and there is no reason to pay
	 * for three identical line traces.
	 */
	void UpdateAimCache() const;

	mutable uint64 CachedAimFrame = 0;
	mutable FVector CachedAimPoint = FVector::ZeroVector;
	mutable TWeakObjectPtr<AActor> CachedAimActor;

	float LastFireTime = -1000.0f;

	/** Set from the cooldown of the weapon that fired, not the one in hand. */
	float NextFireTime = -1000.0f;

	/** Bloom of the last shot, kept so switching weapons mid-recovery does not resize it. */
	float LastFireSpread = 0.0f;

	bool bInputBound = false;
};

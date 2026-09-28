// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRWeaponComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NRMeleeComponent.h"
#include "NRProjectiles.h"
#include "Projectiles/AITestProjectile.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRWeapon, Log, All);

UNRWeaponComponent::UNRWeaponComponent()
{
	// Firing is event driven and the crosshair getters are pulled by whoever draws, so
	// there is nothing to push per frame.
	PrimaryComponentTick.bCanEverTick = false;

	FNRWeaponDefinition Rifle;
	Rifle.DisplayName = NSLOCTEXT("NRWeapon", "Rifle", "Rifle");
	Rifle.EquipKey = EKeys::One;
	Rifle.ProjectileClass = ANRRifleBullet::StaticClass();
	Rifle.ProjectilesPerShot = 1;
	Rifle.SpreadHalfAngle = 0.0f;
	Rifle.FireCooldown = 0.2f;
	Rifle.FireSpread = 0.5f;
	Weapons.Add(Rifle);

	// A pump-action rate, and a cone wide enough that the pattern covers a torso at about
	// 5m and has scattered well past it by the time the pellets expire at 15m.
	FNRWeaponDefinition Shotgun;
	Shotgun.DisplayName = NSLOCTEXT("NRWeapon", "Shotgun", "Shotgun");
	Shotgun.EquipKey = EKeys::Two;
	Shotgun.ProjectileClass = ANRShotgunPellet::StaticClass();
	Shotgun.ProjectilesPerShot = 12;
	Shotgun.SpreadHalfAngle = 5.0f;
	Shotgun.FireCooldown = 0.9f;
	Shotgun.FireSpread = 1.0f;
	Weapons.Add(Shotgun);

	FNRWeaponDefinition Melee;
	Melee.DisplayName = NSLOCTEXT("NRWeapon", "Melee", "Axe");
	Melee.EquipKey = EKeys::Three;
	Melee.Kind = ENRWeaponKind::Melee;
	Weapons.Add(Melee);
}

void UNRWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentWeaponIndex = Weapons.IsValidIndex(CurrentWeaponIndex) ? CurrentWeaponIndex : 0;

	// Covers a pawn configured to start on the melee slot.
	if (IsMeleeEquipped())
	{
		ApplyWeaponKind();
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// Already possessed - a pawn placed in the level with auto-possess - so bind now.
		BindInput();

		// Otherwise the InputComponent does not exist yet. A GameMode-spawned pawn runs
		// BeginPlay before possession, and the component is created during the restart.
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRWeaponComponent::OnPawnRestarted);
	}
}

void UNRWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRWeaponComponent::OnPawnRestarted);
	}

	Super::EndPlay(EndPlayReason);
}

void UNRWeaponComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
}

void UNRWeaponComponent::BindInput()
{
	if (bInputBound)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->InputComponent)
	{
		return;
	}

	// Equip keys are bound directly for the same reason as the fallback fire key below: the
	// component works as soon as it is added. BindKey takes no payload, so the slot index
	// rides on a manually built binding instead.
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		if (!Weapons[Index].EquipKey.IsValid())
		{
			continue;
		}

		FInputKeyBinding Binding(FInputChord(Weapons[Index].EquipKey), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &UNRWeaponComponent::EquipWeapon, Index);
		Pawn->InputComponent->KeyBindings.Emplace(MoveTemp(Binding));
	}

	if (FireAction)
	{
		if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Pawn->InputComponent))
		{
			// Triggered rather than Started: with a Down trigger on the action this gives
			// automatic fire, and the cooldown sets the rate. A Pressed trigger still
			// yields one shot per click.
			Input->BindAction(FireAction, ETriggerEvent::Triggered, this, &UNRWeaponComponent::OnFireInput);
			bInputBound = true;
		}
	}
	else
	{
		// No action asset assigned. Binding the key directly means this component works
		// the moment it is added, without editing anyone's mapping context.
		Pawn->InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &UNRWeaponComponent::OnFireInput);
		bInputBound = true;
	}
}

void UNRWeaponComponent::OnFireInput()
{
	Fire();
}

bool UNRWeaponComponent::IsOnCooldown() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < NextFireTime;
}

// --- Weapon slots ------------------------------------------------------------------

const FNRWeaponDefinition* UNRWeaponComponent::GetCurrentWeapon() const
{
	return Weapons.IsValidIndex(CurrentWeaponIndex) ? &Weapons[CurrentWeaponIndex] : nullptr;
}

FText UNRWeaponComponent::GetCurrentWeaponName() const
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	return Weapon ? Weapon->DisplayName : FText::GetEmpty();
}

bool UNRWeaponComponent::IsMeleeEquipped() const
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	return Weapon && Weapon->Kind == ENRWeaponKind::Melee;
}

void UNRWeaponComponent::ApplyWeaponKind()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const bool bMelee = IsMeleeEquipped();

	if (UNRMeleeComponent* MeleeComponent = Owner->FindComponentByClass<UNRMeleeComponent>())
	{
		MeleeComponent->SetMeleeEquipped(bMelee);
	}

	// The shared animation blueprint decides the rifle upper-body layer from its own HasWeapon
	// variable, set once at its BeginPlay from whether this component exists. There is no
	// function to call, so the variable is set by name; if the ABP renames it, this quietly
	// stops doing anything, which is why it logs when the lookup fails.
	if (const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (const FBoolProperty* HasWeapon = FindFProperty<FBoolProperty>(AnimInstance->GetClass(), TEXT("HasWeapon")))
			{
				HasWeapon->SetPropertyValue_InContainer(AnimInstance, !bMelee);
			}
			else
			{
				UE_LOG(LogNRWeapon, Warning, TEXT("%s has no HasWeapon variable; the rifle layer stays as it is"),
					*GetNameSafe(AnimInstance->GetClass()));
			}
		}
	}
}

void UNRWeaponComponent::EquipWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || Index == CurrentWeaponIndex)
	{
		return;
	}

	// Mid-swing the body belongs to the attack montage; switching would strand it.
	if (const UNRMeleeComponent* MeleeComponent = GetOwner()->FindComponentByClass<UNRMeleeComponent>())
	{
		if (MeleeComponent->IsAttacking())
		{
			return;
		}
	}

	CurrentWeaponIndex = Index;
	ApplyWeaponKind();

	// NextFireTime is left alone: it was set by the weapon that fired, so switching cannot be
	// used to skip the shotgun's pump time.
	UE_LOG(LogNRWeapon, Log, TEXT("%s equipped %s"),
		*GetNameSafe(GetOwner()), *Weapons[Index].DisplayName.ToString());
}

// --- Aiming ------------------------------------------------------------------------

void UNRWeaponComponent::UpdateAimCache() const
{
	if (CachedAimFrame == GFrameCounter)
	{
		return;
	}
	CachedAimFrame = GFrameCounter;

	CachedAimPoint = FVector::ZeroVector;
	CachedAimActor = nullptr;

	const UWorld* World = GetWorld();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!World || !PC)
	{
		// Unpossessed: aim where the body faces. Nothing renders a crosshair for this
		// pawn anyway, so there is no convergence to honour.
		CachedAimPoint = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * MaxAimDistance;
		return;
	}

	// The view point, not a camera component. The pawn is driven by a Gameplay Camera whose
	// output is chosen by a chooser, so asking any one component for its transform would be
	// right only some of the time. This is what the player is actually looking through, and
	// it is the same point a screen-centred crosshair is drawn at.
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector ViewDirection = ViewRotation.Vector();
	const FVector TraceEnd = ViewLocation + ViewDirection * MaxAimDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NRWeaponAim), /*bTraceComplex=*/false, GetOwner());
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		CachedAimPoint = TraceEnd;
		return;
	}

	CachedAimActor = Hit.GetActor();

	// Something very close to the camera - a wall the character is pressed against - would
	// otherwise put the aim point behind the muzzle.
	const float HitDistance = FVector::Dist(ViewLocation, Hit.ImpactPoint);
	CachedAimPoint = (HitDistance >= MinAimDistance)
		? Hit.ImpactPoint
		: ViewLocation + ViewDirection * MinAimDistance;
}

FVector UNRWeaponComponent::GetAimPoint() const
{
	UpdateAimCache();
	return CachedAimPoint;
}

AActor* UNRWeaponComponent::GetAimActor() const
{
	UpdateAimCache();
	return CachedAimActor.Get();
}

FVector UNRWeaponComponent::GetMuzzleLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	if (!MuzzleSocketName.IsNone())
	{
		if (const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
		{
			const FTransform SocketTransform = Mesh->GetSocketTransform(MuzzleSocketName);
			return SocketTransform.TransformPosition(MuzzleOffset);
		}
	}

	return Owner->GetActorLocation() + Owner->GetActorRotation().RotateVector(MuzzleOffset);
}

// --- Crosshair state ---------------------------------------------------------------

float UNRWeaponComponent::GetCrosshairSpread() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return 0.0f;
	}

	const float Speed2D = Owner->GetVelocity().Size2D();
	const float MoveAlpha = FMath::Clamp(Speed2D / SpreadReferenceSpeed, 0.0f, 1.0f);
	const float FromMovement = MoveAlpha * MoveSpread;

	// Each shot bumps the spread and it falls off linearly. One value rather than a stored
	// accumulator, so it cannot drift out of sync with the fire timer.
	const float SinceFire = World->GetTimeSeconds() - LastFireTime;
	const float FromFiring = FMath::Max(0.0f, 1.0f - (SinceFire / SpreadRecoveryTime)) * LastFireSpread;

	return FMath::Clamp(FromMovement + FromFiring, 0.0f, 1.0f);
}

bool UNRWeaponComponent::IsAimingAtHostile() const
{
	const AActor* Target = GetAimActor();
	if (!Target || Target == GetOwner())
	{
		return false;
	}

	// "Hostile" is defined by what the rest of the project already agrees on: an actor the
	// damage pipeline understands. No faction system to consult yet.
	const UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
	return Health && !Health->IsDead();
}

bool UNRWeaponComponent::ShouldShowCrosshair() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled() || IsMeleeEquipped())
	{
		return false;
	}

	if (const UHealthComponent* Health = Pawn->FindComponentByClass<UHealthComponent>())
	{
		if (Health->IsDead())
		{
			return false;
		}
	}

	// A montage owning the body means a traversal, a takedown or a hit reaction - none of
	// which are aiming poses. Crude but it costs nothing and covers every case the sample
	// drives through the default slot.
	if (const USkeletalMeshComponent* Mesh = Pawn->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (AnimInstance->IsAnyMontagePlaying())
			{
				return false;
			}
		}
	}

	return true;
}

// --- Firing ------------------------------------------------------------------------

int32 UNRWeaponComponent::Fire()
{
	UWorld* World = GetWorld();
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	// Melee attacks come through UNRMeleeComponent's own input; the left mouse binding that
	// lands here is the ranged one and has nothing to do with an axe.
	if (!World || !Weapon || Weapon->Kind != ENRWeaponKind::Ranged || !Weapon->ProjectileClass || IsOnCooldown())
	{
		return 0;
	}

	const FVector MuzzleLocation = GetMuzzleLocation();
	const FVector AimPoint = GetAimPoint();

	// After the getters above, so the spread they may have read still reflects the shot
	// before this one.
	LastFireTime = World->GetTimeSeconds();
	NextFireTime = LastFireTime + Weapon->FireCooldown;
	LastFireSpread = Weapon->FireSpread;

	// This subtraction is the whole trick: the projectile is aimed from the muzzle at the
	// point the crosshair resolved to, so the two converge there no matter how far the
	// muzzle sits from the camera.
	FVector FireDirection = (AimPoint - MuzzleLocation).GetSafeNormal();
	if (FireDirection.IsNearlyZero())
	{
		FireDirection = GetOwner()->GetActorForwardVector();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Every projectile scatters around the converged direction, not around the camera ray,
	// so the centre of a shotgun pattern still lands on the crosshair.
	const float SpreadRadians = FMath::DegreesToRadians(Weapon->SpreadHalfAngle);
	const int32 Count = FMath::Max(Weapon->ProjectilesPerShot, 1);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Direction = (SpreadRadians > 0.0f)
			? FMath::VRandCone(FireDirection, SpreadRadians)
			: FireDirection;

		AAITestProjectile* Projectile = World->SpawnActor<AAITestProjectile>(
			Weapon->ProjectileClass, MuzzleLocation, Direction.Rotation(), SpawnParams);

		if (Projectile)
		{
			Projectile->LaunchInDirection(Direction);
			++Spawned;
		}
	}

	if (UAnimMontage* FireMontage = Weapon->FireMontage)
	{
		if (const USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				// A plain montage play, not the Mover variant: a fire animation carries no
				// root motion, and routing it through the Mover's layered move would hand
				// movement authority to an animation that never asked for it.
				AnimInstance->Montage_Play(FireMontage, Weapon->FireMontagePlayRate);
			}
		}
	}

	UE_LOG(LogNRWeapon, Verbose, TEXT("%s fired %s: %d projectile(s) towards %s"),
		*GetNameSafe(GetOwner()), *Weapon->DisplayName.ToString(), Spawned, *AimPoint.ToCompactString());

	return Spawned;
}

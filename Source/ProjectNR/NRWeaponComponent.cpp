// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRWeaponComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NRAimComponent.h"
#include "NRAmmo.h"
#include "NRAttackInput.h"
#include "NREnemyAwarenessSubsystem.h"
#include "NRMeleeComponent.h"
#include "NRProjectiles.h"
#include "NRWeaponPartComponent.h"
#include "Projectiles/AITestProjectile.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRWeapon, Log, All);

namespace
{
	bool IsWeaponOwnerDead(const AActor* Owner)
	{
		const UHealthComponent* Health = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health && Health->IsDead();
	}

	TAutoConsoleVariable<bool> CVarLeftHandIK(
		TEXT("NR.Weapon.LeftHandIK"), true,
		TEXT("Left hand reaches for the weapon's LeftHand socket. Off leaves it to the animation (applied on the next equip or NR.Weapon.Refresh)."));

	/** Blueprint "float" variables are doubles since UE5; older ones are floats. */
	void SetAnimFloat(UAnimInstance* Anim, FName Name, double Value)
	{
		if (const FDoubleProperty* Double = FindFProperty<FDoubleProperty>(Anim->GetClass(), Name))
		{
			Double->SetPropertyValue_InContainer(Anim, Value);
		}
		else if (const FFloatProperty* Float = FindFProperty<FFloatProperty>(Anim->GetClass(), Name))
		{
			Float->SetPropertyValue_InContainer(Anim, static_cast<float>(Value));
		}
	}

	double GetAnimFloat(const UAnimInstance* Anim, FName Name, double Default = 0.0)
	{
		if (const FDoubleProperty* Double = FindFProperty<FDoubleProperty>(Anim->GetClass(), Name))
		{
			return Double->GetPropertyValue_InContainer(Anim);
		}
		if (const FFloatProperty* Float = FindFProperty<FFloatProperty>(Anim->GetClass(), Name))
		{
			return Float->GetPropertyValue_InContainer(Anim);
		}
		return Default;
	}

	void SetAnimVector(UAnimInstance* Anim, FName Name, const FVector& Value)
	{
		const FStructProperty* Struct = FindFProperty<FStructProperty>(Anim->GetClass(), Name);
		if (Struct && Struct->Struct == TBaseStructure<FVector>::Get())
		{
			*Struct->ContainerPtrToValuePtr<FVector>(Anim) = Value;
		}
	}

	/** Mesh-space location of a socket on a static mesh. */
	bool FindMeshSocket(const UStaticMesh* Mesh, FName Socket, FTransform& Out)
	{
		const UStaticMeshSocket* Found = (Mesh && !Socket.IsNone()) ? Mesh->FindSocket(Socket) : nullptr;
		if (!Found)
		{
			return false;
		}
		Out = FTransform(Found->RelativeRotation, Found->RelativeLocation, Found->RelativeScale);
		return true;
	}

	UNRWeaponComponent* FindPlayerWeapon(UWorld* World)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (It->IsPlayerControlled())
			{
				if (UNRWeaponComponent* Weapon = It->FindComponentByClass<UNRWeaponComponent>())
				{
					return Weapon;
				}
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdFitToHands(
		TEXT("NR.Weapon.FitToHands"),
		TEXT("NR.Weapon.FitToHands [Forward 0..1]: fits the equipped weapon's MeshOffset to the current hand pose and logs it. Forward turns the barrel from the hand line towards straight ahead. Turn NR.Weapon.LeftHandIK off and NR.Weapon.Refresh first, so the left hand is where the animation puts it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UNRWeaponComponent* Weapon = FindPlayerWeapon(World))
			{
				Weapon->FitMeshOffsetToHands(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSetOffset(
		TEXT("NR.Weapon.Offset"),
		TEXT("NR.Weapon.Offset X Y Z Pitch Yaw Roll: sets the equipped weapon's MeshOffset (relative to its hand socket) and logs it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UNRWeaponComponent* Weapon = FindPlayerWeapon(World);
			if (!Weapon || Args.Num() < 6)
			{
				return;
			}
			const FTransform Offset(
				FRotator(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), FCString::Atof(*Args[5])),
				FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2])));
			Weapon->SetCurrentMeshOffset(Offset);
		}));

	FAutoConsoleCommandWithWorld CmdDebug(
		TEXT("NR.Weapon.Debug"),
		TEXT("Logs the player's hand bones (the posing mesh and any visible child-actor mesh) and the weapon sockets, in the actor's frame (forward, right, up), and draws them for 10 s."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UNRWeaponComponent* Weapon = FindPlayerWeapon(World);
			AActor* Owner = Weapon ? Weapon->GetOwner() : nullptr;
			if (!Owner)
			{
				return;
			}
			const FTransform Frame = Owner->GetActorTransform();
			auto Report = [&](const FString& Label, const FVector& Point, const FColor& Color)
			{
				const FVector L = Frame.InverseTransformPosition(Point);
				UE_LOG(LogNRWeapon, Log, TEXT("  %-34s fwd %7.1f right %7.1f up %7.1f"), *Label, L.X, L.Y, L.Z);
				DrawDebugSphere(World, Point, 2.5f, 8, Color, false, 10.0f, SDPG_Foreground);
			};
			UE_LOG(LogNRWeapon, Log, TEXT("NR.Weapon.Debug %s"), *Owner->GetName());

			TArray<USkeletalMeshComponent*> Meshes;
			Owner->GetComponents(Meshes);
			TArray<AActor*> Attached;
			Owner->GetAttachedActors(Attached, true, true);
			for (AActor* Child : Attached)
			{
				TArray<USkeletalMeshComponent*> ChildMeshes;
				Child->GetComponents(ChildMeshes);
				Meshes.Append(ChildMeshes);
			}
			for (const USkeletalMeshComponent* Mesh : Meshes)
			{
				UE_LOG(LogNRWeapon, Log, TEXT(" %s.%s visible %d"), *GetNameSafe(Mesh->GetOwner()), *Mesh->GetName(), Mesh->IsVisible());
				for (const TCHAR* Bone : { TEXT("hand_r"), TEXT("hand_l"), TEXT("lowerarm_r"), TEXT("lowerarm_l"),
					TEXT("ik_hand_gun"), TEXT("ik_hand_r"), TEXT("ik_hand_l"),
					TEXT("RightHand"), TEXT("LeftHand"), TEXT("RightForeArm"), TEXT("LeftForeArm"), TEXT("RightHandMiddle1") })
				{
					if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
					{
						Report(FString::Printf(TEXT("%s"), Bone), Mesh->GetSocketLocation(Bone), Mesh->IsVisible() ? FColor::Green : FColor::Red);
					}
				}
				// Axes, in the actor's frame, of the bones a weapon could follow.
				for (const TCHAR* Bone : { TEXT("hand_r"), TEXT("ik_hand_gun"), TEXT("RightHand") })
				{
					if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
					{
						const FQuat Q = Frame.GetRotation().Inverse() * Mesh->GetSocketQuaternion(Bone);
						const FVector X = Q.GetAxisX(), Y = Q.GetAxisY(), Z = Q.GetAxisZ();
						UE_LOG(LogNRWeapon, Log, TEXT("  axes %-14s X(%5.2f %5.2f %5.2f) Y(%5.2f %5.2f %5.2f) Z(%5.2f %5.2f %5.2f)"),
							Bone, X.X, X.Y, X.Z, Y.X, Y.Y, Y.Z, Z.X, Z.Y, Z.Z);
					}
				}
			}
			if (const UStaticMeshComponent* Gun = Weapon->GetWeaponMeshComponent(); Gun && Gun->GetStaticMesh())
			{
				Report(TEXT("weapon origin"), Gun->GetComponentLocation(), FColor::Yellow);
				Report(TEXT("weapon +X (muzzle way) 30cm"), Gun->GetComponentLocation() + Gun->GetForwardVector() * 30.0f, FColor::Yellow);
				const FQuat Q = Frame.GetRotation().Inverse() * Gun->GetComponentQuat();
				UE_LOG(LogNRWeapon, Log, TEXT("  axes %-14s X(%5.2f %5.2f %5.2f) Y(%5.2f %5.2f %5.2f) Z(%5.2f %5.2f %5.2f)"), TEXT("weapon"),
					Q.GetAxisX().X, Q.GetAxisX().Y, Q.GetAxisX().Z, Q.GetAxisY().X, Q.GetAxisY().Y, Q.GetAxisY().Z,
					Q.GetAxisZ().X, Q.GetAxisZ().Y, Q.GetAxisZ().Z);
				for (const FName Socket : Gun->GetAllSocketNames())
				{
					Report(FString::Printf(TEXT("weapon socket %s"), *Socket.ToString()), Gun->GetSocketLocation(Socket), FColor::Cyan);
				}
			}
		}));

	FAutoConsoleCommandWithWorld CmdFitAim(
		TEXT("NR.Weapon.FitAim"),
		TEXT("While aiming: turns the equipped weapon so the barrel runs down the view, stores it as its AimMeshOffset and logs it."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UNRWeaponComponent* Weapon = FindPlayerWeapon(World))
			{
				Weapon->FitAimMeshOffset();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSetAimOffset(
		TEXT("NR.Weapon.AimOffset"),
		TEXT("NR.Weapon.AimOffset X Y Z Pitch Yaw Roll: sets the equipped weapon's AimMeshOffset (turns it on) and logs it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UNRWeaponComponent* Weapon = FindPlayerWeapon(World);
			if (!Weapon || Args.Num() < 6)
			{
				return;
			}
			Weapon->SetCurrentAimMeshOffset(FTransform(
				FRotator(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), FCString::Atof(*Args[5])),
				FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]))));
		}));

	FAutoConsoleCommandWithWorld CmdRefresh(
		TEXT("NR.Weapon.Refresh"),
		TEXT("Re-applies the equipped weapon's mesh and left hand target."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UNRWeaponComponent* Weapon = FindPlayerWeapon(World))
			{
				Weapon->SetCurrentMeshOffset(Weapon->GetCurrentWeapon() ? Weapon->GetCurrentWeapon()->MeshOffset : FTransform::Identity);
			}
		}));
}

UNRWeaponComponent::UNRWeaponComponent()
{
	// Ticks only while the fire button is held, for automatic fire. The crosshair getters are
	// pulled by whoever draws, so nothing else needs a frame.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	FNRWeaponDefinition Rifle;
	Rifle.DisplayName = NSLOCTEXT("NRWeapon", "Rifle", "Rifle");
	Rifle.EquipKey = EKeys::One;
	Rifle.ProjectileClass = ANRRifleBullet::StaticClass();
	Rifle.ProjectilesPerShot = 1;
	Rifle.SpreadHalfAngle = 0.0f;
	Rifle.FireCooldown = 0.2f;
	Rifle.FireSpread = 0.5f;
	Rifle.WeaponId = ENRWeaponId::Rifle;
	Rifle.BaseMagazine = 30;
	Rifle.ReloadTime = 1.6f;
	Rifle.BaseCritChance = 0.05f;
	Rifle.BaseCritMultiplier = 1.5f;
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
	Shotgun.WeaponId = ENRWeaponId::Shotgun;
	Shotgun.BaseMagazine = 6;
	Shotgun.ReloadTime = 2.0f;
	Shotgun.BaseCritChance = 0.05f;
	Shotgun.BaseCritMultiplier = 1.5f;
	Weapons.Add(Shotgun);

	FNRWeaponDefinition Melee;
	Melee.DisplayName = NSLOCTEXT("NRWeapon", "Melee", "Axe");
	Melee.EquipKey = EKeys::Three;
	Melee.Kind = ENRWeaponKind::Melee;
	Melee.WeaponId = ENRWeaponId::Melee;
	Melee.BaseCritChance = 0.1f;
	Melee.BaseCritMultiplier = 1.5f;
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

	if (USkeletalMeshComponent* Body = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
	{
		WeaponMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("NRWeaponMesh"));
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMesh->SetGenerateOverlapEvents(false);
		WeaponMesh->SetCanEverAffectNavigation(false);
		WeaponMesh->SetupAttachment(Body);
		WeaponMesh->RegisterComponent();
	}
	UpdateWeaponMesh();

	// The visual-override body is a child actor that may not exist yet; look again once it does.
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { UpdateWeaponMesh(); }));

	// Full magazines to start. The part component computed its stats in InitializeComponent,
	// which runs before any BeginPlay, so the sizes already include starting loadouts.
	Ammo.SetNum(Weapons.Num());
	Reserve.SetNum(Weapons.Num());
	MagazinesFired.Init(0.0f, Weapons.Num());
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		const bool bRanged = Weapons[Index].Kind == ENRWeaponKind::Ranged;
		const int32 Magazine = bRanged ? GetRuntimeStats(Weapons[Index].WeaponId).MaxMagazine : 0;
		Ammo[Index] = Magazine;
		Reserve[Index] = bRanged ? FMath::Min(FMath::RoundToInt32(Magazine * Weapons[Index].StartingReserveMagazines), GetMaxReserveForIndex(Index)) : 0;
	}

	if (UNRWeaponPartComponent* Parts = GetOwner()->FindComponentByClass<UNRWeaponPartComponent>())
	{
		Parts->OnRuntimeStatsChanged.AddUniqueDynamic(this, &UNRWeaponComponent::OnRuntimeStatsChanged);
	}

	// Built at runtime like the melee context: one key, nothing to keep in sync with assets.
	ReloadAction = NewObject<UInputAction>(this, TEXT("IA_NRReload"));
	ReloadContext = NewObject<UInputMappingContext>(this, TEXT("IMC_NRWeapon"));
	ReloadContext->MapKey(ReloadAction, ReloadKey);

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// Already possessed - a pawn placed in the level with auto-possess - so bind now.
		BindInput();
		UpdateReloadContext();

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

	if (UNRWeaponPartComponent* Parts = GetOwner()->FindComponentByClass<UNRWeaponPartComponent>())
	{
		Parts->OnRuntimeStatsChanged.RemoveDynamic(this, &UNRWeaponComponent::OnRuntimeStatsChanged);
	}

	if (WeaponMesh)
	{
		WeaponMesh->DestroyComponent();
		WeaponMesh = nullptr;
	}

	CancelReload();
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ReloadContextSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(ReloadContext);
	}
	ReloadContextSubsystem = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UNRWeaponComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
	UpdateReloadContext();
}

void UNRWeaponComponent::UpdateReloadContext()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Wanted = PC && PC->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;

	if (!ReloadContext || ReloadContextSubsystem.Get() == Wanted)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Previous = ReloadContextSubsystem.Get())
	{
		Previous->RemoveMappingContext(ReloadContext);
	}
	ReloadContextSubsystem = Wanted;
	if (Wanted)
	{
		Wanted->AddMappingContext(ReloadContext, ReloadInputPriority);
	}
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

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Pawn->InputComponent))
	{
		if (ReloadAction)
		{
			Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &UNRWeaponComponent::OnReloadInput);
		}
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
	// Holding the button keeps firing; the tick stops itself once it is released.
	SetComponentTickEnabled(true);
}

void UNRWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The weapon went on the posing mesh because the visible body did not exist yet (the visual
	// override's child actor appears a few frames after BeginPlay). Move it as soon as it does:
	// MeshOffset is tuned for the visible hand, and on the posing hand the weapon points elsewhere
	// and drags the left hand with it.
	// Also when the body it hangs from went away or was hidden: the visual override respawns its
	// child actor (a new BP_Exosuit_C_n) when it is changed, and the weapon must move with it.
	const USkeletalMeshComponent* Current = HandBody.Get();
	if (bWatchHandBody || (WeaponMesh && WeaponMesh->GetStaticMesh() && (!IsValid(Current) || !Current->IsVisible())))
	{
		FName Bone;
		if (FindHandBody(Bone) != HandBody.Get())
		{
			UpdateWeaponMesh();
		}
	}

	if (bBlendAimOffset)
	{
		UpdateAimBlend();
	}

	if (bTrackLeftHand)
	{
		UpdateLeftHandTarget();
	}

	if (!NRAttackInput::IsAttackHeld(GetOwner()) || IsMeleeEquipped())
	{
		if (!bTrackLeftHand && !bWatchHandBody && !bBlendAimOffset)
		{
			SetComponentTickEnabled(false);
		}
		return;
	}

	// Fire() owns every rule - cooldown at the part-adjusted interval, ammo, reload, death -
	// so calling it each frame fires exactly as fast as a perfect clicker would, and an empty
	// magazine reloads and then carries on.
	Fire();
}

void UNRWeaponComponent::OnReloadInput()
{
	Reload();
}

// --- Ammo --------------------------------------------------------------------------

int32 UNRWeaponComponent::GetAmmoForWeapon(ENRWeaponId WeaponId) const
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	return Ammo.IsValidIndex(Index) ? Ammo[Index] : 0;
}

int32 UNRWeaponComponent::GetMaxReserveForIndex(int32 Index) const
{
	if (!Weapons.IsValidIndex(Index) || Weapons[Index].Kind != ENRWeaponKind::Ranged)
	{
		return 0;
	}
	return FMath::RoundToInt32(GetRuntimeStats(Weapons[Index].WeaponId).MaxMagazine * Weapons[Index].MaxReserveMagazines);
}

int32 UNRWeaponComponent::GetReserveAmmo() const
{
	return Reserve.IsValidIndex(CurrentWeaponIndex) ? Reserve[CurrentWeaponIndex] : 0;
}

int32 UNRWeaponComponent::GetMaxReserveAmmo() const
{
	return GetMaxReserveForIndex(CurrentWeaponIndex);
}

int32 UNRWeaponComponent::GetReserveForWeapon(ENRWeaponId WeaponId) const
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	return Reserve.IsValidIndex(Index) ? Reserve[Index] : 0;
}

bool UNRWeaponComponent::IsOutOfAmmo() const
{
	return GetMaxAmmo() > 0 && NRAmmo::IsOutOfAmmo(GetCurrentAmmo(), GetReserveAmmo());
}

int32 UNRWeaponComponent::AddReserveMagazines(float Magazines)
{
	int32 Taken = 0;
	for (int32 Index = 0; Index < Weapons.Num() && Reserve.IsValidIndex(Index); ++Index)
	{
		if (Weapons[Index].Kind != ENRWeaponKind::Ranged)
		{
			continue;
		}
		const int32 Rounds = FMath::RoundToInt32(GetRuntimeStats(Weapons[Index].WeaponId).MaxMagazine * Magazines);
		Taken += NRAmmo::AddToReserve(Reserve[Index], Rounds, GetMaxReserveForIndex(Index));
	}
	if (Taken > 0)
	{
		UE_LOG(LogNRWeapon, Log, TEXT("%s picked up ammo: %d round(s) into reserve, %s now %d/%d in reserve"), *GetNameSafe(GetOwner()), Taken,
			*GetCurrentWeaponName().ToString(), GetReserveAmmo(), GetMaxReserveAmmo());
	}
	return Taken;
}

int32 UNRWeaponComponent::AddReserveMagazinesFor(ENRWeaponId WeaponId, float Magazines)
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	if (!Reserve.IsValidIndex(Index) || Weapons[Index].Kind != ENRWeaponKind::Ranged)
	{
		return 0;
	}
	const int32 Rounds = FMath::RoundToInt32(GetRuntimeStats(WeaponId).MaxMagazine * Magazines);
	const int32 Taken = NRAmmo::AddToReserve(Reserve[Index], Rounds, GetMaxReserveForIndex(Index));
	if (Taken > 0)
	{
		UE_LOG(LogNRWeapon, Log, TEXT("%s got %d %s round(s), %d/%d in reserve"), *GetNameSafe(GetOwner()), Taken,
			*Weapons[Index].DisplayName.ToString(), Reserve[Index], GetMaxReserveForIndex(Index));
	}
	return Taken;
}

int32 UNRWeaponComponent::GetReserveSpaceForWeapon(ENRWeaponId WeaponId) const
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	return Reserve.IsValidIndex(Index) ? FMath::Max(GetMaxReserveForIndex(Index) - Reserve[Index], 0) : 0;
}

float UNRWeaponComponent::GetMagazinesFired(ENRWeaponId WeaponId) const
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	return MagazinesFired.IsValidIndex(Index) ? MagazinesFired[Index] : 0.0f;
}

TArray<ENRWeaponId> UNRWeaponComponent::GetRangedWeaponIds() const
{
	TArray<ENRWeaponId> Ids;
	for (const FNRWeaponDefinition& Weapon : Weapons)
	{
		if (Weapon.Kind == ENRWeaponKind::Ranged)
		{
			Ids.AddUnique(Weapon.WeaponId);
		}
	}
	return Ids;
}

void UNRWeaponComponent::SetCurrentAmmo(int32 Magazine, int32 InReserve)
{
	if (Ammo.IsValidIndex(CurrentWeaponIndex) && Reserve.IsValidIndex(CurrentWeaponIndex) && GetMaxAmmo() > 0)
	{
		Ammo[CurrentWeaponIndex] = FMath::Clamp(Magazine, 0, GetMaxAmmo());
		Reserve[CurrentWeaponIndex] = FMath::Clamp(InReserve, 0, GetMaxReserveAmmo());
	}
}

int32 UNRWeaponComponent::GetCurrentAmmo() const
{
	return Ammo.IsValidIndex(CurrentWeaponIndex) ? Ammo[CurrentWeaponIndex] : 0;
}

int32 UNRWeaponComponent::GetMaxAmmo() const
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	return Weapon && Weapon->Kind == ENRWeaponKind::Ranged ? GetRuntimeStats(Weapon->WeaponId).MaxMagazine : 0;
}

float UNRWeaponComponent::GetReloadProgress() const
{
	const UWorld* World = GetWorld();
	if (!IsReloading() || !World || ReloadDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp((World->GetTimeSeconds() - ReloadStartTime) / ReloadDuration, 0.0f, 1.0f);
}

bool UNRWeaponComponent::Reload()
{
	UWorld* World = GetWorld();
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	if (!World || !Weapon || Weapon->Kind != ENRWeaponKind::Ranged || IsReloading() || IsWeaponOwnerDead(GetOwner())
		|| !Ammo.IsValidIndex(CurrentWeaponIndex) || !Reserve.IsValidIndex(CurrentWeaponIndex)
		|| !NRAmmo::CanReload(Ammo[CurrentWeaponIndex], Reserve[CurrentWeaponIndex], GetMaxAmmo()))
	{
		return false;
	}

	ReloadingWeaponIndex = CurrentWeaponIndex;
	ReloadStartTime = World->GetTimeSeconds();
	ReloadDuration = Weapon->ReloadTime;

	if (ReloadDuration <= 0.0f)
	{
		FinishReload();
		return true;
	}

	World->GetTimerManager().SetTimer(ReloadTimer, this, &UNRWeaponComponent::FinishReload, ReloadDuration, false);
	UE_LOG(LogNRWeapon, Log, TEXT("%s reloading %s (%.1fs)"), *GetNameSafe(GetOwner()),
		*Weapon->DisplayName.ToString(), ReloadDuration);
	return true;
}

void UNRWeaponComponent::FinishReload()
{
	const int32 Index = ReloadingWeaponIndex;
	ReloadingWeaponIndex = INDEX_NONE;
	if (!Weapons.IsValidIndex(Index) || !Ammo.IsValidIndex(Index) || !Reserve.IsValidIndex(Index))
	{
		return;
	}

	// The size is read now rather than when the reload started, in case a part changed it. The
	// rounds come out of the reserve: a short reserve gives a part-full magazine.
	NRAmmo::Reload(Ammo[Index], Reserve[Index], GetRuntimeStats(Weapons[Index].WeaponId).MaxMagazine);
	UE_LOG(LogNRWeapon, Log, TEXT("%s reloaded %s: %d, %d in reserve"), *GetNameSafe(GetOwner()),
		*Weapons[Index].DisplayName.ToString(), Ammo[Index], Reserve[Index]);
}

void UNRWeaponComponent::CancelReload()
{
	if (!IsReloading())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimer);
	}
	ReloadingWeaponIndex = INDEX_NONE;
}

void UNRWeaponComponent::OnRuntimeStatsChanged(ENRWeaponId WeaponId, const FNRWeaponRuntimeStats& Stats)
{
	const int32 Index = Weapons.IndexOfByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
	if (Ammo.IsValidIndex(Index) && Weapons[Index].Kind == ENRWeaponKind::Ranged && Ammo[Index] > Stats.MaxMagazine)
	{
		// Rounds that no longer fit go back to the reserve, not away.
		if (Reserve.IsValidIndex(Index))
		{
			Reserve[Index] += Ammo[Index] - Stats.MaxMagazine;
		}
		Ammo[Index] = Stats.MaxMagazine;
	}
	if (Reserve.IsValidIndex(Index))
	{
		Reserve[Index] = FMath::Min(Reserve[Index], GetMaxReserveForIndex(Index));
	}
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

const FNRWeaponDefinition* UNRWeaponComponent::FindWeaponById(ENRWeaponId WeaponId) const
{
	return Weapons.FindByPredicate([WeaponId](const FNRWeaponDefinition& Weapon) { return Weapon.WeaponId == WeaponId; });
}

FNRWeaponRuntimeStats UNRWeaponComponent::GetRuntimeStats(ENRWeaponId WeaponId) const
{
	if (const UNRWeaponPartComponent* Parts = GetOwner() ? GetOwner()->FindComponentByClass<UNRWeaponPartComponent>() : nullptr)
	{
		return Parts->GetRuntimeStats(WeaponId);
	}

	const FNRWeaponDefinition* Weapon = FindWeaponById(WeaponId);
	return Weapon ? UNRWeaponPartComponent::ComputeRuntimeStats(*Weapon, {}) : FNRWeaponRuntimeStats();
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

	// Aiming is for ranged weapons; with melee in hand the right button is the heavy attack.
	if (UNRAimComponent* Aim = Owner->FindComponentByClass<UNRAimComponent>())
	{
		Aim->SetAimAllowed(!bMelee);
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

	// A reload belongs to the weapon being put away; it is not finished in the holster.
	CancelReload();

	CurrentWeaponIndex = Index;
	ApplyWeaponKind();
	UpdateWeaponMesh();

	// NextFireTime is left alone: it was set by the weapon that fired, so switching cannot be
	// used to skip the shotgun's pump time.
	UE_LOG(LogNRWeapon, Log, TEXT("%s equipped %s"),
		*GetNameSafe(GetOwner()), *Weapons[Index].DisplayName.ToString());
}

// --- Weapon mesh -------------------------------------------------------------------

void UNRWeaponComponent::UpdateWeaponMesh()
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	UStaticMesh* Mesh = Weapon ? Weapon->Mesh.Get() : nullptr;

	FName Bone;
	USkeletalMeshComponent* Body = FindHandBody(Bone);
	HandBody = Body;
	HandBone = Bone;

	if (WeaponMesh)
	{
		WeaponMesh->SetStaticMesh(Mesh);
		WeaponMesh->SetVisibility(Mesh != nullptr);
		if (Mesh && Body)
		{
			WeaponMesh->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
			WeaponMesh->SetRelativeTransform(Weapon->MeshOffset);
		}
	}

	FTransform Socket;
	const USkeletalMeshComponent* Posing = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	bTrackLeftHand = CVarLeftHandIK.GetValueOnGameThread() && WeaponMesh && Mesh && Body && Posing
		&& Posing->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE && FindMeshSocket(Mesh, Weapon->LeftHandSocket, Socket);

	// Still on the posing mesh while a visible hand is wanted: keep looking for the visible body.
	bWatchHandBody = Mesh && Body && Body == Posing && !VisualHandBone.IsNone();
	bBlendAimOffset = Mesh && WeaponMesh && Weapon->bUseAimMeshOffset;
	if (bBlendAimOffset)
	{
		UpdateAimBlend();
	}

	if (UAnimInstance* Anim = Posing ? Posing->GetAnimInstance() : nullptr)
	{
		SetAnimFloat(Anim, TEXT("LeftHandIKAlpha"), bTrackLeftHand ? 1.0 : 0.0);
	}
	if (bTrackLeftHand)
	{
		UpdateLeftHandTarget();
	}
	if (bTrackLeftHand || bWatchHandBody || bBlendAimOffset)
	{
		SetComponentTickEnabled(true);
	}
}

USkeletalMeshComponent* UNRWeaponComponent::FindHandBody(FName& OutBone) const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (!VisualHandBone.IsNone())
	{
		TArray<AActor*> Attached;
		Owner->GetAttachedActors(Attached, true, true);
		for (const AActor* Child : Attached)
		{
			TArray<USkeletalMeshComponent*> Meshes;
			Child->GetComponents(Meshes);
			for (USkeletalMeshComponent* Mesh : Meshes)
			{
				if (Mesh->IsVisible() && Mesh->GetBoneIndex(VisualHandBone) != INDEX_NONE)
				{
					OutBone = VisualHandBone;
					return Mesh;
				}
			}
		}
	}

	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	OutBone = Weapon ? Weapon->HandSocket : NAME_None;
	return Owner->FindComponentByClass<USkeletalMeshComponent>();
}

void UNRWeaponComponent::UpdateLeftHandTarget()
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	const USkeletalMeshComponent* Body = HandBody.Get();
	const USkeletalMeshComponent* Posing = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	UAnimInstance* Anim = Posing ? Posing->GetAnimInstance() : nullptr;
	if (!Weapon || !Body || !Anim || !WeaponMesh)
	{
		return;
	}

	// The IK runs on the hidden posing mesh, relative to its hand_r, and the visible body copies
	// the result. Its hands sit higher than the posing mesh's, but both hands by about the same
	// amount, so what has to carry over is the reach from the right hand to the left: the world
	// offset from the hand the weapon is in to the LeftHand socket, turned into hand_r's frame.
	// Uses last frame's pose, which is all there is before this frame's animation.
	const FVector Reach = WeaponMesh->GetSocketLocation(Weapon->LeftHandSocket) - Body->GetSocketLocation(HandBone);
	const FTransform PosingHand = Posing->GetSocketTransform(TEXT("hand_r"));
	SetAnimVector(Anim, TEXT("LeftHandIKLocation"), PosingHand.GetRotation().UnrotateVector(Reach));
}

#if WITH_EDITOR
void UNRWeaponComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Editing Weapons (MeshOffset, Mesh, sockets) on the player's WeaponComponent in the Details
	// panel during PIE moves the weapon in the hand straight away, for tuning by eye.
	if (WeaponMesh && GetWorld() && GetWorld()->IsGameWorld())
	{
		UpdateWeaponMesh();
	}
}
#endif

void UNRWeaponComponent::SetCurrentMeshOffset(const FTransform& Offset)
{
	if (!Weapons.IsValidIndex(CurrentWeaponIndex))
	{
		return;
	}
	Weapons[CurrentWeaponIndex].MeshOffset = Offset;
	UpdateWeaponMesh();

	const FVector L = Offset.GetLocation();
	const FRotator R = Offset.Rotator();
	UE_LOG(LogNRWeapon, Log, TEXT("%s MeshOffset: Location (X=%.2f,Y=%.2f,Z=%.2f) Rotation (Pitch=%.2f,Yaw=%.2f,Roll=%.2f)  [NR.Weapon.Offset %.2f %.2f %.2f %.2f %.2f %.2f]"),
		*Weapons[CurrentWeaponIndex].DisplayName.ToString(), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll,
		L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll);
}

void UNRWeaponComponent::UpdateAimBlend()
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	if (!Weapon || !WeaponMesh || !Weapon->bUseAimMeshOffset)
	{
		return;
	}

	// The aim pose's own weight, so the weapon turns exactly as fast as the arms do.
	const USkeletalMeshComponent* Posing = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const UAnimInstance* Anim = Posing ? Posing->GetAnimInstance() : nullptr;
	const float Alpha = Anim ? FMath::Clamp(static_cast<float>(GetAnimFloat(Anim, TEXT("AimAlpha"))), 0.0f, 1.0f) : 0.0f;

	FTransform Blended;
	Blended.Blend(Weapon->MeshOffset, Weapon->AimMeshOffset, Alpha);
	if (!WeaponMesh->GetRelativeTransform().Equals(Blended, 0.01))
	{
		WeaponMesh->SetRelativeTransform(Blended);
	}
}

void UNRWeaponComponent::SetCurrentAimMeshOffset(const FTransform& Offset)
{
	if (!Weapons.IsValidIndex(CurrentWeaponIndex))
	{
		return;
	}
	Weapons[CurrentWeaponIndex].AimMeshOffset = Offset;
	Weapons[CurrentWeaponIndex].bUseAimMeshOffset = true;
	UpdateWeaponMesh();

	const FVector L = Offset.GetLocation();
	const FRotator R = Offset.Rotator();
	UE_LOG(LogNRWeapon, Log, TEXT("%s AimMeshOffset: Location (X=%.2f,Y=%.2f,Z=%.2f) Rotation (Pitch=%.2f,Yaw=%.2f,Roll=%.2f)  [NR.Weapon.AimOffset %.2f %.2f %.2f %.2f %.2f %.2f]"),
		*Weapons[CurrentWeaponIndex].DisplayName.ToString(), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll,
		L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll);
}

FTransform UNRWeaponComponent::FitAimMeshOffset()
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FName Bone;
	const USkeletalMeshComponent* Body = FindHandBody(Bone);
	if (!Weapon || !Weapon->Mesh || !Body || !PC)
	{
		return FTransform::Identity;
	}

	// Barrel along the view, top towards the view's up: straight down the crosshair.
	const FRotator View = PC->GetControlRotation();
	const FQuat WeaponWorld = FRotationMatrix::MakeFromXZ(View.Vector(), FRotationMatrix(View).GetUnitAxis(EAxis::Z)).ToQuat();
	const FQuat Hand = Body->GetSocketQuaternion(Bone);

	// The grip stays where MeshOffset puts it in the hand; only the turn changes.
	const FTransform Offset(Hand.Inverse() * WeaponWorld, Weapon->MeshOffset.GetLocation());
	SetCurrentAimMeshOffset(Offset);
	return Offset;
}

FTransform UNRWeaponComponent::FitMeshOffsetToHands(float Forward)
{
	const FNRWeaponDefinition* Weapon = GetCurrentWeapon();
	FName Bone;
	const USkeletalMeshComponent* Body = FindHandBody(Bone);
	if (!Weapon || !Weapon->Mesh || !Body || Body->GetBoneIndex(Bone) == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	// The left hand: the right hand bone's name with the side swapped (RightHand / hand_r).
	FString LeftName = Bone.ToString().Replace(TEXT("Right"), TEXT("Left"));
	if (LeftName.EndsWith(TEXT("_r")))
	{
		LeftName = LeftName.LeftChop(2) + TEXT("_l");
	}
	if (Body->GetBoneIndex(*LeftName) == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	// Palm: a little past the wrist along the forearm, where the grip sits in a closed hand. Not
	// from the finger bones: a hidden posing mesh runs at a LOD without them, so they are
	// nowhere near the hand.
	constexpr float WristToPalm = 7.0f;
	const FTransform Hand = Body->GetSocketTransform(Bone);
	FVector Palm = Hand.GetLocation();
	const FName Forearm = Body->GetParentBone(Bone);
	if (!Forearm.IsNone())
	{
		Palm += (Palm - Body->GetSocketLocation(Forearm)).GetSafeNormal() * WristToPalm;
	}
	const FVector LeftHand = Body->GetSocketLocation(*LeftName);

	// Turn the weapon so its grip-to-LeftHand-socket line runs from the palm to the left hand,
	// with the weapon's top as close to the character's up as that allows.
	FTransform Socket;
	const FVector LocalReach = FindMeshSocket(Weapon->Mesh, Weapon->LeftHandSocket, Socket)
		? Socket.GetLocation().GetSafeNormal() : FVector::ForwardVector;
	// The animation holds the rifle across the body; Forward turns the barrel from that line
	// towards straight ahead (level, the way the character faces), and the left hand follows it
	// through the IK.
	const FVector Ahead = FVector(GetOwner()->GetActorForwardVector().GetSafeNormal2D());
	const FVector WorldReach = FMath::Lerp((LeftHand - Palm).GetSafeNormal(), Ahead, FMath::Clamp(Forward, 0.0f, 1.0f)).GetSafeNormal();
	const FVector Up = GetOwner()->GetActorUpVector();

	const FMatrix LocalFrame = FRotationMatrix::MakeFromXZ(LocalReach, FVector::UpVector);
	const FMatrix WorldFrame = FRotationMatrix::MakeFromXZ(WorldReach, Up);
	const FQuat WeaponRotation = FQuat(WorldFrame) * FQuat(LocalFrame).Inverse();

	const FTransform WeaponWorld(WeaponRotation, Palm);
	const FTransform Offset = WeaponWorld.GetRelativeTransform(FTransform(Hand.GetRotation(), Hand.GetLocation()));
	SetCurrentMeshOffset(Offset);
	return Offset;
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

	// Precision aim is first person with the character hidden: a shot from the chest would
	// appear from the lower right of the screen. Fire from the view point instead, a little
	// ahead of it so the projectile is past the near clip plane on its first frame.
	if (const UNRAimComponent* Aim = Owner->FindComponentByClass<UNRAimComponent>(); Aim && Aim->IsPrecisionAiming())
	{
		const APawn* Pawn = Cast<APawn>(Owner);
		if (const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
		{
			constexpr float ForwardFromView = 40.0f;
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			return ViewLocation + ViewRotation.Vector() * ForwardFromView;
		}
	}

	// The weapon in hand, when it has a muzzle.
	if (const FNRWeaponDefinition* Weapon = GetCurrentWeapon(); Weapon && WeaponMesh && WeaponMesh->IsVisible()
		&& !Weapon->MuzzleSocket.IsNone() && WeaponMesh->DoesSocketExist(Weapon->MuzzleSocket))
	{
		return WeaponMesh->GetSocketLocation(Weapon->MuzzleSocket);
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

	// Precision aim tightens the bloom; shoulder aim and hip fire are unchanged.
	const UNRAimComponent* Aim = Owner->FindComponentByClass<UNRAimComponent>();
	const float AimScale = Aim ? Aim->GetSpreadScale() : 1.0f;

	return FMath::Clamp((FromMovement + FromFiring) * AimScale, 0.0f, 1.0f);
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

	// Hip fire goes where the character faces, not to the screen centre, so a crosshair there
	// would point at the wrong place. It shows only while aiming.
	if (const UNRAimComponent* Aim = Pawn->FindComponentByClass<UNRAimComponent>())
	{
		if (Aim->GetAimMode() == ENRAimMode::None)
		{
			return false;
		}
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
	// The dead do not shoot: the crosshair already hid itself, but the trigger still worked.
	if (!World || !Weapon || Weapon->Kind != ENRWeaponKind::Ranged || !Weapon->ProjectileClass || IsOnCooldown() || IsReloading()
		|| IsWeaponOwnerDead(GetOwner()))
	{
		return 0;
	}

	if (!Ammo.IsValidIndex(CurrentWeaponIndex) || Ammo[CurrentWeaponIndex] <= 0)
	{
		// Reload refuses with nothing in reserve: the trigger just clicks until ammo is found.
		if (bAutoReloadWhenEmpty)
		{
			Reload();
		}
		return 0;
	}

	const FNRWeaponRuntimeStats Stats = GetRuntimeStats(Weapon->WeaponId);
	const FVector MuzzleLocation = GetMuzzleLocation();
	const FVector AimPoint = GetAimPoint();

	// After the getters above, so the spread they may have read still reflects the shot
	// before this one.
	LastFireTime = World->GetTimeSeconds();
	NextFireTime = LastFireTime + Stats.GetAttackInterval(Weapon->FireCooldown);
	LastFireSpread = Weapon->FireSpread;

	// One round per trigger pull, however many pellets it throws.
	--Ammo[CurrentWeaponIndex];
	if (MagazinesFired.IsValidIndex(CurrentWeaponIndex) && Stats.MaxMagazine > 0)
	{
		MagazinesFired[CurrentWeaponIndex] += 1.0f / Stats.MaxMagazine;
	}

	// Damage is the projectile class's own, scaled here, so the bullet and the pellet keep
	// their separate base values.
	const float BaseDamage = Weapon->ProjectileClass->GetDefaultObject<AAITestProjectile>()->GetDamage();

	// Aiming (shoulder or precision): the projectile is aimed from the muzzle at the point
	// the crosshair resolved to - this subtraction is the whole trick that makes the two
	// converge however far the muzzle sits from the camera.
	// Not aiming: hip fire goes where the character faces, level, whatever the camera does;
	// the crosshair is hidden then, so nothing promises otherwise.
	const UNRAimComponent* AimMode = GetOwner()->FindComponentByClass<UNRAimComponent>();
	const bool bHipFire = AimMode && AimMode->GetAimMode() == ENRAimMode::None;
	FVector FireDirection = bHipFire
		? GetOwner()->GetActorForwardVector().GetSafeNormal2D()
		: (AimPoint - MuzzleLocation).GetSafeNormal();
	if (FireDirection.IsNearlyZero())
	{
		FireDirection = GetOwner()->GetActorForwardVector();
	}

	// Every projectile scatters around the converged direction, not around the camera ray,
	// so the centre of a shotgun pattern still lands on the crosshair.
	// Precision aim narrows the cone too, so the shotgun pattern tightens with the crosshair.
	const UNRAimComponent* Aim = GetOwner()->FindComponentByClass<UNRAimComponent>();
	const float SpreadRadians = FMath::DegreesToRadians(Weapon->SpreadHalfAngle * (Aim ? Aim->GetConeScale() : 1.0f));
	const int32 Count = FMath::Max(Weapon->ProjectilesPerShot, 1);

	int32 Spawned = 0;
	int32 Criticals = 0;
	float TotalDamage = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Direction = (SpreadRadians > 0.0f)
			? FMath::VRandCone(FireDirection, SpreadRadians)
			: FireDirection;

		// Deferred so the final damage is in place before the projectile can touch anything.
		const FTransform SpawnTransform(Direction.Rotation(), MuzzleLocation);
		AAITestProjectile* Projectile = World->SpawnActorDeferred<AAITestProjectile>(
			Weapon->ProjectileClass, SpawnTransform, GetOwner(), Cast<APawn>(GetOwner()),
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

		if (Projectile)
		{
			// Rolled per projectile: each pellet can crit on its own.
			bool bCritical = false;
			const float FinalDamage = Stats.RollDamage(BaseDamage, bCritical);
			Projectile->InitializeDamage(FinalDamage);
			Projectile->SetDamageSource(NRToAdaptationWeapon(Weapon->WeaponId));
			Projectile->FinishSpawning(SpawnTransform);
			Projectile->LaunchInDirection(Direction);

			++Spawned;
			Criticals += bCritical ? 1 : 0;
			TotalDamage += FinalDamage;
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

	// The bang reaches the enemies of the room it was fired in; the shot's line reaches those it
	// passes near, who can dodge it now rather than when the round arrives.
	if (UNREnemyAwarenessSubsystem* Awareness = World->GetSubsystem<UNREnemyAwarenessSubsystem>())
	{
		Awareness->NotifyGunshot(GetOwner());
		Awareness->NotifyShotRay(GetOwner(), MuzzleLocation, FireDirection, SpreadRadians,
			Weapon->ProjectileClass->GetDefaultObject<AAITestProjectile>()->GetFlightRange());
	}

	UE_LOG(LogNRWeapon, Log, TEXT("%s fired %s: %d projectile(s), %d crit, %.1f damage total, ammo %d/%d, next in %.2fs"),
		*GetNameSafe(GetOwner()), *Weapon->DisplayName.ToString(), Spawned, Criticals, TotalDamage,
		Ammo[CurrentWeaponIndex], Stats.MaxMagazine, NextFireTime - LastFireTime);

	return Spawned;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "NREnemyAwarenessSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Components/EnemySensorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NRRoom.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRAwareness, Log, All);

static TAutoConsoleVariable<int32> CVarAwarenessDebug(TEXT("NR.Awareness.Debug"), 0,
	TEXT("1: log every shot ray, also those that pass no enemy, and draw it for a second."));

bool UNREnemyAwarenessSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNREnemyAwarenessSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNREnemyAwarenessSubsystem, STATGROUP_Tickables);
}

void UNREnemyAwarenessSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Test tracking turns the view as a player's mouse would.
	if (AActor* Tracked = TrackedEnemy.Get(); Tracked && World->GetTimeSeconds() < TrackUntil)
	{
		FVector From;
		FRotator Unused;
		PC->GetPlayerViewPoint(From, Unused);
		PC->SetControlRotation((Tracked->GetActorLocation() - From).Rotation());
	}
}

TArray<UEnemySensorComponent*> UNREnemyAwarenessSubsystem::FindListeners(const FVector& Location, FString& OutWhere) const
{
	TArray<UEnemySensorComponent*> Listeners;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Listeners;
	}

	const bool bHasRooms = ANRRoom::GetAllRooms().ContainsByPredicate([World](const TWeakObjectPtr<ANRRoom>& Room)
	{
		return Room.IsValid() && Room->GetWorld() == World;
	});
	if (bHasRooms)
	{
		// The room the shot was fired in, and only its own enemies.
		const ANRRoom* Room = ANRRoom::FindRoomAt(World, Location);
		if (!Room)
		{
			OutWhere = TEXT("outside every room");
			return Listeners;
		}
		OutWhere = FString::Printf(TEXT("in %s"), *Room->GetLabel());
		for (const TObjectPtr<AActor>& Enemy : Room->GetEnemies())
		{
			if (UEnemySensorComponent* Sensor = Enemy ? Enemy->FindComponentByClass<UEnemySensorComponent>() : nullptr)
			{
				Listeners.Add(Sensor);
			}
		}
		return Listeners;
	}

	OutWhere = FString::Printf(TEXT("in a world without rooms (%.0f m radius)"), GunshotRadiusWithoutRooms / 100.0f);
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (FVector::Dist(It->GetActorLocation(), Location) <= GunshotRadiusWithoutRooms)
		{
			if (UEnemySensorComponent* Sensor = It->FindComponentByClass<UEnemySensorComponent>())
			{
				Listeners.Add(Sensor);
			}
		}
	}
	return Listeners;
}

void UNREnemyAwarenessSubsystem::NotifyGunshot(AActor* Shooter)
{
	UWorld* World = GetWorld();
	if (!World || !Shooter)
	{
		return;
	}

	FString Where;
	int32 Heard = 0;
	int32 Alerted = 0;
	for (UEnemySensorComponent* Sensor : FindListeners(Shooter->GetActorLocation(), Where))
	{
		++Heard;
		Alerted += Sensor->HearShot(Shooter) ? 1 : 0;
	}

	// A held trigger fires several times a second; one line a second is enough to follow it.
	const float Now = World->GetTimeSeconds();
	if (Alerted > 0 || Now - LastShotLogTime >= 1.0f)
	{
		LastShotLogTime = Now;
		UE_LOG(LogNRAwareness, Log, TEXT("Shot by %s %s: %d enemy(s) can hear it, %d newly alerted"),
			*GetNameSafe(Shooter), *Where, Heard, Alerted);
	}
}

void UNREnemyAwarenessSubsystem::TrackNearestEnemy(float Seconds)
{
	UWorld* World = GetWorld();
	const APawn* Pawn = World && World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	AActor* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const UEnemySensorComponent* Sensor = It->FindComponentByClass<UEnemySensorComponent>();
		const float Distance = FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation());
		if (Sensor && Sensor->GetCurrentTarget() == Pawn && !Sensor->IsSensingByAlert() && Distance < NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}
	TrackedEnemy = Nearest;
	TrackUntil = World->GetTimeSeconds() + Seconds;
	UE_LOG(LogNRAwareness, Log, TEXT("Tracking %s (%.0f cm) for %.1f s"), *GetNameSafe(Nearest), NearestDistance, Seconds);
}

static FAutoConsoleCommandWithWorldAndArgs GNRAwarenessTrackCommand(TEXT("NR.Awareness.Track"),
	TEXT("NR.Awareness.Track [seconds]: keep the view on the nearest enemy that sees the player (default 3 s). Combine with NR.Aim Shoulder."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNREnemyAwarenessSubsystem* Awareness = World ? World->GetSubsystem<UNREnemyAwarenessSubsystem>() : nullptr)
		{
			Awareness->TrackNearestEnemy(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 3.0f);
		}
	}));

void UNREnemyAwarenessSubsystem::NotifyShotRay(AActor* Shooter, const FVector& Origin, const FVector& Direction, float SpreadRadians, float Range)
{
	UWorld* World = GetWorld();
	if (!World || !Shooter || Direction.IsNearlyZero() || Range <= 0.0f)
	{
		return;
	}

	// The shot stops at the first wall: an enemy behind it never sees the line pass.
	float Length = Range;
	FString Stop;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NRShotRay), false, Shooter);
	if (World->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * Range, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Length = Hit.Distance;
		Stop = FString::Printf(TEXT(" to %s (%s)"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
	}

	const float ConeSlope = FMath::Tan(FMath::Clamp(SpreadRadians, 0.0f, FMath::DegreesToRadians(45.0f)));
	int32 Noticed = 0;
	float Closest = TNumericLimits<float>::Max();
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		UEnemySensorComponent* Sensor = It->FindComponentByClass<UEnemySensorComponent>();
		if (!Sensor || Sensor->IsDormant())
		{
			continue;
		}
		const FVector Location = It->GetActorLocation();
		const float Along = FVector::DotProduct(Location - Origin, Direction);
		if (Along <= 0.0f || Along > Length + ShotSenseRadius)
		{
			continue;
		}
		const FVector LinePoint = Origin + Direction * FMath::Min(Along, Length);
		const float Off = FVector::Dist(LinePoint, Location);
		Closest = FMath::Min(Closest, Off);
		if (Off <= ShotSenseRadius + Along * ConeSlope)
		{
			Sensor->NotifyShotAt(Shooter, LinePoint);
			++Noticed;
		}
	}

	const bool bDebug = CVarAwarenessDebug.GetValueOnGameThread() != 0;
	if (Noticed > 0 || bDebug)
	{
		UE_LOG(LogNRAwareness, Log, TEXT("Shot ray by %s: %.0f m%s, %d enemy(s) noticed it (closest %.0f cm off the line)"),
			*GetNameSafe(Shooter), Length / 100.0f, *Stop, Noticed,
			Closest < TNumericLimits<float>::Max() ? Closest : -1.0f);
	}
	if (bDebug)
	{
		DrawDebugLine(World, Origin, Origin + Direction * Length, Noticed > 0 ? FColor::Red : FColor::Yellow, false, 1.0f, 0, 2.0f);
	}
}

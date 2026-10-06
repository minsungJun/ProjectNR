// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRDoor, Log, All);

ANRDoor::ANRDoor()
{
	// Ticks only while moving.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Root);
	DoorMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// A visible slab by default, standing on the actor's origin: 3 m wide, 25 cm thick, 3 m tall.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		DoorMesh->SetStaticMesh(Cube.Object);
		DoorMesh->SetRelativeScale3D(FVector(3.0f, 0.25f, 3.0f));
		DoorMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	}
}

void ANRDoor::BeginPlay()
{
	Super::BeginPlay();

	ClosedLocation = DoorMesh->GetRelativeLocation();
	bWantOpen = bStartOpen;
	Alpha = bStartOpen ? 1.0f : 0.0f;
	ApplyAlpha();
	SetBlocking(!bStartOpen);
}

void ANRDoor::Open()
{
	if (bWantOpen)
	{
		return;
	}
	bWantOpen = true;
	// Stops blocking at once: the room is cleared, and waiting for the slide would leave the
	// player bumping into a door that is visibly on its way down.
	SetBlocking(false);
	SetActorTickEnabled(true);
	UE_LOG(LogNRDoor, Log, TEXT("%s opening"), *GetNameSafe(this));
}

void ANRDoor::Close()
{
	if (!bWantOpen)
	{
		return;
	}
	bWantOpen = false;
	// Blocks at once too: a room locks the player in, and a gap while it slides up would let
	// them straight back out.
	SetBlocking(true);
	SetActorTickEnabled(true);
	UE_LOG(LogNRDoor, Log, TEXT("%s closing"), *GetNameSafe(this));
}

void ANRDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = bWantOpen ? 1.0f : 0.0f;
	const float Step = MoveTime > 0.0f ? DeltaSeconds / MoveTime : 1.0f;
	Alpha = FMath::Clamp(Alpha + (Target > Alpha ? Step : -Step), 0.0f, 1.0f);
	ApplyAlpha();

	if (FMath::IsNearlyEqual(Alpha, Target))
	{
		Alpha = Target;
		ApplyAlpha();
		SetActorTickEnabled(false);
	}
}

void ANRDoor::ApplyAlpha()
{
	// Ease in and out so the slab does not start or stop with a jolt.
	const float Eased = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
	DoorMesh->SetRelativeLocation(ClosedLocation + OpenOffset * Eased);
	DoorMesh->SetVisibility(Alpha < 1.0f);
}

void ANRDoor::SetBlocking(bool bBlocking)
{
	DoorMesh->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

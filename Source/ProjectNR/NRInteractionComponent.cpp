// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRInteractionComponent.h"

#include "CollisionQueryParams.h"
#include "Components/HealthComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "NRInteractable.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRInteract, Log, All);

UNRInteractionComponent::UNRInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNRInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Built at runtime like the reload context: one key, nothing to keep in sync with assets.
	InteractAction = NewObject<UInputAction>(this, TEXT("IA_NRInteract"));
	InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_NRInteract"));
	InputContext->MapKey(InteractAction, InteractKey);

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput();
		UpdateInputContext();
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRInteractionComponent::OnPawnRestarted);
	}
}

void UNRInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRInteractionComponent::OnPawnRestarted);
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ContextSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(InputContext);
	}
	ContextSubsystem = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UNRInteractionComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
	UpdateInputContext();
}

void UNRInteractionComponent::BindInput()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (bInputBound || !Input || !InteractAction)
	{
		return;
	}

	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &UNRInteractionComponent::OnInteractInput);
	bInputBound = true;
}

void UNRInteractionComponent::UpdateInputContext()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Wanted = PC && PC->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;

	if (!InputContext || ContextSubsystem.Get() == Wanted)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Previous = ContextSubsystem.Get())
	{
		Previous->RemoveMappingContext(InputContext);
	}
	ContextSubsystem = Wanted;
	if (Wanted)
	{
		Wanted->AddMappingContext(InputContext, InputPriority);
	}
}

void UNRInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() < NextScanTime)
	{
		return;
	}
	NextScanTime = World->GetTimeSeconds() + ScanInterval;
	FindTarget();
}

bool UNRInteractionComponent::IsInteractionBlocked() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return true;
	}

	const UHealthComponent* Health = Pawn->FindComponentByClass<UHealthComponent>();
	if (Health && Health->IsDead())
	{
		return true;
	}

	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	return PC && PC->ShouldShowMouseCursor();
}

void UNRInteractionComponent::FindTarget()
{
	Target = nullptr;

	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World || IsInteractionBlocked())
	{
		return;
	}

	const FVector Origin = Pawn->GetActorLocation();
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NRInteractScan), false, Pawn);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects, FCollisionShape::MakeSphere(InteractRange), Params);

	float BestDistanceSq = FMath::Square(InteractRange);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		const INRInteractable* Interactable = Cast<INRInteractable>(Actor);
		if (!Interactable || !Interactable->CanInteract(Pawn))
		{
			continue;
		}

		// Measured flat, so a tall prompt point does not push a chest out of range.
		const float DistanceSq = FVector::DistSquared2D(Origin, Interactable->GetInteractLocation());
		if (DistanceSq <= BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Target = Actor;
		}
	}
}

bool UNRInteractionComponent::TryInteract()
{
	if (IsInteractionBlocked())
	{
		return false;
	}

	// Searched again so a press right after walking up does not wait for the next scan.
	FindTarget();
	AActor* Actor = Target.Get();
	INRInteractable* Interactable = Cast<INRInteractable>(Actor);
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Interactable || !Interactable->CanInteract(Pawn))
	{
		return false;
	}

	UE_LOG(LogNRInteract, Log, TEXT("%s interacts with %s"), *GetNameSafe(Pawn), *GetNameSafe(Actor));
	Interactable->Interact(Pawn);
	FindTarget();
	return true;
}

// NR.Interact: press the interact key from the console.
namespace NRInteractCommands
{
	void Interact(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		UNRInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UNRInteractionComponent>() : nullptr;
		if (!Interaction)
		{
			UE_LOG(LogNRInteract, Warning, TEXT("The player pawn has no UNRInteractionComponent"));
			return;
		}
		if (!Interaction->TryInteract())
		{
			UE_LOG(LogNRInteract, Log, TEXT("Nothing to interact with"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRInteractCommand(TEXT("NR.Interact"),
	TEXT("Use whatever the interact key would use right now."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRInteractCommands::Interact));

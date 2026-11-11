#include "NRTraversalDebug.h"

#include "NRPlayerPawn.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/EngineTypes.h"
#include "Containers/Ticker.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRTraversal, Log, All);

static TAutoConsoleVariable<int32> CVarTraversalLog(
	TEXT("NR.Traversal.Log"),
	0,
	TEXT("1 = log every traversal step for each traverse/jump press (see NRTraversalDebug.h)."),
	ECVF_Default);

namespace NRTraversalDebug
{
	namespace
	{
		// The sample's Blueprint names. The class is matched by name up the hierarchy so that a
		// child Blueprint (BP_NR_ShelfLying_Traversable) counts, as the Blueprint cast does.
		const TCHAR* TraversableClassName = TEXT("LevelBlock_Traversable_C");

		// TryTraversalAction's forward trace while walking: the sample's defaults.
		constexpr float TraceRadius = 30.0f;
		constexpr float TraceHalfHeight = 60.0f;

		FString Name(const AActor* Actor)
		{
			return Actor ? FString::Printf(TEXT("%s (%s)"), *Actor->GetActorNameOrLabel(), *Actor->GetClass()->GetName()) : TEXT("none");
		}

		FString Name(const UPrimitiveComponent* Component)
		{
			return Component ? FString::Printf(TEXT("%s.%s"), *GetNameSafe(Component->GetOwner()), *Component->GetName()) : TEXT("none");
		}

		bool IsTraversable(const AActor* Actor)
		{
			for (const UClass* Class = Actor ? Actor->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
			{
				if (Class->GetName() == TraversableClassName)
				{
					return true;
				}
			}
			return false;
		}

		TArray<USplineComponent*> GetLedges(const AActor* Actor)
		{
			TArray<USplineComponent*> Result;
			const FArrayProperty* Property = FindFProperty<FArrayProperty>(Actor->GetClass(), TEXT("Ledges"));
			const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
			if (!Inner)
			{
				return Result;
			}
			FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Actor));
			for (int32 Index = 0; Index < Array.Num(); ++Index)
			{
				Result.Add(Cast<USplineComponent>(Inner->GetObjectPropertyValue(Array.GetRawPtr(Index))));
			}
			return Result;
		}

		USplineComponent* GetOppositeLedge(const AActor* Actor, const USplineComponent* Ledge)
		{
			const FMapProperty* Property = FindFProperty<FMapProperty>(Actor->GetClass(), TEXT("OppositeLedges"));
			const FObjectPropertyBase* Key = Property ? CastField<FObjectPropertyBase>(Property->KeyProp) : nullptr;
			const FObjectPropertyBase* Value = Property ? CastField<FObjectPropertyBase>(Property->ValueProp) : nullptr;
			if (!Key || !Value)
			{
				return nullptr;
			}
			FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Actor));
			for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
			{
				if (Map.IsValidIndex(Index) && Key->GetObjectPropertyValue(Map.GetKeyPtr(Index)) == Ledge)
				{
					return Cast<USplineComponent>(Value->GetObjectPropertyValue(Map.GetValuePtr(Index)));
				}
			}
			return nullptr;
		}

		double GetMinLedgeWidth(const AActor* Actor)
		{
			const FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), TEXT("MinLedgeWidth"));
			if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
			{
				return AsDouble->GetPropertyValue_InContainer(Actor);
			}
			if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
			{
				return AsFloat->GetPropertyValue_InContainer(Actor);
			}
			return -1.0;
		}

		APawn* FindPlayer(UWorld* World)
		{
			return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		}

		/** A Blueprint function's parameters, held for one ProcessEvent call. */
		struct FParams
		{
			explicit FParams(UFunction* InFunction) : Function(InFunction)
			{
				Memory.SetNumZeroed(FMath::Max<int32>(InFunction->ParmsSize, 1));
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->InitializeValue_InContainer(Memory.GetData());
				}
			}
			~FParams()
			{
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->DestroyValue_InContainer(Memory.GetData());
				}
			}
			uint8* Data() { return Memory.GetData(); }

			UFunction* Function;
			TArray<uint8> Memory;
		};

		bool IsOutput(const FProperty* Property)
		{
			return Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm | CPF_ReferenceParm);
		}

		/**
		 * The sample pawn (SandboxCharacter_Mover and its children, e.g. BP_NRPlayer_V2) presses
		 * traverse in its own graph: GetTraversalCheckInputs, then AC_TraversalLogic.TryTraversalAction.
		 * This does the same and logs what TryTraversalAction reports back.
		 */
		bool TrySampleTraversal(APawn* Pawn)
		{
			UActorComponent* Logic = nullptr;
			for (UActorComponent* Component : Pawn->GetComponents())
			{
				if (Component && Component->GetClass()->GetName().StartsWith(TEXT("AC_TraversalLogic")))
				{
					Logic = Component;
					break;
				}
			}
			UFunction* GetInputs = Pawn->FindFunction(TEXT("GetTraversalCheckInputs"));
			UFunction* Try = Logic ? Logic->FindFunction(TEXT("TryTraversalAction")) : nullptr;
			if (!GetInputs || !Try)
			{
				UE_LOG(LogNRTraversal, Warning, TEXT("[Traversal]  %s has no %s"), *Pawn->GetName(),
					!Logic ? TEXT("AC_TraversalLogic component") : !Try ? TEXT("TryTraversalAction") : TEXT("GetTraversalCheckInputs"));
				return false;
			}

			FParams InputsParams(GetInputs);
			Pawn->ProcessEvent(GetInputs, InputsParams.Data());
			const FStructProperty* Inputs = nullptr;
			for (TFieldIterator<FProperty> It(GetInputs); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (IsOutput(*It) && CastField<FStructProperty>(*It))
				{
					Inputs = CastField<FStructProperty>(*It);
					break;
				}
			}

			FParams TryParams(Try);
			for (TFieldIterator<FProperty> It(Try); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FStructProperty* Param = CastField<FStructProperty>(*It);
				if (Inputs && Param && !IsOutput(Param) && Param->Struct == Inputs->Struct)
				{
					Inputs->CopyCompleteValue(Param->ContainerPtrToValuePtr<void>(TryParams.Data()), Inputs->ContainerPtrToValuePtr<void>(InputsParams.Data()));
				}
			}
			Logic->ProcessEvent(Try, TryParams.Data());

			for (TFieldIterator<FProperty> It(Try); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (IsOutput(*It))
				{
					FString Value;
					It->ExportTextItem_InContainer(Value, TryParams.Data(), nullptr, nullptr, PPF_None);
					UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  TryTraversalAction %s = %s"), *It->GetName(), *Value);
				}
			}
			return true;
		}
	}

	bool IsLogging()
	{
		return CVarTraversalLog.GetValueOnGameThread() != 0;
	}

	void LogProbe(APawn* Pawn, const TCHAR* Source)
	{
		UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
		if (!World)
		{
			UE_LOG(LogNRTraversal, Warning, TEXT("[Traversal] %s: no pawn"), Source);
			return;
		}

		const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
		const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 30.0f;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
		const FVector Location = Pawn->GetActorLocation();
		const UCharacterMoverComponent* Mover = Pawn->FindComponentByClass<UCharacterMoverComponent>();
		const FVector Velocity = Mover ? Mover->GetVelocity() : Pawn->GetVelocity();
		const float Speed = Velocity.Size2D();
		const FVector Direction = Speed > 10.0f ? Velocity.GetSafeNormal2D() : Pawn->GetActorForwardVector().GetSafeNormal2D();
		const float Distance = FMath::GetMappedRangeValueClamped(FVector2f(0.0f, 500.0f), FVector2f(75.0f, 350.0f), Speed);

		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal] %s: pawn %s at %s, facing %s, speed %.0f, capsule r %.0f hh %.0f"),
			Source, *Pawn->GetName(), *Location.ToCompactString(), *Direction.ToCompactString(), Speed, Radius, HalfHeight);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(NRTraversalProbe), false, Pawn);

		// 1. Forward trace on the Traversable channel (TraceTypeQuery3), as TryTraversalAction does.
		const ECollisionChannel TraversableChannel = UEngineTypes::ConvertToCollisionChannel(TraceTypeQuery3);
		FHitResult Hit;
		const FVector TraceEnd = Location + Direction * Distance;
		if (!World->SweepSingleByChannel(Hit, Location, TraceEnd, FQuat::Identity, TraversableChannel,
				FCollisionShape::MakeCapsule(TraceRadius, TraceHalfHeight), Params))
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  1 forward trace (%.0f cm on Traversable): hit nothing -> no traversal"), Distance);
			return;
		}
		AActor* HitActor = Hit.GetActor();
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  1 forward trace (%.0f cm on Traversable): hit %s at %s"),
			Distance, *Name(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString());

		// 2. Cast to LevelBlock_Traversable.
		if (!IsTraversable(HitActor))
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  2 %s is not a LevelBlock_Traversable -> cast fails, no traversal"), *Name(HitActor));
			return;
		}

		// 3. Ledges: the one nearest the pawn (point + up * 10, as FindLedgeClosestToActor), its width.
		const TArray<USplineComponent*> Ledges = GetLedges(HitActor);
		const double MinWidth = GetMinLedgeWidth(HitActor);
		USplineComponent* Front = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (USplineComponent* Ledge : Ledges)
		{
			if (!Ledge)
			{
				continue;
			}
			const FVector Point = Ledge->FindLocationClosestToWorldLocation(Location, ESplineCoordinateSpace::World);
			const FVector Up = Ledge->FindUpVectorClosestToWorldLocation(Location, ESplineCoordinateSpace::World);
			const double D = FVector::Dist(Point + Up * 10.0, Location);
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]    ledge %s: nearest %s, out %s, length %.0f, distance %.0f"),
				*Ledge->GetName(), *Point.ToCompactString(), *Up.ToCompactString(), Ledge->GetSplineLength(), D);
			if (D < Best)
			{
				Best = D;
				Front = Ledge;
			}
		}
		if (!Front)
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  3 %s has no ledges (Ledges is empty) -> no traversal"), *Name(HitActor));
			return;
		}
		const float Length = Front->GetSplineLength();
		if (Length < MinWidth)
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  3 front ledge %s is %.0f long, under MinLedgeWidth %.0f -> no traversal"),
				*Front->GetName(), Length, MinWidth);
			return;
		}
		const float AlongHit = Front->GetDistanceAlongSplineAtSplineInputKey(Front->FindInputKeyClosestToWorldLocation(Hit.ImpactPoint));
		const double Along = FMath::Clamp<double>(AlongHit, MinWidth / 2.0, Length - MinWidth / 2.0);
		const FTransform FrontTransform = Front->GetTransformAtDistanceAlongSpline(static_cast<float>(Along), ESplineCoordinateSpace::World);
		const FVector FrontLocation = FrontTransform.GetLocation();
		const FVector FrontNormal = FrontTransform.GetRotation().GetUpVector();
		const double ObstacleHeight = FMath::Abs((Location.Z - HalfHeight) - FrontLocation.Z);
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  3 front ledge %s at %s, out %s, obstacle height %.0f"),
			*Front->GetName(), *FrontLocation.ToCompactString(), *FrontNormal.ToCompactString(), ObstacleHeight);

		USplineComponent* Back = GetOppositeLedge(HitActor, Front);
		FVector BackLocation = FVector::ZeroVector;
		FVector BackNormal = FVector::ZeroVector;
		if (Back)
		{
			const FTransform BackTransform = Back->FindTransformClosestToWorldLocation(FrontLocation, ESplineCoordinateSpace::World);
			BackLocation = BackTransform.GetLocation();
			BackNormal = BackTransform.GetRotation().GetUpVector();
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]    back ledge %s at %s, out %s"), *Back->GetName(), *BackLocation.ToCompactString(), *BackNormal.ToCompactString());
		}
		else
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]    no opposite ledge for %s (OppositeLedges)"), *Front->GetName());
		}

		// 4. Room to stand at the front ledge: the pawn capsule swept from where it is to just over it.
		const FCollisionShape PawnShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		const FVector FrontRoom = FrontLocation + FrontNormal * (Radius + 2.0f) + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
		FHitResult RoomHit;
		const bool bFrontBlocked = World->SweepSingleByChannel(RoomHit, Location, FrontRoom, FQuat::Identity, ECC_Visibility, PawnShape, Params);
		if (bFrontBlocked || RoomHit.bStartPenetrating)
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  4 room at the front ledge (%s): blocked by %s at %s%s -> no traversal"),
				*FrontRoom.ToCompactString(), *Name(RoomHit.GetComponent()), *RoomHit.ImpactPoint.ToCompactString(),
				RoomHit.bStartPenetrating ? TEXT(" (already overlapping at the start)") : TEXT(""));
			return;
		}
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  4 room at the front ledge (%s): clear"), *FrontRoom.ToCompactString());

		// 5. Across the top to the back ledge, then down behind it for a floor.
		if (!Back)
		{
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  5 no back ledge: depth unknown, the chooser decides from front ledge only"));
			return;
		}
		const FVector BackRoom = BackLocation + BackNormal * (Radius + 2.0f) + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
		FHitResult TopHit;
		if (World->SweepSingleByChannel(TopHit, FrontRoom, BackRoom, FQuat::Identity, ECC_Visibility, PawnShape, Params))
		{
			const double Depth = FVector::Dist2D(TopHit.ImpactPoint, FrontLocation);
			UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  5 across the top: blocked by %s after %.0f cm -> depth %.0f, no back ledge (mantle onto it)"),
				*Name(TopHit.GetComponent()), Depth, Depth);
			return;
		}
		const double Depth = FVector::Dist2D(FrontLocation, BackLocation);
		FHitResult FloorHit;
		const FVector FloorEnd = BackLocation + BackNormal * (Radius + 2.0f) - FVector(0.0f, 0.0f, 50.0f);
		const bool bBackFloor = World->SweepSingleByChannel(FloorHit, BackRoom, FloorEnd, FQuat::Identity, ECC_Visibility, PawnShape, Params);
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  5 across the top: clear, depth %.0f; floor behind: %s%s"),
			Depth, bBackFloor ? *Name(FloorHit.GetComponent()) : TEXT("none (drop)"),
			bBackFloor ? *FString::Printf(TEXT(", back ledge height %.0f"), FMath::Abs(FloorHit.ImpactPoint.Z - BackLocation.Z)) : TEXT(""));
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  -> the checks pass: height %.0f, depth %.0f go to CHT_TraversalMontages_Mover"), ObstacleHeight, Depth);
	}

	void LogOutcome(APawn* Pawn)
	{
		const USkeletalMeshComponent* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		UE_LOG(LogNRTraversal, Log, TEXT("[Traversal]  result: %s"),
			Montage ? *FString::Printf(TEXT("montage %s playing (traversal started)"), *Montage->GetName()) : TEXT("no montage (nothing happened or a plain jump)"));
	}
}

// The sample pawn (BP_NRPlayer_V2) handles IA_Jump in its own graph, so ANRPlayerPawn's hooks never
// see the press. While NR.Traversal.Log is on, this adds a second IA_Jump binding to each game
// player pawn that is not an ANRPlayerPawn and logs the same steps on every press.
namespace NRTraversalDebug
{
	namespace
	{
		TArray<TWeakObjectPtr<APawn>> HookedPawns;

		void HookSamplePawns()
		{
			if (!IsLogging() || !GEngine)
			{
				return;
			}
			HookedPawns.RemoveAll([](const TWeakObjectPtr<APawn>& Pawn) { return !Pawn.IsValid(); });
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || !(World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
				{
					continue;
				}
				APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
				UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
				if (!Input || Pawn->IsA<ANRPlayerPawn>() || HookedPawns.Contains(Pawn))
				{
					continue;
				}
				const UInputAction* Jump = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Jump.IA_Jump"));
				if (!Jump)
				{
					continue;
				}
				TWeakObjectPtr<APawn> WeakPawn(Pawn);
				Input->BindActionValueLambda(Jump, ETriggerEvent::Started, [WeakPawn](const FInputActionValue&)
				{
					APawn* Pressed = WeakPawn.Get();
					if (!Pressed || !IsLogging())
					{
						return;
					}
					LogProbe(Pressed, TEXT("jump key"));
					// The sample's own binding runs first; a montage it starts shows up by the next tick.
					Pressed->GetWorldTimerManager().SetTimerForNextTick([WeakPawn]()
					{
						if (APawn* Later = WeakPawn.Get())
						{
							LogOutcome(Later);
						}
					});
				});
				HookedPawns.Add(Pawn);
				UE_LOG(LogNRTraversal, Log, TEXT("[Traversal] logging IA_Jump presses for %s"), *Pawn->GetName());
			}
		}

		// Pawns spawn and get input after the cvar is set, so look again every second while logging.
		const FTSTicker::FDelegateHandle HookTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([](float) { HookSamplePawns(); return true; }), 1.0f);
	}
}

static FAutoConsoleCommandWithWorld GTraversalProbeCommand(
	TEXT("NR.Traversal.Probe"),
	TEXT("Log the traversal checks from where the player stands now."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		NRTraversalDebug::LogProbe(NRTraversalDebug::FindPlayer(World), TEXT("NR.Traversal.Probe"));
	}));

static FAutoConsoleCommandWithWorld GTraversalTryCommand(
	TEXT("NR.Traversal.Try"),
	TEXT("Log the traversal checks, then press traverse for the player."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		APawn* Pawn = NRTraversalDebug::FindPlayer(World);
		NRTraversalDebug::LogProbe(Pawn, TEXT("NR.Traversal.Try"));
		if (ANRPlayerPawn* Player = Cast<ANRPlayerPawn>(Pawn))
		{
			Player->OnTraversePressed();
		}
		else if (!Pawn || !NRTraversalDebug::TrySampleTraversal(Pawn))
		{
			return;
		}
		NRTraversalDebug::LogOutcome(Pawn);
	}));

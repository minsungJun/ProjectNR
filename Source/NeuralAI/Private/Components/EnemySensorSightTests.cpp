// Neural Enemy AI prototype module.

// What an enemy knows of a target behind a wall. Opens Lvl_ClimbAutoTest in PIE and, where the
// player stands once the game has started, puts down a 10 m wall 4 m off and a shooter (held in
// place) 16 m off behind it: the shooter does not detect the player. Told of something (an
// ally's alert, here of a marker standing in for a target) it knows only where that was then,
// not where it goes after, and does not fire. With the wall taken away it sees the player, knows
// where the player is and fires. Automation test NeuralAI.Sensor.Sight (editor only, needs the map).
//
// The player is never moved: put somewhere by hand, it does not stay (the game puts it back).

#include "Characters/RangedEnemyCharacter.h"
#include "Components/EnemySensorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Tests/AutomationEditorCommon.h"

namespace EnemySightTest
{
	struct FFireLog : public FOutputDevice
	{
		int32 Fired = 0;
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) override
		{
			Fired += FString(Message).Contains(TEXT(" fired at ")) ? 1 : 0;
		}
	};

	struct FState
	{
		FFireLog Log;
		TWeakObjectPtr<ARangedEnemyCharacter> Shooter;
		TWeakObjectPtr<AActor> Wall;
		TWeakObjectPtr<AActor> Marker;
		FVector PlayerAt = FVector::ZeroVector;
		FVector MarkerAt = FVector::ZeroVector;
		int32 SavedNeuralMovement = 1;
	};

	AStaticMeshActor* SpawnCube(UWorld* World, const FVector& At, const FVector& Scale, bool bCollides)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), At, FRotator::ZeroRotator, Params);
		UStaticMeshComponent* Mesh = Cube->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(bCollides ? UCollisionProfile::BlockAllDynamic_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Cube->SetActorScale3D(Scale);
		return Cube;
	}

	const UEnemySensorComponent* SensorOf(const FState& State)
	{
		return State.Shooter.IsValid() ? State.Shooter->GetSensorComponent() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightTest, "NeuralAI.Sensor.Sight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemySightTest::RunTest(const FString& Parameters)
{
	using namespace EnemySightTest;
	TSharedRef<FState> State = MakeShared<FState>();

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		// The game sets the player down once it has started; the navmesh is built by then.
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Projected;
		return Navigation && !Navigation->IsNavigationBuildInProgress()
			&& Navigation->ProjectPointToNavigation(FVector(1000.0f, -1200.0f, 462.0f), Projected, FVector(50.0f, 50.0f, 150.0f));
	}, 30.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

	// Wall and shooter (held in place) laid out from where the player stands.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		const APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Player)
		{
			AddError(TEXT("no player"));
			return true;
		}
		IConsoleVariable* Movement = IConsoleManager::Get().FindConsoleVariable(TEXT("NeuralAI.NeuralMovement"));
		State->SavedNeuralMovement = Movement ? Movement->GetInt() : 1;
		if (Movement)
		{
			Movement->Set(0);
		}
		State->PlayerAt = Player->GetActorLocation();
		const FVector Floor(State->PlayerAt.X, State->PlayerAt.Y, 0.0f);
		State->Wall = SpawnCube(World, Floor + FVector(0.0f, 400.0f, 150.0f), FVector(10.0f, 0.5f, 3.0f), true);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		State->Shooter = World->SpawnActor<ARangedEnemyCharacter>(ARangedEnemyCharacter::StaticClass(), Floor + FVector(0.0f, 1600.0f, 70.0f),
			FRotator(0.0f, -90.0f, 0.0f), Params);
		GLog->AddOutputDevice(&State->Log);
		AddInfo(FString::Printf(TEXT("player at %s"), *State->PlayerAt.ToCompactString()));
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		const UEnemySensorComponent* Sensor = SensorOf(*State);
		TestTrue(TEXT("behind the wall the player is not detected"), Sensor && !Sensor->GetCurrentTarget());

		// An ally calls out something behind the wall: a marker standing in for a target.
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		State->MarkerAt = FVector(State->PlayerAt.X + 300.0f, State->PlayerAt.Y - 200.0f, 90.0f);
		State->Marker = SpawnCube(World, State->MarkerAt, FVector(0.5f), false);
		if (State->Shooter.IsValid())
		{
			State->Shooter->GetSensorComponent()->ReceiveAlert(State->Marker.Get(), nullptr);
		}
	}, 2.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		const UEnemySensorComponent* Sensor = SensorOf(*State);
		TestTrue(TEXT("alerted: the target is known of"), Sensor && Sensor->GetCurrentTarget() == State->Marker.Get());
		TestTrue(TEXT("alerted: but not seen"), Sensor && !Sensor->SeesTarget() && !Sensor->IsTargetInSight());
		TestTrue(FString::Printf(TEXT("alerted: it knows where the target was (thinks %s)"), Sensor ? *Sensor->GetTargetLocation().ToCompactString() : TEXT("-")),
			Sensor && FVector::Dist2D(Sensor->GetTargetLocation(), State->MarkerAt) < 10.0f);
		// The target moves on behind the wall.
		if (AActor* Marker = State->Marker.Get())
		{
			Marker->SetActorLocation(State->MarkerAt + FVector(-600.0f, 0.0f, 0.0f));
		}
	}, 0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		const UEnemySensorComponent* Sensor = SensorOf(*State);
		TestTrue(FString::Printf(TEXT("the target moved on unseen: it still thinks it is where it was (thinks %s)"),
			Sensor ? *Sensor->GetTargetLocation().ToCompactString() : TEXT("-")), Sensor && FVector::Dist2D(Sensor->GetTargetLocation(), State->MarkerAt) < 10.0f);
		TestEqual(TEXT("it did not fire at what it could not see"), State->Log.Fired, 0);
		// The wall taken away: the player in plain sight.
		if (AActor* Wall = State->Wall.Get())
		{
			Wall->Destroy();
		}
	}, 3.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		GLog->RemoveOutputDevice(&State->Log);
		const UEnemySensorComponent* Sensor = SensorOf(*State);
		const APawn* Player = UGameplayStatics::GetPlayerPawn(AutomationCommon::GetAnyGameWorld(), 0);
		TestTrue(TEXT("in the open: it sees the player"), Sensor && Player && Sensor->GetCurrentTarget() == Player && Sensor->SeesTarget());
		TestTrue(FString::Printf(TEXT("in the open: it knows where the player is (thinks %s, player at %s)"), Sensor ? *Sensor->GetTargetLocation().ToCompactString() : TEXT("-"),
			Player ? *Player->GetActorLocation().ToCompactString() : TEXT("-")), Sensor && Player && FVector::Dist2D(Sensor->GetTargetLocation(), Player->GetActorLocation()) < 60.0f);
		TestTrue(FString::Printf(TEXT("in the open: it fired (%d)"), State->Log.Fired), State->Log.Fired > 0);
		if (IConsoleVariable* Movement = IConsoleManager::Get().FindConsoleVariable(TEXT("NeuralAI.NeuralMovement")))
		{
			Movement->Set(State->SavedNeuralMovement);
		}
	}, 4.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

// Neural Enemy AI prototype module.

// Shots from a shooter against what stands in their way. Opens Lvl_ClimbAutoTest in PIE, holds the
// player and a shooter still 16 m apart and watches its shots: a block put down between them
// (Movable, WorldDynamic, as placed level blocks are) stops every shot, LevelBlock_Traversable
// too where the project has it; with the way clear the shots reach the player. Automation test
// NeuralAI.Ranged.Cover (editor only, needs the map).

#include "Characters/RangedEnemyCharacter.h"
#include "Components/EnemySensorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Tests/AutomationEditorCommon.h"

namespace EnemyProjectileCoverTest
{
	/** Counts the shooter's shots as the log reports them: fired, hit someone, stopped by something. */
	struct FShotLog : public FOutputDevice
	{
		int32 Fired = 0;
		int32 HitPlayer = 0;
		int32 Stopped = 0;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) override
		{
			const FString Line(Message);
			Fired += Line.Contains(TEXT(" fired at ")) ? 1 : 0;
			HitPlayer += Line.Contains(TEXT("'s shot hit BP_NRPlayer")) ? 1 : 0;
			Stopped += Line.Contains(TEXT("'s shot stopped by ")) ? 1 : 0;
		}

		void Reset()
		{
			Fired = HitPlayer = Stopped = 0;
		}
	};

	struct FState
	{
		FShotLog Log;
		TWeakObjectPtr<ARangedEnemyCharacter> Shooter;
		TWeakObjectPtr<AActor> Cover;
		int32 SavedNeuralMovement = 1;
	};

	const FVector PlayerAt(-2000.0f, -500.0f, 87.0f);
	const FVector ShooterAt(-2000.0f, 1100.0f, 70.0f);

	/** A Movable 3 x 0.5 x 3 m wall across the line between them, as a player would put one down. */
	AActor* SpawnWall(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(-2000.0f, 300.0f, 150.0f), FRotator::ZeroRotator, Params);
		UStaticMeshComponent* Mesh = Wall->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Wall->SetActorScale3D(FVector(3.0f, 0.5f, 3.0f));
		return Wall;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyProjectileCoverTest, "NeuralAI.Ranged.Cover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyProjectileCoverTest::RunTest(const FString& Parameters)
{
	using namespace EnemyProjectileCoverTest;
	TSharedRef<FState> State = MakeShared<FState>();

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));

	// Both held still: the shooter must not walk round the wall, only shoot.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
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
		Player->SetActorLocation(PlayerAt, false, nullptr, ETeleportType::TeleportPhysics);
		if (ACharacter* Character = Cast<ACharacter>(Player))
		{
			Character->GetCharacterMovement()->DisableMovement();
		}
		State->Cover = SpawnWall(World);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		State->Shooter = World->SpawnActor<ARangedEnemyCharacter>(ARangedEnemyCharacter::StaticClass(), ShooterAt, FRotator(0.0f, -90.0f, 0.0f), Params);
		if (State->Shooter.IsValid())
		{
			State->Shooter->GetSensorComponent()->ReceiveAlert(Player, nullptr);
		}
		GLog->AddOutputDevice(&State->Log);
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		AddInfo(FString::Printf(TEXT("behind a placed wall: %d fired, %d hit the player, %d stopped"), State->Log.Fired, State->Log.HitPlayer, State->Log.Stopped));
		TestTrue(TEXT("the shooter fired at the wall"), State->Log.Fired > 0);
		TestEqual(TEXT("no shot went through the placed wall"), State->Log.HitPlayer, 0);
		TestTrue(TEXT("the wall stopped its shots"), State->Log.Stopped > 0);
		if (AActor* Cover = State->Cover.Get())
		{
			Cover->Destroy();
		}

		// The sample traversable block, where there is one.
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		if (UClass* SampleClass = LoadClass<AActor>(nullptr, TEXT("/Game/Levels/LevelPrototyping/LevelBlock_Traversable.LevelBlock_Traversable_C")))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// Its pivot is a corner and it is 1 m a side when spawned: made 3 x 1 x 3 m, on the floor, across the line.
			State->Cover = World->SpawnActor<AActor>(SampleClass, FVector(-2150.0f, 250.0f, 0.0f), FRotator::ZeroRotator, Params);
			if (State->Cover.IsValid())
			{
				State->Cover->SetActorScale3D(FVector(3.0f, 1.0f, 3.0f));
			}
		}
		// Its alert lasts 8 s: renewed for each part, as the cover hides the player from it.
		if (State->Shooter.IsValid())
		{
			State->Shooter->GetSensorComponent()->ReceiveAlert(UGameplayStatics::GetPlayerPawn(AutomationCommon::GetAnyGameWorld(), 0), nullptr);
		}
		State->Log.Reset();
	}, 6.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		if (State->Cover.IsValid())
		{
			AddInfo(FString::Printf(TEXT("behind LevelBlock_Traversable: %d fired, %d hit the player, %d stopped"), State->Log.Fired, State->Log.HitPlayer, State->Log.Stopped));
			TestEqual(TEXT("no shot went through LevelBlock_Traversable"), State->Log.HitPlayer, 0);
			TestTrue(TEXT("LevelBlock_Traversable stopped its shots"), State->Log.Stopped > 0);
			State->Cover->Destroy();
		}
		else
		{
			AddInfo(TEXT("LevelBlock_Traversable is not in this project: that part is skipped"));
		}
		// Its alert lasts 8 s: renewed for each part, as the cover hides the player from it.
		if (State->Shooter.IsValid())
		{
			State->Shooter->GetSensorComponent()->ReceiveAlert(UGameplayStatics::GetPlayerPawn(AutomationCommon::GetAnyGameWorld(), 0), nullptr);
		}
		State->Log.Reset();
	}, 6.5f));

	// The way clear: the shots reach the player.
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		GLog->RemoveOutputDevice(&State->Log);
		AddInfo(FString::Printf(TEXT("in the open: %d fired, %d hit the player, %d stopped"), State->Log.Fired, State->Log.HitPlayer, State->Log.Stopped));
		TestTrue(TEXT("in the open, shots hit the player"), State->Log.HitPlayer > 0);
		if (IConsoleVariable* Movement = IConsoleManager::Get().FindConsoleVariable(TEXT("NeuralAI.NeuralMovement")))
		{
			Movement->Set(State->SavedNeuralMovement);
		}
	}, 6.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

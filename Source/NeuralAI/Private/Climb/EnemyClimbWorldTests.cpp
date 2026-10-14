// Neural Enemy AI prototype module.

// Climbs in a real world: opens Lvl_ClimbTest in PIE, puts crawlers in front of the 1 m ledge,
// the 2 m block, the 4 m platform and the 6 m tower, has each climb the nearest wall it can and
// checks it ends up standing on top. One in front of the 7 m tower finds it too high and takes the
// 4 m platform beside it instead. Last, one is killed half way up the 6 m tower and must fall to
// the floor and lie there. Automation test NeuralAI.Climb.World (editor only, needs the map).

#include "Characters/EnemyCharacter.h"
#include "Climb/EnemyClimbComponent.h"
#include "Climb/EnemyClimbProbe.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Tests/AutomationEditorCommon.h"

namespace EnemyClimbWorldTest
{
	struct FCase
	{
		const TCHAR* Name;
		FVector2D Spawn;
		float Yaw;
		/** Capsule centre height once standing on what it climbed: the top plus 62. */
		float StandZ;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
	};

	struct FState
	{
		TArray<FCase> Cases;
		TWeakObjectPtr<AEnemyCharacter> Doomed;
		int32 SavedNeuralMovement = 1;
	};

	AEnemyCharacter* SpawnCrawler(UWorld* World, const FVector2D& At, float Yaw)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		return World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(), FVector(At.X, At.Y, 70.0f), FRotator(0.0f, Yaw, 0.0f), Params);
	}

	bool IsSettled(const FCase& Case)
	{
		const AEnemyCharacter* Enemy = Case.Enemy.Get();
		return !Enemy || !Enemy->GetClimbComponent()->IsClimbing();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbWorldTest, "NeuralAI.Climb.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbWorldTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;
	TSharedRef<FState> State = MakeShared<FState>();
	State->Cases = {
		{ TEXT("1 m ledge"), FVector2D(-1000.0f, -700.0f), -90.0f, 162.0f },
		{ TEXT("2 m block"), FVector2D(-1000.0f, 800.0f), 90.0f, 262.0f },
		{ TEXT("4 m platform"), FVector2D(1000.0f, -600.0f), -90.0f, 462.0f },
		{ TEXT("6 m tower"), FVector2D(2400.0f, -700.0f), -90.0f, 662.0f },
		{ TEXT("7 m tower, too high: the 4 m platform beside it"), FVector2D(2400.0f, 700.0f), 90.0f, 462.0f },
	};

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbTest"));

	// Crawlers out, standing still but for the climb, each up the nearest wall it can.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		if (!World)
		{
			AddError(TEXT("no game world"));
			return true;
		}
		IConsoleVariable* Movement = IConsoleManager::Get().FindConsoleVariable(TEXT("NeuralAI.NeuralMovement"));
		State->SavedNeuralMovement = Movement ? Movement->GetInt() : 1;
		if (Movement)
		{
			Movement->Set(0);
		}
		const UEnemyClimbSettings& Settings = *GetDefault<UEnemyClimbSettings>();
		for (FCase& Case : State->Cases)
		{
			AEnemyCharacter* Enemy = SpawnCrawler(World, Case.Spawn, Case.Yaw);
			Case.Enemy = Enemy;
			if (!Enemy)
			{
				AddError(FString::Printf(TEXT("%s: no crawler"), Case.Name));
				continue;
			}
			FEnemyClimbLedge Ledge;
			const EEnemyClimbProbeResult Result = FEnemyClimbProbe::FindNearest(World, Enemy, Settings, 16, Ledge);
			TestTrue(FString::Printf(TEXT("%s: found a wall (%s)"), Case.Name, FEnemyClimbProbe::ToString(Result)), Result == EEnemyClimbProbeResult::Ok);
			TestTrue(FString::Printf(TEXT("%s: started"), Case.Name), Result == EEnemyClimbProbeResult::Ok && Enemy->GetClimbComponent()->StartClimb(Ledge));
		}

		// Straight at the 7 m tower: there is a wall, but not one to climb.
		if (AEnemyCharacter* Probe = State->Cases.Last().Enemy.Get())
		{
			FEnemyClimbLedge Ledge;
			const EEnemyClimbProbeResult Result = FEnemyClimbProbe::ProbeDirection(World, Probe, FVector(0.0f, 1.0f, 0.0f), Settings, Ledge);
			TestTrue(FString::Printf(TEXT("7 m tower is too high (got %s, %.0f cm)"), FEnemyClimbProbe::ToString(Result), Ledge.GetHeight()),
				Result == EEnemyClimbProbeResult::TooHigh);
		}
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
	{
		for (const FCase& Case : State->Cases)
		{
			if (!IsSettled(Case))
			{
				return false;
			}
		}
		return true;
	}, 12.0f));

	// Everyone on top, standing.
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		for (const FCase& Case : State->Cases)
		{
			const AEnemyCharacter* Enemy = Case.Enemy.Get();
			if (!Enemy)
			{
				continue;
			}
			TestFalse(FString::Printf(TEXT("%s: done climbing"), Case.Name), Enemy->GetClimbComponent()->IsClimbing());
			TestEqual(FString::Printf(TEXT("%s: standing on top (at %s)"), Case.Name, *Enemy->GetActorLocation().ToCompactString()),
				Enemy->GetActorLocation().Z, static_cast<double>(Case.StandZ), 5.0);
		}
	}, 0.5f));

	// One killed half way up the 6 m tower falls to the floor.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		AEnemyCharacter* Enemy = World ? SpawnCrawler(World, FVector2D(2400.0f, -1700.0f), 90.0f) : nullptr;
		State->Doomed = Enemy;
		FEnemyClimbLedge Ledge;
		const bool bStarted = Enemy
			&& FEnemyClimbProbe::FindNearest(World, Enemy, *GetDefault<UEnemyClimbSettings>(), 16, Ledge) == EEnemyClimbProbeResult::Ok
			&& Enemy->GetClimbComponent()->StartClimb(Ledge);
		TestTrue(TEXT("doomed crawler started up the 6 m tower"), bStarted && Ledge.GetHeight() > 590.0f);
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
	{
		const AEnemyCharacter* Enemy = State->Doomed.Get();
		return !Enemy || (Enemy->GetClimbComponent()->GetPhase() == EEnemyClimbPhase::Climb && Enemy->GetActorLocation().Z > 300.0f);
	}, 6.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		AEnemyCharacter* Enemy = State->Doomed.Get();
		TestTrue(TEXT("doomed crawler is up the wall"), Enemy && Enemy->GetActorLocation().Z > 300.0f);
		if (Enemy)
		{
			Enemy->GetHealthComponent()->ApplyDamage(100000.0f, nullptr, FVector::ZeroVector);
		}
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		if (const AEnemyCharacter* Enemy = State->Doomed.Get())
		{
			TestTrue(TEXT("doomed crawler is dead"), Enemy->IsDead());
			TestFalse(TEXT("doomed crawler let go"), Enemy->GetClimbComponent()->IsClimbing());
			TestEqual(FString::Printf(TEXT("doomed crawler lies on the floor (at %s)"), *Enemy->GetActorLocation().ToCompactString()),
				Enemy->GetActorLocation().Z, 62.0, 10.0);
		}
		if (IConsoleVariable* Movement = IConsoleManager::Get().FindConsoleVariable(TEXT("NeuralAI.NeuralMovement")))
		{
			Movement->Set(State->SavedNeuralMovement);
		}
	}, 3.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

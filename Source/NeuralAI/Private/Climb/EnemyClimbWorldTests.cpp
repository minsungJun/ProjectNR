// Neural Enemy AI prototype module.

// Climbs in a real world: opens Lvl_ClimbAutoTest in PIE, puts crawlers in front of the 1 m ledge,
// the 2 m block, the 4 m platform and the 6 m tower, has each climb the nearest wall it can and
// checks it ends up standing on top. One in front of the 7 m tower finds it too high and takes the
// 4 m platform beside it instead. Last, one is killed half way up the 6 m tower and must fall to
// the floor and lie there. Automation test NeuralAI.Climb.World (editor only, needs the map).
//
// NeuralAI.Climb.Links, in the same map: every climbable block has links up it and the 7 m tower
// none; a path onto the 4 m platform goes up its wall, and leaves it out for an enemy that does
// not climb; onto the platform with a ramp the path takes the ramp from near its foot and the wall
// from the far side; Path Cost in the settings makes enemies' paths there go round by the ramp -
// up when climbing is made dearer (per cm or per climb), down when the way down is; and a crawler
// chasing the player onto the platform climbs up after them.
//
// NeuralAI.Climb.OnDemand, in the same map with no links: a crawler chasing the player across the
// floor never climbs; with the player up on the 4 m platform or the 2 m block (no path up) it
// climbs the wall on its own; with the player on the platform with the ramp it climbs from the
// far side (the wall is much shorter than walking round) and walks up the ramp from near its foot.
//
// NeuralAI.Climb.Crowd, with links: eight crawlers after the player on the 4 m platform go up its
// wall side by side - never two bodies on the wall in each other - and all get up.
//
// NeuralAI.Climb.Traversable: blocks put down during play get links (AddLinksFor) when they never
// move or block the "Traversable" channel - a Movable block that does gets links and a path up its
// wall, a Movable block that does not gets none, and LevelBlock_Traversable (sample content, half
// sunk into the floor as placed by default) gets links when the asset is there. One tagged NoClimb
// gets none, and nor does a wall too thin to stand on. Under a 3 m ceiling a 1.3 m block is climbed
// and a 2 m one is not.
//
// NeuralAI.Climb.Down, with links: a crawler up on the 4 m platform or the 6 m tower, the player on
// the floor, climbs down the wall to get at them; up on the 2 m block (lower than ClimbDownMinHeight)
// it steps off the edge and drops instead.
//
// NeuralAI.Climb.Ranged, with links: a shooter climbs only when it has no place to shoot from -
// with the player in the middle of the 4 m platform, its edge hides the player from every ranged
// slot on the floor and the shooter climbs up; with the player on the 2 m block it shoots from the
// floor and stays there.

#include "Characters/EnemyCharacter.h"
#include "Characters/RangedEnemyCharacter.h"
#include "Climb/EnemyClimbComponent.h"
#include "Climb/EnemyClimbLinkSubsystem.h"
#include "Climb/EnemyClimbNavTypes.h"
#include "Climb/EnemyClimbProbe.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/EnemySensorComponent.h"
#include "Components/HealthComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
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

	AEnemyCharacter* SpawnCrawler(UWorld* World, const FVector2D& At, float Yaw, float FloorZ = 0.0f, bool bRanged = false)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		UClass* Class = bRanged ? ARangedEnemyCharacter::StaticClass() : AEnemyCharacter::StaticClass();
		return World->SpawnActor<AEnemyCharacter>(Class, FVector(At.X, At.Y, FloorZ + 70.0f), FRotator(0.0f, Yaw, 0.0f), Params);
	}

	/** Whether Path goes up a climb link anywhere. */
	/** Whether the path goes over a link of the given area (UNavArea_Climb up, UNavArea_ClimbDown down). */
	bool GoesOverLink(const UWorld* World, UNavigationPath* Path, TSubclassOf<UNavArea> Area)
	{
		const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = Navigation ? Navigation->GetDefaultNavDataInstance() : nullptr;
		const int32 LinkArea = NavData ? NavData->GetAreaID(Area) : INDEX_NONE;
		if (!Path || !Path->GetPath().IsValid() || LinkArea == INDEX_NONE)
		{
			return false;
		}
		for (const FNavPathPoint& Point : Path->GetPath()->GetPathPoints())
		{
			const FNavMeshNodeFlags Flags(Point.Flags);
			if (Flags.IsNavLink() && Flags.Area == LinkArea)
			{
				return true;
			}
		}
		return false;
	}

	bool GoesUpWall(const UWorld* World, UNavigationPath* Path)
	{
		return GoesOverLink(World, Path, UNavArea_Climb::StaticClass());
	}

	bool IsComplete(const UNavigationPath* Path)
	{
		return Path && Path->IsValid() && !Path->IsPartial();
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

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbLinksTest, "NeuralAI.Climb.Links",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbLinksTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;
	struct FChase
	{
		TWeakObjectPtr<AEnemyCharacter> Crawler;
		float MaxZ = 0.0f;
	};
	TSharedRef<FChase> Chase = MakeShared<FChase>();

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));

	// Links in, and the navmesh built (the map rebuilds it on load) - on top of the platforms too.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		const UEnemyClimbLinkSubsystem* Links = World ? World->GetSubsystem<UEnemyClimbLinkSubsystem>() : nullptr;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Projected;
		return Links && Navigation && Links->GetLedges().Num() > 0 && !Navigation->IsNavigationBuildInProgress()
			&& Navigation->ProjectPointToNavigation(FVector(1000.0f, -1200.0f, 462.0f), Projected, FVector(50.0f, 50.0f, 150.0f))
			&& Navigation->ProjectPointToNavigation(FVector(-2000.0f, 0.0f, 62.0f), Projected, FVector(50.0f, 50.0f, 150.0f));
	}, 30.0f));

	// The links take effect once the tiles round them are rebuilt.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return IsComplete(UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1000.0f, -300.0f, 62.0f), FVector(1000.0f, -1200.0f, 462.0f)));
	}, 20.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		const UEnemyClimbLinkSubsystem* Links = World ? World->GetSubsystem<UEnemyClimbLinkSubsystem>() : nullptr;
		if (!Links)
		{
			AddError(TEXT("no climb link subsystem"));
			return;
		}

		// Links on every block that can be climbed, none on the one that cannot.
		TMap<FString, int32> PerWall;
		for (const FEnemyClimbLedge& Ledge : Links->GetLedges())
		{
			PerWall.FindOrAdd(Ledge.Wall.IsValid() ? Ledge.Wall->GetActorLabel() : TEXT("?"))++;
		}
		for (const TCHAR* Wall : { TEXT("Climb_Ledge_1m"), TEXT("Climb_Block_2m"), TEXT("Climb_Platform_4m"), TEXT("Climb_Platform_4m_Ramp"), TEXT("Climb_Tower_6m") })
		{
			TestTrue(FString::Printf(TEXT("%s has climb links (%d)"), Wall, PerWall.FindRef(Wall)), PerWall.FindRef(Wall) > 0);
		}
		TestEqual(TEXT("Climb_Tower_7m has no climb links"), PerWall.FindRef(TEXT("Climb_Tower_7m")), 0);
		for (const TPair<FString, int32>& Wall : PerWall)
		{
			AddInfo(FString::Printf(TEXT("%s: %d climb link(s)"), *Wall.Key, Wall.Value));
		}

		// Onto the 4 m platform from the floor in front of it: up its wall. Not for a shooter.
		const FVector Floor(1000.0f, -300.0f, 62.0f);
		const FVector OnPlatform(1000.0f, -1200.0f, 462.0f);
		UNavigationPath* Climb = UNavigationSystemV1::FindPathToLocationSynchronously(World, Floor, OnPlatform);
		TestTrue(TEXT("a path onto the 4 m platform"), IsComplete(Climb));
		TestTrue(TEXT("it goes up the wall"), GoesUpWall(World, Climb));
		UNavigationPath* NoClimb = UNavigationSystemV1::FindPathToLocationSynchronously(World, Floor, OnPlatform, nullptr, UNavFilter_EnemyNoClimb::StaticClass());
		TestFalse(TEXT("no full path onto it for an enemy that does not climb"), IsComplete(NoClimb));

		// Onto the platform with the ramp: the ramp from near its foot, the wall from the far side.
		const FVector OnRampPlatform(1000.0f, 1200.0f, 462.0f);
		UNavigationPath* ByRamp = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(-300.0f, 1200.0f, 62.0f), OnRampPlatform);
		TestTrue(TEXT("from the ramp's foot: a path"), IsComplete(ByRamp));
		TestFalse(TEXT("from the ramp's foot: up the ramp, not the wall"), GoesUpWall(World, ByRamp));
		UNavigationPath* ByWall = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1700.0f, 1200.0f, 62.0f), OnRampPlatform);
		TestTrue(TEXT("from the far side: a path"), IsComplete(ByWall));
		TestTrue(TEXT("from the far side: up the wall, not round to the ramp"), GoesUpWall(World, ByWall));

		// Path Cost: enemies' paths (UNavFilter_EnemyClimb) read the settings for every path.
		UEnemyClimbSettings* Settings = GetMutableDefault<UEnemyClimbSettings>();
		const float Saved[] = { Settings->ClimbCostFactor, Settings->ClimbEnterCost, Settings->ClimbDownCostFactor, Settings->ClimbDownEnterCost };
		const FVector FarSide(1700.0f, 1200.0f, 62.0f);
		const auto EnemyPath = [World](const FVector& From, const FVector& To)
		{
			return UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, nullptr, UNavFilter_EnemyClimb::StaticClass());
		};
		const auto Restore = [Settings, &Saved]()
		{
			Settings->ClimbCostFactor = Saved[0];
			Settings->ClimbEnterCost = Saved[1];
			Settings->ClimbDownCostFactor = Saved[2];
			Settings->ClimbDownEnterCost = Saved[3];
		};
		Restore();
		TestTrue(TEXT("an enemy's path from the far side: up the wall"), GoesUpWall(World, EnemyPath(FarSide, OnRampPlatform)));
		UNavigationPath* Down = EnemyPath(OnRampPlatform, FarSide);
		TestTrue(TEXT("an enemy's path down to the far side: a path"), IsComplete(Down));
		TestTrue(TEXT("an enemy's path down to the far side: down a link"), GoesOverLink(World, Down, UNavArea_ClimbDown::StaticClass()));

		struct FDearer
		{
			const TCHAR* Name;
			float* Value;
			float Set;
			bool bUp;
		};
		for (const FDearer& Case : {
			FDearer{ TEXT("Climb Cost Factor 100"), &Settings->ClimbCostFactor, 100.0f, true },
			FDearer{ TEXT("Climb Enter Cost 100000"), &Settings->ClimbEnterCost, 100000.0f, true },
			FDearer{ TEXT("Climb Down Cost Factor 100"), &Settings->ClimbDownCostFactor, 100.0f, false },
			FDearer{ TEXT("Climb Down Enter Cost 100000"), &Settings->ClimbDownEnterCost, 100000.0f, false } })
		{
			*Case.Value = Case.Set;
			UNavigationPath* Path = Case.bUp ? EnemyPath(FarSide, OnRampPlatform) : EnemyPath(OnRampPlatform, FarSide);
			TestTrue(FString::Printf(TEXT("%s: still a path"), Case.Name), IsComplete(Path));
			TestFalse(FString::Printf(TEXT("%s: round by the ramp, not over a link"), Case.Name),
				GoesOverLink(World, Path, Case.bUp ? UNavArea_Climb::StaticClass() : UNavArea_ClimbDown::StaticClass()));
			Restore();
		}
	}, 0.1f));

	// The player up on the 4 m platform; a crawler below follows them up.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Chase]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Player)
		{
			AddError(TEXT("no player"));
			return true;
		}
		Player->SetActorLocation(FVector(1000.0f, -1250.0f, 520.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Chase->Crawler = SpawnCrawler(World, FVector2D(1000.0f, -300.0f), -90.0f);
		// The platform's edge hides the player from below: as if an ally had called out.
		if (Chase->Crawler.IsValid())
		{
			Chase->Crawler->GetSensorComponent()->ReceiveAlert(Player, nullptr);
		}
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Chase]()
	{
		const AEnemyCharacter* Crawler = Chase->Crawler.Get();
		if (Crawler)
		{
			Chase->MaxZ = FMath::Max(Chase->MaxZ, static_cast<float>(Crawler->GetActorLocation().Z));
		}
		return !Crawler || (Chase->MaxZ > 450.0f && !Crawler->GetClimbComponent()->IsClimbing());
	}, 15.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Chase]()
	{
		const AEnemyCharacter* Crawler = Chase->Crawler.Get();
		TestTrue(FString::Printf(TEXT("the crawler climbed up after the player (highest %.0f, now at %s)"), Chase->MaxZ,
			Crawler ? *Crawler->GetActorLocation().ToCompactString() : TEXT("gone")), Chase->MaxZ > 450.0f);
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace EnemyClimbWorldTest
{
	struct FChaseCase
	{
		const TCHAR* Name;
		FVector PlayerAt;
		FVector2D CrawlerAt;
		bool bExpectClimb;
		/** Capsule height it must get up to: on top of what the player stands on, or 0 to stay on the floor. */
		float ExpectHeight;
		/** A shooter rather than a crawler. */
		bool bRanged = false;
	};

	struct FChaseRun
	{
		TWeakObjectPtr<AEnemyCharacter> Crawler;
		float MaxZ = 0.0f;
		bool bClimbed = false;
	};

	/** Latent commands for one chase: player put up, crawler set on them, watched until done or out of time. */
	void AddChase(FAutomationTestBase* Test, const FChaseCase& Case, float Timeout)
	{
		TSharedRef<FChaseRun> Run = MakeShared<FChaseRun>();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Case, Run]()
		{
			UWorld* World = AutomationCommon::GetAnyGameWorld();
			APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
			if (!Player)
			{
				Test->AddError(TEXT("no player"));
				return true;
			}
			// Standing where it is put (PlayerAt is its capsule centre), and held there: left alone the
			// player walks off at once (to the platform's edge, in sight of the floor), which would
			// decide the case instead of the climbing.
			Player->SetActorLocation(Case.PlayerAt, false, nullptr, ETeleportType::TeleportPhysics);
			if (ACharacter* Character = Cast<ACharacter>(Player))
			{
				Character->GetCharacterMovement()->DisableMovement();
			}
			Run->Crawler = SpawnCrawler(World, Case.CrawlerAt, 0.0f, 0.0f, Case.bRanged);
			if (Run->Crawler.IsValid())
			{
				Run->Crawler->GetSensorComponent()->ReceiveAlert(Player, nullptr);
			}
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Case, Run]()
		{
			const AEnemyCharacter* Crawler = Run->Crawler.Get();
			if (!Crawler)
			{
				return true;
			}
			Run->MaxZ = FMath::Max(Run->MaxZ, static_cast<float>(Crawler->GetActorLocation().Z));
			Run->bClimbed |= Crawler->GetClimbComponent()->IsClimbing();
			// Done once up where it should be; a crawler that should stay down is watched to the end.
			return Case.ExpectHeight > 0.0f && Run->MaxZ > Case.ExpectHeight && !Crawler->GetClimbComponent()->IsClimbing();
		}, Timeout));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Case, Run]()
		{
			AEnemyCharacter* Crawler = Run->Crawler.Get();
			const FString Where = Crawler ? Crawler->GetActorLocation().ToCompactString() : TEXT("gone");
			Test->TestEqual(FString::Printf(TEXT("%s: climbed (highest %.0f, at %s)"), Case.Name, Run->MaxZ, *Where), Run->bClimbed, Case.bExpectClimb);
			if (Case.ExpectHeight > 0.0f)
			{
				Test->TestTrue(FString::Printf(TEXT("%s: got up (highest %.0f, at %s)"), Case.Name, Run->MaxZ, *Where), Run->MaxZ > Case.ExpectHeight);
			}
			if (Crawler)
			{
				Crawler->Destroy();
			}
			if (ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(AutomationCommon::GetAnyGameWorld(), 0)))
			{
				Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			}
			return true;
		}));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbOnDemandTest, "NeuralAI.Climb.OnDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbOnDemandTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;

	// No links: every climb here is the controller's own decision.
	UEnemyClimbSettings* Settings = GetMutableDefault<UEnemyClimbSettings>();
	const bool bLinks = Settings->bBuildClimbLinks;
	Settings->bBuildClimbLinks = false;
	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));

	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Projected;
		return Navigation && !Navigation->IsNavigationBuildInProgress()
			&& Navigation->ProjectPointToNavigation(FVector(1000.0f, -1200.0f, 462.0f), Projected, FVector(50.0f, 50.0f, 150.0f))
			&& Navigation->ProjectPointToNavigation(FVector(-2000.0f, 0.0f, 62.0f), Projected, FVector(50.0f, 50.0f, 150.0f));
	}, 30.0f));

	AddChase(this, { TEXT("flat floor"), FVector(-2000.0f, -300.0f, 87.0f), FVector2D(-2000.0f, 1500.0f), false, 0.0f }, 5.0f);
	AddChase(this, { TEXT("4 m platform, no way up"), FVector(1000.0f, -1250.0f, 487.0f), FVector2D(1000.0f, -300.0f), true, 450.0f }, 12.0f);
	AddChase(this, { TEXT("2 m block, no way up"), FVector(-1000.0f, 1200.0f, 287.0f), FVector2D(-1000.0f, 500.0f), true, 250.0f }, 12.0f);
	AddChase(this, { TEXT("ramp platform from the far side"), FVector(1000.0f, 1250.0f, 487.0f), FVector2D(1700.0f, 1200.0f), true, 450.0f }, 12.0f);
	AddChase(this, { TEXT("ramp platform from the ramp's foot"), FVector(1000.0f, 1250.0f, 487.0f), FVector2D(-400.0f, 1200.0f), false, 450.0f }, 15.0f);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([bLinks]()
	{
		GetMutableDefault<UEnemyClimbSettings>()->bBuildClimbLinks = bLinks;
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbCrowdTest, "NeuralAI.Climb.Crowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbCrowdTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;
	struct FCrowd
	{
		TArray<TWeakObjectPtr<AEnemyCharacter>> Crawlers;
		TArray<float> MaxZ;
		float Closest = TNumericLimits<float>::Max();
		FString ClosestWhere;
		int32 Moved = 0;
	};
	TSharedRef<FCrowd> Crowd = MakeShared<FCrowd>();

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return IsComplete(UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1000.0f, -300.0f, 62.0f), FVector(1000.0f, -1200.0f, 462.0f)));
	}, 30.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Crowd]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Player)
		{
			AddError(TEXT("no player"));
			return true;
		}
		Player->SetActorLocation(FVector(1000.0f, -1250.0f, 520.0f), false, nullptr, ETeleportType::TeleportPhysics);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FVector2D At(760.0f + (Index % 4) * 160.0f, -450.0f - (Index / 4) * 160.0f);
			if (AEnemyCharacter* Crawler = SpawnCrawler(World, At, -90.0f))
			{
				Crawler->GetSensorComponent()->ReceiveAlert(Player, nullptr);
				Crowd->Crawlers.Add(Crawler);
				Crowd->MaxZ.Add(0.0f);
			}
		}
		return true;
	}));

	// Watched every frame: who is on the wall, and how close the nearest two of them come.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Crowd]()
	{
		bool bAllUp = Crowd->Crawlers.Num() > 0;
		TArray<const AEnemyCharacter*> OnWall;
		for (int32 Index = 0; Index < Crowd->Crawlers.Num(); ++Index)
		{
			const AEnemyCharacter* Crawler = Crowd->Crawlers[Index].Get();
			if (!Crawler)
			{
				continue;
			}
			Crowd->MaxZ[Index] = FMath::Max(Crowd->MaxZ[Index], static_cast<float>(Crawler->GetActorLocation().Z));
			bAllUp &= Crowd->MaxZ[Index] > 450.0f && !Crawler->GetClimbComponent()->IsClimbing();
			const EEnemyClimbPhase Phase = Crawler->GetClimbComponent()->GetPhase();
			if (Phase == EEnemyClimbPhase::Climb || Phase == EEnemyClimbPhase::Align)
			{
				OnWall.Add(Crawler);
			}
		}
		for (int32 A = 0; A < OnWall.Num(); ++A)
		{
			for (int32 B = A + 1; B < OnWall.Num(); ++B)
			{
				// Side by side or one above the other, but not in each other: closer than two radii
				// across only with more than a body's height between them.
				const FVector Between = OnWall[A]->GetActorLocation() - OnWall[B]->GetActorLocation();
				if (FMath::Abs(Between.Z) >= 120.0f)
				{
					continue;
				}
				const float Gap = Between.Size2D();
				if (Gap < Crowd->Closest)
				{
					Crowd->Closest = Gap;
					Crowd->ClosestWhere = FString::Printf(TEXT("%s and %s"), *GetNameSafe(OnWall[A]), *GetNameSafe(OnWall[B]));
				}
			}
		}
		return bAllUp;
	}, 25.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Crowd]()
	{
		int32 Up = 0;
		for (const float Z : Crowd->MaxZ)
		{
			Up += Z > 450.0f ? 1 : 0;
		}
		TestEqual(TEXT("crawlers that got up onto the platform"), Up, Crowd->Crawlers.Num());
		if (Crowd->Closest < TNumericLimits<float>::Max())
		{
			AddInfo(FString::Printf(TEXT("closest two side by side on the wall: %.0f cm (%s)"), Crowd->Closest, *Crowd->ClosestWhere));
			TestTrue(FString::Printf(TEXT("never two bodies on the wall in each other (closest %.0f cm across)"), Crowd->Closest), Crowd->Closest >= 83.0f);
		}
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace EnemyClimbWorldTest
{
	/** A Movable 2 x 2 x 2.5 m block standing on the floor at At, blocking the channel named Channel if given. */
	AStaticMeshActor* SpawnMovableBlock(UWorld* World, const FVector2D& At, FName Channel)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(At.X, At.Y, 125.0f), FRotator::ZeroRotator, Params);
		if (!Block)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Block->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Block->SetActorScale3D(FVector(2.0f, 2.0f, 2.5f));
		for (int32 Index = 0; Channel != NAME_None && Index < ECC_MAX; ++Index)
		{
			if (UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(Index) == Channel)
			{
				Mesh->SetCollisionResponseToChannel(static_cast<ECollisionChannel>(Index), ECR_Block);
			}
		}
		return Block;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbTraversableTest, "NeuralAI.Climb.Traversable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbTraversableTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return IsComplete(UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1000.0f, -300.0f, 62.0f), FVector(1000.0f, -1200.0f, 462.0f)));
	}, 30.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		UEnemyClimbLinkSubsystem* Links = World ? World->GetSubsystem<UEnemyClimbLinkSubsystem>() : nullptr;
		if (!Links)
		{
			AddError(TEXT("no climb link subsystem"));
			return true;
		}
		AStaticMeshActor* Traversable = SpawnMovableBlock(World, FVector2D(-2200.0f, -1300.0f), TEXT("Traversable"));
		AStaticMeshActor* Plain = SpawnMovableBlock(World, FVector2D(-2200.0f, 1300.0f), NAME_None);
		AStaticMeshActor* Tagged = SpawnMovableBlock(World, FVector2D(-2600.0f, -1300.0f), TEXT("Traversable"));
		Tagged->Tags.Add(GetDefault<UEnemyClimbSettings>()->NoClimbTag);
		const int32 TraversableLinks = Links->AddLinksFor(Traversable);
		const int32 PlainLinks = Links->AddLinksFor(Plain);
		const int32 TaggedLinks = Links->AddLinksFor(Tagged);
		TestEqual(TEXT("a block tagged NoClimb gets none"), TaggedLinks, 0);

		// A wall 50 cm thick and 3 m tall, like the corridor walls: nothing up there to stand on.
		AStaticMeshActor* Thin = SpawnMovableBlock(World, FVector2D(-2600.0f, 1300.0f), TEXT("Traversable"));
		Thin->SetActorScale3D(FVector(0.5f, 4.0f, 3.0f));
		Thin->SetActorLocation(FVector(-2600.0f, 1300.0f, 150.0f));
		TestEqual(TEXT("a wall 50 cm thick gets none"), Links->AddLinksFor(Thin), 0);

		// Under a 3 m ceiling, as in a roofed room: a 1.3 m console leaves room for a body on top
		// and is climbed; a 2 m shelf does not. The ceiling itself is never taken for the top.
		struct FRoofCase { const TCHAR* Name; float Y; float Height; bool bClimbable; };
		for (const FRoofCase& Case : { FRoofCase{ TEXT("a 1.3 m block under a 3 m ceiling"), -500.0f, 130.0f, true },
			FRoofCase{ TEXT("a 2 m block under a 3 m ceiling"), 500.0f, 200.0f, false } })
		{
			const float Side = FMath::Sign(Case.Y);
			AStaticMeshActor* Block = SpawnMovableBlock(World, FVector2D(-1600.0f, Case.Y), NAME_None);
			Block->SetActorScale3D(FVector(2.0f, 2.0f, Case.Height / 100.0f));
			Block->SetActorLocation(FVector(-1600.0f, Case.Y, Case.Height * 0.5f));
			AStaticMeshActor* Roof = SpawnMovableBlock(World, FVector2D(-1600.0f, Case.Y), NAME_None);
			Roof->SetActorScale3D(FVector(6.0f, 6.0f, 0.25f));
			Roof->SetActorLocation(FVector(-1600.0f, Case.Y, 312.5f));
			FEnemyClimbLedge Ledge;
			const FVector Start(-1600.0f, Case.Y - Side * 250.0f, 62.0f);
			const EEnemyClimbProbeResult Result = FEnemyClimbProbe::ProbeFrom(World, FEnemyClimbProbe::FBody(), Start,
				FVector(0.0f, Side, 0.0f), *GetDefault<UEnemyClimbSettings>(), 0.7f, nullptr, Ledge);
			const FString What = FString::Printf(TEXT("%s (%s, top %.0f)"), Case.Name, FEnemyClimbProbe::ToString(Result), Ledge.TopZ);
			if (Case.bClimbable)
			{
				TestTrue(What + TEXT(": climbed onto the block"), Result == EEnemyClimbProbeResult::Ok && FMath::IsNearlyEqual(Ledge.TopZ, Case.Height, 2.0f));
			}
			else
			{
				TestTrue(What + TEXT(": not climbed"), Result != EEnemyClimbProbeResult::Ok);
			}
		}
		TestTrue(FString::Printf(TEXT("a Movable block that blocks Traversable gets links (%d)"), TraversableLinks), TraversableLinks > 0);
		TestEqual(TEXT("a Movable block that does not gets none"), PlainLinks, 0);

		// The sample block itself, where the project has it: dropped in with its middle on the floor.
		if (UClass* SampleClass = LoadClass<AActor>(nullptr, TEXT("/Game/Levels/LevelPrototyping/LevelBlock_Traversable.LevelBlock_Traversable_C")))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Sample = World->SpawnActor<AActor>(SampleClass, FVector(-2200.0f, 600.0f, 0.0f), FRotator::ZeroRotator, Params);
			const int32 SampleLinks = Links->AddLinksFor(Sample);
			TestTrue(FString::Printf(TEXT("LevelBlock_Traversable, half sunk, gets links (%d)"), SampleLinks), SampleLinks > 0);
			for (const FEnemyClimbLedge& Ledge : Links->GetLedges())
			{
				if (Ledge.Wall.Get() == Sample)
				{
					AddInfo(FString::Printf(TEXT("LevelBlock_Traversable: %.0f cm up, foot %s"), Ledge.GetHeight(), *Ledge.BaseCenter.ToCompactString()));
					break;
				}
			}
		}
		else
		{
			AddInfo(TEXT("LevelBlock_Traversable is not in this project: that part is skipped"));
		}
		return true;
	}));

	// Once the navmesh round the new block is rebuilt, a path onto it goes up its wall.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return GoesUpWall(World, UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(-2200.0f, -800.0f, 62.0f), FVector(-2200.0f, -1300.0f, 312.0f)));
	}, 15.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(-2200.0f, -800.0f, 62.0f), FVector(-2200.0f, -1300.0f, 312.0f));
		TestTrue(TEXT("a path onto the Traversable block"), IsComplete(Path));
		TestTrue(TEXT("it goes up the block's wall"), GoesUpWall(World, Path));
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace EnemyClimbWorldTest
{
	struct FDownCase
	{
		const TCHAR* Name;
		FVector2D CrawlerAt;
		float TopZ;
		FVector PlayerAt;
		bool bExpectClimbDown;
	};

	struct FDownRun
	{
		TWeakObjectPtr<AEnemyCharacter> Crawler;
		float MinZ = TNumericLimits<float>::Max();
		bool bClimbedDown = false;
		bool bDropped = false;
	};

	void AddDown(FAutomationTestBase* Test, const FDownCase& Case)
	{
		TSharedRef<FDownRun> Run = MakeShared<FDownRun>();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Case, Run]()
		{
			UWorld* World = AutomationCommon::GetAnyGameWorld();
			APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
			if (!Player)
			{
				Test->AddError(TEXT("no player"));
				return true;
			}
			Player->SetActorLocation(Case.PlayerAt, false, nullptr, ETeleportType::TeleportPhysics);
			Run->Crawler = SpawnCrawler(World, Case.CrawlerAt, 0.0f, Case.TopZ);
			if (Run->Crawler.IsValid())
			{
				Run->Crawler->GetSensorComponent()->ReceiveAlert(Player, nullptr);
			}
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Run]()
		{
			const AEnemyCharacter* Crawler = Run->Crawler.Get();
			if (!Crawler)
			{
				return true;
			}
			Run->MinZ = FMath::Min(Run->MinZ, static_cast<float>(Crawler->GetActorLocation().Z));
			Run->bClimbedDown |= Crawler->GetClimbComponent()->IsClimbingDown();
			Run->bDropped |= Crawler->GetClimbComponent()->IsDropping();
			return Run->MinZ < 100.0f && !Crawler->GetClimbComponent()->IsClimbing();
		}, 12.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Case, Run]()
		{
			AEnemyCharacter* Crawler = Run->Crawler.Get();
			const FString Where = Crawler ? Crawler->GetActorLocation().ToCompactString() : TEXT("gone");
			Test->TestTrue(FString::Printf(TEXT("%s: got down to the floor (lowest %.0f, at %s)"), Case.Name, Run->MinZ, *Where), Run->MinZ < 100.0f);
			Test->TestEqual(FString::Printf(TEXT("%s: climbed down"), Case.Name), Run->bClimbedDown, Case.bExpectClimbDown);
			Test->TestEqual(FString::Printf(TEXT("%s: dropped off"), Case.Name), Run->bDropped, !Case.bExpectClimbDown);
			if (Crawler)
			{
				Crawler->Destroy();
			}
			return true;
		}));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbDownTest, "NeuralAI.Climb.Down",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbDownTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return IsComplete(UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1000.0f, -300.0f, 62.0f), FVector(1000.0f, -1200.0f, 462.0f)));
	}, 30.0f));

	AddDown(this, { TEXT("4 m platform"), FVector2D(1000.0f, -1200.0f), 400.0f, FVector(1000.0f, 300.0f, 150.0f), true });
	AddDown(this, { TEXT("6 m tower"), FVector2D(2400.0f, -1200.0f), 600.0f, FVector(2400.0f, -200.0f, 150.0f), true });
	AddDown(this, { TEXT("2 m block"), FVector2D(-1000.0f, 1200.0f), 200.0f, FVector(-1000.0f, 400.0f, 150.0f), false });

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbRangedTest, "NeuralAI.Climb.Ranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbRangedTest::RunTest(const FString& Parameters)
{
	using namespace EnemyClimbWorldTest;

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return IsComplete(UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(1000.0f, -300.0f, 62.0f), FVector(1000.0f, -1200.0f, 462.0f)));
	}, 30.0f));

	FChaseCase Hidden{ TEXT("shooter, player out of sight on the 4 m platform"), FVector(1000.0f, -1200.0f, 487.0f), FVector2D(1000.0f, 0.0f), true, 450.0f };
	Hidden.bRanged = true;
	AddChase(this, Hidden, 15.0f);
	FChaseCase InSight{ TEXT("shooter, player in sight on the 2 m block"), FVector(-1000.0f, 1200.0f, 287.0f), FVector2D(-1000.0f, 0.0f), false, 0.0f };
	InSight.bRanged = true;
	AddChase(this, InSight, 6.0f);

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

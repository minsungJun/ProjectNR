// Copyright Epic Games, Inc. All Rights Reserved.

// Gold and finite ammo in play. Opens Lvl_ClimbAutoTest in PIE and, with the player where the
// game put it: the gun in hand starts with a full magazine and StartingReserveMagazines in
// reserve; emptied, it cannot reload; an ammo box dropped on the player fills its reserve by a
// magazine and a reload then takes from it; with every gun's reserve full a box is left lying; gold
// dropped on the player goes to the run; and a killed enemy drops gold and an ammo box (their
// chances set to 1 for the test). Automation test ProjectNR.Loot.World (editor only).

#include "Characters/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "NRItemPickup.h"
#include "NRLootSettings.h"
#include "NRLootSubsystem.h"
#include "NRRunSubsystem.h"
#include "NRWeaponComponent.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Tests/AutomationEditorCommon.h"

namespace NRLootWorldTest
{
	struct FState
	{
		int32 MaxMagazine = 0;
		int32 MoneyBefore = 0;
		FVector EnemyAt = FVector::ZeroVector;
		float SavedMoneyChance = 1.0f;
		float SavedAmmoChance = 0.4f;
		float SavedDropChance = 0.3f;
	};

	UWorld* World()
	{
		return AutomationCommon::GetAnyGameWorld();
	}

	APawn* Player()
	{
		return UGameplayStatics::GetPlayerPawn(World(), 0);
	}

	UNRWeaponComponent* Weapon()
	{
		APawn* Pawn = Player();
		return Pawn ? Pawn->FindComponentByClass<UNRWeaponComponent>() : nullptr;
	}

	int32 CountPickups(ENRItemType Type, const FVector& Near, float Radius, int32* OutCount = nullptr)
	{
		int32 Found = 0;
		for (TActorIterator<ANRItemPickup> It(World()); It; ++It)
		{
			if (It->GetItemType() == Type && FVector::Dist2D(It->GetActorLocation(), Near) <= Radius)
			{
				++Found;
				if (OutCount)
				{
					*OutCount = It->GetCount();
				}
			}
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRLootWorldTest, "ProjectNR.Loot.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRLootWorldTest::RunTest(const FString& Parameters)
{
	using namespace NRLootWorldTest;
	TSharedRef<FState> State = MakeShared<FState>();

	AutomationOpenMap(TEXT("/Game/Levels/Lvl_ClimbAutoTest"));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World());
		FNavLocation Projected;
		return Navigation && !Navigation->IsNavigationBuildInProgress()
			&& Navigation->ProjectPointToNavigation(FVector(1000.0f, -1200.0f, 462.0f), Projected, FVector(50.0f, 50.0f, 150.0f));
	}, 30.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

	// Starting rounds, then emptied: no reload; then an ammo box.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UNRWeaponComponent* Gun = Weapon();
		if (!Gun || Gun->GetMaxAmmo() <= 0)
		{
			AddError(TEXT("the player has no gun in hand"));
			return true;
		}
		State->MaxMagazine = Gun->GetMaxAmmo();
		AddInfo(FString::Printf(TEXT("%s: %d in the magazine, %d / %d in reserve"), *Gun->GetCurrentWeaponName().ToString(),
			Gun->GetCurrentAmmo(), Gun->GetReserveAmmo(), Gun->GetMaxReserveAmmo()));
		TestEqual(TEXT("starts with a full magazine"), Gun->GetCurrentAmmo(), State->MaxMagazine);
		TestTrue(TEXT("starts with some rounds in reserve"), Gun->GetReserveAmmo() > 0);
		TestTrue(TEXT("the reserve has a cap above it"), Gun->GetMaxReserveAmmo() > Gun->GetReserveAmmo());

		Gun->SetCurrentAmmo(0, 0);
		TestTrue(TEXT("emptied: out of ammo"), Gun->IsOutOfAmmo());
		TestFalse(TEXT("emptied: no reload"), Gun->Reload());

		// An ammo box at the player's feet is picked up at once.
		World()->GetSubsystem<UNRLootSubsystem>()->SpawnPickup(ENRItemType::Ammo, 1, Player()->GetActorLocation());
		return true;
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		UNRWeaponComponent* Gun = Weapon();
		TestEqual(TEXT("an ammo box: a magazine's worth in reserve"), Gun->GetReserveAmmo(), State->MaxMagazine);
		TestTrue(TEXT("with a reserve it reloads"), Gun->Reload());
	}, 0.3f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		UNRWeaponComponent* Gun = Weapon();
		TestEqual(TEXT("after the reload: a full magazine"), Gun->GetCurrentAmmo(), State->MaxMagazine);
		TestEqual(TEXT("after the reload: the reserve spent"), Gun->GetReserveAmmo(), 0);

		// Every gun's reserve full (a box tops up all of them): a box is left lying.
		Gun->AddReserveMagazines(100.0f);
		World()->GetSubsystem<UNRLootSubsystem>()->SpawnPickup(ENRItemType::Ammo, 1, Player()->GetActorLocation());

		// Gold on the player's feet goes to the run.
		State->MoneyBefore = World()->GetSubsystem<UNRRunSubsystem>()->GetMoney();
		World()->GetSubsystem<UNRLootSubsystem>()->SpawnPickup(ENRItemType::Money, 7, Player()->GetActorLocation());
	}, 3.0f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		TestEqual(TEXT("a full reserve: the ammo box is left lying"), CountPickups(ENRItemType::Ammo, Player()->GetActorLocation(), 200.0f), 1);
		TestEqual(TEXT("gold picked up: the run's gold up by 7"), World()->GetSubsystem<UNRRunSubsystem>()->GetMoney(), State->MoneyBefore + 7);

		// A killed enemy drops gold and ammo (their chances made sure for the test, items off).
		UNRLootSettings* Settings = GetMutableDefault<UNRLootSettings>();
		State->SavedMoneyChance = Settings->EnemyMoneyChance;
		State->SavedAmmoChance = Settings->EnemyAmmoChance;
		State->SavedDropChance = Settings->EnemyDropChance;
		Settings->EnemyMoneyChance = 1.0f;
		Settings->EnemyAmmoChance = 1.0f;
		Settings->EnemyDropChance = 0.0f;

		State->EnemyAt = Player()->GetActorLocation() + FVector(0.0f, 900.0f, 0.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyCharacter* Enemy = World()->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(), State->EnemyAt, FRotator::ZeroRotator, Params);
		if (Enemy && Enemy->GetHealthComponent())
		{
			Enemy->GetHealthComponent()->ApplyDamage(100000.0f, Player(), FVector::ForwardVector);
		}
	}, 0.3f));

	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
	{
		int32 Gold = 0;
		TestEqual(TEXT("a killed enemy drops gold"), CountPickups(ENRItemType::Money, State->EnemyAt, 150.0f, &Gold), 1);
		const UNRLootSettings* Settings = GetDefault<UNRLootSettings>();
		TestTrue(FString::Printf(TEXT("as much as the settings say (%d)"), Gold), Gold >= Settings->EnemyMoneyMin && Gold <= Settings->EnemyMoneyMax);
		TestEqual(TEXT("a killed enemy drops an ammo box"), CountPickups(ENRItemType::Ammo, State->EnemyAt, 150.0f), 1);

		UNRLootSettings* Mutable = GetMutableDefault<UNRLootSettings>();
		Mutable->EnemyMoneyChance = State->SavedMoneyChance;
		Mutable->EnemyAmmoChance = State->SavedAmmoChance;
		Mutable->EnemyDropChance = State->SavedDropChance;
	}, 0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

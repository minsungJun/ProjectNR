// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NRRoom.h"
#include "NRRunSubsystem.generated.h"

class UHealthComponent;

UENUM(BlueprintType)
enum class ENRRunState : uint8
{
	/** The level has no rooms: a sandbox, not a run. */
	NoRun,
	InProgress,
	Won,
	Lost
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNROnRunStateChanged, ENRRunState, NewState);

/**
 * One run of the game: start, the rooms in between, and how it ended.
 *
 * A world subsystem, so a run is one play of a level and restarting is reloading it. A run
 * exists only in a level that has ANRRoom actors; sandbox levels see NoRun and nothing here
 * touches them.
 *
 * Owns the decisions the rest of the loop will hang off: the run is lost when the player
 * dies, and won when the room marked bFinalRoom is cleared (or every room, when none is
 * marked). It also keeps the running totals a result screen and, later, enemy adaptation
 * need - time, rooms cleared, kills, damage dealt.
 *
 * Polls the player's health rather than binding to it: the pawn is spawned and possessed
 * after the world begins play, and may be replaced; a poll needs no rebinding.
 */
UCLASS()
class PROJECTNR_API UNRRunSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintPure, Category = "Run")
	ENRRunState GetRunState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsRunActive() const { return State == ENRRunState::InProgress; }

	/** Seconds since the run ended; negative while it is still going. */
	UFUNCTION(BlueprintPure, Category = "Run")
	float GetTimeSinceEnded() const;

	/** Seconds since the run started, frozen once it ends. */
	UFUNCTION(BlueprintPure, Category = "Run")
	float GetElapsedTime() const;

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetRoomsCleared() const { return RoomsCleared; }

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetTotalRooms() const { return Rooms.Num(); }

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category = "Run")
	float GetDamageDealt() const { return DamageDealt; }

	/** Ends the run now. Ignored once it has ended. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void EndRun(bool bWon);

	/** Reloads the level, which starts a fresh run. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void RestartRun();

	UPROPERTY(BlueprintAssignable, Category = "Run")
	FNROnRunStateChanged OnRunStateChanged;

private:
	UFUNCTION()
	void HandleRoomStateChanged(ANRRoom* Room, ENRRoomState NewState);

	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	void SetState(ENRRunState NewState);

	ENRRunState State = ENRRunState::NoRun;
	double StartTime = 0.0;
	double EndTime = 0.0;
	int32 RoomsCleared = 0;
	int32 Kills = 0;
	float DamageDealt = 0.0f;

	TArray<TWeakObjectPtr<ANRRoom>> Rooms;
	FDelegateHandle AnyDamagedHandle;
};

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

	/** Gold picked up this run. Starts at 0 with every run. */
	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetMoney() const { return Money; }

	/** Gold picked up (or, negative, spent). Never goes below 0. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void AddMoney(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Run")
	float GetDamageDealt() const { return DamageDealt; }

	/** Ends the run now. Ignored once it has ended. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void EndRun(bool bWon);

	/** Reloads the level, which starts a fresh run. In a generated map, bSameSeed rebuilds the
	 *  same map; otherwise the map is generated anew. */
	UFUNCTION(BlueprintCallable, Category = "Run")
	void RestartRun(bool bSameSeed = false);

	/**
	 * Adds a room loaded after the world began play (a generated map). Safe to call twice.
	 * Rooms added before StartRun only count once the run starts.
	 */
	void RegisterRoom(ANRRoom* Room);

	/** Removes a room, e.g. when its level is unloaded. Safe for a room never added. */
	void UnregisterRoom(ANRRoom* Room);

	/** Starts the run over the registered rooms. Ignored while one is in progress. A generated
	 *  map calls this once every room is registered, so no half-registered map can be won. */
	void StartRun();

	/** Drops the run and every room: back to NoRun, e.g. before a map is rebuilt. */
	void ResetRun();

	/**
	 * Generated maps: the run decides when loadouts may change, in one place, from where the
	 * player is - inside a Start or Safe room with no fight going on anywhere. Also turns off
	 * the prototype "edit anywhere" bypass for the run.
	 */
	void SetManagesSafePhase(bool bManage);

	/** Seed of the generated map being played, for a same-seed restart. 0 for none. */
	void SetGeneratedSeed(int32 InSeed) { GeneratedSeed = InSeed; }
	int32 GetGeneratedSeed() const { return GeneratedSeed; }

	/** Level options the generated map was asked for besides its seed (e.g. GridEnv=Hybrid), kept
	 *  by every restart. Empty for the map's own defaults. */
	void SetGeneratedMapOptions(const FString& InOptions) { GeneratedMapOptions = InOptions; }

	/** Whether the run currently allows loadout changes (generated maps only). */
	bool IsSafePhase() const { return bSafePhase; }

	UPROPERTY(BlueprintAssignable, Category = "Run")
	FNROnRunStateChanged OnRunStateChanged;

private:
	UFUNCTION()
	void HandleRoomStateChanged(ANRRoom* Room, ENRRoomState NewState);

	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	void SetState(ENRRunState NewState);
	void UpdateSafePhase();

	ENRRunState State = ENRRunState::NoRun;
	double StartTime = 0.0;
	double EndTime = 0.0;
	int32 RoomsCleared = 0;
	int32 Kills = 0;
	int32 Money = 0;
	float DamageDealt = 0.0f;

	TArray<TWeakObjectPtr<ANRRoom>> Rooms;
	/** Rooms counted as cleared, so a room that reports twice is counted once. */
	TSet<TWeakObjectPtr<ANRRoom>> ClearedRooms;
	FDelegateHandle AnyDamagedHandle;

	bool bManagesSafePhase = false;
	bool bSafePhase = false;
	int32 GeneratedSeed = 0;
	FString GeneratedMapOptions;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRGridTypes.h"
#include "NRRoom.generated.h"

class ANRDoor;
class UBoxComponent;

UENUM(BlueprintType)
enum class ENRRoomState : uint8
{
	/** Player not here yet: its enemies wait, sensing nothing. */
	Dormant,
	/** Player inside: doors locked, enemies awake. */
	Active,
	/** Every enemy dead: doors open. */
	Cleared
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNROnRoomStateChanged, ANRRoom*, Room, ENRRoomState, NewState);

/**
 * One combat room of the basic loop: clear the room, its doors open, walk into the next.
 *
 * Place one per room and size the box to the floor area inside the walls. Enemies standing
 * in the box at the start of play belong to the room (plus any listed by hand); they are
 * held dormant until the room activates.
 *
 * The room activates when the player is EntryInset inside the box - past the doorway, so
 * the door closing behind them never closes on them - or as soon as one of its enemies is
 * hurt, so shooting in from the corridor starts the fight rather than farming a sleeping
 * room. Active, it closes every listed door; when the last enemy dies it opens them all.
 * A door between two rooms goes in both rooms' lists.
 *
 * A room with no enemies (reward, safe) clears the moment the player walks in, without
 * locking anything.
 *
 * In a level the map generator loads (bWaitForGenerator), the room does nothing at all until
 * the generator hands it its id, type, doors and enemies (InitializeGenerated) and the run
 * has started (OpenGate): no enemy search, no activation, no clearing. Its doors and enemies
 * then come from its own level instance only, never from a search of the world.
 */
UCLASS()
class PROJECTNR_API ANRRoom : public AActor
{
	GENERATED_BODY()

public:
	ANRRoom();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Room")
	ENRRoomState GetRoomState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Room")
	int32 GetRemainingEnemies() const;

	UFUNCTION(BlueprintPure, Category = "Room")
	int32 GetTotalEnemies() const { return RoomEnemies.Num(); }

	/** The room's own enemies, dead ones included. */
	const TArray<TObjectPtr<AActor>>& GetEnemies() const { return RoomEnemies; }

	/** Seconds since the room was cleared; negative while it is not. */
	UFUNCTION(BlueprintPure, Category = "Room")
	float GetTimeSinceCleared() const;

	/** Whether Location is inside the box, at least Inset from its sides (height ignored). */
	UFUNCTION(BlueprintPure, Category = "Room")
	bool ContainsLocation(const FVector& Location, float Inset = 0.0f) const;

	/** Starts the fight now, as if the player had walked in. Ignored while the gate is closed. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void Activate();

	/**
	 * Set up by the map generator. Doors are the ones on joined doorways, opened now so the
	 * map can be walked; enemies are the room's own, held dormant. The room leaves the loadout
	 * rule to the run (it no longer flips the safe phase itself). Stays inert until OpenGate.
	 */
	void InitializeGenerated(int32 InRoomId, ENRGridRoomType InRoomType, bool bInFinalRoom,
		const TArray<ANRDoor*>& InDoors, const TArray<AActor*>& InEnemies);

	/** Lets a generated room run: activation, clearing, doors. Called once the run has started. */
	void OpenGate();

	bool IsWaitingForGenerator() const { return bWaitForGenerator; }

	/** Name for logs: "Room 3 (Combat)" for a generated room, the actor name otherwise. Every
	 *  generated room actor has the same name inside its own level, so the name alone is useless. */
	FString GetLabel() const;
	bool IsGenerated() const { return GeneratedRoomId != INDEX_NONE; }
	int32 GetGeneratedRoomId() const { return GeneratedRoomId; }
	ENRGridRoomType GetRoomType() const { return RoomType; }

	/** Test helper: kills every living enemy of this room. */
	UFUNCTION(BlueprintCallable, Category = "Room|Debug")
	void KillAllEnemies();

	/** The room whose box contains Location, or null. */
	static ANRRoom* FindRoomAt(const UWorld* World, const FVector& Location);

	static const TArray<TWeakObjectPtr<ANRRoom>>& GetAllRooms() { return AllRooms; }

	UPROPERTY(BlueprintAssignable, Category = "Room")
	FNROnRoomStateChanged OnRoomStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The room's floor area. Only its shape is used; it has no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TObjectPtr<UBoxComponent> Bounds;

	/** Doors this room locks when it activates and opens when it is cleared. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room")
	TArray<TObjectPtr<ANRDoor>> Doors;

	/** Enemies that belong to the room besides those found inside the box. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room")
	TArray<TObjectPtr<AActor>> ExtraEnemies;

public:
	/**
	 * Clearing this room wins the run (the boss room). With no room marked, the run is won
	 * when every room is cleared. @see UNRRunSubsystem
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room")
	bool bFinalRoom = false;

protected:
	/** Take every enemy standing inside the box at the start of play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bCollectEnemiesInBounds = true;

	/** How far past the box's edge the player must be before the room activates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room", meta = (ClampMin = "0.0"))
	float EntryInset = 150.0f;

	/** Set in room templates for the map generator: stay inert until it sets the room up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Generated")
	bool bWaitForGenerator = false;

private:
	void CollectEnemies();
	void SetEnemiesDormant(bool bDormant) const;
	bool IsAnyEnemyHurt() const;
	void SetState(ENRRoomState NewState);

	/** Tells the player's part component whether this is a safe moment to change loadouts. */
	void UpdateSafePhase(bool bSafe) const;

	ENRRoomState State = ENRRoomState::Dormant;
	float ClearedTime = -1.0f;

	/** False while a generated room waits for the generator and the run. */
	bool bGateOpen = true;
	int32 GeneratedRoomId = INDEX_NONE;
	ENRGridRoomType RoomType = ENRGridRoomType::Combat;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> RoomEnemies;

	static TArray<TWeakObjectPtr<ANRRoom>> AllRooms;
};

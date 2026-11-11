// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRAdaptationConfig.h"
#include "Subsystems/WorldSubsystem.h"
#include "NRAdaptationSubsystem.generated.h"

class AEnemyCharacter;
class ANRRoom;
class UHealthComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogNRAdapt, Log, All);

/**
 * Enemy adaptation over a run: watches how the player fights in a room and changes the enemies
 * of the rooms after it.
 *
 *  - Collects: damage and kills per player weapon (from the hit's EAdaptationWeapon source, via
 *    UHealthComponent::OnAnyDamaged), and magazines fired per weapon (UNRWeaponComponent).
 *  - Learns: when a fighting room (Combat, Elite, Boss) clears, hands its record to
 *    FNRAdaptationModel, and the stage goes up by one.
 *  - Applies: as an enemy joins a room (placed ones on activation, spawned ones as they come),
 *    its brain gets its kind's adaptation profile and its body the stat modifiers (weapon
 *    resistance, speed, regeneration, attack rate; health and damage from level and stage).
 *    The spawn subsystem reads GetRangedBias for the mix of close-in and ranged enemies.
 *
 * A world subsystem, so it lives exactly as long as a run: restarting reloads the level.
 * Numbers come from UNREncounterData::Adaptation, or UNRAdaptationConfig's defaults.
 * NR.Adapt.Status / NR.Adapt.Learn / NR.Adapt.KillRoom / NR.Adapt.Reset. @see FNRAdaptationModel
 */
UCLASS()
class PROJECTNR_API UNRAdaptationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** A room has woken: starts its record and adapts the enemies it already holds. */
	void HandleRoomActivated(ANRRoom* Room);

	/** A room has cleared: a fighting room's record is learned from. */
	void HandleRoomCleared(ANRRoom* Room);

	/** Gives Enemy the current adaptation and modifiers. Safe to call more than once. */
	void ApplyToEnemy(AActor* Enemy);

	/** Feeds a made-up room straight to the model (console and tests). */
	void LearnFromRecord(const FNRRoomCombatRecord& Record, bool bCountsAsStage = true);

	/** Forgets everything learned this run. */
	void ResetAdaptation();

	float GetRangedBias() const;

	const FNRAdaptationModel& GetModel() const { return Model; }
	const FNRRoomCombatRecord& GetCurrentRecord() const { return Record; }
	const UNRAdaptationConfig& GetConfig() const;

	static ENREnemyFamily GetFamily(const AActor* Enemy);

	void LogStatus() const;

private:
	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	/** Player's magazines fired per weapon so far, to take the room's share as a difference. */
	void SnapshotMagazines(float Out[NRAdaptWeaponCount]) const;
	int32 GetPlayerLevel() const;

	FNRAdaptationModel Model;
	FNRRoomCombatRecord Record;
	float MagazinesAtStart[NRAdaptWeaponCount] = {};
	bool bRecording = false;
	FDelegateHandle AnyDamagedHandle;
};

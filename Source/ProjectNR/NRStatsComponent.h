// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NRStatsComponent.generated.h"

class UHealthComponent;

/** The player stats points can be spent on. Order is the order on the stat screen. */
UENUM(BlueprintType)
enum class ENRPlayerStat : uint8
{
	MaxHealth,
	KillHeal,
	Armor,
	MoveSpeed,
	MaxStamina,
	Count UMETA(Hidden)
};

constexpr int32 NRPlayerStatCount = static_cast<int32>(ENRPlayerStat::Count);

/** One stat's numbers and how it is shown. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRStatDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat")
	FText Description;

	/** Value with no points in it. A percentage stat stores 100 for 100%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat")
	float BaseValue = 0.0f;

	/** Added per point invested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat")
	float PerPoint = 1.0f;

	/** Decimal places shown on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat", meta = (ClampMin = "0", ClampMax = "3"))
	int32 Decimals = 0;

	/** Shown with a % sign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stat")
	bool bPercent = false;
};

DECLARE_MULTICAST_DELEGATE(FOnNRStatsChanged);

/**
 * The player's growth: experience from kills, levels, stat points, and what the points buy.
 *
 * Every enemy kill gives XPPerKill. Each level needs XPForFirstLevel plus XPPerLevel more for
 * every level after the first, and grants one stat point. Points are spent through
 * SpendPoints (the stat screen stages them and applies in one go), and the stats are pushed
 * into the components that own them:
 *  - Max health and armor  -> UHealthComponent
 *  - Max stamina           -> UNRStaminaComponent
 *  - Move speed            -> the walking movement mode's walk/run/sprint/crouch speeds
 *  - Kill heal             -> applied here on every kill
 *
 * Kills are heard through UHealthComponent::OnAnyDamaged, like the run and loot subsystems.
 */
UCLASS(ClassGroup = (ProjectNR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRStatsComponent();

	/** Fired after experience, level, points or stats change. */
	FOnNRStatsChanged OnStatsChanged;

	const FNRStatDefinition& GetDefinition(ENRPlayerStat Stat) const;

	UFUNCTION(BlueprintPure, Category = "Stats")
	int32 GetInvestedPoints(ENRPlayerStat Stat) const;

	/** The stat's value with its current points. */
	UFUNCTION(BlueprintPure, Category = "Stats")
	float GetStatValue(ENRPlayerStat Stat) const { return GetStatValueWithPoints(Stat, GetInvestedPoints(Stat)); }

	/** The stat's value as it would be with Points invested in it. */
	float GetStatValueWithPoints(ENRPlayerStat Stat, int32 Points) const;

	/** Value as text, with the stat's decimals and % sign. */
	FText FormatValue(ENRPlayerStat Stat, float Value) const;

	/**
	 * Invests Points[i] more in stat i. All or nothing: false, with nothing spent, when a count
	 * is negative or they add up to more than the unspent points.
	 */
	bool SpendPoints(const TArray<int32>& Points);

	UFUNCTION(BlueprintPure, Category = "Stats")
	int32 GetLevel() const { return Level; }

	UFUNCTION(BlueprintPure, Category = "Stats")
	int32 GetExperience() const { return Experience; }

	/** Experience the current level needs before the next. */
	UFUNCTION(BlueprintPure, Category = "Stats")
	int32 GetExperienceToNextLevel() const;

	UFUNCTION(BlueprintPure, Category = "Stats")
	int32 GetUnspentPoints() const { return UnspentPoints; }

	UFUNCTION(BlueprintCallable, Category = "Stats")
	void AddExperience(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Stats")
	void AddStatPoints(int32 Amount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Indexed by ENRPlayerStat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "Stats")
	TArray<FNRStatDefinition> Definitions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats|Experience", meta = (ClampMin = "0"))
	int32 XPPerKill = 20;

	/** Experience from level 1 to 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats|Experience", meta = (ClampMin = "1"))
	int32 XPForFirstLevel = 100;

	/** How much more each level after that needs than the one before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats|Experience", meta = (ClampMin = "0"))
	int32 XPPerLevel = 50;

	/** Points granted on each level up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats|Experience", meta = (ClampMin = "0"))
	int32 PointsPerLevel = 1;

private:
	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	/** Pushes every stat into the component that owns it. */
	void ApplyStats();
	void ApplyMoveSpeed();

	int32 Level = 1;
	int32 Experience = 0;
	int32 UnspentPoints = 0;

	/** Indexed by ENRPlayerStat. */
	TArray<int32> Invested;

	/** The walking mode's speeds as the sample authored them, which move speed scales. */
	struct FSpeedEntry
	{
		TWeakObjectPtr<UObject> Mode;
		FNumericProperty* Property = nullptr;
		double Base = 0.0;
	};
	TArray<FSpeedEntry> SpeedEntries;

	FDelegateHandle AnyDamagedHandle;
};

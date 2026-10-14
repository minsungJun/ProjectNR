// Copyright Epic Games, Inc. All Rights Reserved.

#include "NREncounterData.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/RangedEnemyCharacter.h"

UNREncounterData::UNREncounterData()
{
	MeleeClass = AEnemyCharacter::StaticClass();
	RangedClass = ARangedEnemyCharacter::StaticClass();

	Combat.MinUseRatio = 0.6f;
	Combat.MaxUseRatio = 0.8f;
	Combat.MaxRanged = 2;

	Elite.MinUseRatio = 0.7f;
	Elite.MaxUseRatio = 0.9f;
	Elite.MaxRanged = 3;

	Boss.MinUseRatio = 0.7f;
	Boss.MaxUseRatio = 0.9f;
	Boss.MaxRanged = 4;
}

const FNREncounterRule& UNREncounterData::GetRule(ENRGridRoomType RoomType) const
{
	switch (RoomType)
	{
	case ENRGridRoomType::Elite: return Elite;
	case ENRGridRoomType::Boss: return Boss;
	default: return Combat;
	}
}

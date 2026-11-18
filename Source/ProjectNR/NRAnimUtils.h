// Copyright Epic Games, Inc. All Rights Reserved.

// Montage questions shared by the player's systems. A montage in the UpperBody slot (the reload)
// moves the arms over whatever the legs do; it does not own the body the way a vault, a roll or a
// takedown does, so the checks that ask "is the body busy" must not count it.

#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

namespace NRAnim
{
	/** The slot the player's animation blueprint layers onto the upper body only. */
	inline const FName UpperBodySlot(TEXT("UpperBody"));

	/** True when every track of the montage is in the UpperBody slot. */
	inline bool IsUpperBodyOnly(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.Num() == 0)
		{
			return false;
		}
		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			if (Track.SlotName != UpperBodySlot)
			{
				return false;
			}
		}
		return true;
	}

	/** Whether a montage that owns the whole body (traversal, roll, takedown, hit reaction) is playing. */
	inline bool IsBodyMontagePlaying(const UAnimInstance* AnimInstance)
	{
		if (!AnimInstance)
		{
			return false;
		}
		for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
		{
			if (Instance && Instance->IsActive() && Instance->Montage && !IsUpperBodyOnly(Instance->Montage))
			{
				return true;
			}
		}
		return false;
	}
}

// Traversal (parkour) diagnostics.
//
// The traversal itself is the sample's Blueprint (AC_TraversalLogic.TryTraversalAction), which
// only prints anything once a montage has been chosen. These helpers walk the same checks in
// native code and log each one, so a press that does nothing says where it stopped.
//
//   NR.Traversal.Log 1   log every traverse/jump press, then whether a montage started
//                        (ANRPlayerPawn's keys, and IA_Jump on the sample pawn BP_NRPlayer_V2)
//   NR.Traversal.Probe   log the checks from where the player stands now
//   NR.Traversal.Try     the same, then press traverse for the player (no keyboard needed)

#pragma once

#include "CoreMinimal.h"

class APawn;

namespace NRTraversalDebug
{
	/** NR.Traversal.Log is on. */
	bool IsLogging();

	/** Logs each step of the traversal check for Pawn. Source names what asked (a key, a command). */
	void LogProbe(APawn* Pawn, const TCHAR* Source);

	/** Logs whether a traversal montage is playing now, i.e. whether the press took. */
	void LogOutcome(APawn* Pawn);
}

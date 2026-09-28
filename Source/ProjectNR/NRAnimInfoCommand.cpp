// Copyright Epic Games, Inc. All Rights Reserved.

// NR.AnimInfo <asset path> [<asset path> ...]
//
// Prints what an animation actually contains, measured from its bone tracks rather than
// inferred from a thumbnail: how far the root and pelvis travel, the notifies and their
// timing, curves, and sync markers.
//
// It exists because deciding "does this clip carry root motion" from a thumbnail went wrong
// once already - a root bone with a constant offset changes the thumbnail under root lock
// without moving at all. The root track's first and last keys settle it.

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRAnimInfo, Log, All);

#if WITH_EDITOR
namespace
{
	/** Travel of one bone track: start-to-end, farthest point from the start, yaw change. */
	void ReportTrack(const IAnimationDataModel& Model, FName Bone, float Length)
	{
		TArray<FTransform> Keys;
		Model.GetBoneTrackTransforms(Bone, Keys);
		if (Keys.Num() == 0)
		{
			UE_LOG(LogNRAnimInfo, Log, TEXT("  %s: no track (stays at reference pose)"), *Bone.ToString());
			return;
		}

		const FVector Start = Keys[0].GetTranslation();
		const FVector End = Keys.Last().GetTranslation();
		float Farthest = 0.0f;
		for (const FTransform& Key : Keys)
		{
			Farthest = FMath::Max(Farthest, FVector::Dist(Start, Key.GetTranslation()));
		}

		const float YawDelta = FRotator::NormalizeAxis(
			Keys.Last().Rotator().Yaw - Keys[0].Rotator().Yaw);

		UE_LOG(LogNRAnimInfo, Log,
			TEXT("  %s: start %s -> end %s | net %.1fcm (XY %.1f, Z %.1f) | farthest %.1fcm | yaw %+.1f deg | %d keys"),
			*Bone.ToString(), *Start.ToCompactString(), *End.ToCompactString(),
			FVector::Dist(Start, End), FVector::Dist2D(Start, End), End.Z - Start.Z,
			Farthest, YawDelta, Keys.Num());

		// Quarter samples show where in the clip the travel happens - a lunge at the start
		// and a step at the end look the same in the net figure.
		FString Samples;
		for (int32 Q = 0; Q <= 4; ++Q)
		{
			const int32 Index = FMath::Clamp(FMath::RoundToInt((Keys.Num() - 1) * Q / 4.0f), 0, Keys.Num() - 1);
			Samples += FString::Printf(TEXT(" %.2fs:%.0f"), Length * Q / 4.0f,
				FVector::Dist2D(Start, Keys[Index].GetTranslation()));
		}
		UE_LOG(LogNRAnimInfo, Log, TEXT("    XY distance from start at quarters:%s"), *Samples);
	}

	/** Local transform of Bone at Frame: its track key if it has one, otherwise the reference pose. */
	FTransform LocalAt(const IAnimationDataModel& Model, const FReferenceSkeleton& RefSkel, int32 BoneIndex, int32 Frame)
	{
		TArray<FTransform> Keys;
		Model.GetBoneTrackTransforms(RefSkel.GetBoneName(BoneIndex), Keys);
		if (Keys.Num() == 0)
		{
			return RefSkel.GetRefBonePose()[BoneIndex];
		}
		return Keys[FMath::Clamp(Frame, 0, Keys.Num() - 1)];
	}

	/** Component-space position of Bone for every key, composed from root down its chain. */
	bool ComponentPath(const UAnimSequence& Sequence, FName Bone, TArray<FVector>& OutPath)
	{
		const USkeleton* Skeleton = Sequence.GetSkeleton();
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		if (!Skeleton || !Model)
		{
			return false;
		}

		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		const int32 BoneIndex = RefSkel.FindBoneIndex(Bone);
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<int32> Chain;
		for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = RefSkel.GetParentIndex(Index))
		{
			Chain.Insert(Index, 0);
		}

		const int32 NumKeys = Model->GetNumberOfKeys();
		OutPath.SetNum(NumKeys);
		for (int32 Frame = 0; Frame < NumKeys; ++Frame)
		{
			FTransform Accumulated = FTransform::Identity;
			for (const int32 Index : Chain)
			{
				Accumulated = LocalAt(*Model, RefSkel, Index, Frame) * Accumulated;
			}
			OutPath[Frame] = Accumulated.GetLocation();
		}
		return true;
	}

	/**
	 * Where a swing happens: the frame the hand moves fastest, and the span around it where it
	 * moves at over half that speed. With no notifies to say when an attack connects, this is
	 * the least arbitrary hit window there is.
	 */
	void ReportSwing(const UAnimSequence& Sequence, FName Bone)
	{
		TArray<FVector> Path;
		if (!ComponentPath(Sequence, Bone, Path) || Path.Num() < 2)
		{
			return;
		}

		const float Length = Sequence.GetPlayLength();
		const float Step = Length / (Path.Num() - 1);

		TArray<float> Speeds;
		Speeds.SetNumZeroed(Path.Num());
		int32 PeakFrame = 0;
		for (int32 Frame = 1; Frame < Path.Num(); ++Frame)
		{
			Speeds[Frame] = FVector::Dist(Path[Frame], Path[Frame - 1]) / Step;
			if (Speeds[Frame] > Speeds[PeakFrame])
			{
				PeakFrame = Frame;
			}
		}

		const float Threshold = Speeds[PeakFrame] * 0.5f;
		int32 First = PeakFrame;
		int32 Last = PeakFrame;
		while (First > 1 && Speeds[First - 1] >= Threshold) { --First; }
		while (Last < Speeds.Num() - 1 && Speeds[Last + 1] >= Threshold) { ++Last; }

		UE_LOG(LogNRAnimInfo, Log, TEXT("  %s swing: peak %.0fcm/s at %.2fs | over 50%% from %.2fs to %.2fs"),
			*Bone.ToString(), Speeds[PeakFrame], PeakFrame * Step, First * Step, Last * Step);
	}

	/**
	 * The time in From whose pose is closest to To's first frame - where From can hand over to
	 * To without the body jumping. Compares every bone both clips animate, by local rotation
	 * angle, plus pelvis height.
	 */
	void ReportBestChain(const UAnimSequence& From, const UAnimSequence& To)
	{
		const IAnimationDataModel* FromModel = From.GetDataModel();
		const IAnimationDataModel* ToModel = To.GetDataModel();
		if (!FromModel || !ToModel)
		{
			return;
		}

		TArray<FName> FromTracks;
		TArray<FName> ToTracks;
		FromModel->GetBoneTrackNames(FromTracks);
		ToModel->GetBoneTrackNames(ToTracks);

		TArray<TArray<FTransform>> FromKeys;
		TArray<FTransform> ToFirst;
		TArray<FName> Common;
		for (const FName& Track : FromTracks)
		{
			if (Track == TEXT("root") || !ToTracks.Contains(Track))
			{
				continue;
			}
			TArray<FTransform> A;
			TArray<FTransform> B;
			FromModel->GetBoneTrackTransforms(Track, A);
			ToModel->GetBoneTrackTransforms(Track, B);
			if (A.Num() > 0 && B.Num() > 0)
			{
				Common.Add(Track);
				FromKeys.Add(MoveTemp(A));
				ToFirst.Add(B[0]);
			}
		}

		const int32 NumKeys = FromModel->GetNumberOfKeys();
		const float Step = From.GetPlayLength() / FMath::Max(NumKeys - 1, 1);

		TArray<TPair<float, int32>> Costs;
		for (int32 Frame = 0; Frame < NumKeys; ++Frame)
		{
			float Cost = 0.0f;
			for (int32 Index = 0; Index < Common.Num(); ++Index)
			{
				const FTransform& Key = FromKeys[Index][FMath::Min(Frame, FromKeys[Index].Num() - 1)];
				Cost += FMath::RadiansToDegrees(Key.GetRotation().AngularDistance(ToFirst[Index].GetRotation()));
				if (Common[Index] == TEXT("pelvis"))
				{
					Cost += FVector::Dist(Key.GetTranslation(), ToFirst[Index].GetTranslation());
				}
			}
			Costs.Add({ Cost / FMath::Max(Common.Num(), 1), Frame });
		}

		// The whole curve, not just the minimum: an early handover with a little mismatch that
		// a blend can hide is often a better trade than waiting for a perfect one.
		FString Curve;
		const int32 Every = FMath::Max(1, FMath::RoundToInt(0.05f / Step));
		for (int32 Frame = 0; Frame < Costs.Num(); Frame += Every)
		{
			Curve += FString::Printf(TEXT(" %.2f:%.1f"), Frame * Step, Costs[Frame].Key);
		}
		UE_LOG(LogNRAnimInfo, Log, TEXT("  mismatch by time (s:deg):%s"), *Curve);

		Costs.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });

		FString Best;
		for (int32 Index = 0; Index < FMath::Min(5, Costs.Num()); ++Index)
		{
			Best += FString::Printf(TEXT(" %.2fs(%.1f)"), Costs[Index].Value * Step, Costs[Index].Key);
		}
		UE_LOG(LogNRAnimInfo, Log, TEXT("== chain %s -> %s | %d shared tracks | best handover (avg deg/bone):%s | end frame %.1f"),
			*From.GetName(), *To.GetName(), Common.Num(), *Best,
			Costs.FindByPredicate([NumKeys](const TPair<float, int32>& C) { return C.Value == NumKeys - 1; })->Key);
	}

	void ReportSequence(const FString& Path)
	{
		const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Sequence)
		{
			UE_LOG(LogNRAnimInfo, Warning, TEXT("NR.AnimInfo: %s is not an AnimSequence"), *Path);
			return;
		}

		const IAnimationDataModel* Model = Sequence->GetDataModel();
		if (!Model)
		{
			UE_LOG(LogNRAnimInfo, Warning, TEXT("NR.AnimInfo: %s has no data model"), *Path);
			return;
		}

		const float Length = Sequence->GetPlayLength();
		UE_LOG(LogNRAnimInfo, Log, TEXT("== %s | %.3fs, %d frames | root motion %s | skeleton %s"),
			*Sequence->GetName(), Length, Model->GetNumberOfFrames(),
			Sequence->bEnableRootMotion ? TEXT("ON") : TEXT("off"),
			*GetNameSafe(Sequence->GetSkeleton()));

		ReportTrack(*Model, TEXT("root"), Length);
		ReportTrack(*Model, TEXT("pelvis"), Length);
		ReportSwing(*Sequence, TEXT("hand_r"));
		ReportSwing(*Sequence, TEXT("hand_l"));

		UE_LOG(LogNRAnimInfo, Log, TEXT("  notifies: %d"), Sequence->Notifies.Num());
		for (const FAnimNotifyEvent& Event : Sequence->Notifies)
		{
			const UObject* Class = Event.NotifyStateClass ? static_cast<const UObject*>(Event.NotifyStateClass) : Event.Notify.Get();
			UE_LOG(LogNRAnimInfo, Log, TEXT("    %.3fs +%.3fs  %s  (%s)"),
				Event.GetTriggerTime(), Event.GetDuration(), *Event.NotifyName.ToString(),
				Class ? *Class->GetClass()->GetName() : TEXT("named"));
		}

		const TArray<FFloatCurve>& Curves = Model->GetFloatCurves();
		FString CurveNames;
		for (const FFloatCurve& Curve : Curves)
		{
			CurveNames += TEXT(" ") + Curve.GetName().ToString();
		}
		UE_LOG(LogNRAnimInfo, Log, TEXT("  curves: %d%s"), Curves.Num(), *CurveNames);

		FString Markers;
		for (const FAnimSyncMarker& Marker : Sequence->AuthoredSyncMarkers)
		{
			Markers += FString::Printf(TEXT(" %s@%.2f"), *Marker.MarkerName.ToString(), Marker.Time);
		}
		UE_LOG(LogNRAnimInfo, Log, TEXT("  sync markers: %d%s"), Sequence->AuthoredSyncMarkers.Num(), *Markers);
	}
}
#endif

static FAutoConsoleCommand GNRAnimInfoCommand(
	TEXT("NR.AnimInfo"),
	TEXT("NR.AnimInfo <AnimSequence path> [...]: log root/pelvis travel, notifies, curves and sync markers."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
#if WITH_EDITOR
		for (const FString& Path : Args)
		{
			ReportSequence(Path);
		}
#else
		UE_LOG(LogNRAnimInfo, Warning, TEXT("NR.AnimInfo needs editor data and is unavailable in this build."));
#endif
	}));

// NR.AnimChain <from> <to>: when in <from> the pose best matches <to>'s first frame.
static FAutoConsoleCommand GNRAnimChainCommand(
	TEXT("NR.AnimChain"),
	TEXT("NR.AnimChain <from AnimSequence> <to AnimSequence>: find the time in <from> closest to <to>'s first pose."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
#if WITH_EDITOR
		if (Args.Num() < 2)
		{
			UE_LOG(LogNRAnimInfo, Warning, TEXT("NR.AnimChain needs two AnimSequence paths"));
			return;
		}
		const UAnimSequence* From = LoadObject<UAnimSequence>(nullptr, *Args[0]);
		const UAnimSequence* To = LoadObject<UAnimSequence>(nullptr, *Args[1]);
		if (From && To)
		{
			ReportBestChain(*From, *To);
		}
#endif
	}));

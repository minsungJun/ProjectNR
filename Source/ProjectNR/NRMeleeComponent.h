// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LayeredMoveBase.h"
#include "NRWeaponPartTypes.h"
#include "NRMeleeComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UNRMeleeComponent;
struct FInputActionValue;

/**
 * One swing. The clips carry no notifies, so every timing here is seconds into the clip,
 * measured with NR.AnimInfo (hand speed) and NR.AnimChain (handover pose) rather than guessed.
 */
USTRUCT(BlueprintType)
struct FNRMeleeAttack
{
	GENERATED_BODY()

	/** Root motion must be enabled on it: the lunge is what carries the attack forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TObjectPtr<UAnimSequenceBase> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	/** Span, in seconds into the clip, during which the swing can hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.0"))
	float HitStart = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.0"))
	float HitEnd = 0.4f;

	/** From this time on, a press is remembered and chains into the next attack. Earlier
	 *  presses are ignored so mashing does not skip straight through the combo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.0"))
	float ComboInputStart = 0.2f;

	/** When a remembered press starts the next attack. The next clip's first pose should
	 *  match this moment's; NR.AnimChain finds it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.0"))
	float ChainTime = 0.6f;

	/**
	 * Cuts the recovery short: at this time, unless a follow-up is already queued, the attack
	 * ends and the body is handed back with the montage's blend-out. 0 plays the whole clip.
	 * A queued follow-up still waits for ChainTime, so combos are unaffected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.0"))
	float RecoveryEndTime = 0.0f;

	/** Reach of the hit test from the character's centre, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Hit", meta = (ClampMin = "0.0"))
	float Range = 170.0f;

	/** Half-angle of the frontal arc that counts as in front, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Hit", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HalfAngle = 70.0f;
};

/**
 * An attack's lunge, applied to the capsule from the montage's root motion.
 *
 * Mover already has this - UPlayMoverMontageCallbackProxy queues FLayeredMove_AnimRootMotion -
 * but that move is fixed to EMoveMixMode::OverrideAll, and two things follow from that:
 *  - nothing can add to it: Mover's mixer drops additive moves under an OverrideAll one, so the
 *    attack cannot be steered;
 *  - it takes over vertical velocity, so a lunge at a ledge glided out over the drop.
 * This move does the same root motion extraction under OverrideAllExceptVerticalVelocity and
 * adds a fixed turn at the start (A/D held when the attack began), while gravity and ground
 * handling stay with the movement mode.
 *
 * It reports Mover_AnimRootMotion_Montage as a tag, so the existing cancel path
 * (CancelFeaturesWithTag) finds it exactly as it found the proxy's move.
 */
USTRUCT()
struct FNRMeleeRootMotionMove : public FLayeredMoveBase
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage;

	/** Montage position, in seconds, the move started from. */
	UPROPERTY()
	float StartPosition = 0.0f;

	UPROPERTY()
	float PlayRate = 1.0f;

	/** Turn applied over the first TurnSeconds of the move, degrees (positive = right). */
	UPROPERTY()
	float TurnDegrees = 0.0f;

	UPROPERTY()
	float TurnSeconds = 0.1f;

	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;
	virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const override;
	virtual FLayeredMoveBase* Clone() const override;
	virtual void NetSerialize(FArchive& Ar) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits<FNRMeleeRootMotionMove> : public TStructOpsTypeTraitsBase2<FNRMeleeRootMotionMove>
{
	enum
	{
		WithCopy = true
	};
};

/**
 * Melee combat for weapon slot 3: a light combo on the left mouse button and a heavy attack
 * on the right.
 *
 * Input: the sample maps the mouse buttons to Takedown and Aim in IMC_Sandbox at priority 0.
 * While melee is equipped this component adds its own mapping context at a higher priority
 * with both buttons, built at runtime so there are no input assets to keep in sync. Enhanced
 * Input lets the higher context consume a key, so aiming and takedowns stop responding to the
 * mouse until the context is removed again.
 *
 * Movement: the attack clips carry real root motion (roughly 0.4 to 1.3 m of lunge), and feet
 * are planted through them, so the capsule follows the animation - through FNRMeleeRootMotionMove
 * rather than Mover's own montage path, which is what allows steering and leaves vertical
 * velocity (ledges, slopes) to the movement mode.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRMeleeComponent();

	/** Called by UNRWeaponComponent when the equipped slot changes kind. */
	void SetMeleeEquipped(bool bEquipped);

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsMeleeEquipped() const { return bMeleeEquipped; }

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsAttacking() const { return CurrentMontage != nullptr; }


	/** Starts the combo, or queues its next hit if one is already swinging. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void LightAttack();

	UFUNCTION(BlueprintCallable, Category = "Melee")
	void HeavyAttack();

	/**
	 * Abandons the current attack so something else can take the body - a roll, for one.
	 * Stops the montage and, just as important, cancels the root motion layered move that
	 * was carrying the lunge: left running it overrides everything, including the roll.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void CancelAttack();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	/** Light attacks in order. Pressing again inside an attack's combo window chains to the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TArray<FNRMeleeAttack> LightCombo;

	/** After the last light attack, a press in its combo window goes back to the first, so
	 *  holding the rhythm cycles 1, 2, 1, 2... The last attack's ChainTime is then its
	 *  handover into the first; NR.AnimChain <last> <first> finds a pose that fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	bool bLoopLightCombo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	FNRMeleeAttack Heavy;

	/** Montage slot. DefaultSlot owns the whole body in the sample ABP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	FName SlotName = TEXT("DefaultSlot");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation", meta = (ClampMin = "0.0"))
	float BlendInTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation", meta = (ClampMin = "0.0"))
	float BlendOutTime = 0.25f;

	/** Priority of the melee mapping context. Must be above IMC_Sandbox's 0 to take the mouse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Input")
	int32 InputPriority = 10;

	/**
	 * Holding A or D with an attack turns it this far towards that side, once - not a continuous
	 * steer. Counts if the key was held at the click or when the attack starts: a chained hit
	 * starts at its predecessor's ChainTime, well after the click that queued it, so the
	 * click's input is remembered for it. 0 disables it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Aim", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AttackTurnAngle = 15.0f;

	/** How long the turn takes, seconds from the attack's start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Aim", meta = (ClampMin = "0.01"))
	float AttackTurnTime = 0.1f;

	/** Sideways input (relative to facing, 0..1) needed to count as A or D being held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SideInputThreshold = 0.3f;

	/** Draws the hit arc while a hit window is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Debug")
	bool bDrawDebugHits = false;

private:
	void BuildInput();
	void BindInput();
	void UpdateMappingContext();

	void OnLightInput(const FInputActionValue& Value);
	void OnHeavyInput(const FInputActionValue& Value);

	bool CanStartAttack() const;
	bool StartAttack(const FNRMeleeAttack& Attack, UAnimMontage* Montage, int32 ComboIndex, float PressedSide = 0.0f);

	/** -1 for A, +1 for D, 0 for neither, from the input right now relative to the facing. */
	float ReadSideInput() const;
	void TestHits(const FNRMeleeAttack& Attack);
	void EndAttack();

	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	const FNRMeleeAttack* GetCurrentAttack() const;

	/** Light attack that follows the current one, or INDEX_NONE at the end of a non-looping combo. */
	int32 GetNextComboIndex() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> LightMontages;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> HeavyMontage;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MeleeContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HeavyAction;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CurrentMontage;

	/** Seconds into the current clip. Every FNRMeleeAttack timing is clip time, so scaling by
	 *  the play rate keeps hit, chain and recovery windows on the same frames of the swing. */
	float GetAttackClipTime() const;

	/** Melee stats after parts, from UNRWeaponComponent, or base values when it is absent. */
	FNRWeaponRuntimeStats ReadMeleeStats() const;

	/** Index into LightCombo, or INDEX_NONE while the heavy attack plays. */
	int32 CurrentComboIndex = INDEX_NONE;
	float AttackStartTime = 0.0f;

	/**
	 * Stats frozen when the attack started. A part changed mid-swing waits for the next
	 * attack, so the swing's timing cannot shift under the montage that is already playing.
	 */
	FNRWeaponRuntimeStats AttackStats;
	float AttackPlayRate = 1.0f;
	bool bComboQueued = false;

	/** Side held at the click that queued the next hit, for its start-of-attack turn. */
	float QueuedSide = 0.0f;


	/** Each target takes one hit per swing, however many frames it spends in the arc. */
	TSet<TWeakObjectPtr<AActor>> HitThisSwing;

	bool bMeleeEquipped = false;
	bool bInputBound = false;

	/**
	 * The subsystem the melee context was added to, if any. Remembered rather than looked up
	 * again at removal: by the time a pawn ends play its controller is already gone, so a fresh
	 * lookup finds nothing and the context stays behind - still consuming both mouse buttons
	 * for a pawn that no longer exists, and starving the next pawn's attacks.
	 */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ContextSubsystem;
};

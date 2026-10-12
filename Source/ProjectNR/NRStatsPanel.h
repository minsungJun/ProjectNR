// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NRStatsComponent.h"
#include "NRStatsPanel.generated.h"

class UBorder;
class UTextBlock;
class UWidgetTree;

/**
 * A clickable box. Its content is built into its own widget tree by a function given to
 * Configure, so the caller never has to move widgets between trees.
 *
 * A user widget rather than a UButton so the look stays a flat border like the rest of the
 * menu, and so the click can carry a native delegate instead of a parameterless UFUNCTION.
 */
UCLASS()
class PROJECTNR_API UNRClickBox : public UUserWidget
{
	GENERATED_BODY()

public:
	struct FStyle
	{
		FLinearColor Edge = FLinearColor::White;
		FLinearColor Fill = FLinearColor::Black;
		FLinearColor HoverFill = FLinearColor::Black;
		float EdgeWidth = 2.0f;
		FMargin Padding = FMargin(8.0f);
	};

	/** Call before the box is added to a panel. A disabled box ignores clicks and hover. */
	void Configure(TFunction<UWidget*(UWidgetTree*)> InBuildContent, const FStyle& InStyle, bool bInEnabled = true);

	FSimpleDelegate OnClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	TFunction<UWidget*(UWidgetTree*)> BuildContent;
	FStyle Style;
	bool bEnabled = true;
	bool bPressed = false;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FillBorder;
};

/**
 * The stat upgrade page of the Tab menu, laid out after the "스탯 강화" mock-up: level and
 * experience across the top with the unspent points beside them, one row per stat with its
 * current value, the value after the staged points and -/+ buttons, a summary of every change
 * on the right, and Apply / Cancel at the bottom.
 *
 * Points are only staged here; Apply hands them to UNRStatsComponent::SpendPoints in one go,
 * Cancel (or closing the menu) drops them.
 */
UCLASS()
class PROJECTNR_API UNRStatsPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Drops every staged point. The menu calls this when it closes. */
	void ResetStaged();

	/** Rebuilt on the next frame, so a button is never destroyed inside its own click. */
	void RequestRefresh();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BindToStats();
	void UnbindFromStats();
	void HandleStatsChanged();

	void Refresh();

	int32 GetStagedTotal() const;
	int32 GetRemainingPoints() const;

	void ChangeStaged(ENRPlayerStat Stat, int32 Delta);
	void Apply();
	void Cancel();

	UWidget* MakeHeader();
	UWidget* MakeStatRow(ENRPlayerStat Stat);
	UWidget* MakeSummary();
	UWidget* MakeDetail();
	UWidget* MakeFooter();
	UNRClickBox* MakeButton(const FText& Label, int32 Size, bool bEnabled, bool bPrimary, float Width, float Height);
	UTextBlock* MakeText(const FText& Text, int32 Size, FLinearColor Color, bool bBold = true);

	TWeakObjectPtr<UNRStatsComponent> Stats;
	FDelegateHandle StatsChangedHandle;

	/** Points staged per stat, indexed by ENRPlayerStat. */
	TArray<int32> Staged;
	ENRPlayerStat SelectedStat = ENRPlayerStat::MaxHealth;
	bool bRefreshPending = false;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder;
};

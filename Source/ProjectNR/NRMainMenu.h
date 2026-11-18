// Copyright Epic Games, Inc. All Rights Reserved.

// The main menu (Lvl_MainMenu): start a run, open the permanent growth screen, quit. No pawn; the
// HUD puts the menu widget up in UI-only input mode.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "NRMetaProgression.h"
#include "NRMainMenu.generated.h"

class UNRMainMenuWidget;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

UCLASS()
class PROJECTNR_API ANRMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANRMainMenuGameMode();
};

UCLASS()
class PROJECTNR_API ANRMainMenuHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<UNRMainMenuWidget> Menu;
};

/** One stat on the growth screen with its invest button. */
UCLASS()
class PROJECTNR_API UNRGrowthRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(UNRMainMenuWidget* InOwner, ENRMetaStat InStat);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleInvestClicked();

	TWeakObjectPtr<UNRMainMenuWidget> Owner;
	ENRMetaStat Stat = ENRMetaStat::MaxHealth;
};

/**
 * Two pages built in code: the menu (start / growth / quit) and the growth screen (points left, then
 * a row per stat: level pips, current gain, next level's gain and cost, invest button).
 */
UCLASS()
class PROJECTNR_API UNRMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Level a new run opens. */
	FName RunLevel = TEXT("/Game/Levels/GridRun/Lvl_GridRun_Test");

	void Invest(ENRMetaStat Stat);
	void RefreshGrowth();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleStart();

	UFUNCTION()
	void HandleGrowth();

	UFUNCTION()
	void HandleQuit();

	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleRefund();

	UPROPERTY()
	TObjectPtr<UWidgetSwitcher> Pages;

	UPROPERTY()
	TObjectPtr<UVerticalBox> Rows;

	UPROPERTY()
	TObjectPtr<UTextBlock> PointsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> MenuPointsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
};

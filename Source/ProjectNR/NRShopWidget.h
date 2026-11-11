// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NRShopWidget.generated.h"

class ANRShop;
class UNRShopWidget;
class UTextBlock;
class UVerticalBox;

/** One line of the shop screen with its buy button. Knows only its line number and its screen. */
UCLASS()
class PROJECTNR_API UNRShopRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(UNRShopWidget* InOwner, int32 InIndex, const FText& InName, int32 InPrice, bool bInAffordable, bool bInSoldOut);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleBuyClicked();

	TWeakObjectPtr<UNRShopWidget> Owner;
	int32 Index = INDEX_NONE;
	FText ItemName;
	int32 Price = 0;
	bool bAffordable = true;
	bool bSoldOut = false;
};

/**
 * The shop screen: the run's gold, then every line ANRShop::GetOffers gives with its price and a
 * buy button, and a line saying how the last purchase went. Built in code like UNRLoadoutWidget;
 * ANRHUD opens and closes it. Esc or the interact key closes it.
 */
UCLASS()
class PROJECTNR_API UNRShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetShop(ANRShop* InShop);
	ANRShop* GetShop() const { return Shop.Get(); }

	/** Rebuilds the list (prices and gold may have changed). */
	void Refresh();

	/** Called by a row's button. */
	void BuyLine(int32 Index);

	FSimpleDelegate OnCloseRequested;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION()
	void HandleCloseClicked();

	TWeakObjectPtr<ANRShop> Shop;

	UPROPERTY()
	TObjectPtr<UTextBlock> GoldText;

	UPROPERTY()
	TObjectPtr<UVerticalBox> List;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	FText Status;
	bool bStatusIsError = false;
};

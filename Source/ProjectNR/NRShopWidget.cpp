// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NRInteractionComponent.h"
#include "NRRunSubsystem.h"
#include "NRShop.h"
#include "Styling/CoreStyle.h"

namespace NRShopStyle
{
	const FLinearColor Backdrop(0.0f, 0.0f, 0.0f, 0.55f);
	const FLinearColor Panel(0.015f, 0.06f, 0.07f, 0.94f);
	const FLinearColor PanelEdge(0.95f, 0.6f, 0.1f, 1.0f);
	const FLinearColor Row(0.02f, 0.09f, 0.1f, 1.0f);
	const FLinearColor TextColor(0.85f, 0.97f, 1.0f, 1.0f);
	const FLinearColor Dim(0.45f, 0.62f, 0.65f, 1.0f);
	const FLinearColor Gold(1.0f, 0.82f, 0.25f, 1.0f);
	const FLinearColor Bad(1.0f, 0.4f, 0.35f, 1.0f);
	const FLinearColor Ok(0.35f, 1.0f, 0.45f, 1.0f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Text);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}
}

// --- Row -------------------------------------------------------------------------

void UNRShopRow::Configure(UNRShopWidget* InOwner, int32 InIndex, const FText& InName, int32 InPrice, bool bInAffordable, bool bInSoldOut)
{
	Owner = InOwner;
	Index = InIndex;
	ItemName = InName;
	Price = InPrice;
	bAffordable = bInAffordable;
	bSoldOut = bInSoldOut;
}

TSharedRef<SWidget> UNRShopRow::RebuildWidget()
{
	using namespace NRShopStyle;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Back = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Back->SetBrushColor(Row);
		Back->SetPadding(FMargin(12.0f, 6.0f));
		WidgetTree->RootWidget = Back;

		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Back->SetContent(Line);

		UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(MakeText(WidgetTree, ItemName, 14, bSoldOut ? Dim : TextColor));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);

		const FText PriceText = bSoldOut ? NSLOCTEXT("NRShop", "SoldOutTag", "SOLD OUT") : FText::Format(NSLOCTEXT("NRShop", "Price", "{0} G"), Price);
		USizeBox* PriceBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		PriceBox->SetWidthOverride(110.0f);
		PriceBox->SetContent(MakeText(WidgetTree, PriceText, 14, bSoldOut ? Dim : bAffordable ? Gold : Bad, true));
		UHorizontalBoxSlot* PriceSlot = Line->AddChildToHorizontalBox(PriceBox);
		PriceSlot->SetVerticalAlignment(VAlign_Center);

		UButton* Buy = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Buy->SetIsEnabled(!bSoldOut);
		Buy->SetContent(MakeText(WidgetTree, NSLOCTEXT("NRShop", "Buy", "BUY"), 13, FLinearColor::Black, true));
		Buy->OnClicked.AddDynamic(this, &UNRShopRow::HandleBuyClicked);
		UHorizontalBoxSlot* BuySlot = Line->AddChildToHorizontalBox(Buy);
		BuySlot->SetVerticalAlignment(VAlign_Center);
	}
	return Super::RebuildWidget();
}

void UNRShopRow::HandleBuyClicked()
{
	if (UNRShopWidget* Screen = Owner.Get())
	{
		Screen->BuyLine(Index);
	}
}

// --- Screen ----------------------------------------------------------------------

void UNRShopWidget::SetShop(ANRShop* InShop)
{
	if (Shop.Get() != InShop)
	{
		Status = FText::GetEmpty();
	}
	Shop = InShop;
	Refresh();
}

TSharedRef<SWidget> UNRShopWidget::RebuildWidget()
{
	using namespace NRShopStyle;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Root->SetBrushColor(Backdrop);
		Root->SetHorizontalAlignment(HAlign_Center);
		Root->SetVerticalAlignment(VAlign_Center);
		WidgetTree->RootWidget = Root;

		UBorder* Edge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Edge->SetBrushColor(PanelEdge);
		Edge->SetPadding(FMargin(3.0f));
		Root->SetContent(Edge);

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(Panel);
		Card->SetPadding(FMargin(20.0f, 16.0f));
		Edge->SetContent(Card);

		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Width->SetWidthOverride(560.0f);
		Card->SetContent(Width);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Width->SetContent(Column);

		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(MakeText(WidgetTree, NSLOCTEXT("NRShop", "Title", "SHOP"), 22, PanelEdge, true));
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		GoldText = MakeText(WidgetTree, FText::GetEmpty(), 18, Gold, true);
		UHorizontalBoxSlot* GoldSlot = Header->AddChildToHorizontalBox(GoldText);
		GoldSlot->SetVerticalAlignment(VAlign_Center);
		Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

		List = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Column->AddChildToVerticalBox(List);

		StatusText = MakeText(WidgetTree, FText::GetEmpty(), 13, Ok);
		Column->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));

		UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UHorizontalBoxSlot* HintSlot = Footer->AddChildToHorizontalBox(MakeText(WidgetTree, NSLOCTEXT("NRShop", "Hint", "Esc / E  close"), 12, Dim));
		HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HintSlot->SetVerticalAlignment(VAlign_Center);
		UButton* Close = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Close->SetContent(MakeText(WidgetTree, NSLOCTEXT("NRShop", "Close", "CLOSE"), 13, FLinearColor::Black, true));
		Close->OnClicked.AddDynamic(this, &UNRShopWidget::HandleCloseClicked);
		Footer->AddChildToHorizontalBox(Close);
		Column->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));

		Refresh();
	}
	return Super::RebuildWidget();
}

void UNRShopWidget::Refresh()
{
	using namespace NRShopStyle;
	if (!List || !GoldText)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr;
	const int32 Money = Run ? Run->GetMoney() : 0;
	GoldText->SetText(FText::Format(NSLOCTEXT("NRShop", "Gold", "GOLD  {0}"), Money));

	List->ClearChildren();
	if (ANRShop* Current = Shop.Get())
	{
		const TArray<FNRShopOffer> Offers = Current->GetOffers(Pawn);
		for (int32 Index = 0; Index < Offers.Num(); ++Index)
		{
			const FNRShopOffer& Offer = Offers[Index];
			UNRShopRow* RowWidget = CreateWidget<UNRShopRow>(this, UNRShopRow::StaticClass());
			RowWidget->Configure(this, Index, NRShop::DescribeOffer(Offer), Offer.Price, Money >= Offer.Price, Offer.bSoldOut);
			List->AddChildToVerticalBox(RowWidget)->SetPadding(FMargin(0.0f, 2.0f));
		}
	}

	if (StatusText)
	{
		StatusText->SetText(Status);
		StatusText->SetColorAndOpacity(FSlateColor(bStatusIsError ? Bad : Ok));
	}
}

void UNRShopWidget::BuyLine(int32 Index)
{
	ANRShop* Current = Shop.Get();
	APlayerController* PC = GetOwningPlayer();
	if (!Current || !PC)
	{
		return;
	}
	const ENRShopResult Result = Current->Buy(PC->GetPawn(), Index);
	Status = ANRShop::DescribeResult(Result);
	bStatusIsError = Result != ENRShopResult::Bought;
	Refresh();
}

FReply UNRShopWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UNRInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UNRInteractionComponent>() : nullptr;
	const FKey InteractKey = Interaction ? Interaction->GetInteractKey() : EKeys::E;
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == InteractKey)
	{
		OnCloseRequested.ExecuteIfBound();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UNRShopWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Swallow clicks between rows: in GameAndUI mode they would reach the viewport and fire.
	return FReply::Handled();
}

void UNRShopWidget::HandleCloseClicked()
{
	OnCloseRequested.ExecuteIfBound();
}

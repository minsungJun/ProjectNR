// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRMainMenu.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"

namespace NRMenuStyle
{
	const FLinearColor Backdrop(0.01f, 0.02f, 0.025f, 1.0f);
	const FLinearColor Panel(0.015f, 0.06f, 0.07f, 0.94f);
	const FLinearColor Edge(0.1f, 0.55f, 0.6f, 1.0f);
	const FLinearColor Row(0.02f, 0.09f, 0.1f, 1.0f);
	const FLinearColor TextColor(0.85f, 0.97f, 1.0f, 1.0f);
	const FLinearColor Dim(0.45f, 0.62f, 0.65f, 1.0f);
	const FLinearColor Accent(1.0f, 0.82f, 0.25f, 1.0f);
	const FLinearColor PipOn(0.25f, 0.9f, 0.95f, 1.0f);
	const FLinearColor PipOff(0.1f, 0.25f, 0.27f, 1.0f);

	UTextBlock* Text(UWidgetTree* Tree, const FText& Value, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Value);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}

	UButton* Button(UWidgetTree* Tree, const FText& Label, int32 Size = 16)
	{
		UButton* Result = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		Result->SetContent(Text(Tree, Label, Size, FLinearColor::Black, true));
		return Result;
	}
}

// --- Game mode / HUD ---------------------------------------------------------------

ANRMainMenuGameMode::ANRMainMenuGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = ANRMainMenuHUD::StaticClass();
}

void ANRMainMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	if (!PlayerOwner)
	{
		return;
	}
	Menu = CreateWidget<UNRMainMenuWidget>(PlayerOwner, UNRMainMenuWidget::StaticClass());
	if (!Menu)
	{
		return;
	}
	Menu->AddToViewport();
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Menu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerOwner->SetInputMode(InputMode);
	PlayerOwner->SetShowMouseCursor(true);
}

// --- Growth row --------------------------------------------------------------------

void UNRGrowthRow::Configure(UNRMainMenuWidget* InOwner, ENRMetaStat InStat)
{
	Owner = InOwner;
	Stat = InStat;
}

TSharedRef<SWidget> UNRGrowthRow::RebuildWidget()
{
	using namespace NRMenuStyle;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const UNRMetaProgressionSubsystem* Meta = UNRMetaProgressionSubsystem::Get(this);
		const UNRMetaProgressionSettings& Settings = *GetDefault<UNRMetaProgressionSettings>();
		const int32 Level = Meta ? Meta->GetLevel(Stat) : 0;
		const int32 Max = Settings.MaxLevel;
		const int32 Cost = Meta ? Meta->GetNextCost(Stat) : 0;
		const bool bCan = Meta && Meta->CanUpgrade(Stat);

		UBorder* Back = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Back->SetBrushColor(Row);
		Back->SetPadding(FMargin(14.0f, 8.0f));
		WidgetTree->RootWidget = Back;
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Back->SetContent(Line);

		USizeBox* NameBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		NameBox->SetWidthOverride(170.0f);
		NameBox->SetContent(Text(WidgetTree, NRMeta::GetStatName(Stat), 16, TextColor, true));
		Line->AddChildToHorizontalBox(NameBox)->SetVerticalAlignment(VAlign_Center);

		// Level pips.
		UHorizontalBox* Pips = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (int32 Index = 0; Index < Max; ++Index)
		{
			UBorder* Pip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Pip->SetBrushColor(Index < Level ? PipOn : PipOff);
			USizeBox* PipBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			PipBox->SetWidthOverride(18.0f);
			PipBox->SetHeightOverride(18.0f);
			PipBox->SetContent(Pip);
			Pips->AddChildToHorizontalBox(PipBox)->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		}
		USizeBox* PipsBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		PipsBox->SetWidthOverride(130.0f);
		PipsBox->SetContent(Pips);
		Line->AddChildToHorizontalBox(PipsBox)->SetVerticalAlignment(VAlign_Center);

		const FText Now = NRMeta::FormatGain(Stat, NRMeta::GainAt(Settings, Stat, Level));
		const FText Next = Level < Max
			? FText::Format(NSLOCTEXT("NRMenu", "Next", "{0}  →  {1}"), Now, NRMeta::FormatGain(Stat, NRMeta::GainAt(Settings, Stat, Level + 1)))
			: FText::Format(NSLOCTEXT("NRMenu", "Maxed", "{0}  (최대)"), Now);
		UTextBlock* GainText = Text(WidgetTree, Next, 14, Dim);
		GainText->SetClipping(EWidgetClipping::ClipToBounds);
		UHorizontalBoxSlot* GainSlot = Line->AddChildToHorizontalBox(GainText);
		GainSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		GainSlot->SetVerticalAlignment(VAlign_Center);

		USizeBox* CostBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CostBox->SetWidthOverride(90.0f);
		CostBox->SetContent(Text(WidgetTree, Level < Max ? FText::Format(NSLOCTEXT("NRMenu", "Cost", "{0} P"), Cost) : FText::FromString(TEXT("-")),
			15, bCan ? Accent : Dim, true));
		Line->AddChildToHorizontalBox(CostBox)->SetVerticalAlignment(VAlign_Center);

		UButton* Invest = Button(WidgetTree, NSLOCTEXT("NRMenu", "Invest", "투자"), 14);
		Invest->SetIsEnabled(bCan);
		Invest->OnClicked.AddDynamic(this, &UNRGrowthRow::HandleInvestClicked);
		Line->AddChildToHorizontalBox(Invest)->SetVerticalAlignment(VAlign_Center);
	}
	return Super::RebuildWidget();
}

void UNRGrowthRow::HandleInvestClicked()
{
	if (UNRMainMenuWidget* Menu = Owner.Get())
	{
		Menu->Invest(Stat);
	}
}

// --- Menu --------------------------------------------------------------------------

TSharedRef<SWidget> UNRMainMenuWidget::RebuildWidget()
{
	using namespace NRMenuStyle;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Root->SetBrushColor(Backdrop);
		Root->SetHorizontalAlignment(HAlign_Center);
		Root->SetVerticalAlignment(VAlign_Center);
		WidgetTree->RootWidget = Root;

		UBorder* EdgeBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		EdgeBorder->SetBrushColor(Edge);
		EdgeBorder->SetPadding(FMargin(3.0f));
		Root->SetContent(EdgeBorder);
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(Panel);
		Card->SetPadding(FMargin(28.0f, 22.0f));
		EdgeBorder->SetContent(Card);

		Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
		Card->SetContent(Pages);

		// Page 0: the menu.
		USizeBox* MenuWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		MenuWidth->SetWidthOverride(380.0f);
		UVerticalBox* MenuColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		MenuWidth->SetContent(MenuColumn);
		MenuColumn->AddChildToVerticalBox(Text(WidgetTree, FText::FromString(TEXT("PROJECT NR")), 34, Edge, true))->SetHorizontalAlignment(HAlign_Center);
		MenuPointsText = Text(WidgetTree, FText::GetEmpty(), 14, Accent);
		MenuColumn->AddChildToVerticalBox(MenuPointsText)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 22.0f));
		UButton* Start = Button(WidgetTree, NSLOCTEXT("NRMenu", "Start", "시작"), 18);
		Start->OnClicked.AddDynamic(this, &UNRMainMenuWidget::HandleStart);
		MenuColumn->AddChildToVerticalBox(Start)->SetPadding(FMargin(0.0f, 5.0f));
		UButton* Growth = Button(WidgetTree, NSLOCTEXT("NRMenu", "Growth", "영구 성장"), 18);
		Growth->OnClicked.AddDynamic(this, &UNRMainMenuWidget::HandleGrowth);
		MenuColumn->AddChildToVerticalBox(Growth)->SetPadding(FMargin(0.0f, 5.0f));
		UButton* Quit = Button(WidgetTree, NSLOCTEXT("NRMenu", "Quit", "종료"), 18);
		Quit->OnClicked.AddDynamic(this, &UNRMainMenuWidget::HandleQuit);
		MenuColumn->AddChildToVerticalBox(Quit)->SetPadding(FMargin(0.0f, 5.0f));
		Pages->AddChild(MenuWidth);

		// Page 1: growth.
		USizeBox* GrowthWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		GrowthWidth->SetWidthOverride(760.0f);
		UVerticalBox* GrowthColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		GrowthWidth->SetContent(GrowthColumn);
		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Text(WidgetTree, NSLOCTEXT("NRMenu", "GrowthTitle", "영구 성장"), 24, Edge, true));
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PointsText = Text(WidgetTree, FText::GetEmpty(), 18, Accent, true);
		Header->AddChildToHorizontalBox(PointsText)->SetVerticalAlignment(VAlign_Center);
		GrowthColumn->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		UTextBlock* Hint = Text(WidgetTree,
			NSLOCTEXT("NRMenu", "GrowthHint", "런이 끝날 때 받는 포인트로 스탯을 영구히 올립니다. 스탯마다 최대 5단계, 단계가 오를수록 필요한 포인트가 늘어납니다. 탄창 1개 = 소총 30발 / 산탄총 6발."),
			13, Dim);
		Hint->SetAutoWrapText(true);
		GrowthColumn->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		GrowthColumn->AddChildToVerticalBox(Rows);
		StatusText = Text(WidgetTree, FText::GetEmpty(), 13, Dim);
		GrowthColumn->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
		UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UButton* Refund = Button(WidgetTree, NSLOCTEXT("NRMenu", "Refund", "초기화(포인트 반환)"), 14);
		Refund->OnClicked.AddDynamic(this, &UNRMainMenuWidget::HandleRefund);
		Footer->AddChildToHorizontalBox(Refund);
		UHorizontalBoxSlot* Spacer = Footer->AddChildToHorizontalBox(Text(WidgetTree, FText::GetEmpty(), 12, Dim));
		Spacer->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UButton* Back = Button(WidgetTree, NSLOCTEXT("NRMenu", "Back", "뒤로"), 14);
		Back->OnClicked.AddDynamic(this, &UNRMainMenuWidget::HandleBack);
		Footer->AddChildToHorizontalBox(Back);
		GrowthColumn->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
		Pages->AddChild(GrowthWidth);

		RefreshGrowth();
	}
	return Super::RebuildWidget();
}

void UNRMainMenuWidget::RefreshGrowth()
{
	const UNRMetaProgressionSubsystem* Meta = UNRMetaProgressionSubsystem::Get(this);
	const int32 Points = Meta ? Meta->GetPoints() : 0;
	if (PointsText)
	{
		PointsText->SetText(FText::Format(NSLOCTEXT("NRMenu", "Points", "보유 포인트  {0}"), Points));
	}
	if (MenuPointsText)
	{
		MenuPointsText->SetText(FText::Format(NSLOCTEXT("NRMenu", "MenuPoints", "영구 성장 포인트 {0}"), Points));
	}
	if (Rows)
	{
		Rows->ClearChildren();
		for (int32 Index = 0; Index < NRMetaStatCount; ++Index)
		{
			UNRGrowthRow* Row = CreateWidget<UNRGrowthRow>(this, UNRGrowthRow::StaticClass());
			Row->Configure(this, static_cast<ENRMetaStat>(Index));
			Rows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 2.0f));
		}
	}
}

void UNRMainMenuWidget::Invest(ENRMetaStat Stat)
{
	UNRMetaProgressionSubsystem* Meta = UNRMetaProgressionSubsystem::Get(this);
	const bool bOk = Meta && Meta->Upgrade(Stat);
	if (StatusText)
	{
		StatusText->SetText(bOk
			? FText::Format(NSLOCTEXT("NRMenu", "Invested", "{0} {1}단계"), NRMeta::GetStatName(Stat), Meta->GetLevel(Stat))
			: NSLOCTEXT("NRMenu", "CannotInvest", "포인트가 모자라거나 이미 최대 단계입니다."));
	}
	RefreshGrowth();
}

FReply UNRMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && Pages && Pages->GetActiveWidgetIndex() == 1)
	{
		HandleBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UNRMainMenuWidget::HandleStart()
{
	UGameplayStatics::OpenLevel(this, RunLevel);
}

void UNRMainMenuWidget::HandleGrowth()
{
	RefreshGrowth();
	if (Pages)
	{
		Pages->SetActiveWidgetIndex(1);
	}
}

void UNRMainMenuWidget::HandleBack()
{
	RefreshGrowth();
	if (Pages)
	{
		Pages->SetActiveWidgetIndex(0);
	}
}

void UNRMainMenuWidget::HandleRefund()
{
	if (UNRMetaProgressionSubsystem* Meta = UNRMetaProgressionSubsystem::Get(this))
	{
		Meta->Refund();
	}
	if (StatusText)
	{
		StatusText->SetText(NSLOCTEXT("NRMenu", "Refunded", "모든 스탯을 0단계로 돌리고 포인트를 돌려받았습니다."));
	}
	RefreshGrowth();
}

void UNRMainMenuWidget::HandleQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// --- Console -----------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs GNRMenuGrowthCommand(TEXT("NR.Menu.Growth"),
	TEXT("On the main menu: shows the permanent growth page (1) or the menu (0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}
		TArray<UUserWidget*> Found;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UNRMainMenuWidget::StaticClass(), false);
		const bool bGrowth = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
		for (UUserWidget* Widget : Found)
		{
			// The same handlers the buttons call.
			if (UFunction* Handler = Widget->FindFunction(bGrowth ? TEXT("HandleGrowth") : TEXT("HandleBack")))
			{
				Widget->ProcessEvent(Handler, nullptr);
			}
		}
	}));

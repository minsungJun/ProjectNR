#include "NRWallMask.h"

#include "NRMapGenerator.h"

#include "Async/ParallelFor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRWallMask, Log, All);

const FName FNRWallMask::AreaParameter(TEXT("WallMaskArea"));
const FName FNRWallMask::HeightParameter(TEXT("WallRaiseHeight"));

namespace
{
	struct FMaskBox
	{
		FVector2D Center;
		FVector2D Half;

		/** Distance outside the box (0 inside), and how deep inside it (0 outside). */
		void Measure(const FVector2D& At, double& OutOutside, double& OutInside) const
		{
			const FVector2D Q(FMath::Abs(At.X - Center.X) - Half.X, FMath::Abs(At.Y - Center.Y) - Half.Y);
			OutOutside = FVector2D(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0)).Size();
			OutInside = Q.X < 0.0 && Q.Y < 0.0 ? -FMath::Max(Q.X, Q.Y) : 0.0;
		}
	};

	double Smooth(double A, double B, double X)
	{
		return FMath::SmoothStep(A, B, X);
	}

	FMaskBox Span(const FVector2D& A, const FVector2D& B, double HalfWidth)
	{
		const bool bAlongX = FMath::Abs(A.X - B.X) > FMath::Abs(A.Y - B.Y);
		const FVector2D Half = bAlongX ? FVector2D(FMath::Abs(A.X - B.X) * 0.5, HalfWidth) : FVector2D(HalfWidth, FMath::Abs(A.Y - B.Y) * 0.5);
		return { (A + B) * 0.5, Half };
	}
}

void FNRWallMask::Reset()
{
	Pixels.Reset();
	Size = 0;
	Extent = 0.0;
	Height = 0.0;
}

void FNRWallMask::Build(const ANRMapGenerator& Generator, const FNRWallMaskSettings& Settings)
{
	Reset();
	if (!Settings.bEnabled)
	{
		return;
	}
	const FNRGeneratedLayout& Layout = Generator.GetActiveLayout();
	const double Pitch = Generator.GetCellPitch();
	const double Room = Generator.GetRoomSize();
	auto IsIndoor = [](const FNRGeneratedRoom* Each) { return !Each || Each->Environment == ENRRoomEnvironment::Indoor; };

	// Outdoor: the walkable inside of the outdoor walls. Indoor: everything else the layout stands on.
	TArray<FMaskBox> Outdoor;
	TArray<FMaskBox> Indoor;
	for (const FNRGeneratedRoom& Each : Layout.Rooms)
	{
		const FVector2D Center(Generator.GetRoomCenter(Each));
		const FVector2D Half(((Each.Footprint.X - 1) * Pitch + Room) * 0.5 + Settings.RoomWallPad,
			((Each.Footprint.Y - 1) * Pitch + Room) * 0.5 + Settings.RoomWallPad);
		if (IsIndoor(&Each))
		{
			Indoor.Add({ Center, Half });
		}
		else if (Each.RoomType == ENRGridRoomType::Hall)
		{
			// An outdoor hall cell is only its walkway: the middle here, the arms from its corridors.
			Outdoor.Add({ Center, FVector2D(Settings.PathHalfWidth) });
		}
		else
		{
			// Outdoor rooms, and Transition rooms (their building half faces an indoor corridor).
			Outdoor.Add({ Center, Half });
		}
	}
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		// Centre to centre: a room's own box covers the part inside it, a hall's arm reaches its middle.
		const FVector2D A(Generator.GetCellCenter(Connection.CellA));
		const FVector2D B(Generator.GetCellCenter(Connection.CellB));
		if (IsIndoor(Layout.FindRoom(Connection.RoomA)) || IsIndoor(Layout.FindRoom(Connection.RoomB)))
		{
			Indoor.Add(Span(A, B, Settings.IndoorCorridorHalfWidth));
		}
		else
		{
			Outdoor.Add(Span(A, B, Settings.PathHalfWidth));
		}
	}
	if (Outdoor.Num() == 0)
	{
		return;
	}

	// A square over everything outdoor, plus the fade.
	FBox2D Bounds(ForceInit);
	for (const FMaskBox& Box : Outdoor)
	{
		Bounds += Box.Center - Box.Half;
		Bounds += Box.Center + Box.Half;
	}
	Bounds = Bounds.ExpandBy(Settings.FadeEnd + 200.0);
	Extent = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y);
	Min = Bounds.GetCenter() - FVector2D(Extent * 0.5);
	Size = Settings.Resolution;
	Height = Settings.RaiseHeight;
	Pixels.SetNumZeroed(Size * Size);

	const double Step = Extent / Size;
	int32 Raised = 0;
	ParallelFor(Size, [&](int32 Row)
	{
		for (int32 Column = 0; Column < Size; ++Column)
		{
			const FVector2D At = Min + FVector2D((Column + 0.5) * Step, (Row + 0.5) * Step);
			double ToOutdoor = TNumericLimits<double>::Max();
			double DeepInside = 0.0;
			for (const FMaskBox& Box : Outdoor)
			{
				double Outside, Inside;
				Box.Measure(At, Outside, Inside);
				ToOutdoor = FMath::Min(ToOutdoor, Outside);
				DeepInside = FMath::Max(DeepInside, Inside);
			}
			if (ToOutdoor >= Settings.FadeEnd)
			{
				continue;
			}
			double ToIndoor = TNumericLimits<double>::Max();
			for (const FMaskBox& Box : Indoor)
			{
				double Outside, Inside;
				Box.Measure(At, Outside, Inside);
				ToIndoor = FMath::Min(ToIndoor, Outside);
			}
			const double AwayFromIndoor = Smooth(0.0, Settings.IndoorClear, ToIndoor);
			double Raise = 0.0;
			double Dirt = 0.0;
			if (ToOutdoor <= 0.0)
			{
				Dirt = 1.0 - Smooth(0.0, Settings.DirtInside, DeepInside);
			}
			else
			{
				const double Fall = 1.0 - Smooth(Settings.HoldEnd, Settings.FadeEnd, ToOutdoor);
				Raise = Smooth(0.0, Settings.RiseWidth, ToOutdoor) * Fall;
				Dirt = Fall;
			}
			FColor& Pixel = Pixels[Row * Size + Column];
			Pixel.R = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Raise * AwayFromIndoor, 0.0, 1.0) * 255.0));
			Pixel.G = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Dirt * AwayFromIndoor, 0.0, 1.0) * 255.0));
			Pixel.A = 255;
		}
	});
	for (const FColor& Pixel : Pixels)
	{
		Raised += Pixel.R > 0 ? 1 : 0;
	}
	UE_LOG(LogNRWallMask, Log, TEXT("Wall mask: %d outdoor box(es), %d indoor, %.0f m square at %s, %.0f cm/px, %d raised px"),
		Outdoor.Num(), Indoor.Num(), Extent / 100.0, *Min.ToString(), Step, Raised);
}

double FNRWallMask::RaiseAt(const FVector2D& At) const
{
	if (IsEmpty())
	{
		return 0.0;
	}
	// Bilinear, like the material's sampler.
	const double U = (At.X - Min.X) / Extent * Size - 0.5;
	const double V = (At.Y - Min.Y) / Extent * Size - 0.5;
	const int32 X0 = FMath::FloorToInt(U);
	const int32 Y0 = FMath::FloorToInt(V);
	const double FX = U - X0;
	const double FY = V - Y0;
	auto R = [this](int32 X, int32 Y)
	{
		return X < 0 || Y < 0 || X >= Size || Y >= Size ? 0.0 : Pixels[Y * Size + X].R / 255.0;
	};
	const double Top = FMath::Lerp(R(X0, Y0), R(X0 + 1, Y0), FX);
	const double Bottom = FMath::Lerp(R(X0, Y0 + 1), R(X0 + 1, Y0 + 1), FX);
	return FMath::Lerp(Top, Bottom, FY) * Height;
}

void FNRWallMask::Upload(UWorld* World, UTextureRenderTarget2D* Target) const
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	// The landscape makes its own dynamic material instances for these (play world only).
	const bool bOn = !IsEmpty() && Target;
	const FLinearColor Area(Min.X, Min.Y, bOn ? 1.0 / Extent : 0.0, bOn ? 1.0f : 0.0f);
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (!It->bUseDynamicMaterialInstance)
		{
			UE_LOG(LogNRWallMask, Warning, TEXT("%s: turn on Use Dynamic Material Instance, or the wall mask does not reach its material"), *It->GetName());
			continue;
		}
		It->SetLandscapeMaterialVectorParameterValue(AreaParameter, Area);
		It->SetLandscapeMaterialScalarParameterValue(HeightParameter, bOn ? Height : 0.0f);
	}
	if (!Target || IsEmpty())
	{
		return;
	}
	if (Target->SizeX != Size || Target->SizeY != Size || Target->RenderTargetFormat != RTF_RGBA8)
	{
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->ResizeTarget(Size, Size);
		Target->UpdateResourceImmediate(true);
	}
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}
	// RTF_RGBA8 is B8G8R8A8 on the GPU, the same byte order as FColor.
	ENQUEUE_RENDER_COMMAND(NRUploadWallMask)([Resource, Data = Pixels, Width = Size](FRHICommandListImmediate& RHICmdList)
	{
		if (FRHITexture* Texture = Resource->GetRenderTargetTexture())
		{
			const FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, Width);
			RHICmdList.UpdateTexture2D(Texture, 0, Region, Width * sizeof(FColor), reinterpret_cast<const uint8*>(Data.GetData()));
		}
	});
}

bool FNRWallMask::SavePng(const FString& Path) const
{
	if (IsEmpty())
	{
		return false;
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Size, Size, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	return FFileHelper::SaveArrayToFile(Png, *Path);
}

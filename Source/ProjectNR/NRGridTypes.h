// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRGridTypes.generated.h"

/**
 * The four ways out of a grid cell. North is +Y, East is +X, South is -Y, West is -X.
 * Listed clockwise, so turning a room by +90 degrees of yaw (counter-clockwise seen from
 * above) moves each direction one step back in this list.
 */
UENUM(BlueprintType, meta = (Bitflags))
enum class ENRGridDirection : uint8
{
	North,
	East,
	South,
	West
};

/** What a generated room is for. Only drives placement rules; the room's own Blueprint does the rest. */
UENUM(BlueprintType)
enum class ENRGridRoomType : uint8
{
	Combat,
	Start,
	Safe,
	Elite,
	Boss,
	/** A cell given over to a corridor - straight, corner, T or crossing - between rooms. Not a
	 *  room: no fight, no doors, not counted in the room counts. */
	Hall
};

/** A doorway place on a room's outline: one side of one of its cells. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRRoomPort
{
	GENERATED_BODY()

	/** Cell within the room's footprint, (0, 0) being its south-west corner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** Side of that cell. Must face out of the footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	ENRGridDirection Side = ENRGridDirection::North;

	bool operator==(const FNRRoomPort& Other) const { return Cell == Other.Cell && Side == Other.Side; }
};

/** One room of a layout: which cells it occupies and how its template is turned. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRGeneratedRoom
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 RoomId = 0;

	/** South-west (lowest X and Y) cell of the room's footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** Cells the room covers along X and Y once turned: (1, 1) for a one-cell room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1"))
	FIntPoint Footprint = FIntPoint(1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	ENRGridRoomType RoomType = ENRGridRoomType::Combat;

	/** Quarter turns of yaw applied to the template, 0..3 (0, 90, 180, 270 degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "0", ClampMax = "3"))
	int32 RotationSteps = 0;

	/** World directions this room is joined in, one bit per ENRGridDirection, from any of its
	 *  cells. Filled by the generator. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	uint8 ConnectionMask = 0;

	/** Position on the main path from start (0) to boss, or INDEX_NONE for a branch room. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 MainPathIndex = INDEX_NONE;

	/** Index into the config's room templates, chosen with RotationSteps when the room was placed.
	 *  INDEX_NONE for a hand-made layout, which uses the generator's fallback room level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 TemplateIndex = INDEX_NONE;

	bool Covers(const FIntPoint& At) const
	{
		return At.X >= Cell.X && At.Y >= Cell.Y && At.X < Cell.X + Footprint.X && At.Y < Cell.Y + Footprint.Y;
	}
};

/**
 * A corridor between two rooms, across the side two of their cells share. Generated ones keep
 * RoomA < RoomB. Two rooms have at most one corridor between them.
 */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRGeneratedConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 RoomA = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 RoomB = 0;

	/** Cell of RoomA the corridor leaves from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint CellA = FIntPoint::ZeroValue;

	/** Cell of RoomB it arrives at, next to CellA. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FIntPoint CellB = FIntPoint::ZeroValue;
};

/** A whole map: rooms, the corridors between them, and which rooms start and end the run. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRGeneratedLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	TArray<FNRGeneratedRoom> Rooms;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	TArray<FNRGeneratedConnection> Connections;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 StartRoomId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 BossRoomId = INDEX_NONE;

	/** Generator version the layout came from. A seed reproduces a layout only for the same
	 *  settings, templates and version. 0 for a hand-made layout. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 GeneratorVersion = 0;

	/** Which retry of the seed produced it, 0 for the first. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	int32 Attempt = 0;

	const FNRGeneratedRoom* FindRoom(int32 RoomId) const
	{
		return Rooms.FindByPredicate([RoomId](const FNRGeneratedRoom& Room) { return Room.RoomId == RoomId; });
	}
};

namespace NRGrid
{
	inline FIntPoint ToOffset(ENRGridDirection Direction)
	{
		switch (Direction)
		{
		case ENRGridDirection::North: return FIntPoint(0, 1);
		case ENRGridDirection::East: return FIntPoint(1, 0);
		case ENRGridDirection::South: return FIntPoint(0, -1);
		case ENRGridDirection::West:
		default: return FIntPoint(-1, 0);
		}
	}

	/** The direction from one cell to a neighbouring one, or false when they are not neighbours. */
	inline bool DirectionBetween(const FIntPoint& From, const FIntPoint& To, ENRGridDirection& OutDirection)
	{
		const FIntPoint Delta = To - From;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const ENRGridDirection Direction = static_cast<ENRGridDirection>(Index);
			if (ToOffset(Direction) == Delta)
			{
				OutDirection = Direction;
				return true;
			}
		}
		return false;
	}

	inline ENRGridDirection Opposite(ENRGridDirection Direction)
	{
		return static_cast<ENRGridDirection>((static_cast<int32>(Direction) + 2) % 4);
	}

	/** Where a template's local direction points once the template is turned by Steps quarter turns of yaw. */
	inline ENRGridDirection LocalToWorld(ENRGridDirection Local, int32 Steps)
	{
		// +90 yaw is counter-clockwise, one step back in the clockwise list.
		return static_cast<ENRGridDirection>(((static_cast<int32>(Local) - Steps) % 4 + 4) % 4);
	}

	/** The template direction that ends up facing World once turned by Steps. */
	inline ENRGridDirection WorldToLocal(ENRGridDirection World, int32 Steps)
	{
		return static_cast<ENRGridDirection>(((static_cast<int32>(World) + Steps) % 4 + 4) % 4);
	}

	/** A footprint's size once turned by Steps quarter turns: odd turns swap X and Y. */
	inline FIntPoint TurnFootprint(const FIntPoint& Footprint, int32 Steps)
	{
		return (Steps & 1) ? FIntPoint(Footprint.Y, Footprint.X) : Footprint;
	}

	/**
	 * Where a cell of a footprint ends up, counted from the turned footprint's south-west corner,
	 * once the footprint is turned about its centre by Steps quarter turns of yaw.
	 */
	inline FIntPoint TurnCell(const FIntPoint& Cell, const FIntPoint& Footprint, int32 Steps)
	{
		switch (((Steps % 4) + 4) % 4)
		{
		case 1: return FIntPoint(Footprint.Y - 1 - Cell.Y, Cell.X);
		case 2: return FIntPoint(Footprint.X - 1 - Cell.X, Footprint.Y - 1 - Cell.Y);
		case 3: return FIntPoint(Cell.Y, Footprint.X - 1 - Cell.X);
		default: return Cell;
		}
	}

	/** The template cell (in its own, unturned footprint) that ends up at Turned. */
	inline FIntPoint UnturnCell(const FIntPoint& Turned, const FIntPoint& Footprint, int32 Steps)
	{
		const int32 Back = (4 - ((Steps % 4) + 4) % 4) % 4;
		return TurnCell(Turned, TurnFootprint(Footprint, Steps), Back);
	}

	/** A port mask (one bit per ENRGridDirection, in a template's own frame) once the template
	 *  is turned by Steps quarter turns of yaw. */
	inline uint8 RotateMask(uint8 LocalMask, int32 Steps)
	{
		uint8 World = 0;
		for (int32 Local = 0; Local < 4; ++Local)
		{
			if (LocalMask & (1 << Local))
			{
				World |= 1 << static_cast<int32>(LocalToWorld(static_cast<ENRGridDirection>(Local), Steps));
			}
		}
		return World;
	}

	/** Suffix used in module actor tags: NRPort.N, NRBlock.E and so on. */
	inline const TCHAR* Letter(ENRGridDirection Direction)
	{
		switch (Direction)
		{
		case ENRGridDirection::North: return TEXT("N");
		case ENRGridDirection::East: return TEXT("E");
		case ENRGridDirection::South: return TEXT("S");
		case ENRGridDirection::West:
		default: return TEXT("W");
		}
	}
}

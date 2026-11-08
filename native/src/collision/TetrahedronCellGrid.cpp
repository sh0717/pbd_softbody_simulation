#include "collision/TetrahedronCellGrid.h"
#include <cmath>

namespace
{
	constexpr double MaximumExactCellCoordinate = 9007199254740991.0;
	constexpr double MaximumDenseGridCellCount = 262144;

	bool IsExactCellCoordinate(double Coordinate)
	{
		return std::isfinite(Coordinate) && std::abs(Coordinate) <= MaximumExactCellCoordinate;
	}
} // namespace

FTetrahedronCellGrid::FTetrahedronCellGrid(double InCellSize, double InEpsilon, int InMaxCells)
	: CellSize(InCellSize)
	, Epsilon(InEpsilon)
	, MaxCells(InMaxCells)
{
}

FVector3D FTetrahedronCellGrid::CalculateCellCoordinates(FVector3D Position) const
{
	for (EAxis Axis : AllAxes)
	{
		Position[Axis] = std::floor(Position[Axis] / CellSize);
	}

	return Position;
}

void FTetrahedronCellGrid::Reset(FVector3D BoundsMin, FVector3D BoundsMax)
{
	SparseCellTetrahedra.clear();
	OverflowTetrahedronIndices.clear();
	DenseNodeTetrahedronIndices.clear();
	DenseNodeNextIndices.clear();
	ActiveDenseCellCount = 0;

	// 경계 대각선에 비례한 여유로 경계 부근 사면체가 조밀 그리드 밖으로 빠지는 일을 줄입니다.
	const double GridPadding = Epsilon * (BoundsMax - BoundsMin).Length() * 2;
	FVector3D LastGridCell{};

	// 축별 셀 수의 곱이며 조밀 저장소 전체 크기의 안전 한도를 검사합니다.
	double DenseCellCount = 1;
	bool bGridSafe = true;

	for (EAxis Axis : AllAxes)
	{
		DenseGridOrigin[Axis] = std::floor((BoundsMin[Axis] - GridPadding) / CellSize);
		LastGridCell[Axis] = std::floor((BoundsMax[Axis] + GridPadding) / CellSize);

		const double AxisCellCount = LastGridCell[Axis] - DenseGridOrigin[Axis] + 1;
		bGridSafe = bGridSafe && IsExactCellCoordinate(DenseGridOrigin[Axis]) && IsExactCellCoordinate(LastGridCell[Axis]) && AxisCellCount > 0 && AxisCellCount <= MaximumDenseGridCellCount;
		DenseGridSize[ToIndex(Axis)] = bGridSafe ? static_cast<int>(AxisCellCount) : 0;
		DenseCellCount *= AxisCellCount;
	}

	bUsesDenseGrid = bGridSafe && DenseCellCount > 0 && DenseCellCount <= MaximumDenseGridCellCount;

	if (bUsesDenseGrid)
	{
		DenseCellFirstNodeIndices.assign(static_cast<std::size_t>(DenseCellCount), -1);
	}
}

void FTetrahedronCellGrid::AddDenseCellReference(std::size_t CellIndex, std::uint32_t TetrahedronIndex)
{
	if (DenseCellFirstNodeIndices[CellIndex] == -1)
	{
		++ActiveDenseCellCount;
	}

	DenseNodeTetrahedronIndices.push_back(static_cast<int>(TetrahedronIndex));
	DenseNodeNextIndices.push_back(DenseCellFirstNodeIndices[CellIndex]);
	DenseCellFirstNodeIndices[CellIndex] = static_cast<int>(DenseNodeTetrahedronIndices.size() - 1);
}

void FTetrahedronCellGrid::RegisterTetrahedron(std::uint32_t TetrahedronIndex, FVector3D BoundsMin, FVector3D BoundsMax)
{
	const FVector3D FirstCell = CalculateCellCoordinates(BoundsMin);
	const FVector3D LastCell = CalculateCellCoordinates(BoundsMax);

	// 여유 경계가 덮는 셀 개수이며 등록 한도를 넘으면 초과 목록을 사용합니다.
	double CoveredCellCount = 1;
	bool bCoordinatesExact = true;

	for (EAxis Axis : AllAxes)
	{
		CoveredCellCount *= LastCell[Axis] - FirstCell[Axis] + 1;
		bCoordinatesExact = bCoordinatesExact && IsExactCellCoordinate(FirstCell[Axis]) && IsExactCellCoordinate(LastCell[Axis]);
	}

	if (!bCoordinatesExact || CoveredCellCount > MaxCells)
	{
		OverflowTetrahedronIndices.push_back(TetrahedronIndex);

		return;
	}

	if (bUsesDenseGrid)
	{
		bool bOutside = false;

		for (EAxis Axis : AllAxes)
		{
			if (FirstCell[Axis] < DenseGridOrigin[Axis] || LastCell[Axis] - DenseGridOrigin[Axis] >= DenseGridSize[ToIndex(Axis)])
			{
				bOutside = true;
			}
		}

		if (bOutside)
		{
			OverflowTetrahedronIndices.push_back(TetrahedronIndex);

			return;
		}
	}

	for (double CellX = FirstCell.X; CellX <= LastCell.X; ++CellX)
	{
		for (double CellY = FirstCell.Y; CellY <= LastCell.Y; ++CellY)
		{
			for (double CellZ = FirstCell.Z; CellZ <= LastCell.Z; ++CellZ)
			{
				if (bUsesDenseGrid)
				{
					const std::size_t CellIndex = static_cast<std::size_t>((CellX - DenseGridOrigin.X) + DenseGridSize[0] * ((CellY - DenseGridOrigin.Y) + DenseGridSize[1] * (CellZ - DenseGridOrigin.Z)));
					AddDenseCellReference(CellIndex, TetrahedronIndex);
				}
				else
				{
					SparseCellTetrahedra[FVector3D{CellX, CellY, CellZ}].push_back(TetrahedronIndex);
				}
			}
		}
	}
}

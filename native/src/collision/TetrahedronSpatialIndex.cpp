#include "collision/TetrahedronSpatialIndex.h"
#include "simulation/SoftBody.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
	constexpr double MaximumExactCellCoordinate = 9007199254740991.0;
	constexpr double MaximumDenseGridCellCount = 262144;
	constexpr double MinimumAutomaticCellSize = 1E-12;

	FVector3D CalculateCellCoordinates(FVector3D Position, double CellSize)
	{
		for (EAxis Axis : AllAxes)
		{
			Position[Axis] = std::floor(Position[Axis] / CellSize);
		}
		return Position;
	}

	bool IsExactCellCoordinate(double Coordinate)
	{
		return std::isfinite(Coordinate) && std::abs(Coordinate) <= MaximumExactCellCoordinate;
	}

	double CalculateCellSize(const FSoftBody& Body, double RequestedCellSize)
	{
		if (RequestedCellSize > 0)
		{
			return RequestedCellSize;
		}
		const FIndices& TetVertexIndices = Body.GetTetIndices();
		double TotalEdgeLength = 0;
		int PositiveEdgeCount = 0;
		for (std::size_t TetOffset = 0; TetOffset < TetVertexIndices.size(); TetOffset += 4)
		{
			for (int FirstCorner = 0; FirstCorner < 4; ++FirstCorner)
			{
				for (int SecondCorner = FirstCorner + 1; SecondCorner < 4; ++SecondCorner)
				{
					const double Distance = (ReadParticleVector(Body.GetRestPositions(), TetVertexIndices[TetOffset + FirstCorner]) - ReadParticleVector(Body.GetRestPositions(), TetVertexIndices[TetOffset + SecondCorner])).Length();
					if (Distance > 0)
					{
						TotalEdgeLength += Distance;
						++PositiveEdgeCount;
					}
				}
			}
		}
		return std::max(MinimumAutomaticCellSize, TotalEdgeLength / std::max(1, PositiveEdgeCount));
	}
} // namespace

FTetrahedronSpatialIndex::FTetrahedronSpatialIndex(const FSoftBody& Body, double InCellSize, double InEpsilon, int InMaxCells)
	: CellSize(CalculateCellSize(Body, InCellSize))
	, Epsilon(InEpsilon)
	, MaxCells(InMaxCells)
	, Tetrahedra(Body.GetTetIndices().size() / 4)
{
}

void FTetrahedronSpatialIndex::ConfigureCellGrid(FSoftBody& Body)
{
	Body.UpdateBounds();
	const FVector3D BoundsMin = Body.GetBoundsMin();
	const FVector3D BoundsMax = Body.GetBoundsMax();
	// 몸체 경계의 대각선 길이에 비례한 여유입니다. 경계 부근 사면체가 조밀 그리드 밖으로 빠지는 일을 줄입니다.
	const double GridPadding = Epsilon * (BoundsMax - BoundsMin).Length() * 2;
	FVector3D LastGridCell{};
	// 축별 셀 수의 곱입니다. 활성 셀 수와 달리 조밀 저장소 전체 크기의 안전 한도를 검사합니다.
	double DenseCellCount = 1;
	// 모든 축의 셀 좌표와 축별 크기가 안전한지 누적하여 조밀·희소 경로를 결정합니다.
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

bool FTetrahedronSpatialIndex::RefitTetrahedron(const FSoftBody& Body, std::uint32_t TetIndex)
{
	const FIndices& TetVertices = Body.GetTetIndices();
	FTetrahedronGeometry& Tet = Tetrahedra[TetIndex];
	Tet.bValid = false;
	Tet.Origin = ReadParticleVector(Body.GetPositions(), TetVertices[TetIndex * 4]);
	const FVector3D EdgeFrom0To1 = ReadParticleVector(Body.GetPositions(), TetVertices[TetIndex * 4 + 1]) - Tet.Origin;
	const FVector3D EdgeFrom0To2 = ReadParticleVector(Body.GetPositions(), TetVertices[TetIndex * 4 + 2]) - Tet.Origin;
	const FVector3D EdgeFrom0To3 = ReadParticleVector(Body.GetPositions(), TetVertices[TetIndex * 4 + 3]) - Tet.Origin;
	const FVector3D OppositeFaceNormal = EdgeFrom0To2.Cross(EdgeFrom0To3);
	// 사면체 변 행렬의 행렬식으로 부호 있는 체적의 여섯 배입니다. 역기저 계산 전 퇴화를 판정합니다.
	const double Determinant = EdgeFrom0To1.Dot(OppositeFaceNormal);
	const double MaximumEdgeLength = std::sqrt(std::max({EdgeFrom0To1.Dot(EdgeFrom0To1), EdgeFrom0To2.Dot(EdgeFrom0To2), EdgeFrom0To3.Dot(EdgeFrom0To3)}));
	if (!std::isfinite(Determinant) || !(MaximumEdgeLength > 0) || std::abs(Determinant) <= Epsilon * MaximumEdgeLength * MaximumEdgeLength * MaximumEdgeLength)
	{
		return false;
	}
	Tet.BarycentricRowU = OppositeFaceNormal * (1 / Determinant);
	Tet.BarycentricRowV = EdgeFrom0To3.Cross(EdgeFrom0To1) * (1 / Determinant);
	Tet.BarycentricRowW = EdgeFrom0To1.Cross(EdgeFrom0To2) * (1 / Determinant);
	Tet.bValid = true;
	Tet.BoundsMin = Tet.BoundsMax = Tet.Origin;
	for (int CornerIndex = 1; CornerIndex < 4; ++CornerIndex)
	{
		const FVector3D Position = ReadParticleVector(Body.GetPositions(), TetVertices[TetIndex * 4 + CornerIndex]);
		for (EAxis Axis : AllAxes)
		{
			Tet.BoundsMin[Axis] = std::min(Tet.BoundsMin[Axis], Position[Axis]);
			Tet.BoundsMax[Axis] = std::max(Tet.BoundsMax[Axis], Position[Axis]);
		}
	}
	for (EAxis Axis : AllAxes)
	{
		Tet.BoundsMin[Axis] -= Epsilon * MaximumEdgeLength;
		Tet.BoundsMax[Axis] += Epsilon * MaximumEdgeLength;
	}
	return true;
}

void FTetrahedronSpatialIndex::AddDenseCellReference(std::size_t CellIndex, std::uint32_t TetIndex)
{
	if (DenseCellFirstNodeIndices[CellIndex] == -1)
	{
		++ActiveCellCount;
	}
	DenseNodeTetrahedronIndices.push_back(static_cast<int>(TetIndex));
	DenseNodeNextIndices.push_back(DenseCellFirstNodeIndices[CellIndex]);
	DenseCellFirstNodeIndices[CellIndex] = static_cast<int>(DenseNodeTetrahedronIndices.size() - 1);
}

void FTetrahedronSpatialIndex::RegisterTetrahedronInCells(std::uint32_t TetIndex)
{
	const FTetrahedronGeometry& Tet = Tetrahedra[TetIndex];
	const FVector3D FirstCell = CalculateCellCoordinates(Tet.BoundsMin, CellSize);
	const FVector3D LastCell = CalculateCellCoordinates(Tet.BoundsMax, CellSize);
	// 사면체의 여유 경계 상자가 덮는 셀 개수입니다. 등록 한도를 넘으면 초과 목록으로 보냅니다.
	double CoveredCellCount = 1;
	// 모든 시작·끝 셀 좌표가 double로 정확한 정수 연산을 할 수 있는 범위인지 확인합니다.
	bool bCoordinatesExact = true;
	for (EAxis Axis : AllAxes)
	{
		CoveredCellCount *= LastCell[Axis] - FirstCell[Axis] + 1;
		bCoordinatesExact = bCoordinatesExact && IsExactCellCoordinate(FirstCell[Axis]) && IsExactCellCoordinate(LastCell[Axis]);
	}
	if (!bCoordinatesExact || CoveredCellCount > MaxCells)
	{
		OverflowTetrahedronIndices.push_back(TetIndex);
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
			OverflowTetrahedronIndices.push_back(TetIndex);
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
					const std::size_t DenseCellIndex = static_cast<std::size_t>((CellX - DenseGridOrigin.X) + DenseGridSize[0] * ((CellY - DenseGridOrigin.Y) + DenseGridSize[1] * (CellZ - DenseGridOrigin.Z)));
					AddDenseCellReference(DenseCellIndex, TetIndex);
				}
				else
				{
					SparseCellTetrahedra[FVector3D{CellX, CellY, CellZ}].push_back(TetIndex);
				}
			}
		}
	}
}

void FTetrahedronSpatialIndex::Rebuild(FSoftBody& Body)
{
	SparseCellTetrahedra.clear();
	OverflowTetrahedronIndices.clear();
	DenseNodeTetrahedronIndices.clear();
	DenseNodeNextIndices.clear();
	ActiveCellCount = 0;
	ConfigureCellGrid(Body);
	for (std::uint32_t TetIndex = 0; TetIndex < Tetrahedra.size(); ++TetIndex)
	{
		if (RefitTetrahedron(Body, TetIndex))
		{
			RegisterTetrahedronInCells(TetIndex);
		}
	}
	if (!bUsesDenseGrid)
	{
		ActiveCellCount = SparseCellTetrahedra.size();
	}
}

bool FTetrahedronSpatialIndex::ContainsPoint(std::uint32_t TetIndex, FVector3D QueryPoint, double& InOutCandidateTests) const
{
	const FTetrahedronGeometry& Tet = Tetrahedra[TetIndex];
	if (!Tet.bValid)
	{
		return false;
	}
	for (EAxis Axis : AllAxes)
	{
		if (QueryPoint[Axis] < Tet.BoundsMin[Axis] || QueryPoint[Axis] > Tet.BoundsMax[Axis])
		{
			return false;
		}
	}
	++InOutCandidateTests;
	const FVector3D RelativePosition = QueryPoint - Tet.Origin;
	const double WeightU = Tet.BarycentricRowU.Dot(RelativePosition);
	const double WeightV = Tet.BarycentricRowV.Dot(RelativePosition);
	const double WeightW = Tet.BarycentricRowW.Dot(RelativePosition);
	return WeightU >= -Epsilon && WeightV >= -Epsilon && WeightW >= -Epsilon && 1 - WeightU - WeightV - WeightW >= -Epsilon;
}

bool FTetrahedronSpatialIndex::QueryContainingTetrahedra(FVector3D QueryPoint, FIndices* OutTetrahedra, bool bFirst, double& InOutCandidateTests) const
{
	const FVector3D Cell = CalculateCellCoordinates(QueryPoint, CellSize);
	bool bFound = false;
	const auto VisitCandidate = [this, QueryPoint, OutTetrahedra, bFirst, &InOutCandidateTests, &bFound](const std::uint32_t TetIndex)
	{
		if (ContainsPoint(TetIndex, QueryPoint, InOutCandidateTests))
		{
			bFound = true;
			if (OutTetrahedra)
			{
				OutTetrahedra->push_back(TetIndex);
			}
			return bFirst;
		}
		return false;
	};
	if (bUsesDenseGrid)
	{
		// 전역 셀 키에서 조밀 그리드 시작 셀을 뺀 로컬 좌표입니다. 배열 접근 전에 축별 범위를 검사합니다.
		const FVector3D LocalCell = Cell - DenseGridOrigin;
		if (LocalCell.X >= 0 && LocalCell.Y >= 0 && LocalCell.Z >= 0 && LocalCell.X < DenseGridSize[0] && LocalCell.Y < DenseGridSize[1] && LocalCell.Z < DenseGridSize[2])
		{
			const std::size_t DenseCellIndex = static_cast<std::size_t>(LocalCell.X + DenseGridSize[0] * (LocalCell.Y + DenseGridSize[1] * LocalCell.Z));
			for (int NodeIndex = DenseCellFirstNodeIndices[DenseCellIndex]; NodeIndex != -1; NodeIndex = DenseNodeNextIndices[NodeIndex])
			{
				if (VisitCandidate(static_cast<std::uint32_t>(DenseNodeTetrahedronIndices[NodeIndex])))
				{
					return bFound;
				}
			}
		}
	}
	else
	{
		const auto CellCandidates = SparseCellTetrahedra.find(Cell);
		if (CellCandidates != SparseCellTetrahedra.end())
		{
			for (std::uint32_t TetIndex : CellCandidates->second)
			{
				if (VisitCandidate(TetIndex))
				{
					return bFound;
				}
			}
		}
	}
	for (std::uint32_t TetIndex : OverflowTetrahedronIndices)
	{
		if (VisitCandidate(TetIndex))
		{
			return bFound;
		}
	}
	return bFound;
}

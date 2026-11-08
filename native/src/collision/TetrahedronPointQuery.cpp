#include "collision/TetrahedronPointQuery.h"
#include "simulation/SoftBody.h"
#include <algorithm>
#include <cstdint>

namespace
{
	constexpr double MinimumAutomaticCellSize = 1E-12;

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

FTetrahedronPointQuery::FTetrahedronPointQuery(const FSoftBody& Body, double InCellSize, double InEpsilon, int InMaxCells)
	: Epsilon(InEpsilon)
	, Tetrahedra(Body.GetTetIndices().size() / 4)
	, CellGrid(CalculateCellSize(Body, InCellSize), InEpsilon, InMaxCells)
{
}

void FTetrahedronPointQuery::Rebuild(FSoftBody& Body)
{
	Body.UpdateBounds();
	CellGrid.Reset(Body.GetBoundsMin(), Body.GetBoundsMax());

	const FIndices& VertexIndices = Body.GetTetIndices();
	const std::vector<double>& Positions = Body.GetPositions();

	for (std::uint32_t TetrahedronIndex = 0; TetrahedronIndex < Tetrahedra.size(); ++TetrahedronIndex)
	{
		FTetrahedronGeometry& Geometry = Tetrahedra[TetrahedronIndex];
		const std::size_t Offset = TetrahedronIndex * 4;
		Geometry.Update(ReadParticleVector(Positions, VertexIndices[Offset]), ReadParticleVector(Positions, VertexIndices[Offset + 1]), ReadParticleVector(Positions, VertexIndices[Offset + 2]), ReadParticleVector(Positions, VertexIndices[Offset + 3]), Epsilon);

		if (Geometry.IsValid())
		{
			CellGrid.RegisterTetrahedron(TetrahedronIndex, Geometry.GetBoundsMin(), Geometry.GetBoundsMax());
		}
	}
}

bool FTetrahedronPointQuery::QueryContainingTetrahedra(FVector3D QueryPoint, FIndices* OutTetrahedra, bool bFirst, double& InOutCandidateTests) const
{
	bool bFound = false;
	CellGrid.VisitCandidates(QueryPoint, [this, QueryPoint, OutTetrahedra, bFirst, &InOutCandidateTests, &bFound](std::uint32_t TetrahedronIndex)
	{
		const FTetrahedronGeometry& Geometry = Tetrahedra[TetrahedronIndex];

		if (!Geometry.IsPointWithinBounds(QueryPoint))
		{
			return false;
		}

		// 셀 후보 수가 아닌 실제 무게중심 좌표 검사 횟수만 누적합니다.
		++InOutCandidateTests;

		if (!Geometry.ContainsPoint(QueryPoint, Epsilon))
		{
			return false;
		}

		bFound = true;

		if (OutTetrahedra)
		{
			OutTetrahedra->push_back(TetrahedronIndex);
		}

		return bFirst;
	});

	return bFound;
}

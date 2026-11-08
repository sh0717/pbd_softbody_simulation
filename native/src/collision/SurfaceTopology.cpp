#include "collision/SurfaceTopology.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

FSurfaceTopology BuildSurfaceTopology(const FIndices& Tetrahedra, const FIndices& SuppliedSurface, FOperationResult* OutResult)
{
	if (Tetrahedra.size() % 4 != 0 || SuppliedSurface.size() % 3 != 0)
	{
		PublishOperationResult({EErrorCode::InvalidArgument, "Invalid contact topology index count"}, OutResult);
		return {};
	}

	FSurfaceTopology Result;

	struct FFace
	{
		// 처음 발견한 면의 정점 나열 순서입니다. 공유 면 비교용 정렬 키와 구분하여 보존합니다.
		std::array<std::uint32_t, 3> VertexIndices;

		// 같은 세 정점을 가진 면의 출현 횟수입니다. 1은 외부 면, 2는 내부 면이며 3 이상은 거부합니다.
		int OccurrenceCount = 1;
	};

	std::vector<FFace> Faces;

	// 방향과 무관한 정렬 정점 키에서 Faces 위치를 찾습니다. 면 출력 순서 자체를 정렬하지 않습니다.
	std::map<std::array<std::uint32_t, 3>, std::size_t> FaceIndexBySortedVertices;

	for (std::size_t IndexOffset = 0; IndexOffset < Tetrahedra.size(); IndexOffset += 4)
	{
		// 꼭짓점 네 개의 검증용 복사본만 정렬하며 원본 사면체의 정점 순서는 유지합니다.
		std::array<std::uint32_t, 4> SortedTetrahedronVertices{Tetrahedra[IndexOffset], Tetrahedra[IndexOffset + 1], Tetrahedra[IndexOffset + 2], Tetrahedra[IndexOffset + 3]};
		std::sort(SortedTetrahedronVertices.begin(), SortedTetrahedronVertices.end());

		if (std::adjacent_find(SortedTetrahedronVertices.begin(), SortedTetrahedronVertices.end()) != SortedTetrahedronVertices.end())
		{
			PublishOperationResult({EErrorCode::InvalidArgument, "Invalid contact tetrahedron"}, OutResult);
			return {};
		}

		for (int OppositeCornerIndex = 0; OppositeCornerIndex < 4; ++OppositeCornerIndex)
		{
			std::array<std::uint32_t, 3> Face{};
			int FaceCornerIndex = 0;

			for (int TetrahedronCornerIndex = 0; TetrahedronCornerIndex < 4; ++TetrahedronCornerIndex)
			{
				if (TetrahedronCornerIndex != OppositeCornerIndex)
				{
					Face[FaceCornerIndex++] = Tetrahedra[IndexOffset + TetrahedronCornerIndex];
				}
			}

			std::array<std::uint32_t, 3> SortedFaceVertices = Face;
			std::sort(SortedFaceVertices.begin(), SortedFaceVertices.end());

			const auto IndexedFace = FaceIndexBySortedVertices.try_emplace(SortedFaceVertices, Faces.size());

			if (IndexedFace.second)
			{
				Faces.push_back({Face, 1});
			}
			else if (++Faces[IndexedFace.first->second].OccurrenceCount > 2)
			{
				PublishOperationResult({EErrorCode::InvalidArgument, "Non-manifold tetrahedral face"}, OutResult);
				return {};
			}
		}
	}

	std::set<std::array<std::uint32_t, 3>> BoundaryFaces;
	std::set<std::array<std::uint32_t, 3>> SuppliedFaces;

	// 경계 정점 중복 검사용 집합입니다. 결과 정점은 이 집합 순서 대신 최초 출현 순서로 추가합니다.
	std::set<std::uint32_t> SeenVertices;

	for (const FFace& Face : Faces)
	{
		if (Face.OccurrenceCount != 1)
		{
			continue;
		}

		std::array<std::uint32_t, 3> SortedFaceVertices = Face.VertexIndices;
		std::sort(SortedFaceVertices.begin(), SortedFaceVertices.end());
		BoundaryFaces.insert(SortedFaceVertices);

		for (std::uint32_t VertexIndex : Face.VertexIndices)
		{
			Result.Indices.push_back(VertexIndex);

			if (SeenVertices.insert(VertexIndex).second)
			{
				Result.Vertices.push_back(VertexIndex);
			}
		}
	}

	if (!SuppliedSurface.empty())
	{
		for (std::size_t IndexOffset = 0; IndexOffset < SuppliedSurface.size(); IndexOffset += 3)
		{
			std::array<std::uint32_t, 3> SortedFaceVertices{SuppliedSurface[IndexOffset], SuppliedSurface[IndexOffset + 1], SuppliedSurface[IndexOffset + 2]};
			std::sort(SortedFaceVertices.begin(), SortedFaceVertices.end());

			if (!BoundaryFaces.count(SortedFaceVertices) || !SuppliedFaces.insert(SortedFaceVertices).second)
			{
				PublishOperationResult({EErrorCode::InvalidArgument, "Supplied surface differs from tetrahedron boundary"}, OutResult);
				return {};
			}
		}

		if (SuppliedFaces != BoundaryFaces)
		{
			PublishOperationResult({EErrorCode::InvalidArgument, "Supplied surface omits exterior faces"}, OutResult);
			return {};
		}
	}

	PublishOperationResult({}, OutResult);
	return Result;
}

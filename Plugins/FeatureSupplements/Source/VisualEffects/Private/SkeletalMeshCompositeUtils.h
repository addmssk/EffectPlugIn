
#pragma once

#include "EngineMinimal.h"

class USkeletalMesh;

class FSnapotControllerBackfaceDiscardTool
{
public:
	FSnapotControllerBackfaceDiscardTool (const FVector3f* InCachedPositions)
		: ComponentSpaceVertexPositions(InCachedPositions)
	{
	}

	inline bool IsBackface(int32 Index0, int32 Index1, int32 Index2) const
	{
		const FVector3f* Vert0 = ComponentSpaceVertexPositions + Index0;
		const FVector3f* Vert1 = ComponentSpaceVertexPositions + Index1;
		const FVector3f* Vert2 = ComponentSpaceVertexPositions + Index2;

		// Assume CCW
		FVector3f PlaneNormal = ((*Vert2 - *Vert0) ^ (*Vert1 - *Vert0)).GetSafeNormal();
		return FVector3f::DotProduct(ComponentSpaceDir, PlaneNormal) > DiscardBackFaceDotLimit;
		//FPlane4f Plane = FPlane4f(*Vert0, *Vert2, *Vert1);
		//return FVector3f::DotProduct(ComponentSpaceDir, Plane) > DiscardBackFaceDotLimit;
	};

	const FVector3f* ComponentSpaceVertexPositions;
	FVector3f ComponentSpaceDir;
	float DiscardBackFaceDotLimit;
};

bool GenerateFilteredSkeletalMesh (
	USkeletalMesh* DestMesh,
	USkeletalMesh* SrcMesh,
	const TBitArray<>& ValidVertices,
	const FSnapotControllerBackfaceDiscardTool* DiscardTool,
	const TArray<FVector2f>* OverrideUVs = nullptr,
	const bool bBuildParticleSamplingRegionByUVRange = false,
	const FName ParticleSamplingRegionName = NAME_None,
	const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(0.4f, 0.6f),
	const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f(0.1f, 0.9f),
	const TArray<int32>& ParticleSamplingRegionBones = TArray<int32>()
);

bool GenerateFilteredSkeletalMesh (
	USkeletalMesh* DestMesh,
	USkeletalMesh* SrcMesh,
	const TBitArray<>& ValidVertices,
	const TArray<FVector2f>* OverrideUVs = nullptr,
	const bool bBuildParticleSamplingRegionByUVRange = false,
	const FName ParticleSamplingRegionName = NAME_None,
	const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(0.4f, 0.6f),
	const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f(0.1f, 0.9f),
	const TArray<int32>& ParticleSamplingRegionBones = TArray<int32>()
);

bool GeneratedMeshIsReadyForNiagaraGPUSpawnLocation(USkeletalMesh* InMesh);
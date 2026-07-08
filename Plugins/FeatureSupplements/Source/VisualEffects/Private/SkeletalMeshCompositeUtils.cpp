
#include "SkeletalMeshCompositeUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

#include "RHIResources.h"

#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "VfxUtils.h"


/**************************************************************************************************
*
*   Local Variables
*
***/

static FVfxUniqueNameGenerator sSkeletalMeshCompositeUtilPhysicsUniqueNameGen = FVfxUniqueNameGenerator(TEXT("SkeletalMeshCompositeUtil_Physics"));
static FVfxUniqueNameGenerator sSkeletalMeshCompositeUtilAnimDynamicsUniqueNameGen = FVfxUniqueNameGenerator(TEXT("SkeletalMeshCompositeUtil_AnimDynamics"));


/**************************************************************************************************
*
*  FClippingContext
*
**/

struct FClippingContext
{
	FClippingContext (const TBitArray<>& InClippingData, const FSnapotControllerBackfaceDiscardTool* InBackfaceDiscardTool, const FSkeletalMeshLODRenderData& InSrcLOD)
		: VisibilityQueryPtr(nullptr)
		, BackfaceDiscardTool(InBackfaceDiscardTool)
		, bValid(false)
	{
		const int32 SrcVertexNum = InSrcLOD.GetNumVertices();
		if (InClippingData.Num() < SrcVertexNum)
		{
			return;
		}
		VisibilityQueryPtr = &InClippingData;

		VertexRemap.SetNum(SrcVertexNum);
		FinalVertexVisibility.SetNumZeroed(SrcVertexNum);
		for (int32 VisibleVert = 0; VisibleVert < SrcVertexNum; ++VisibleVert)
		{
			VertexRemap[VisibleVert] = VisibleVert;
		}

		bValid = true;
	}

	inline bool IsValid () const { return bValid; }
	inline bool QueryInputVisibility (const int32 VertexIndex) { return (*VisibilityQueryPtr)[VertexIndex]; }
	inline bool IsBackface (const int32 Index0, const int32 Index1, const int32 Index2) const
	{
		checkSlow(BackfaceDiscardTool);
		return BackfaceDiscardTool->IsBackface(Index0, Index1, Index2);
	}
	inline void RemapVertex (const int32& OriginalIndex, const int32& NewIndex) { VertexRemap[OriginalIndex] = NewIndex; }
	inline int32 GetRemappedVertexIndex (const int32& OriginalIndex) { return VertexRemap[OriginalIndex]; }
	inline void SetFinalVisiblity (const int32& VertexIndex) { FinalVertexVisibility[VertexIndex] = true; }
	inline bool GetFinalVisiblity (const int32& VertexIndex) const { return FinalVertexVisibility[VertexIndex]; }

private:
	TArray<uint32> VertexRemap;
	TBitArray<> VertexVisibilityBitArray;
	TArray<bool> FinalVertexVisibility;

	const TBitArray<>* VisibilityQueryPtr;
	const FSnapotControllerBackfaceDiscardTool* BackfaceDiscardTool;
	bool bValid;
};


/**************************************************************************************************
*
*  Local Functions
*
**/

template <bool OVERRIDE_UV>
inline void CopyVertexFromSource (
	FStaticMeshBuildVertex& DestVert,
	const FSkeletalMeshLODRenderData& SrcLODModel,
	int32 SourceVertIdx,
	const TArray<FVector2f>* OverrideUVs
)
{
	DestVert.Position = SrcLODModel.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(SourceVertIdx);
	DestVert.TangentX = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(SourceVertIdx);
	DestVert.TangentY = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(SourceVertIdx);
	DestVert.TangentZ = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(SourceVertIdx);

	if (OVERRIDE_UV)
	{
		// Copy all UVs that are available
		uint32 LODNumTexCoords = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		for (uint32 UVIndex = 0; UVIndex < LODNumTexCoords && UVIndex < MAX_TEXCOORDS; ++UVIndex)
		{
			DestVert.UVs[UVIndex] = (*OverrideUVs)[SourceVertIdx];
		}
	}
	else
	{
		// Copy all UVs that are available
		uint32 LODNumTexCoords = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		for (uint32 UVIndex = 0; UVIndex < LODNumTexCoords && UVIndex < MAX_TEXCOORDS; ++UVIndex)
		{
			FVector2f UVs = SrcLODModel.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(SourceVertIdx, UVIndex);
			DestVert.UVs[UVIndex] = UVs;
		}
	}
}

inline void CopyWeightFromSource (
	FSkinWeightInfo& DestWeight,
	const FSkeletalMeshLODRenderData& SrcLODModel,
	int32 SourceVertIdx
)
{
	FSkinWeightInfo SrcSkinWeightsBuf = SrcLODModel.SkinWeightVertexBuffer.GetVertexSkinWeights(SourceVertIdx);
	FSkinWeightInfo* SrcSkinWeights = &SrcSkinWeightsBuf;

	// if source doesn't have extra influence, we have to clear the buffer
	FMemory::Memzero(DestWeight.InfluenceBones);
	FMemory::Memzero(DestWeight.InfluenceWeights);

	FMemory::Memcpy(
		DestWeight.InfluenceBones,
		SrcSkinWeights->InfluenceBones,
		sizeof(SrcSkinWeights->InfluenceBones)
	);
	FMemory::Memcpy(
		DestWeight.InfluenceWeights,
		SrcSkinWeights->InfluenceWeights,
		sizeof(SrcSkinWeights->InfluenceWeights)
	);
}

template <const bool bIncludeOnlyFullVsibileTriangle>
inline bool IsInvalidTriangle (
	FClippingContext& ClippingContext,
	const int32& Index0,
	const int32& Index1,
	const int32& Index2
)
{
	if (bIncludeOnlyFullVsibileTriangle)
	{
		return ClippingContext.QueryInputVisibility(Index0) == false ||
							ClippingContext.QueryInputVisibility(Index1) == false ||
							ClippingContext.QueryInputVisibility(Index2) == false;
	}
	else
	{
		return ClippingContext.QueryInputVisibility(Index0) == false &&
							ClippingContext.QueryInputVisibility(Index1) == false &&
							ClippingContext.QueryInputVisibility(Index2) == false;
	}
}

template<typename VertexDataType, const bool OVERRIDE_UV, const bool DISCARD_BACKFACE, const bool BUILD_PARTICLESAMPLEREGION>
bool CopySkeletalMeshLod (
	USkeletalMesh* DestMesh,
	FSkeletalMeshLODRenderData* DestLOD,
	const FSkeletalMeshLODRenderData& SrcLOD,
	const TArray<FVector2f>* OverrideUVs,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y,
	const TArray<int32>& ParticleSamplingRegionBones,
	FClippingContext& ClippingContext
)
{
	int32 UVCount = SrcLOD.GetNumTexCoords();

	DestLOD->RenderSections.Empty(SrcLOD.RenderSections.Num());

	DestLOD->RequiredBones = SrcLOD.RequiredBones;
	DestLOD->ActiveBoneIndices = SrcLOD.ActiveBoneIndices;

	// Temp Buffers
	TArray<FStaticMeshBuildVertex> DestVerts;
	TArray<FSkinWeightInfo> DestWeights;

	TArray<FColor> DestColorBuffer;
	TArray<uint32> DestIndexBuffer;
	TArray<int32> SamplingRegionTriangle;
	TArray<int32> SamplingRegionVertex;

	int32 ValidVertexCount = 0;
	uint32 MaxIndex = 0;

	const FRawStaticIndexBuffer16or32Interface* SrcIndexBuffer = SrcLOD.MultiSizeIndexContainer.GetIndexBuffer();
	const int32 SrcIndexCount = SrcIndexBuffer->Num();
	check((SrcIndexCount % 3) == 0);
	DestIndexBuffer.Reserve(SrcIndexCount);
	for (int32 Sec = 0; Sec < SrcLOD.RenderSections.Num(); ++Sec)
	{
		FSkelMeshRenderSection& DestSection = *new(DestLOD->RenderSections) FSkelMeshRenderSection;
		const FSkelMeshRenderSection& SrcSection = SrcLOD.RenderSections[Sec];

		{
			// add the indices from the original source mesh to the merged index buffer
			const int32 ExpectedMaxIndex = SrcSection.BaseIndex + SrcSection.NumTriangles * 3;
			int32 MaxIndexIdx = FMath::Min<int32>(ExpectedMaxIndex, SrcIndexCount);
			check((ExpectedMaxIndex % 3) == 0);
			bool ZeroIndexBuffer = true;
			if (ClippingContext.IsValid())
			{
				const int32 OldNum = DestIndexBuffer.Num();
				for (int32 IndexIdx = SrcSection.BaseIndex; IndexIdx < MaxIndexIdx; IndexIdx += 3)
				{
					uint32 SrcIndex0 = SrcIndexBuffer->Get(IndexIdx);
					uint32 SrcIndex1 = SrcIndexBuffer->Get(IndexIdx + 1);
					uint32 SrcIndex2 = SrcIndexBuffer->Get(IndexIdx + 2);
						
					if (IsInvalidTriangle<true>(ClippingContext, SrcIndex0, SrcIndex1, SrcIndex2))
					{
						continue;
					}

					if (DISCARD_BACKFACE)
					{
						if (ClippingContext.IsBackface(SrcIndex0, SrcIndex1, SrcIndex2))
						{
							continue;
						}
					}

					if (BUILD_PARTICLESAMPLEREGION)
					{
						const FVector2f* Vert0_UV = OverrideUVs->GetData() + SrcIndex0;
						const FVector2f* Vert1_UV = OverrideUVs->GetData() + SrcIndex1;
						const FVector2f* Vert2_UV = OverrideUVs->GetData() + SrcIndex2;

						const bool InUV_X =
							(Vert0_UV->X > ParticleSamplingRegionByUV_X.X && Vert0_UV->X < ParticleSamplingRegionByUV_X.Y) ||
							(Vert1_UV->X > ParticleSamplingRegionByUV_X.X && Vert1_UV->X < ParticleSamplingRegionByUV_X.Y) ||
							(Vert2_UV->X > ParticleSamplingRegionByUV_X.X && Vert2_UV->X < ParticleSamplingRegionByUV_X.Y);

						
						const bool InUV_Y =
							(Vert0_UV->Y > ParticleSamplingRegionByUV_Y.X && Vert0_UV->Y < ParticleSamplingRegionByUV_Y.Y) ||
							(Vert1_UV->Y > ParticleSamplingRegionByUV_Y.X && Vert1_UV->Y < ParticleSamplingRegionByUV_Y.Y) ||
							(Vert2_UV->Y > ParticleSamplingRegionByUV_Y.X && Vert2_UV->Y < ParticleSamplingRegionByUV_Y.Y);

						if (InUV_X && InUV_Y)
						{
							SamplingRegionTriangle.Add(DestIndexBuffer.Num());
						}
					}

					ZeroIndexBuffer = false;

					DestIndexBuffer.Add(SrcIndex0);
					DestIndexBuffer.Add(SrcIndex1);
					DestIndexBuffer.Add(SrcIndex2);

					ClippingContext.SetFinalVisiblity(SrcIndex0);
					ClippingContext.SetFinalVisiblity(SrcIndex1);
					ClippingContext.SetFinalVisiblity(SrcIndex2);
				}

				const int32 OldVertCnt = ValidVertexCount;
				const int32 MaxVert = SrcSection.NumVertices + SrcSection.BaseVertexIndex;
				for (int32 Vert = SrcSection.BaseVertexIndex; Vert < MaxVert; ++Vert)
				{
					if (ClippingContext.GetFinalVisiblity(Vert) == true)
					{
						SamplingRegionVertex.Add(ValidVertexCount);
						ClippingContext.RemapVertex(Vert, ValidVertexCount++);
					}
				}

				const int32 ValidIndexCount = DestIndexBuffer.Num() - OldNum;
				uint32* BeginIndex = DestIndexBuffer.GetData() + OldNum;
				uint32* EndIndex = BeginIndex + ValidIndexCount;
				for (uint32* Curr = BeginIndex; Curr != EndIndex; ++Curr)
				{
					*Curr = ClippingContext.GetRemappedVertexIndex(*Curr);
					if (MaxIndex < *Curr)
					{
						MaxIndex = *Curr;
					}
				}

				DestSection.BaseIndex = OldNum;
				DestSection.NumTriangles = ValidIndexCount / 3;

				DestSection.BaseVertexIndex = OldVertCnt;
				DestSection.NumVertices = ValidVertexCount - OldVertCnt;
			}
			else
			{
				ZeroIndexBuffer = false;
				for (int32 IndexIdx = SrcSection.BaseIndex; IndexIdx < MaxIndexIdx; ++IndexIdx)
				{
					uint32 SrcIndex = SrcIndexBuffer->Get(IndexIdx);
					DestIndexBuffer.Add(SrcIndex);
					if (MaxIndex < SrcIndex)
					{
						MaxIndex = SrcIndex;
					}
				}

				DestSection.BaseIndex = SrcSection.BaseIndex;
				DestSection.NumTriangles = SrcSection.NumTriangles;

				DestSection.BaseVertexIndex = SrcSection.BaseVertexIndex;
				DestSection.NumVertices = SrcSection.NumVertices;

				ValidVertexCount += SrcSection.NumVertices;
			}

			if (ZeroIndexBuffer)
			{
				DestLOD->RenderSections.RemoveAt(DestLOD->RenderSections.Num() - 1);
				continue;
			}
		}

		DestSection.BoneMap = SrcSection.BoneMap;
		DestSection.MaxBoneInfluences = SrcSection.MaxBoneInfluences;

		DestSection.MaterialIndex = SrcSection.MaterialIndex;

		int32 MaxVertIdx = FMath::Min<int32>(
			SrcSection.BaseVertexIndex + SrcSection.NumVertices,
			SrcLOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices()
		);

		// Copy Vertex
		{
			int32 MaxColorIdx = SrcLOD.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices();
			DestVerts.Reserve(MaxColorIdx);
			DestWeights.Reserve(MaxColorIdx);

			if (ClippingContext.IsValid())
			{
				for (int32 VertIdx = SrcSection.BaseVertexIndex; VertIdx < MaxVertIdx; ++VertIdx)
				{
					if (ClippingContext.GetFinalVisiblity(VertIdx) == false)
					{
						continue;
					}

					// add the new vertex
					FStaticMeshBuildVertex& DestSkinVert = DestVerts[DestVerts.AddUninitialized()];
					FSkinWeightInfo& DestWeightVert = DestWeights[DestWeights.AddUninitialized()];

					CopyVertexFromSource<OVERRIDE_UV>(DestSkinVert, SrcLOD, VertIdx, OverrideUVs);
					CopyWeightFromSource(DestWeightVert, SrcLOD, VertIdx);

					// if the mesh uses vertex colors, copy the source color if possible or default to white
					if (DestMesh->GetHasVertexColors())
					{
						if( VertIdx < MaxColorIdx )
						{
							const FColor& SrcColor = SrcLOD.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertIdx);
							DestColorBuffer.Add(SrcColor);
						}
						else
						{
							const FColor ColorWhite(255, 255, 255);
							DestColorBuffer.Add(ColorWhite);
						}
					}
				}
			}
			else
			{
				for (int32 VertIdx = SrcSection.BaseVertexIndex; VertIdx < MaxVertIdx; ++VertIdx)
				{
					// add the new vertex
					FStaticMeshBuildVertex& DestSkinVert = DestVerts[DestVerts.AddUninitialized()];
					FSkinWeightInfo& DestWeightVert = DestWeights[DestWeights.AddUninitialized()];

					CopyVertexFromSource<OVERRIDE_UV>(DestSkinVert, SrcLOD, VertIdx, OverrideUVs);
					CopyWeightFromSource(DestWeightVert, SrcLOD, VertIdx);

					// if the mesh uses vertex colors, copy the source color if possible or default to white
					if (DestMesh->GetHasVertexColors())
					{
						if( VertIdx < MaxColorIdx )
						{
							const FColor& SrcColor = SrcLOD.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertIdx);
							DestColorBuffer.Add(SrcColor);
						}
						else
						{
							const FColor ColorWhite(255, 255, 255);
							DestColorBuffer.Add(ColorWhite);
						}
					}
				}
			}
		}

		DestSection.DuplicatedVerticesBuffer.DupVertData.ResizeBuffer(1);
		DestSection.DuplicatedVerticesBuffer.DupVertIndexData.ResizeBuffer(DestSection.NumVertices);

		uint8* VertData = DestSection.DuplicatedVerticesBuffer.DupVertData.GetDataPointer();
		uint8* IndexData = DestSection.DuplicatedVerticesBuffer.DupVertIndexData.GetDataPointer();

		FMemory::Memzero(IndexData, DestSection.NumVertices * sizeof(FIndexLengthPair));
		FMemory::Memzero(VertData, sizeof(uint32));
	}

	if (DestLOD->RenderSections.Num() == 0)
	{
		return false;
	}

	DestMesh->GetRefSkeleton().EnsureParentsExist(DestLOD->ActiveBoneIndices);

	DestLOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(SrcLOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs());

	DestLOD->StaticVertexBuffers.PositionVertexBuffer.Init(DestVerts);
	DestLOD->StaticVertexBuffers.StaticMeshVertexBuffer.Init(DestVerts, VertexDataType::NumTexCoords);

#if IS_MONOLITHIC
	const uint32 MaxBoneInfluences = SrcLOD.GetSkinWeightVertexBuffer()->GetMaxBoneInfluences();
	const bool bUse16BitBoneIndex = SrcLOD.GetSkinWeightVertexBuffer()->Use16BitBoneIndex();
	
	DestLOD->SkinWeightVertexBuffer.SetNeedsCPUAccess(true);
	DestLOD->SkinWeightVertexBuffer.SetMaxBoneInfluences(MaxBoneInfluences);
	DestLOD->SkinWeightVertexBuffer.SetUse16BitBoneIndex(bUse16BitBoneIndex);
	DestLOD->SkinWeightVertexBuffer = DestWeights;
#else
	check(WITH_EDITOR);
	// 4.25 : check FSkinWeightVertexBuffer& FSkinWeightVertexBuffer::operator=(const FSkinWeightVertexBuffer& Other) in Engine\Private\Rendering\SkinWeightVertexBuffer.cpp
	//
	// CleanUp();
	// 
	// bool bNeedsCPUAccess = Other.GetNeedsCPUAccess();
	// DataVertexBuffer.SetNeedsCPUAccess(bNeedsCPUAccess);
	// LookupVertexBuffer.SetNeedsCPUAccess(bNeedsCPUAccess);
	// 
	// SetMaxBoneInfluences(Other.GetMaxBoneInfluences());
	// SetUse16BitBoneIndex(Other.Use16BitBoneIndex());

	DestLOD->SkinWeightVertexBuffer = SrcLOD.SkinWeightVertexBuffer;
	DestLOD->SkinWeightVertexBuffer.SetNeedsCPUAccess(true);

	TArray<FSoftSkinVertex> SkinTemp;
	SkinTemp.SetNum(DestWeights.Num());
	for (int32 V = 0; V < SkinTemp.Num(); ++V)
	{
		const FSkinWeightInfo& WeightVert = DestWeights[V];
		FSoftSkinVertex& Dest = SkinTemp[V];

		// if source doesn't have extra influence, we have to clear the buffer
		FMemory::Memzero(Dest.InfluenceBones);
		FMemory::Memzero(Dest.InfluenceWeights);

		FMemory::Memcpy(
			Dest.InfluenceBones,
			WeightVert.InfluenceBones,
			sizeof(WeightVert.InfluenceBones)
		);
		FMemory::Memcpy(
			Dest.InfluenceWeights,
			WeightVert.InfluenceWeights,
			sizeof(WeightVert.InfluenceWeights)
		);
	}

	DestLOD->SkinWeightVertexBuffer.Init(SkinTemp);
#endif

	if (DestMesh->GetHasVertexColors())
	{
		DestLOD->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(DestColorBuffer);
	}

	const uint8 DataTypeSize = (MaxIndex < MAX_uint16) ? sizeof(uint16) : sizeof(uint32);
	DestLOD->MultiSizeIndexContainer.RebuildIndexBuffer(DataTypeSize, DestIndexBuffer);

	if (BUILD_PARTICLESAMPLEREGION)
	{
		FSkeletalMeshLODInfo& LODInfo = *DestMesh->GetLODInfo(0);

		const FSkeletalMeshSamplingInfo& DestSamplingInfoConst = DestMesh->GetSamplingInfo();
		FSkeletalMeshSamplingInfo& DestSamplingInfo = const_cast<FSkeletalMeshSamplingInfo&>(DestSamplingInfoConst);
		// We are creating mesh
		//DestMesh->WaitUntilAsyncPropertyReleased(ESkeletalMeshAsyncProperties::SamplingInfo);
		
		const FSkeletalMeshSamplingBuiltData& BuiltDataConst = DestSamplingInfo.GetBuiltData();
		FSkeletalMeshSamplingBuiltData& BuiltData = const_cast<FSkeletalMeshSamplingBuiltData&>(BuiltDataConst);
		FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = BuiltData.WholeMeshBuiltData.AddDefaulted_GetRef();
		
		LODInfo.bSupportUniformlyDistributedSampling = false;
		//WholeMeshBuiltData.AreaWeightedTriangleSampler.Init(DestMesh, 0, nullptr);
		
		//based on DestSamplingInfo.BuildRegions(DestMesh);

		if (SamplingRegionTriangle.Num())
		{
			FSkeletalMeshSamplingRegion& NewRegion = DestSamplingInfo.Regions.AddDefaulted_GetRef();

			NewRegion.Name = ParticleSamplingRegionName;
			NewRegion.LODIndex = 0;
			NewRegion.bSupportUniformlyDistributedSampling = true;

			FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = BuiltData.RegionBuiltData.AddDefaulted_GetRef();

			RegionBuiltData.TriangleIndices = MoveTemp(SamplingRegionTriangle);
			RegionBuiltData.Vertices = MoveTemp(SamplingRegionVertex);
			RegionBuiltData.BoneIndices = ParticleSamplingRegionBones;
			RegionBuiltData.AreaWeightedSampler.Init(DestMesh, 0, &RegionBuiltData.TriangleIndices);
		}
		else
		{
			UE_LOG(LogVfxGeneral, Error, TEXT("%s : Sampling Region is empty. Fallback to whole generated mesh"), ANSI_TO_TCHAR(__FUNCTION__));

			FSkeletalMeshSamplingRegion& NewRegion = DestSamplingInfo.Regions.AddDefaulted_GetRef();

			NewRegion.Name = ParticleSamplingRegionName;
			NewRegion.LODIndex = 0;
			NewRegion.bSupportUniformlyDistributedSampling = true;


			FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = BuiltData.RegionBuiltData.AddDefaulted_GetRef();
			RegionBuiltData.TriangleIndices.Reserve(DestIndexBuffer.Num());
			RegionBuiltData.Vertices.Reserve(DestVerts.Num());

			for (const FSkelMeshRenderSection& Section : DestLOD->RenderSections)
			{
				int32 StartTri = Section.BaseIndex;
				int32 FinalIndex = StartTri + Section.NumTriangles * 3;
				for (int32 TriBase = StartTri; TriBase < FinalIndex; TriBase += 3)
				{
					RegionBuiltData.TriangleIndices.Add(TriBase);
				}

				int32 FirstVert = Section.BaseVertexIndex;
				int32 MaxVert = FirstVert + Section.GetNumVertices();
				for (int32 VertexIdx = Section.BaseVertexIndex ; VertexIdx < MaxVert; ++VertexIdx)
				{
					RegionBuiltData.Vertices.Add(VertexIdx);
				}
			}

			RegionBuiltData.BoneIndices = ParticleSamplingRegionBones;
			RegionBuiltData.AreaWeightedSampler.Init(DestMesh, 0, &RegionBuiltData.TriangleIndices);
		}
	}
	return true;
}


/**************************************************************************************************
*
*  Exported Functions
*
**/

template <const bool DISCARD_BACKFACE, const bool BUILD_PARTICLESAMPLEREGION>
bool TGenerateFilteredSkeletalMesh (
	USkeletalMesh* DestMesh,
	USkeletalMesh* SrcMesh,
	const TBitArray<>& ValidVertices,
	const FSnapotControllerBackfaceDiscardTool* DiscardTool,
	const TArray<FVector2f>* OverrideUVs,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y,
	const TArray<int32>& ParticleSamplingRegionBones
)
{
	check(DISCARD_BACKFACE == false || DiscardTool != nullptr);

	if (DestMesh->GetResourceForRendering())
	{
		check(false); // Always new skeletalmesh
		UE_LOG(LogVfxGeneral, Error, TEXT("%s : unexpected reusing SkeletalMesh for runtime generation."), ANSI_TO_TCHAR(__FUNCTION__));
		DestMesh->ReleaseResources();
		DestMesh->ReleaseResourcesFence.Wait();
	}
	
	DestMesh->SetRefSkeleton(SrcMesh->GetRefSkeleton());
	DestMesh->SetSkeleton(SrcMesh->GetSkeleton());

	// initialize the merged mesh with the first src mesh entry used
	DestMesh->SetImportedBounds(SrcMesh->GetImportedBounds());
#if WITH_EDITORONLY_DATA
	//DestMesh->SkelMirrorAxis = SrcMesh->SkelMirrorAxis;
	//DestMesh->SkelMirrorFlipAxis = SrcMesh->SkelMirrorFlipAxis;
#endif
	DestMesh->SetPhysicsAsset(SrcMesh->GetPhysicsAsset());
	DestMesh->GetRefBasesInvMatrix().Empty();
	DestMesh->CalculateInvRefMatrices();

	int32 LODNum = SrcMesh->GetLODNum();

	DestMesh->SetNumSourceModels(0);
	DestMesh->SetMaterials(SrcMesh->GetMaterials());

	DestMesh->SetHasVertexColors(SrcMesh->GetHasVertexColors());

	DestMesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* DestResource = DestMesh->GetResourceForRendering();
	check(DestResource);
	DestResource->LODRenderData.Empty(LODNum);

	FSkeletalMeshRenderData* SrcResource = SrcMesh->GetResourceForRendering();
	check(SrcResource);
	
	// NOTE : LOD 0 only
	//for (int32 LODIndex = 0; LODIndex < LODNum; ++LODIndex)
	int32 LODIndex = 0;
	{
		FSkeletalMeshLODInfo* SrcLODInfo = SrcMesh->GetLODInfo(LODIndex);
		DestMesh->AddLODInfo(*SrcLODInfo);
		FSkeletalMeshLODInfo* DestLODInfo = DestMesh->GetLODInfo(LODIndex);
		DestLODInfo->bAllowCPUAccess = true;

		FSkeletalMeshLODRenderData& DestLOD = *new FSkeletalMeshLODRenderData(); 
		DestResource->LODRenderData.Add(&DestLOD);

		const FSkeletalMeshLODRenderData& SrcLOD = SrcResource->LODRenderData[LODIndex];

		int32 UVCount = SrcLOD.GetNumTexCoords();
		FClippingContext ClippingContext(ValidVertices, DiscardTool, SrcLOD);
		bool bIsValidLOD = false;
		if (OverrideUVs != nullptr)
		{
			switch (UVCount)
			{
				case 1: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat16Uvs<1>, true, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, OverrideUVs, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 2: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat16Uvs<2>, true, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, OverrideUVs, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 3: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat16Uvs<3>, true, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, OverrideUVs, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 4: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat16Uvs<4>, true, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, OverrideUVs, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				default: verify(false);
			}
		}
		else
		{
			switch (UVCount)
			{
				case 1: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat32Uvs<1>, false, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, nullptr, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 2: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat32Uvs<2>, false, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, nullptr, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 3: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat32Uvs<3>, false, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, nullptr, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				case 4: bIsValidLOD = CopySkeletalMeshLod<TGPUSkinVertexFloat32Uvs<4>, false, DISCARD_BACKFACE, BUILD_PARTICLESAMPLEREGION>(DestMesh, &DestLOD, SrcLOD, nullptr, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y, ParticleSamplingRegionBones, ClippingContext); break;
				default: verify(false);
			}
		}

		if (bIsValidLOD == false)
		{
			return false;
		}
	}

	return DestMesh->GetLODNum() != 0;

	//if (DestMesh->GetLODNum() != 0)
	//{
	//	DestMesh->InitResources();
	//	return true;
	//}
	//
	//return false;
}

bool GenerateFilteredSkeletalMesh (
	USkeletalMesh* DestMesh,
	USkeletalMesh* SrcMesh,
	const TBitArray<>& ValidVertices,
	const FSnapotControllerBackfaceDiscardTool* DiscardTool,
	const TArray<FVector2f>* OverrideUVs,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y,
	const TArray<int32>& ParticleSamplingRegionBones
)
{
	if (bBuildParticleSamplingRegionByUVRange && OverrideUVs)
	{
		return TGenerateFilteredSkeletalMesh<true, true>(
			DestMesh, SrcMesh, ValidVertices, DiscardTool, OverrideUVs,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			ParticleSamplingRegionBones
		);
	}
	else
	{
		return TGenerateFilteredSkeletalMesh<true, false>(
			DestMesh, SrcMesh, ValidVertices, DiscardTool, OverrideUVs,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			ParticleSamplingRegionBones
		);
	}
}

bool GenerateFilteredSkeletalMesh (
	USkeletalMesh* DestMesh,
	USkeletalMesh* SrcMesh,
	const TBitArray<>& ValidVertices,
	const TArray<FVector2f>* OverrideUVs,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y,
	const TArray<int32>& ParticleSamplingRegionBones
)
{
	if (bBuildParticleSamplingRegionByUVRange && OverrideUVs)
	{
		return TGenerateFilteredSkeletalMesh<false, true>(
			DestMesh, SrcMesh, ValidVertices, nullptr, OverrideUVs,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			ParticleSamplingRegionBones
		);
	}
	else
	{
		return TGenerateFilteredSkeletalMesh<false, false>(
			DestMesh, SrcMesh, ValidVertices, nullptr, OverrideUVs,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			ParticleSamplingRegionBones
		);
	}
}

bool GeneratedMeshIsReadyForNiagaraGPUSpawnLocation(USkeletalMesh* InMesh)
{
	if (InMesh == nullptr)
	{
		return false;
	}

	// NOTE : FSkeletalMeshGpuSpawnStaticBuffers::InitRHI(FRHICommandListBase& RHICmdList)
	if (FSkeletalMeshRenderData* RenderResource = InMesh->GetResourceForRendering())
	{
		if (RenderResource->LODRenderData.Num())
		{
			FSkeletalMeshLODRenderData& LODData = RenderResource->LODRenderData[0];
		
			FShaderResourceViewRHIRef MeshIndexBufferSRV = LODData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
			FShaderResourceViewRHIRef MeshVertexBufferSRV = LODData.StaticVertexBuffers.PositionVertexBuffer.GetSRV();
			FShaderResourceViewRHIRef MeshTangentBufferSRV = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
			return MeshIndexBufferSRV.IsValid() && MeshVertexBufferSRV.IsValid() && MeshTangentBufferSRV.IsValid();
		}
	}

	return false;
}
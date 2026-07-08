
#include "Components/VfxSnapshotManagerComponent.h"
#include "Components/VfxSnapshotSkinnedMeshComponent.h"
#include "Components/VfxSnapshotSkeletalMeshComponent.h"
#include "Components/VfxSnapshotStaticMeshComponent.h"
#include "Components/VfxSnapshotGroomComponent.h"
#include "GameFramework/Character.h"
#include "VfxUtils.h"
#include "SkeletalMeshCompositeUtils.h"

#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GroomComponent.h"

#include "SkeletalMeshMerge.h"
#include "BoneWeights.h"
#include "DrawDebugHelpers.h"

#include "Material/VfxMaterialEffectManager.h"



/**************************************************************************************************
*
*   Local Variables
*
***/

static int32 gVfxSnapshotGeneratorForceSync = 0;
static FAutoConsoleVariableRef CVarVfxSnapshotGeneratorForceSync (
	TEXT("msk.fx.snapshot.GeneratorForceSync"),
	gVfxSnapshotGeneratorForceSync,
	TEXT("Snapshot mesh ready at once when StartGenerate called"),
	ECVF_Default);

static FVfxUniqueNameGenerator sSnapshotUtil_MergedSkeletalMeshUniqueNameGen = FVfxUniqueNameGenerator(TEXT("Snapshot_MergedSkeletalMesh"));
static FVfxUniqueNameGenerator sSnapshotUtil_MergedSkeletonUniqueNameGen = FVfxUniqueNameGenerator(TEXT("Snapshot_MergedSkeleton"));
static FVfxUniqueNameGenerator sSnapshotUtil_CulledSkeletalMeshUniqueNameGen = FVfxUniqueNameGenerator(TEXT("Snapshot_CulledSkeletalMesh"));
static FVfxUniqueNameGenerator sSnapshotUtil_ComponentUniqueNameGen = FVfxUniqueNameGenerator(TEXT("Snapshot_Component"));


/**************************************************************************************************
*
*   FVisibleMeshToAnimMeshBoneRemapper
*
***/

class FVisibleMeshToAnimMeshBoneRemapper
{
	public:
		int32 Get(int32 VisibleMeshBoneIndex) const
		{
			return Owner->Bones[VisibleMeshBoneIndex].AnimBoneIndex;
		}

		explicit FVisibleMeshToAnimMeshBoneRemapper(FVfxSnapshotManagerSkeletalMesh* InOwner)
			: Owner(InOwner)
		{
		}

	private:
		FVfxSnapshotManagerSkeletalMesh* Owner;		
};


/**************************************************************************************************
*
*   FSnapshotControllerUtils
*
***/

struct FSnapshotControllerUtils
{
	int32 BoneIndex[MAX_TOTAL_INFLUENCES];
	float BoneWeight[MAX_TOTAL_INFLUENCES];
	int32 BoneWeightCount;

	FSnapshotControllerUtils ()
		: BoneWeightCount(0)
	{
	}

	/** Simple, CPU evaluation of a vertex's skinned position helper function */
	/** Modified from SkinnedMeshComponent.cpp, GetTypedSkinnedVertexPosition  */
	template <bool bCachedMatrices>
	FVector3f GetTypedSkinnedVertexPosition(
		USkeletalMesh* SrcMesh,
		const TArray<FMatrix44f>& ComponentSpaceTransforms,
		const FVisibleMeshToAnimMeshBoneRemapper& MeshToAnimBoneRemapper,
		const FSkelMeshRenderSection& Section,
		const FPositionVertexBuffer& PositionVertexBuffer,
		const FSkinWeightVertexBuffer& SkinWeightVertexBuffer,
		const int32 VertIndex,
		const TArray<FMatrix44f> & RefToLocals
	)
	{
		FVector3f SkinnedPos(0, 0, 0);

		// Do soft skinning for this vertex.
		int32 BufferVertIndex = Section.GetVertexBufferIndex() + VertIndex;
		FSkinWeightInfo SrcSkinWeightsBuf = SkinWeightVertexBuffer.GetVertexSkinWeights(BufferVertIndex);
		FSkinWeightInfo* SrcSkinWeights = &SrcSkinWeightsBuf;
		int32 MaxBoneInfluences = SkinWeightVertexBuffer.GetMaxBoneInfluences();

		if (MaxBoneInfluences > MAX_TOTAL_INFLUENCES)
		{
			UE_LOG(LogVfxGeneral, Error,
				TEXT("%s : SkeletalMesh(%s) has more than %d vertex influence per vertex"),
				ANSI_TO_TCHAR(__FUNCTION__), *SrcMesh->GetName(), MaxBoneInfluences
			);
			MaxBoneInfluences = MAX_TOTAL_INFLUENCES;
		}

		BoneWeightCount = 0;

	#if !PLATFORM_LITTLE_ENDIAN
		// uint8[] elements in LOD.VertexBufferGPUSkin have been swapped for VET_UBYTE4 vertex stream use
		for (int32 InfluenceIndex = MAX_INFLUENCES - 1; InfluenceIndex >= MAX_INFLUENCES - MaxBoneInfluences; InfluenceIndex--)
	#else
		for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
	#endif
		{
			uint8 InfluenceBoneIndex = SrcSkinWeights->InfluenceBones[InfluenceIndex];
			if (false == Section.BoneMap.IsValidIndex(InfluenceBoneIndex))
			{
				ensureMsgf(Section.BoneMap.IsValidIndex(InfluenceBoneIndex),
					TEXT("%s has attempted to access a BoneMap of size %i with an invalid index of %i in GetTypedSkinnedVertexPosition()"),
					*SrcMesh->GetFullName(), Section.BoneMap.Num(), InfluenceBoneIndex
				);
				continue;
			}
			const int32 MeshBoneIndex = Section.BoneMap[InfluenceBoneIndex];

			int32 TransformBoneIndex = MeshBoneIndex;
			const float	Weight = (float)SrcSkinWeights->InfluenceWeights[InfluenceIndex] * UE::AnimationCore::InvMaxRawBoneWeightFloat;
			BoneIndex[BoneWeightCount] = MeshBoneIndex;
			BoneWeight[BoneWeightCount++] = Weight;
			{
				if (bCachedMatrices)
				{
					if (RefToLocals.IsValidIndex(MeshBoneIndex) == false)
					{
						ensureMsgf(RefToLocals.IsValidIndex(MeshBoneIndex),
							TEXT("%s has attempted to access a RefToLocals of size %i with an invalid index of %i in GetTypedSkinnedVertexPosition()"),
							*SrcMesh->GetFullName(), RefToLocals.Num(), MeshBoneIndex
						);
						continue;
					}
					const FMatrix44f& RefToLocal = RefToLocals[MeshBoneIndex];
					SkinnedPos += RefToLocal.TransformPosition(PositionVertexBuffer.VertexPosition(BufferVertIndex)) * Weight;
				}
				else
				{
					int32 AnimTransformBoneIndex = MeshToAnimBoneRemapper.Get(TransformBoneIndex);
					const FMatrix44f& BoneTransformMatrix = (AnimTransformBoneIndex != INDEX_NONE) ? ComponentSpaceTransforms[AnimTransformBoneIndex] : FMatrix44f::Identity;
					const FMatrix44f RefToLocal = SrcMesh->GetRefBasesInvMatrix()[MeshBoneIndex] * BoneTransformMatrix;
					SkinnedPos += RefToLocal.TransformPosition(PositionVertexBuffer.VertexPosition(BufferVertIndex)) * Weight;
				}
			}
		}

		return SkinnedPos;
	}
};


/**************************************************************************************************
*
*	FVfxSnapshotTaskScratch
*
***/

struct FVfxSnapshotTaskScratch : public TThreadSingleton<FVfxSnapshotTaskScratch>
{
	TArray<FVector3f> ComponentSpacePositions;
	TArray<int32> BoneIndice;
};


/**************************************************************************************************
*
*	FVfxSnapshotManagerCullingTask
*
***/

class FVfxSnapshotManagerCullingTask : public FNonAbandonableTask
{
public:
	FVfxSnapshotManagerCullingTask (FVfxSnapshotManagerSkeletalMesh* InOwner)
		: Owner(InOwner)
		, bSuccess(false)
	{
	}

	FVfxSnapshotManagerCullingTask ()
		: Owner(nullptr)
		, bSuccess(false)
	{
	}

	~FVfxSnapshotManagerCullingTask ();

	void DoWork ();

	FORCEINLINE TStatId GetStatId () const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(VfxSnapshotCulledMeshGenerator, STATGROUP_ThreadPoolAsyncTasks);
	}

	TObjectPtr<USkeletalMesh> SrcMesh;
	TObjectPtr<USkeletalMesh> DestMesh;
	FVfxSnapshotManagerSkeletalMesh* Owner;
	TBitArray<> ValidVertexBits;
	TArray<FVector2f> ProjectedUVs;
	FTransform CompToWorld;
	FVector Center;
	FVector ForwardDir;
	FVfxSnapshotCullOption CullShapeOption;
	bool bProjectUV;
	float ProjectScale;
	float ProjectRollFactor;
	bool bDiscardBackFace;
	float DiscardBackFaceDotLimit;
	
	bool bBuildParticleSamplingRegionByUVRange;
	FName ParticleSamplingRegionName;
	FVector2f ParticleSamplingRegionByUV_X;
	FVector2f ParticleSamplingRegionByUV_Y;

	bool bSuccess;
};
	
FVfxSnapshotManagerCullingTask::~FVfxSnapshotManagerCullingTask ()
{
}

void FVfxSnapshotManagerCullingTask::DoWork ()
{
	const FTransform WorldToCompo = CompToWorld.Inverse();
	const FVector ComponentSpaceQueryCenter = WorldToCompo.TransformPosition(Center);
	const FVector3f ComponentSpaceQueryCenter3f = (FVector3f)ComponentSpaceQueryCenter;
	
	if (Owner->Octree == nullptr)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("UVfxSnapshotManagerComponent:: Owner->Octree nullptr"));
		return;
	}

	Owner->OctreeLock->ReadLock();
	
	FVfxSnapshotTaskScratch& Scratch = FVfxSnapshotTaskScratch::Get();
	Scratch.BoneIndice.Reset();


	// preliminary cull with bone obb
	{
		float MaxRadius = CullShapeOption.Shape == EVfxSnapshotCullShape::Sphere?
			CullShapeOption.SphereRadius : CullShapeOption.BoxExtent.GetMax();
	
		FBoxCenterAndExtent ComponentSpcaeBoundBox = FBoxCenterAndExtent(ComponentSpaceQueryCenter, FVector(MaxRadius));
		bool bIntersect = false;

		Owner->Octree->FindElementsWithBoundsTest(
			ComponentSpcaeBoundBox,
			[&] (const FVfxSnapshotSourceMeshBoneOBB& InElem) {
				FVfxSnapshotManagerSkeletalMesh::FBoneOBB& Obb = Owner->Bones[InElem.BoneIndex];

				uint32 NumDWords = FMath::DivideAndRoundUp(ValidVertexBits.Num(), NumBitsPerDWORD);
				uint32* Dest = ValidVertexBits.GetData();
				const uint32* Src = Obb.InfluencingVertices.GetData();
				const uint32* DestTerm = Dest + NumDWords;
				for (; Dest != DestTerm; ++Dest, ++Src)
				{
					*Dest |= *Src;
				}

				bIntersect = true;

				Scratch.BoneIndice.Add(InElem.BoneIndex);
			}
		);
		
		if (bIntersect == false)
		{
			Owner->OctreeLock->ReadUnlock();
			return;
		}
	}

	Scratch.ComponentSpacePositions.SetNumZeroed(Owner->VertexCount, EAllowShrinking::No);

	FVector3f* CachedComponentSpacePositions = Scratch.ComponentSpacePositions.GetData();
	FSnapotControllerBackfaceDiscardTool BackFaceDiscardTool = FSnapotControllerBackfaceDiscardTool(CachedComponentSpacePositions);

	// Vertex Cull Begin
	{
		FSkeletalMeshRenderData* Resource = SrcMesh->GetResourceForRendering();
		FSkeletalMeshLODRenderData& LODModel = Resource->LODRenderData[0];
		FSkinWeightVertexBuffer& SkinWeightVertexBuffer = LODModel.SkinWeightVertexBuffer;
		const TArray<FTransform>& RefPose = SrcMesh->GetRefSkeleton().GetRefBonePose();

		FSnapshotControllerUtils LocalUtil;
		
		FVector3f ComponentSpaceQueryDir = (FVector3f)WorldToCompo.TransformVector(ForwardDir);
		ComponentSpaceQueryDir.Normalize();
		FRotator3f SphereRot = ComponentSpaceQueryDir.Rotation();
		SphereRot.Roll += ProjectRollFactor;

		FRotationMatrix44f RotationMat(SphereRot);
		FMatrix44f Transposed = RotationMat.GetTransposed();

		BackFaceDiscardTool.ComponentSpaceDir = ComponentSpaceQueryDir;
		BackFaceDiscardTool.DiscardBackFaceDotLimit = DiscardBackFaceDotLimit;

		FVisibleMeshToAnimMeshBoneRemapper VisibleMeshToAnimMeshBoneRemapper(Owner);

		if (CullShapeOption.Shape == EVfxSnapshotCullShape::Sphere)
		{
			const float ScaledRadius = WorldToCompo.GetScale3D().GetMin() * CullShapeOption.SphereRadius;
			const float RadiusSq = ScaledRadius * ScaledRadius;
			const float Diameter = (1.f / ProjectScale) * 2.0f * ScaledRadius;
			for (TConstSetBitIterator<> Itr(ValidVertexBits); Itr; ++Itr)
			{
				int32 VertIdx = Itr.GetIndex();
				int32 SectionIndex;
				int32 VertIndex;
				LODModel.GetSectionFromVertexIndex(VertIdx, SectionIndex, VertIndex);

				check(SectionIndex < LODModel.RenderSections.Num());
				const FSkelMeshRenderSection& Section = LODModel.RenderSections[SectionIndex];

				FVector3f Vertex = LocalUtil.GetTypedSkinnedVertexPosition<false>(
					SrcMesh, Owner->ComponentSpaceTransformCache, VisibleMeshToAnimMeshBoneRemapper,
					Section, LODModel.StaticVertexBuffers.PositionVertexBuffer,
					SkinWeightVertexBuffer, VertIndex, SrcMesh->GetRefBasesInvMatrix()
				);
				FVector3f& Cached = *(CachedComponentSpacePositions + VertIndex);
				Cached = Vertex;

				Vertex -= ComponentSpaceQueryCenter3f;
				if (Vertex.SizeSquared() > RadiusSq)
				{
					ValidVertexBits.SetRange(VertIdx, 1, false);
				}
				else if (bProjectUV)
				{
					Vertex = Transposed.TransformPosition(Vertex);

					FVector2f UV;
					UV.X = (Vertex.Y / Diameter) + 0.5f;
					UV.Y = (Vertex.Z / Diameter) + 0.5f;
					ProjectedUVs[VertIdx] = UV;
				}
			}
		}
		else
		{
			FVector3f ScaledExtent = (FVector3f)(WorldToCompo.GetScale3D() * CullShapeOption.BoxExtent);
			FVector3f ScaledExtentSQ = ScaledExtent * ScaledExtent;
			float MaxExtent = ScaledExtent.GetMax();
			float MaxExtentSQ = MaxExtent * MaxExtent;
			FVector3f ScaledDimension = ScaledExtent * 2.0 * (1.f / ProjectScale);

			for (TConstSetBitIterator<> Itr(ValidVertexBits); Itr; ++Itr)
			{
				int32 VertIdx = Itr.GetIndex();
				int32 SectionIndex;
				int32 VertIndex;
				LODModel.GetSectionFromVertexIndex(VertIdx, SectionIndex, VertIndex);

				check(SectionIndex < LODModel.RenderSections.Num());
				const FSkelMeshRenderSection& Section = LODModel.RenderSections[SectionIndex];

				FVector3f Vertex = LocalUtil.GetTypedSkinnedVertexPosition<false>(
					SrcMesh, Owner->ComponentSpaceTransformCache, VisibleMeshToAnimMeshBoneRemapper,
					Section, LODModel.StaticVertexBuffers.PositionVertexBuffer,
					SkinWeightVertexBuffer, VertIndex, SrcMesh->GetRefBasesInvMatrix()
				);
				FVector3f& Cached = *(CachedComponentSpacePositions + VertIdx);
				Cached = Vertex;

				Vertex -= ComponentSpaceQueryCenter3f;

				if (Vertex.SizeSquared() > MaxExtentSQ)
				{
					ValidVertexBits.SetRange(VertIdx, 1, false);
					continue;
				}

				Vertex = Transposed.TransformPosition(Vertex);
				FVector3f VertexSQ = Vertex * Vertex;
				if (VertexSQ.X > ScaledExtentSQ.X || VertexSQ.Y > ScaledExtentSQ.Y || VertexSQ.Z > ScaledExtentSQ.Z)
				{
					ValidVertexBits.SetRange(VertIdx, 1, false);
				}
				else if (bProjectUV)
				{
					FVector2f UV;
					UV.X = (Vertex.Y / ScaledDimension.Y) + 0.5f;
					UV.Y = (Vertex.Z / ScaledDimension.Z) + 0.5f;
					ProjectedUVs[VertIdx] = UV;
				}
			}
		}
	} // Vertex Cull End

	Owner->OctreeLock->ReadUnlock();

	if (bDiscardBackFace)
	{
		bSuccess = GenerateFilteredSkeletalMesh(
			DestMesh, SrcMesh, ValidVertexBits, &BackFaceDiscardTool, bProjectUV ? &ProjectedUVs : nullptr,
			bBuildParticleSamplingRegionByUVRange, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			Scratch.BoneIndice
		);
	}
	else
	{
		bSuccess = GenerateFilteredSkeletalMesh(
			DestMesh, SrcMesh, ValidVertexBits, bProjectUV ? &ProjectedUVs : nullptr,
			bBuildParticleSamplingRegionByUVRange, ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y,
			Scratch.BoneIndice
		);
	}
}


/**************************************************************************************************
*
*	FVfxSnapshotManagerOctreeRefreshTask
*
***/

class FVfxSnapshotManagerOctreeRefreshTask : public FNonAbandonableTask
{
public:
	FVfxSnapshotManagerOctreeRefreshTask (FVfxSnapshotManagerSkeletalMesh* InOwner)
		: Owner(InOwner)
		, bSuccess(false)
	{
	}

	FVfxSnapshotManagerOctreeRefreshTask ()
		: Owner(nullptr)
		, bSuccess(false)
	{
	}

	~FVfxSnapshotManagerOctreeRefreshTask ();

	void DoWork ();

	FORCEINLINE TStatId GetStatId () const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(VfxSnapshotManagerOctreeRefreshTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	FVfxSnapshotManagerSkeletalMesh* Owner;

	TArray<FTransform> SrcComponentSpaceTransforms;
	FVector Center;
	float Radius;

	bool bSuccess;
};

FVfxSnapshotManagerOctreeRefreshTask::~FVfxSnapshotManagerOctreeRefreshTask ()
{
}

void FVfxSnapshotManagerOctreeRefreshTask::DoWork ()
{
	FVfxSnapshotSourceMeshBoneOBBOctree* Octree = new FVfxSnapshotSourceMeshBoneOBBOctree(Center, Radius);
	for (const FVfxSnapshotManagerSkeletalMesh::FBoneOBB& Curr : Owner->Bones)
	{
		if (Curr.LocalBoundBox.IsValid == false)
		{
			continue;
		}
		if (SrcComponentSpaceTransforms.IsValidIndex(Curr.AnimBoneIndex) == false)
		{
			continue;
		}
		const FTransform& ComponentTransform = SrcComponentSpaceTransforms[Curr.AnimBoneIndex];

		FVfxSnapshotSourceMeshBoneOBB MeshTri;
		MeshTri.BoneIndex = Curr.MeshBoneIndex;

		FVector3f TempCenter, TempLocalExtent;
		Curr.LocalBoundBox.GetCenterAndExtents(TempCenter, TempLocalExtent);
		
		FVector LocalCenter = (FVector)TempCenter;
		FVector LocalExtents = (FVector)TempLocalExtent;
		LocalCenter = ComponentTransform.TransformPosition(LocalCenter);
		LocalExtents = ComponentTransform.TransformVector(LocalExtents);
		LocalExtents.X = FMath::Abs(LocalExtents.X);
		LocalExtents.Y = FMath::Abs(LocalExtents.Y);
		LocalExtents.Z = FMath::Abs(LocalExtents.Z);
		FVector CompoMin = LocalCenter - LocalExtents;
		FVector CompoMax = LocalCenter + LocalExtents;
		FBox ComponentSpaceBox(CompoMin, CompoMax);
		MeshTri.BoxCenterAndExtent = FBoxCenterAndExtent(ComponentSpaceBox);
		Octree->AddElement(MeshTri);
	}

	Owner->OctreeLock->WriteLock();

	Owner->ComponentSpaceTransformCache.Reset(SrcComponentSpaceTransforms.Num());
	for (const FTransform& Curr : SrcComponentSpaceTransforms)
	{
		Owner->ComponentSpaceTransformCache.Add((FMatrix44f)Curr.ToMatrixWithScale());
	}
	
	if (Owner->Octree != nullptr)
	{
		delete Owner->Octree;
		Owner->Octree = nullptr;
	}
	Owner->Octree = Octree;
	Owner->OctreeLock->WriteUnlock();
}


/**************************************************************************************************
*
*   UVfxSnapshotManagerComponent
*
***/

FVfxSnapshotManagerSkeletalMesh::FVfxSnapshotManagerSkeletalMesh(UVfxSnapshotManagerComponent* InOwner)
	: OwnerComponent(InOwner)
	, bInited(false)
	, FrameCounter(0)
	, MaxLiveSnapshotMeshCount(16)
	, LiveSnapshotMeshCount(0)
	, VertexCount(0)
	, OctreeRefreshTask(nullptr)
	, Octree(nullptr)
	, OctreeLock(MakePimpl<FRWLock>())
{
}

FVfxSnapshotManagerSkeletalMesh::~FVfxSnapshotManagerSkeletalMesh ()
{
}

void FVfxSnapshotManagerSkeletalMesh::Uninitialize ()
{
	TArray<int64, TInlineAllocator<8>> InstanceKeys;
	Instances.GetKeys(InstanceKeys);

	for (int64& CurrKey : InstanceKeys)
	{
		DestroyInstance(CurrKey);
	}
	Instances.Reset();

	ClearAsyncOctreeRefreshTask();

	if (Octree != nullptr)
	{
		delete Octree;
		Octree = nullptr;
	}

	VertexCount = 0;
	SourceComponent = nullptr;

	bInited = false;
}

void FVfxSnapshotManagerSkeletalMesh::Reinitialize ()
{
	USkeletalMeshComponent* Old = SourceComponent;
	Uninitialize();
	FrameCounter = 0;
	Initialize(Old);
}

bool FVfxSnapshotManagerSkeletalMesh::Initialize (USkeletalMeshComponent* InSrcComponent)
{
	if (bInited)
	{
		return true;
	}
	check(IsInGameThread());

	USkeletalMesh* Mesh = InSrcComponent->GetSkeletalMeshAsset();
	if (Mesh == nullptr)
	{
		return false;
	}

	SourceComponent = InSrcComponent;
	SourceMesh = Mesh;
		
	const FSkeletalMeshRenderData* Resource =  Mesh->GetResourceForRendering();
	const FSkeletalMeshLODRenderData& LODModel = Resource->LODRenderData[0];
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer = LODModel.SkinWeightVertexBuffer;
	if (SkinWeightVertexBuffer.GetNeedsCPUAccess() == false)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("%S : SkeletalMesh %s do not allow cpu access to vertex data."), __FUNCTION__, *Mesh->GetFullName());
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	USkeletalMesh* BaseMesh = Mesh;
	USkeletalMeshComponent* AnimCompo = InSrcComponent;
	if (ACharacter* CharacterActor = Cast<ACharacter>(GetOwnerActor()))
	{
		if (USkeletalMeshComponent* SkeletalMeshCompo = CharacterActor->GetMesh())
		{
			if (USkeletalMesh* AnimMesh = SkeletalMeshCompo->GetSkeletalMeshAsset())
			{
				BaseMesh = AnimMesh;
				AnimCompo = SkeletalMeshCompo;
			}
		}
	}
	const FReferenceSkeleton& BaseRefSkeleton = BaseMesh->GetRefSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	
	Bones.SetNum(RefPose.Num());
	for (int32 BoneIndex = 0; BoneIndex < RefPose.Num(); ++BoneIndex)
	{
		FBoneOBB& BoneOBB = Bones[BoneIndex];
		BoneOBB.BoneName = RefSkeleton.GetBoneName(BoneIndex);
		BoneOBB.MeshBoneIndex = RefSkeleton.FindBoneIndex(BoneOBB.BoneName);
		BoneOBB.AnimBoneIndex = BaseRefSkeleton.FindBoneIndex(BoneOBB.BoneName);
		BoneOBB.LocalBoundBox.IsValid = false;
		BoneOBB.InfluencingVertices.Init(false, LODModel.GetNumVertices());
	}
	
	FSnapshotControllerUtils LocalUtil;
	const TArray<FTransform>& ComponentSpaceTransforms = AnimCompo->GetComponentSpaceTransforms();
	ComponentSpaceTransformCache.Reset(ComponentSpaceTransforms.Num());
	for (const FTransform& Curr : ComponentSpaceTransforms)
	{
		ComponentSpaceTransformCache.Add((FMatrix44f)Curr.ToMatrixWithScale());
	}
	
	FVisibleMeshToAnimMeshBoneRemapper VisibleMeshToAnimMeshBoneRemapper(this);

	for (uint32 VertIdx = 0; VertIdx < LODModel.GetNumVertices(); ++VertIdx)
	{
		int32 SectionIndex;
		int32 VertIndex;
		LODModel.GetSectionFromVertexIndex(VertIdx, SectionIndex, VertIndex);

		check(SectionIndex < LODModel.RenderSections.Num());
		const FSkelMeshRenderSection& Section = LODModel.RenderSections[SectionIndex];

		FVector3f Vertex = LocalUtil.GetTypedSkinnedVertexPosition<true>(
			Mesh, ComponentSpaceTransformCache, VisibleMeshToAnimMeshBoneRemapper,
			Section, LODModel.StaticVertexBuffers.PositionVertexBuffer,
			SkinWeightVertexBuffer, VertIndex, Mesh->GetRefBasesInvMatrix()
		);

		for (int32 WeightIndex = 0; WeightIndex < LocalUtil.BoneWeightCount; ++WeightIndex)
		{
			if (LocalUtil.BoneWeight[WeightIndex] < 0.1f)
			{
				continue;
			}

			FBoneOBB& BoneOBB = Bones[LocalUtil.BoneIndex[WeightIndex]];
			BoneOBB.LocalBoundBox += Vertex;
			BoneOBB.InfluencingVertices.SetRange(VertIdx, 1, true);
		}
	}

	VertexCount = LODModel.GetNumVertices();
	bInited = true;

	return true;
}

AActor* FVfxSnapshotManagerSkeletalMesh::GetOwnerActor()
{
	return OwnerComponent? OwnerComponent->GetOwner() : nullptr;
}

UWorld* FVfxSnapshotManagerSkeletalMesh::GetWorld()
{
	return OwnerComponent? OwnerComponent->GetWorld() : nullptr;
}

void FVfxSnapshotManagerSkeletalMesh::RefreshObbOctree ()
{
	ACharacter* CharacterActor = Cast<ACharacter>(GetOwnerActor());
	if (CharacterActor == nullptr)
	{
		return;
	}

	USkeletalMeshComponent* SkeletalMeshCompo = CharacterActor->GetMesh();
	if (SkeletalMeshCompo == nullptr)
	{
		return;
	}

	USkeletalMesh* Mesh = SkeletalMeshCompo->GetSkeletalMeshAsset();
	check(Mesh);

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const TArray<FTransform>& ComponentSpaceTransforms = SkeletalMeshCompo->GetComponentSpaceTransforms();

	if (ComponentSpaceTransforms.Num() == 0)
	{
		// Maybe Swapped for AnimEvaluation
		return;
	}
	
	if (OctreeRefreshTask == nullptr)
	{
		OctreeRefreshTask = new FAsyncTask<FVfxSnapshotManagerOctreeRefreshTask>(this);
	}

	if (OctreeRefreshTask->IsDone() == false)
	{
		return;
	}

	FVfxSnapshotManagerOctreeRefreshTask& Task = OctreeRefreshTask->GetTask();
	Task.SrcComponentSpaceTransforms.Reset();
	Task.SrcComponentSpaceTransforms.Append(ComponentSpaceTransforms);
	Task.Center = SkeletalMeshCompo->GetComponentTransform().Inverse().TransformPosition(SkeletalMeshCompo->Bounds.Origin);
	Task.Radius = SkeletalMeshCompo->Bounds.SphereRadius;
	
	OctreeRefreshTask->StartBackgroundTask();
}

void FVfxSnapshotManagerSkeletalMesh::ClearAsyncOctreeRefreshTask()
{
	if (OctreeRefreshTask != nullptr)
	{
		OctreeRefreshTask->EnsureCompletion();
		delete OctreeRefreshTask;
		OctreeRefreshTask = nullptr;
	}
}

void FVfxSnapshotManagerSkeletalMesh::SpawnNiagaraSystem (FVfxSnapshotManagerInstanceSkeletalMesh& InInst)
{
	if (InInst.NiagaraSpawnStage == FVfxSnapshotManagerInstance::ENiagaraSpawnStage::Waiting)
	{
		if (InInst.NiagaraSystem->HasAnyGPUEmitters() == false || GeneratedMeshIsReadyForNiagaraGPUSpawnLocation(InInst.Generated))
		{
			FFXSystemSpawnParameters SpawnParams;
			SpawnParams.SystemTemplate		= InInst.NiagaraSystem;
			SpawnParams.AttachToComponent	= InInst.AttachedComponent;
			SpawnParams.LocationType		= EAttachLocation::KeepRelativeOffset;
			SpawnParams.PoolingMethod       = EPSCPoolMethod::AutoRelease;
			SpawnParams.bAutoDestroy		= true;

			UNiagaraComponent* NiagaraSystemComponent = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(SpawnParams);
			InInst.NiagaraSpawnStage = FVfxSnapshotManagerInstance::ENiagaraSpawnStage::Done;
		}
	}
}

int64 FVfxSnapshotManagerSkeletalMesh::StartGenerate (
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const FVector& Center,
	const FVector& ForwardDir,
	const FVfxSnapshotCullOption& InCullShapeOption,
	const bool bProjectUV,
	const float ProjectScale,
	const float ProjectRollFactor,
	const bool bDebugDraw,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration,
	UNiagaraSystem* InNiagaraSystem,
	const bool bDiscardBackFace,
	const float DiscardBackFaceDotLimit,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}

	USkeletalMesh* SrcMesh = SourceComponent->GetSkeletalMeshAsset();
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerSkeletalMesh::StartGenerate failed. Mesh Changed"));
		return -1;
	}
	
	// Generate Culled SkeletalMesh
	FName NewMeshName = sSnapshotUtil_CulledSkeletalMeshUniqueNameGen.GetUniqueName();
	USkeletalMesh* DestMesh = NewObject<USkeletalMesh>(SourceComponent->GetOwner(), NewMeshName, RF_Transient);
	
	FAsyncTask<FVfxSnapshotManagerCullingTask>* NewAsyncTask = new FAsyncTask<FVfxSnapshotManagerCullingTask>(this);
	FVfxSnapshotManagerCullingTask& Task = NewAsyncTask->GetTask();

	Task.ValidVertexBits.Init(false, VertexCount);
	Task.ProjectedUVs.SetNumZeroed(VertexCount);
	Task.SrcMesh = SrcMesh;
	Task.DestMesh = DestMesh;
	Task.CompToWorld = SourceComponent->GetComponentToWorld();
	Task.Center = Center;
	Task.ForwardDir = ForwardDir;
	Task.CullShapeOption = InCullShapeOption;
	Task.bProjectUV = bProjectUV;
	Task.ProjectScale = ProjectScale;
	Task.ProjectRollFactor = ProjectRollFactor;
	Task.bDiscardBackFace = bDiscardBackFace;
	Task.DiscardBackFaceDotLimit = DiscardBackFaceDotLimit;
	Task.bBuildParticleSamplingRegionByUVRange = bBuildParticleSamplingRegionByUVRange;
	Task.ParticleSamplingRegionName = ParticleSamplingRegionName;
	Task.ParticleSamplingRegionByUV_X = ParticleSamplingRegionByUV_X;
	Task.ParticleSamplingRegionByUV_Y = ParticleSamplingRegionByUV_Y;

	if (bDebugDraw)
	{
		if (InCullShapeOption.Shape == EVfxSnapshotCullShape::Box)
		{
			FRotator SphereRot = ForwardDir.Rotation();
			SphereRot.Roll += ProjectRollFactor;

			DrawDebugBox(GetWorld(), Center,  InCullShapeOption.BoxExtent, SphereRot.Quaternion(), FColor::Green, false, 3, SDPG_World);
		}
		else
		{
			DrawDebugSphere(GetWorld(), Center, InCullShapeOption.SphereRadius, 16, FColor::Green, false, 3, SDPG_World);
		}

	}

	if (gVfxSnapshotGeneratorForceSync != 0)
	{
		NewAsyncTask->StartSynchronousTask();
	}
	else
	{
		NewAsyncTask->StartBackgroundTask();
	}

	int64 NewKey = (int64)NewAsyncTask;

	FVfxSnapshotManagerInstanceSkeletalMesh& NewInstance = Instances.Add(NewKey);
	NewInstance.AsyncTask = NewAsyncTask;
	NewInstance.AsyncTaskStartFrameCounter = GFrameCounter;
	NewInstance.Generated = DestMesh;
	NewInstance.Materials = InMaterials;
	NewInstance.bLoop = InDuration <= 0.f;
	NewInstance.RemainDuration = InDuration;
	NewInstance.MaterialParams = InMaterialParamData;
	NewInstance.bOverrideMaterialParamDuration = bOverrideMaterialParamDuration;
	NewInstance.NiagaraSystem = InNiagaraSystem;
	NewInstance.NiagaraSpawnStage = InNiagaraSystem != nullptr?
		FVfxSnapshotManagerInstance::ENiagaraSpawnStage::Waiting : 
		FVfxSnapshotManagerInstance::ENiagaraSpawnStage::NotRequired;
	NewInstance.MaterialSlotNames = InTargetMaterialSlotNames;

	return (int64)NewAsyncTask;
}

int64 FVfxSnapshotManagerSkeletalMesh::StartWithSameMesh(
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}
	
	USkeletalMesh* SrcMesh = SourceComponent->GetSkeletalMeshAsset();
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerSkeletalMesh::StartWithSameMesh failed. Mesh Changed"));
		return -1;
	}
	
	int64 NewKey = FMath::Rand32();
	while(Instances.Contains(NewKey) == true)
	{
		NewKey = FMath::Rand32();
	};

	FVfxSnapshotManagerInstanceSkeletalMesh& NewInstance = Instances.Add(NewKey);
	NewInstance.AsyncTask = nullptr;
	NewInstance.AsyncTaskStartFrameCounter = GFrameCounter;
	NewInstance.Generated = nullptr;
	NewInstance.Materials = InMaterials;
	NewInstance.bLoop = InDuration <= 0.f;
	NewInstance.RemainDuration = InDuration;
	NewInstance.MaterialParams = InMaterialParamData;
	NewInstance.bOverrideMaterialParamDuration = bOverrideMaterialParamDuration;
	NewInstance.NiagaraSystem = nullptr;
	NewInstance.NiagaraSpawnStage = FVfxSnapshotManagerInstance::ENiagaraSpawnStage::NotRequired;
	NewInstance.MaterialSlotNames = InTargetMaterialSlotNames;
	
	AActor* OwnerActor = SourceComponent->GetOwner();
	const bool bHasClothingAsset = SrcMesh->HasActiveClothingAssets();

	USkinnedMeshComponent* NewCompo = nullptr;
	FVfxMaterialEffectManager* MaterialEffectManager = nullptr;
	
	FName UniqueName = sSnapshotUtil_ComponentUniqueNameGen.GetUniqueName();

	if (bHasClothingAsset)
	{
		UVfxSnapshotSkeletalMeshComponent* SkeletalMeshComp = NewObject<UVfxSnapshotSkeletalMeshComponent>(OwnerActor, UniqueName, RF_Transient);
		MaterialEffectManager = &SkeletalMeshComp->MaterialEffectManager;
		NewCompo = SkeletalMeshComp;
	}
	else
	{
		UVfxSnapshotSkinnedMeshComponent* SkinnedMeshComp = NewObject<UVfxSnapshotSkinnedMeshComponent>(OwnerActor, UniqueName, RF_Transient);
		MaterialEffectManager = &SkinnedMeshComp->MaterialEffectManager;
		NewCompo = SkinnedMeshComp;
	}

	if (NewCompo)
	{
		NewCompo->ResetRelativeTransform();
		NewCompo->EmptyOverrideMaterials();
		NewCompo->DepthPriorityGroup = ESceneDepthPriorityGroup::SDPG_Foreground;

		USkinnedMeshComponent* LeaderComponent = SourceComponent->LeaderPoseComponent.IsValid()? SourceComponent->LeaderPoseComponent.Get() : SourceComponent;

		NewCompo->SetLeaderPoseComponent(LeaderComponent);
		NewCompo->SetAbsolute(false, false, false);
		NewCompo->AttachToComponent(SourceComponent, FAttachmentTransformRules::KeepRelativeTransform);
		NewCompo->SetSkinnedAssetAndUpdate(SrcMesh, true);
		NewInstance.AttachedComponent = NewCompo;

		for (int32 MatIdx = 0; MatIdx < NewInstance.Materials.Num(); ++MatIdx)
		{
			UMaterialInterface* MatInst = *(NewInstance.Materials.GetData() + MatIdx);
			NewCompo->SetMaterial(MatIdx, MatInst);
		}

		if (UVfxMaterialParamsData* MaterialParams = NewInstance.MaterialParams.Get())
		{
			MaterialEffectManager->Initialize(NewCompo, NewCompo);
			MaterialEffectManager->CreateEffect(
				MaterialParams,
				0,
				NewInstance.bOverrideMaterialParamDuration,
				NewInstance.bOverrideMaterialParamDuration? NewInstance.RemainDuration : 0.f,
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				NewInstance.bLoop,
				NewInstance.MaterialSlotNames
			);
		}
		NewCompo->RegisterComponentWithWorld(SourceComponent->GetWorld());

		if (bHasClothingAsset)
		{
			Cast<UVfxSnapshotSkeletalMeshComponent>(NewCompo)->BindClothToLeaderPoseComponent();
		}
	}

	return NewKey;
}

USkinnedMeshComponent* FVfxSnapshotManagerSkeletalMesh::GetInstance (int64 InKey) const
{
	const FVfxSnapshotManagerInstanceSkeletalMesh* Inst = Instances.Find(InKey);
	if (Inst)
	{
		return Cast<USkinnedMeshComponent>(Inst->AttachedComponent.Get());
	}

	return nullptr;
}

bool FVfxSnapshotManagerSkeletalMesh::IsInstanceReady (int64 InKey)
{
	FVfxSnapshotManagerInstanceSkeletalMesh* Inst = Instances.Find(InKey);
	if (Inst)
	{
		FAsyncTask<FVfxSnapshotManagerCullingTask>* CurrTask = Inst->AsyncTask;
		return CurrTask->IsDone();
	}

	return false;
}

void FVfxSnapshotManagerSkeletalMesh::DestroyInstance (int64 InKey)
{
	FVfxSnapshotManagerInstanceSkeletalMesh* Inst = Instances.Find(InKey);
	if (Inst)
	{
		if (FAsyncTask<FVfxSnapshotManagerCullingTask>* CurrTask = Inst->AsyncTask)
		{
			CurrTask->EnsureCompletion();
			delete CurrTask;
			Inst->AsyncTask = nullptr;
		}

		if (::IsValid(Inst->AttachedComponent))
		{
			if (Inst->AttachedComponent->IsRegistered())
			{
				Inst->AttachedComponent->UnregisterComponent();
				Inst->AttachedComponent->DestroyComponent();
			}
			Inst->AttachedComponent = nullptr;
		}

		if (Inst->Generated)
		{
			Inst->Generated->MarkAsGarbage();
			Inst->Generated = nullptr;
		}

		Instances.Remove(InKey);
	}
}

USkeletalMeshComponent* FVfxSnapshotManagerSkeletalMesh::GetSourceComponent() const
{
	return SourceComponent.Get();
}

void FVfxSnapshotManagerSkeletalMesh::Tick(float DeltaTime)
{
	if (bInited == false)
	{
		return;
	}

	if (FrameCounter != GFrameCounter)
	{
		FrameCounter = GFrameCounter;
		AActor* Owner = GetOwnerActor();
		UWorld* World = GetWorld();

		TArray<int64, TInlineAllocator<4>> InvalidInst;

		for (TPair<int64, FVfxSnapshotManagerInstanceSkeletalMesh>& CurrElem : Instances)
		{
			FVfxSnapshotManagerInstanceSkeletalMesh& Inst = CurrElem.Value;

			Inst.RemainDuration -= DeltaTime;
			if (Inst.bLoop == false && Inst.RemainDuration < 0.f)
			{
				InvalidInst.Add(CurrElem.Key);
				continue;
			}

			if (Inst.AttachedComponent == nullptr)
			{
				FAsyncTask<FVfxSnapshotManagerCullingTask>* CurrTask = Inst.AsyncTask;
				if (CurrTask->IsDone() == false)
				{
					uint64 FrameDiff = GFrameCounter - Inst.AsyncTaskStartFrameCounter;
					if (FrameDiff < 3)
					{
						continue;
					}
					CurrTask->EnsureCompletion(true);
				}
				
				FVfxSnapshotManagerCullingTask& CurrTaskContext = CurrTask->GetTask();
				if (CurrTaskContext.bSuccess == false)
				{
					InvalidInst.Add(CurrElem.Key);
					continue;
				}
			
				USkeletalMesh* Result = CurrTaskContext.DestMesh;
				//Result->SetPositiveBoundsExtension(-FVector(50,50,50));
				//Result->SetNegativeBoundsExtension(-FVector(50,50,50));
				Result->InitResources();
				const bool bHasClothingAsset = Result->HasActiveClothingAssets();
				
				FName UniqueName = sSnapshotUtil_ComponentUniqueNameGen.GetUniqueName();

				USkinnedMeshComponent* NewCompo = nullptr;
				FVfxMaterialEffectManager* MaterialEffectManager = nullptr;

				if (bHasClothingAsset || Inst.NiagaraSystem)
				{
					// Niagara SkeletalMesh Module requires SkeletalMeshComponent event if Asset is SkeletalMesh.
					UVfxSnapshotSkeletalMeshComponent* SkeletalMeshComp = NewObject<UVfxSnapshotSkeletalMeshComponent>(Owner, UniqueName, RF_Transient);
					MaterialEffectManager = &SkeletalMeshComp->MaterialEffectManager;
					NewCompo = SkeletalMeshComp;
				}
				else
				{
					UVfxSnapshotSkinnedMeshComponent* SkinnedMeshComp = NewObject<UVfxSnapshotSkinnedMeshComponent>(Owner, UniqueName, RF_Transient);
					MaterialEffectManager = &SkinnedMeshComp->MaterialEffectManager;
					NewCompo = SkinnedMeshComp;
				}

				if (NewCompo)
				{
					NewCompo->ResetRelativeTransform();
					NewCompo->EmptyOverrideMaterials();
					NewCompo->DepthPriorityGroup = ESceneDepthPriorityGroup::SDPG_Foreground;

					USkinnedMeshComponent* LeaderComponent = SourceComponent->LeaderPoseComponent.IsValid()? SourceComponent->LeaderPoseComponent.Get() : SourceComponent;

					NewCompo->SetLeaderPoseComponent(LeaderComponent);
					NewCompo->SetAbsolute(false, false, false);
					NewCompo->AttachToComponent(SourceComponent, FAttachmentTransformRules::KeepRelativeTransform);
					NewCompo->SetSkinnedAssetAndUpdate(Result, true);
					Inst.AttachedComponent = NewCompo;
					
					for (int32 MatIdx = 0; MatIdx < Inst.Materials.Num(); ++MatIdx)
					{
						UMaterialInterface* MatInst = *(Inst.Materials.GetData() + MatIdx);
						NewCompo->SetMaterial(MatIdx, MatInst);
					}

					if (UVfxMaterialParamsData* MaterialParams = Inst.MaterialParams.Get())
					{
						MaterialEffectManager->Initialize(Owner, NewCompo);
						MaterialEffectManager->CreateEffect(
							MaterialParams,
							0,
							Inst.bOverrideMaterialParamDuration,
							Inst.bOverrideMaterialParamDuration? Inst.RemainDuration : 0.f,
							0.f,		 // if Inst.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
							0.f,		 // if Inst.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
							Inst.bLoop,
							Inst.MaterialSlotNames
						);
					}
					NewCompo->RegisterComponentWithWorld(World);

					SpawnNiagaraSystem(Inst);

					if (bHasClothingAsset)
					{
						Cast<UVfxSnapshotSkeletalMeshComponent>(NewCompo)->BindClothToLeaderPoseComponent();
					}
				}
				else
				{
					InvalidInst.Add(CurrElem.Key);
					continue;
				}
			}
			else
			{
				SpawnNiagaraSystem(Inst);
			}
		}

		for (int64 InstKey : InvalidInst)
		{
			DestroyInstance(InstKey);
		}

		RefreshObbOctree();
	}
}


/**************************************************************************************************
*
*   FVfxSnapshotManagerStaticMesh
*
***/

FVfxSnapshotManagerStaticMesh::FVfxSnapshotManagerStaticMesh(UVfxSnapshotManagerComponent* InOwner)
	: OwnerComponent(InOwner)
	, bInited(false)
	, FrameCounter(0)
	, MaxLiveSnapshotMeshCount(16)
	, LiveSnapshotMeshCount(0)
	, InstanceIDGen(0)
{
}

FVfxSnapshotManagerStaticMesh::~FVfxSnapshotManagerStaticMesh()
{
}

void FVfxSnapshotManagerStaticMesh::Reinitialize ()
{
	UStaticMeshComponent* Old = SourceComponent;
	Uninitialize();
	FrameCounter = 0;
	Initialize(Old);
}

void FVfxSnapshotManagerStaticMesh::Uninitialize ()
{
	TArray<int64, TInlineAllocator<8>> InstanceKeys;
	Instances.GetKeys(InstanceKeys);

	for (int64& CurrKey : InstanceKeys)
	{
		DestroyInstance(CurrKey);
	}
	Instances.Reset();

	SourceComponent = nullptr;

	bInited = false;
}

bool FVfxSnapshotManagerStaticMesh::Initialize(UStaticMeshComponent* InSrcComponent)
{
	if (bInited)
	{
		return true;
	}
	check(IsInGameThread());

	UStaticMesh* Mesh = InSrcComponent->GetStaticMesh();
	if (Mesh == nullptr)
	{
		return false;
	}

	SourceComponent = InSrcComponent;
	SourceMesh = Mesh;

	bInited = true;

	return true;
}

AActor* FVfxSnapshotManagerStaticMesh::GetOwnerActor()
{
	return OwnerComponent? OwnerComponent->GetOwner() : nullptr;
}

UWorld* FVfxSnapshotManagerStaticMesh::GetWorld()
{
	return OwnerComponent? OwnerComponent->GetWorld() : nullptr;
}

void FVfxSnapshotManagerStaticMesh::SpawnNiagaraSystem (FVfxSnapshotManagerInstanceStaticMesh& InInst)
{
	if (InInst.NiagaraSpawnStage == FVfxSnapshotManagerInstance::ENiagaraSpawnStage::Waiting)
	{
		FFXSystemSpawnParameters SpawnParams;
		SpawnParams.SystemTemplate		= InInst.NiagaraSystem;
		SpawnParams.AttachToComponent	= InInst.AttachedComponent;
		SpawnParams.LocationType		= EAttachLocation::KeepRelativeOffset;
		SpawnParams.PoolingMethod       = EPSCPoolMethod::AutoRelease;
		SpawnParams.bAutoDestroy		= true;

		UNiagaraComponent* NiagaraSystemComponent = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(SpawnParams);
		InInst.NiagaraSpawnStage = FVfxSnapshotManagerInstance::ENiagaraSpawnStage::Done;
	}
}

int64 FVfxSnapshotManagerStaticMesh::StartGenerate (
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const FVector& Center,
	const FVector& ForwardDir,
	const FVfxSnapshotCullOption& InCullShapeOption,
	const bool bProjectUV,
	const float ProjectScale,
	const float ProjectRollFactor,
	const bool bDebugDraw,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration,
	UNiagaraSystem* InNiagaraSystem,
	const bool bDiscardBackFace,
	const float DiscardBackFaceDotLimit,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}

	UStaticMesh* SrcMesh = SourceComponent->GetStaticMesh();
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerStaticMesh::StartGenerate failed. Mesh Changed"));
		return -1;
	}
	
	if (bDebugDraw)
	{
		if (InCullShapeOption.Shape == EVfxSnapshotCullShape::Box)
		{
			FRotator SphereRot = ForwardDir.Rotation();
			SphereRot.Roll += ProjectRollFactor;

			DrawDebugBox(GetWorld(), Center,  InCullShapeOption.BoxExtent, SphereRot.Quaternion(), FColor::Green, false, 3, SDPG_World);
		}
		else
		{
			DrawDebugSphere(GetWorld(), Center, InCullShapeOption.SphereRadius, 16, FColor::Green, false, 3, SDPG_World);
		}
	}
	
	UE_LOG(LogVfxGeneral, Log, TEXT("FVfxSnapshotManagerStaticMesh::StartGenerate failed. Not yet supported"));
	return -1;
}

int64 FVfxSnapshotManagerStaticMesh::StartWithSameMesh(
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}
	
	UStaticMesh* SrcMesh = SourceComponent->GetStaticMesh();
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerStaticMesh::StartWithSameMesh failed. Mesh Changed"));
		return -1;
	}
	
	int64 NewKey = ++InstanceIDGen;

	FVfxSnapshotManagerInstanceStaticMesh& NewInstance = Instances.Add(NewKey);
	NewInstance.Materials = InMaterials;
	NewInstance.bLoop = InDuration <= 0.f;
	NewInstance.RemainDuration = InDuration;
	NewInstance.MaterialParams = InMaterialParamData;
	NewInstance.bOverrideMaterialParamDuration = bOverrideMaterialParamDuration;
	NewInstance.NiagaraSystem = nullptr;
	NewInstance.NiagaraSpawnStage = FVfxSnapshotManagerInstance::ENiagaraSpawnStage::NotRequired;
	NewInstance.MaterialSlotNames = InTargetMaterialSlotNames;
	
	AActor* OwnerActor = SourceComponent->GetOwner();

	FName UniqueName = sSnapshotUtil_ComponentUniqueNameGen.GetUniqueName();

	UVfxSnapshotStaticMeshComponent* NewCompo = NewObject<UVfxSnapshotStaticMeshComponent>(OwnerActor, UniqueName, RF_Transient);
	FVfxMaterialEffectManager* MaterialEffectManager = &NewCompo->MaterialEffectManager;

	if (NewCompo)
	{
		NewCompo->ResetRelativeTransform();
		NewCompo->EmptyOverrideMaterials();
		NewCompo->DepthPriorityGroup = ESceneDepthPriorityGroup::SDPG_Foreground;

		NewCompo->SetAbsolute(false, false, false);
		NewCompo->AttachToComponent(SourceComponent, FAttachmentTransformRules::KeepRelativeTransform);
		NewCompo->SetStaticMesh(SrcMesh);
		NewInstance.AttachedComponent = NewCompo;
		NewInstance.SnapshotComponent = NewCompo;

		for (int32 MatIdx = 0; MatIdx < NewInstance.Materials.Num(); ++MatIdx)
		{
			UMaterialInterface* MatInst = *(NewInstance.Materials.GetData() + MatIdx);
			NewCompo->SetMaterial(MatIdx, MatInst);
		}

		if (UVfxMaterialParamsData* MaterialParams = NewInstance.MaterialParams.Get())
		{
			MaterialEffectManager->Initialize(NewCompo, NewCompo);
			MaterialEffectManager->CreateEffect(
				MaterialParams,
				0,
				NewInstance.bOverrideMaterialParamDuration,
				NewInstance.bOverrideMaterialParamDuration? NewInstance.RemainDuration : 0.f,
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				NewInstance.bLoop,
				NewInstance.MaterialSlotNames
			);
		}
		NewCompo->RegisterComponentWithWorld(SourceComponent->GetWorld());
		
		SpawnNiagaraSystem(NewInstance);
	}

	return NewKey;
}

UStaticMeshComponent* FVfxSnapshotManagerStaticMesh::GetInstance (int64 InKey) const
{
	const FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		return Cast<UStaticMeshComponent>(Inst->AttachedComponent.Get());
	}

	return nullptr;
}

bool FVfxSnapshotManagerStaticMesh::IsInstanceReady (int64 InKey)
{
	FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		return true;
	}

	return false;
}

void FVfxSnapshotManagerStaticMesh::DestroyInstance (int64 InKey)
{
	FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		if (::IsValid(Inst->AttachedComponent))
		{
			if (Inst->AttachedComponent->IsRegistered())
			{
				Inst->AttachedComponent->UnregisterComponent();
				Inst->AttachedComponent->DestroyComponent();
			}
			Inst->AttachedComponent = nullptr;
		}

		Instances.Remove(InKey);
	}
}

UStaticMeshComponent* FVfxSnapshotManagerStaticMesh::GetSourceComponent() const
{
	return SourceComponent.Get();
}

void FVfxSnapshotManagerStaticMesh::Tick(float DeltaTime)
{
	if (bInited == false)
	{
		return;
	}

	if (FrameCounter != GFrameCounter)
	{
		FrameCounter = GFrameCounter;
		AActor* Owner = GetOwnerActor();
		UWorld* World = GetWorld();

		TArray<int64, TInlineAllocator<4>> InvalidInst;

		for (TPair<int64, FVfxSnapshotManagerInstanceStaticMesh>& CurrElem : Instances)
		{
			FVfxSnapshotManagerInstanceStaticMesh& Inst = CurrElem.Value;

			Inst.RemainDuration -= DeltaTime;
			if (Inst.bLoop == false && Inst.RemainDuration < 0.f)
			{
				InvalidInst.Add(CurrElem.Key);
				continue;
			}

			if (Inst.AttachedComponent == nullptr)
			{
				InvalidInst.Add(CurrElem.Key);
				continue;
			}

			FVfxMaterialEffectManager* MaterialEffectManager = &Inst.SnapshotComponent->MaterialEffectManager;
			MaterialEffectManager->Tick(DeltaTime);
		}

		for (int64 InstKey : InvalidInst)
		{
			DestroyInstance(InstKey);
		}
	}
}


/**************************************************************************************************
*
*   FVfxSnapshotManagerGroom
*
***/

FVfxSnapshotManagerGroom::FVfxSnapshotManagerGroom(UVfxSnapshotManagerComponent* InOwner)
	: OwnerComponent(InOwner)
	, bInited(false)
	, FrameCounter(0)
	, MaxLiveSnapshotMeshCount(16)
	, LiveSnapshotMeshCount(0)
	, InstanceIDGen(0)
{
}

FVfxSnapshotManagerGroom::~FVfxSnapshotManagerGroom()
{
}

void FVfxSnapshotManagerGroom::Reinitialize ()
{
	UGroomComponent* Old = SourceComponent;
	Uninitialize();
	FrameCounter = 0;
	Initialize(Old);
}

void FVfxSnapshotManagerGroom::Uninitialize ()
{
	TArray<int64, TInlineAllocator<8>> InstanceKeys;
	Instances.GetKeys(InstanceKeys);

	for (int64& CurrKey : InstanceKeys)
	{
		DestroyInstance(CurrKey);
	}
	Instances.Reset();

	SourceComponent = nullptr;

	bInited = false;
}

bool FVfxSnapshotManagerGroom::Initialize(UGroomComponent* InSrcComponent)
{
	if (bInited)
	{
		return true;
	}
	check(IsInGameThread());

	UGroomAsset* Mesh = InSrcComponent->GroomAsset;
	if (Mesh == nullptr)
	{
		return false;
	}

	SourceComponent = InSrcComponent;
	SourceMesh = Mesh;

	bInited = true;

	return true;
}

AActor* FVfxSnapshotManagerGroom::GetOwnerActor()
{
	return OwnerComponent? OwnerComponent->GetOwner() : nullptr;
}

UWorld* FVfxSnapshotManagerGroom::GetWorld()
{
	return OwnerComponent? OwnerComponent->GetWorld() : nullptr;
}

int64 FVfxSnapshotManagerGroom::StartGenerate (
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const FVector& Center,
	const FVector& ForwardDir,
	const FVfxSnapshotCullOption& InCullShapeOption,
	const bool bProjectUV,
	const float ProjectScale,
	const float ProjectRollFactor,
	const bool bDebugDraw,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration,
	UNiagaraSystem* InNiagaraSystem,
	const bool bDiscardBackFace,
	const float DiscardBackFaceDotLimit,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}

	UGroomAsset* SrcMesh = SourceComponent->GroomAsset;
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerGroom::StartGenerate failed. Mesh Changed"));
		return -1;
	}
	
	if (bDebugDraw)
	{
		if (InCullShapeOption.Shape == EVfxSnapshotCullShape::Box)
		{
			FRotator SphereRot = ForwardDir.Rotation();
			SphereRot.Roll += ProjectRollFactor;

			DrawDebugBox(GetWorld(), Center,  InCullShapeOption.BoxExtent, SphereRot.Quaternion(), FColor::Green, false, 3, SDPG_World);
		}
		else
		{
			DrawDebugSphere(GetWorld(), Center, InCullShapeOption.SphereRadius, 16, FColor::Green, false, 3, SDPG_World);
		}
	}
	
	UE_LOG(LogVfxGeneral, Log, TEXT("FVfxSnapshotManagerGroom::StartGenerate failed. Not yet supported"));
	return -1;
}

int64 FVfxSnapshotManagerGroom::StartWithSameMesh(
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration
)
{
	if (bInited == false)
	{
		return -1;
	}

	if (Instances.Num() > MaxLiveSnapshotMeshCount)
	{
		return -1;
	}

	UGroomAsset* SrcMesh = SourceComponent->GroomAsset;
	check(SrcMesh);

	if (SrcMesh != SourceMesh)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("FVfxSnapshotManagerGroom::StartWithSameMesh failed. Mesh Changed"));
		return -1;
	}
	
	int64 NewKey = ++InstanceIDGen;

	FVfxSnapshotManagerInstanceGroom& NewInstance = Instances.Add(NewKey);
	NewInstance.Materials = InMaterials;
	NewInstance.bLoop = InDuration <= 0.f;
	NewInstance.RemainDuration = InDuration;
	NewInstance.MaterialParams = InMaterialParamData;
	NewInstance.bOverrideMaterialParamDuration = bOverrideMaterialParamDuration;
	NewInstance.NiagaraSystem = nullptr;
	NewInstance.NiagaraSpawnStage = FVfxSnapshotManagerInstance::ENiagaraSpawnStage::NotRequired;
	NewInstance.MaterialSlotNames = InTargetMaterialSlotNames;
	
	AActor* OwnerActor = SourceComponent->GetOwner();

	FName UniqueName = sSnapshotUtil_ComponentUniqueNameGen.GetUniqueName();

	UVfxSnapshotGroomComponent* NewCompo = NewObject<UVfxSnapshotGroomComponent>(OwnerActor, UniqueName, RF_Transient);
	FVfxMaterialEffectManager* MaterialEffectManager = &NewCompo->MaterialEffectManager;

	if (NewCompo)
	{
		NewCompo->ResetRelativeTransform();
		NewCompo->EmptyOverrideMaterials();
		NewCompo->DepthPriorityGroup = ESceneDepthPriorityGroup::SDPG_Foreground;

		NewCompo->SetAbsolute(false, false, false);
		NewCompo->AttachToComponent(SourceComponent, FAttachmentTransformRules::KeepRelativeTransform);
		NewCompo->GroomAsset = SrcMesh;
		NewCompo->SourceComponent = SourceComponent;
		NewInstance.AttachedComponent = NewCompo;
		NewInstance.SnapshotComponent = NewCompo;

		for (int32 MatIdx = 0; MatIdx < NewInstance.Materials.Num(); ++MatIdx)
		{
			UMaterialInterface* MatInst = *(NewInstance.Materials.GetData() + MatIdx);
			NewCompo->SetMaterial(MatIdx, MatInst);
		}

		if (UVfxMaterialParamsData* MaterialParams = NewInstance.MaterialParams.Get())
		{
			MaterialEffectManager->Initialize(NewCompo, NewCompo);
			MaterialEffectManager->CreateEffect(
				MaterialParams,
				0,
				NewInstance.bOverrideMaterialParamDuration,
				NewInstance.bOverrideMaterialParamDuration? NewInstance.RemainDuration : 0.f,
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				0.f,		 // if NewInstance.bOverrideMaterialParamDuration == true, Control Fade in/out by curve
				NewInstance.bLoop,
				NewInstance.MaterialSlotNames
			);
		}
		NewCompo->RegisterComponentWithWorld(SourceComponent->GetWorld());
	}

	return NewKey;
}

UGroomComponent* FVfxSnapshotManagerGroom::GetInstance (int64 InKey) const
{
	const FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		return Cast<UGroomComponent>(Inst->AttachedComponent.Get());
	}

	return nullptr;
}

bool FVfxSnapshotManagerGroom::IsInstanceReady (int64 InKey)
{
	FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		return true;
	}

	return false;
}

void FVfxSnapshotManagerGroom::DestroyInstance (int64 InKey)
{
	FVfxSnapshotManagerInstance* Inst = Instances.Find(InKey);
	if (Inst)
	{
		if (::IsValid(Inst->AttachedComponent))
		{
			if (Inst->AttachedComponent->IsRegistered())
			{
				Inst->AttachedComponent->UnregisterComponent();
				Inst->AttachedComponent->DestroyComponent();
			}
			Inst->AttachedComponent = nullptr;
		}

		Instances.Remove(InKey);
	}
}

UGroomComponent* FVfxSnapshotManagerGroom::GetSourceComponent() const
{
	return SourceComponent.Get();
}

void FVfxSnapshotManagerGroom::Tick(float DeltaTime)
{
	if (bInited == false)
	{
		return;
	}

	if (FrameCounter != GFrameCounter)
	{
		FrameCounter = GFrameCounter;
		AActor* Owner = GetOwnerActor();
		UWorld* World = GetWorld();

		TArray<int64, TInlineAllocator<4>> InvalidInst;

		for (TPair<int64, FVfxSnapshotManagerInstanceGroom>& CurrElem : Instances)
		{
			FVfxSnapshotManagerInstanceGroom& Inst = CurrElem.Value;

			Inst.RemainDuration -= DeltaTime;
			if (Inst.bLoop == false && Inst.RemainDuration < 0.f)
			{
				InvalidInst.Add(CurrElem.Key);
				continue;
			}

			if (Inst.AttachedComponent == nullptr)
			{
				InvalidInst.Add(CurrElem.Key);
				continue;
			}

			FVfxMaterialEffectManager* MaterialEffectManager = &Inst.SnapshotComponent->MaterialEffectManager;
			MaterialEffectManager->Tick(DeltaTime);
		}

		for (int64 InstKey : InvalidInst)
		{
			DestroyInstance(InstKey);
		}
	}
}


/**************************************************************************************************
*
*   UVfxSnapshotManagerComponent
*
***/

UVfxSnapshotManagerComponent::UVfxSnapshotManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SourceMeshType(EVfxSnapshotSourceMeshType::MAX)
	, SourceMeshComponent(nullptr)
	, SKManager(this)
	, GroomManager(this)
{
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PostPhysics;
	PrimaryComponentTick.EndTickGroup = ETickingGroup::TG_PostPhysics;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 1.f / 60.f;
}

UVfxSnapshotManagerComponent::~UVfxSnapshotManagerComponent ()
{
	
}

void UVfxSnapshotManagerComponent::Uninitialize ()
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		SKManager.Uninitialize();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		SMManager.Uninitialize();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		GroomManager.Uninitialize();
	}
	SourceMeshType = EVfxSnapshotSourceMeshType::MAX;
}

void UVfxSnapshotManagerComponent::Reinitialize ()
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		SKManager.Reinitialize();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		SMManager.Reinitialize();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		GroomManager.Reinitialize();
	}
}

bool UVfxSnapshotManagerComponent::Initialize (UMeshComponent* InSrcComponent)
{
	if (USkeletalMeshComponent* SKComp = Cast<USkeletalMeshComponent>(InSrcComponent))
	{
		SourceMeshType = EVfxSnapshotSourceMeshType::SkeletalMesh;
		SourceMeshComponent = InSrcComponent;
		return SKManager.Initialize(SKComp);
	}
	else if (UStaticMeshComponent* SMComp = Cast<UStaticMeshComponent>(InSrcComponent))
	{
		SourceMeshType = EVfxSnapshotSourceMeshType::StaticMesh;
		SourceMeshComponent = InSrcComponent;
		return SMManager.Initialize(SMComp);
	}
	else if (UGroomComponent* GroomComp = Cast<UGroomComponent>(InSrcComponent))
	{
		SourceMeshType = EVfxSnapshotSourceMeshType::Groom;
		SourceMeshComponent = InSrcComponent;
		return GroomManager.Initialize(GroomComp);
	}

	return false;
}

int64 UVfxSnapshotManagerComponent::StartGenerate (
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const FVector& Center,
	const FVector& ForwardDir,
	const FVfxSnapshotCullOption& InCullShapeOption,
	const bool bProjectUV,
	const float ProjectScale,
	const float ProjectRollFactor,
	const bool bDebugDraw,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration,
	UNiagaraSystem* InNiagaraSystem,
	const bool bDiscardBackFace,
	const float DiscardBackFaceDotLimit,
	const bool bBuildParticleSamplingRegionByUVRange,
	const FName ParticleSamplingRegionName,
	const FVector2f& ParticleSamplingRegionByUV_X,
	const FVector2f& ParticleSamplingRegionByUV_Y
)
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.StartGenerate(
			InMaterials, InDuration, Center, ForwardDir, InCullShapeOption, bProjectUV, ProjectScale, ProjectRollFactor,
			bDebugDraw, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration, InNiagaraSystem,
			bDiscardBackFace, DiscardBackFaceDotLimit, bBuildParticleSamplingRegionByUVRange,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y
		);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.StartGenerate(
			InMaterials, InDuration, Center, ForwardDir, InCullShapeOption, bProjectUV, ProjectScale, ProjectRollFactor,
			bDebugDraw, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration, InNiagaraSystem,
			bDiscardBackFace, DiscardBackFaceDotLimit, bBuildParticleSamplingRegionByUVRange,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y
		);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.StartGenerate(
			InMaterials, InDuration, Center, ForwardDir, InCullShapeOption, bProjectUV, ProjectScale, ProjectRollFactor,
			bDebugDraw, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration, InNiagaraSystem,
			bDiscardBackFace, DiscardBackFaceDotLimit, bBuildParticleSamplingRegionByUVRange,
			ParticleSamplingRegionName, ParticleSamplingRegionByUV_X, ParticleSamplingRegionByUV_Y
		);
	}
	
	return -1;
}

int64 UVfxSnapshotManagerComponent::StartWithSameMesh(
	const TArray<UMaterialInterface*>& InMaterials,
	const float InDuration,
	const TArray<FName>& InTargetMaterialSlotNames,
	UVfxMaterialParamsData* InMaterialParamData,
	const bool bOverrideMaterialParamDuration
)
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.StartWithSameMesh(
			InMaterials, InDuration, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration
		);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.StartWithSameMesh(
			InMaterials, InDuration, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration
		);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.StartWithSameMesh(
			InMaterials, InDuration, InTargetMaterialSlotNames, InMaterialParamData, bOverrideMaterialParamDuration
		);
	}
	
	return -1;
}

UMeshComponent* UVfxSnapshotManagerComponent::GetInstance(int64 InKey) const
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.GetInstance(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.GetInstance(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.GetInstance(InKey);
	}

	return nullptr;
}

bool UVfxSnapshotManagerComponent::IsInstanceReady(int64 InKey)
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.IsInstanceReady(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.IsInstanceReady(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.IsInstanceReady(InKey);
	}

	return false;	
}

void UVfxSnapshotManagerComponent::DestroyInstance (int64 InKey)
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.DestroyInstance(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.DestroyInstance(InKey);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.DestroyInstance(InKey);
	}

	return;	
}

UMeshComponent* UVfxSnapshotManagerComponent::GetSourceComponent() const
{
	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		return SKManager.GetSourceComponent();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		return SMManager.GetSourceComponent();
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		return GroomManager.GetSourceComponent();
	}
	
	return nullptr;
}

void UVfxSnapshotManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SourceMeshType == EVfxSnapshotSourceMeshType::SkeletalMesh)
	{
		SKManager.Tick(DeltaTime);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::StaticMesh)
	{
		SMManager.Tick(DeltaTime);
	}
	else if (SourceMeshType == EVfxSnapshotSourceMeshType::Groom)
	{
		GroomManager.Tick(DeltaTime);
	}
}

void UVfxSnapshotManagerComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Uninitialize();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UVfxSnapshotManagerComponent::OnUnregister()
{
	Uninitialize();
	
	Super::OnUnregister();
}


/**************************************************************************************************
*
*	FVfxSnapshotManagerOrganizer
*
***/

UVfxSnapshotManagerComponent* FVfxSnapshotManagerOrganizer::Find(FName InName)
{
	if (TObjectPtr<UVfxSnapshotManagerComponent>* Exist = NameToManager.Find(InName))
	{
		return Exist->Get();
	}

	return nullptr;
}

UVfxSnapshotManagerComponent* FVfxSnapshotManagerOrganizer::Find(UMeshComponent* InMeshComponent)
{
	if (TObjectPtr<UVfxSnapshotManagerComponent>* Exist = MeshToManager.Find(InMeshComponent))
	{
		return Exist->Get();
	}

	return nullptr;
}

void FVfxSnapshotManagerOrganizer::Remove(FName InName)
{
	UVfxSnapshotManagerComponent* Comp = nullptr;
	if (TObjectPtr<UVfxSnapshotManagerComponent>* Exist = NameToManager.Find(InName))
	{
		Comp = Exist->Get();
		NameToManager.Remove(InName);
	}

	if (Comp)
	{
		for (TPair<TObjectPtr<UMeshComponent>, TObjectPtr<UVfxSnapshotManagerComponent>>& Curr : MeshToManager)
		{
			if (Curr.Value == Comp)
			{
				MeshToManager.Remove(Curr.Key);
				break;
			}
		}
		Managers.Remove(Comp);
		Names.Remove(InName);

		Comp->Uninitialize();
		Comp->DestroyComponent();
	}
}

void FVfxSnapshotManagerOrganizer::Remove(UMeshComponent* InMeshComponent)
{
	UVfxSnapshotManagerComponent* Comp = nullptr;
	if (TObjectPtr<UVfxSnapshotManagerComponent>* Exist = MeshToManager.Find(InMeshComponent))
	{
		Comp = Exist->Get();
		MeshToManager.Remove(InMeshComponent);
	}

	if (Comp)
	{
		for (TPair<FName, TObjectPtr<UVfxSnapshotManagerComponent>>& Curr : NameToManager)
		{
			if (Curr.Value == Comp)
			{
				NameToManager.Remove(Curr.Key);
				Names.Remove(Curr.Key);
				break;
			}
		}
		Managers.Remove(Comp);

		Comp->Uninitialize();
		Comp->DestroyComponent();
	}
}

void FVfxSnapshotManagerOrganizer::Clear()
{
	for (TObjectPtr<UVfxSnapshotManagerComponent>& Curr : Managers)
	{
		Curr->Uninitialize();
		Curr->DestroyComponent();
	}

	Managers.Reset();
	NameToManager.Reset();
	MeshToManager.Reset();
	Names.Reset();
}

UVfxSnapshotManagerComponent* FVfxSnapshotManagerOrganizer::FindOrCreate(FName InName, UMeshComponent* InMeshComponent)
{
	if (TObjectPtr<UVfxSnapshotManagerComponent>* FromMeshExist = MeshToManager.Find(InMeshComponent))
	{
		return FromMeshExist->Get();
	}

	if (TObjectPtr<UVfxSnapshotManagerComponent>* FromNameExist = NameToManager.Find(InName))
	{
		checkf(false, TEXT("FVfxSnapshotManagerOrganizer::FindOrCreate. This should not happened"));
		return FromNameExist->Get();
	}

	AActor* OwnerPtr = Owner.Get();
	if (OwnerPtr == nullptr)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("UVfxSnapshotManagerComponent::FindOrCreate failed. Owner is not valid"));
		return nullptr;
	}

	static FVfxUniqueNameGenerator sComponentUniqueNameGen = FVfxUniqueNameGenerator(TEXT("SnapshotManager_Component"));

	FName NewName = sComponentUniqueNameGen.GetUniqueName();
	UVfxSnapshotManagerComponent* Comp = NewObject<UVfxSnapshotManagerComponent>(OwnerPtr, NewName, RF_Transient);
	if (Comp == nullptr)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("UVfxSnapshotManagerComponent::FindOrCreate failed. component creation failed"));
		return nullptr;
	}

	Comp->RegisterComponentWithWorld(OwnerPtr->GetWorld());
	if (false == Comp->Initialize(InMeshComponent))
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("UVfxSnapshotManagerComponent::FindOrCreate failed."));
		Comp->ConditionalBeginDestroy();
		return nullptr;
	}

	MeshToManager.Add(InMeshComponent, Comp);
	NameToManager.Add(InName, Comp);
	Managers.Add(Comp);
	Names.Add(InName);

	return Comp;
}	
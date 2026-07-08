
#pragma once

#include "EngineMinimal.h"
#include "Math/GenericOctree.h"

#include "VfxSnapshotManagerComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedMeshComponent;
class UStaticMeshComponent;
class UVfxSnapshotStaticMeshComponent;
class UVfxSnapshotGroomComponent;
class UMeshComponent;
class UGroomComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UGroomAsset;
class UVfxSnapshotManagerComponent;
class FVfxSnapshotManagerCullingTask;
class FVfxSnapshotManagerOctreeRefreshTask;
class UVfxMaterialParamsData;

struct FVfxSnapshotSourceMeshBoneOBB
{
	uint32 BoneIndex;

	FBoxCenterAndExtent BoxCenterAndExtent;
};

struct FVfxSnapshotSourceMeshBoneOBBOctreeSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	/**
	* Get the bounding box of the provided octree element. In this case, the box
	* is merely the point specified by the element.
	*
	* @param	Element	Octree element to get the bounding box for
	*
	* @return	Bounding box of the provided octree element
	*/
	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FVfxSnapshotSourceMeshBoneOBB& Element)
	{
		return Element.BoxCenterAndExtent;
	}


	/**
	* Determine if two octree elements are equal
	*
	* @param	A	First octree element to check
	* @param	B	Second octree element to check
	*
	* @return	true if both octree elements are equal, false if they are not
	*/
	FORCEINLINE static bool AreElementsEqual(const FVfxSnapshotSourceMeshBoneOBB& A, const FVfxSnapshotSourceMeshBoneOBB& B)
	{
		return (A.BoneIndex == B.BoneIndex);
	}

	/** Ignored for this implementation */
	FORCEINLINE static void SetElementId(const FVfxSnapshotSourceMeshBoneOBB& Element, FOctreeElementId2 Id)
	{

	}
};
typedef TOctree2<FVfxSnapshotSourceMeshBoneOBB, FVfxSnapshotSourceMeshBoneOBBOctreeSemantics> FVfxSnapshotSourceMeshBoneOBBOctree;


/**************************************************************************************************
*
*	FVfxSnapshotManagerInstance
*
***/

USTRUCT()
struct FVfxSnapshotManagerInstance
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Transient)
	TArray<UMaterialInterface*> Materials;

	UPROPERTY(Transient)
	TArray<FName> MaterialSlotNames;

	UPROPERTY(Transient)
	TObjectPtr<UVfxMaterialParamsData> MaterialParams;

	bool bOverrideMaterialParamDuration;
	
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> NiagaraSystem;
	
	enum class ENiagaraSpawnStage
	{
		NotRequired,
		Waiting,
		Done
	};
	ENiagaraSpawnStage NiagaraSpawnStage;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> AttachedComponent;

	bool bLoop;
	float RemainDuration;

	FVfxSnapshotManagerInstance ()
		: MaterialParams(nullptr)
		, bOverrideMaterialParamDuration(false)
		, NiagaraSystem(nullptr)
		, NiagaraSpawnStage(ENiagaraSpawnStage::NotRequired)
		, AttachedComponent(nullptr)
		, bLoop(false)
		, RemainDuration(3)
	{
	}
};

USTRUCT()
struct FVfxSnapshotManagerInstanceSkeletalMesh : public FVfxSnapshotManagerInstance
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> Generated;
	
	FAsyncTask<FVfxSnapshotManagerCullingTask>* AsyncTask;
	uint64 AsyncTaskStartFrameCounter;

	FVfxSnapshotManagerInstanceSkeletalMesh ()
		: FVfxSnapshotManagerInstance()
		, Generated(nullptr)
		, AsyncTask(nullptr)
		, AsyncTaskStartFrameCounter(0)
	{
	}
};

USTRUCT()
struct FVfxSnapshotManagerInstanceStaticMesh : public FVfxSnapshotManagerInstance
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(VisibleAnywhere, Transient)
	TObjectPtr<UVfxSnapshotStaticMeshComponent> SnapshotComponent;

	FVfxSnapshotManagerInstanceStaticMesh ()
		: FVfxSnapshotManagerInstance()
		, SnapshotComponent(nullptr)
	{
	}
};

USTRUCT()
struct FVfxSnapshotManagerInstanceGroom : public FVfxSnapshotManagerInstance
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(VisibleAnywhere, Transient)
	TObjectPtr<UVfxSnapshotGroomComponent> SnapshotComponent;

	FVfxSnapshotManagerInstanceGroom ()
		: FVfxSnapshotManagerInstance()
		, SnapshotComponent(nullptr)
	{
	}
};

UENUM(BlueprintType)
enum class EVfxSnapshotCullShape : uint8
{
	Sphere,
	Box,

	MAX UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FVfxSnapshotCullOption
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EVfxSnapshotCullShape Shape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SphereRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector BoxExtent;

	FVfxSnapshotCullOption ()
		: Shape(EVfxSnapshotCullShape::Sphere)
		, SphereRadius(50)
		, BoxExtent(25,25,25)
	{
	}
};

UENUM(BlueprintType)
enum class EVfxSnapshotSourceMeshType : uint8
{
	StaticMesh,
	SkeletalMesh,
	Groom,

	MAX UMETA(Hidden)
};


/**************************************************************************************************
*
*   FVfxSnapshotManagerSkeletalMesh
*
***/

USTRUCT()
struct VISUALEFFECTS_API FVfxSnapshotManagerSkeletalMesh
{
	GENERATED_BODY()
	
	friend class FVfxSnapshotManagerOctreeRefreshTask;
	friend class FVfxSnapshotManagerCullingTask;
	friend class FVisibleMeshToAnimMeshBoneRemapper;

public:
	FVfxSnapshotManagerSkeletalMesh(UVfxSnapshotManagerComponent* InOwner = nullptr);
	~FVfxSnapshotManagerSkeletalMesh ();

	bool Initialize (USkeletalMeshComponent* InSrcComponent);
	void Reinitialize ();
	void Uninitialize ();

	int64 StartWithSameMesh (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,   /** <= 0 means loop */
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
	);

	int64 StartGenerate (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,  /** <= 0 means loop */
		const FVector& Center,
		const FVector& ForwardDir,
		const FVfxSnapshotCullOption& InCullShapeOption,
		const bool bProjectUV,
		const float ProjectScale,
		const float ProjectRollFactor,
		const bool bDebugDraw,
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true, // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
		UNiagaraSystem* InNiagaraSystem = nullptr, /** Optional Niagara FX attached to generated mesh */
		const bool bDiscardBackFace = false, /** Experimental */
		const float DiscardBackFaceDotLimit = 0.1,
		const bool bBuildParticleSamplingRegionByUVRange = false,
		const FName ParticleSamplingRegionName = NAME_None,
		const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(),
		const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f()
	);

	USkinnedMeshComponent* GetInstance (int64 InKey) const;

	bool IsInstanceReady (int64 InKey);

	void DestroyInstance (int64 InKey);

	USkeletalMeshComponent* GetSourceComponent() const;

	void Tick(float DeltaTime);

private:
	AActor* GetOwnerActor();
	UWorld* GetWorld();
	void RefreshObbOctree ();

	void ClearAsyncOctreeRefreshTask();

	inline void SpawnNiagaraSystem (FVfxSnapshotManagerInstanceSkeletalMesh& InInst);

private:
	UPROPERTY(Transient)
	TObjectPtr<UVfxSnapshotManagerComponent> OwnerComponent;

	bool bInited;
	uint64 FrameCounter;
	int32 MaxLiveSnapshotMeshCount;
	int32 LiveSnapshotMeshCount;
	int32 VertexCount;
	
	FAsyncTask<FVfxSnapshotManagerOctreeRefreshTask>* OctreeRefreshTask;
	FVfxSnapshotSourceMeshBoneOBBOctree* Octree;
	TPimplPtr<FRWLock> OctreeLock;

	struct FBoneOBB
	{
		FName BoneName;
		int32 MeshBoneIndex;
		int32 AnimBoneIndex;

		FBox3f LocalBoundBox;

		FBoxCenterAndExtent ComponentSpaceBound;

		TBitArray<> InfluencingVertices;
	};

	TArray<FBoneOBB> Bones;
	TArray<FMatrix44f> ComponentSpaceTransformCache;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SourceComponent;
	
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> SourceMesh;

	UPROPERTY(Transient)
	TMap<int64, FVfxSnapshotManagerInstanceSkeletalMesh> Instances;
};

template<>
struct TStructOpsTypeTraits<FVfxSnapshotManagerSkeletalMesh> : public TStructOpsTypeTraitsBase2<FVfxSnapshotManagerSkeletalMesh>
{
	enum
	{
		WithCopy = false,
	};
};


/**************************************************************************************************
*
*   FVfxSnapshotManagerStaticMesh
*
***/

USTRUCT()
struct VISUALEFFECTS_API FVfxSnapshotManagerStaticMesh
{
	GENERATED_BODY()

public:
	FVfxSnapshotManagerStaticMesh(UVfxSnapshotManagerComponent* InOwner = nullptr);
	~FVfxSnapshotManagerStaticMesh ();

	bool Initialize (UStaticMeshComponent* InSrcComponent);
	void Reinitialize ();
	void Uninitialize ();

	int64 StartWithSameMesh (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,   /** <= 0 means loop */
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
	);

	int64 StartGenerate (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,  /** <= 0 means loop */
		const FVector& Center,
		const FVector& ForwardDir,
		const FVfxSnapshotCullOption& InCullShapeOption,
		const bool bProjectUV,
		const float ProjectScale,
		const float ProjectRollFactor,
		const bool bDebugDraw,
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true, // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
		UNiagaraSystem* InNiagaraSystem = nullptr, /** Optional Niagara FX attached to generated mesh */
		const bool bDiscardBackFace = false, /** Experimental */
		const float DiscardBackFaceDotLimit = 0.1,
		const bool bBuildParticleSamplingRegionByUVRange = false,
		const FName ParticleSamplingRegionName = NAME_None,
		const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(),
		const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f()
	);

	UStaticMeshComponent* GetInstance (int64 InKey) const;

	bool IsInstanceReady (int64 InKey);

	void DestroyInstance (int64 InKey);

	UStaticMeshComponent* GetSourceComponent() const;

	void Tick(float DeltaTime);

private:
	AActor* GetOwnerActor();
	UWorld* GetWorld();

	inline void SpawnNiagaraSystem (FVfxSnapshotManagerInstanceStaticMesh& InInst);

private:
	UPROPERTY(Transient)
	TObjectPtr<UVfxSnapshotManagerComponent> OwnerComponent;

	bool bInited;
	uint64 FrameCounter;
	int32 MaxLiveSnapshotMeshCount;
	int32 LiveSnapshotMeshCount;

	int64 InstanceIDGen;
	
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SourceComponent;
	
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SourceMesh;

	UPROPERTY(Transient)
	TMap<int64, FVfxSnapshotManagerInstanceStaticMesh> Instances;
};

template<>
struct TStructOpsTypeTraits<FVfxSnapshotManagerStaticMesh> : public TStructOpsTypeTraitsBase2<FVfxSnapshotManagerStaticMesh>
{
	enum
	{
		WithCopy = false,
	};
};


/**************************************************************************************************
*
*   FVfxSnapshotManagerGroom
*
***/

USTRUCT()
struct VISUALEFFECTS_API FVfxSnapshotManagerGroom
{
	GENERATED_BODY()

public:
	FVfxSnapshotManagerGroom(UVfxSnapshotManagerComponent* InOwner = nullptr);
	~FVfxSnapshotManagerGroom ();

	bool Initialize (UGroomComponent* InSrcComponent);
	void Reinitialize ();
	void Uninitialize ();

	int64 StartWithSameMesh (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,   /** <= 0 means loop */
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
	);

	int64 StartGenerate (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,  /** <= 0 means loop */
		const FVector& Center,
		const FVector& ForwardDir,
		const FVfxSnapshotCullOption& InCullShapeOption,
		const bool bProjectUV,
		const float ProjectScale,
		const float ProjectRollFactor,
		const bool bDebugDraw,
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true, // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
		UNiagaraSystem* InNiagaraSystem = nullptr, /** Optional Niagara FX attached to generated mesh */
		const bool bDiscardBackFace = false, /** Experimental */
		const float DiscardBackFaceDotLimit = 0.1,
		const bool bBuildParticleSamplingRegionByUVRange = false,
		const FName ParticleSamplingRegionName = NAME_None,
		const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(),
		const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f()
	);

	UGroomComponent* GetInstance (int64 InKey) const;

	bool IsInstanceReady (int64 InKey);

	void DestroyInstance (int64 InKey);

	UGroomComponent* GetSourceComponent() const;

	void Tick(float DeltaTime);

private:
	AActor* GetOwnerActor();
	UWorld* GetWorld();

private:
	UPROPERTY(Transient)
	TObjectPtr<UVfxSnapshotManagerComponent> OwnerComponent;

	bool bInited;
	uint64 FrameCounter;
	int32 MaxLiveSnapshotMeshCount;
	int32 LiveSnapshotMeshCount;

	int64 InstanceIDGen;
	
	UPROPERTY(Transient)
	TObjectPtr<UGroomComponent> SourceComponent;
	
	UPROPERTY(Transient)
	TObjectPtr<UGroomAsset> SourceMesh;

	UPROPERTY(Transient)
	TMap<int64, FVfxSnapshotManagerInstanceGroom> Instances;
};

template<>
struct TStructOpsTypeTraits<FVfxSnapshotManagerGroom> : public TStructOpsTypeTraitsBase2<FVfxSnapshotManagerGroom>
{
	enum
	{
		WithCopy = false,
	};
};


/**************************************************************************************************
*
*   UVfxSnapshotManagerComponent
*
***/

UCLASS(meta=(BlueprintSpawnableComponent))
class VISUALEFFECTS_API UVfxSnapshotManagerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVfxSnapshotManagerComponent(const FObjectInitializer& ObjectInitializer);
	virtual ~UVfxSnapshotManagerComponent ();

	UFUNCTION(BlueprintCallable)
	bool Initialize (UMeshComponent* InSrcComponent);

	UFUNCTION(BlueprintCallable)
	void Reinitialize ();

	UFUNCTION(BlueprintCallable)
	void Uninitialize ();

	UFUNCTION(BlueprintCallable)
	int64 StartWithSameMesh (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,   /** <= 0 means loop */
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
	);

	UFUNCTION(BlueprintCallable)
	int64 StartGenerate (
		const TArray<UMaterialInterface*>& InMaterials,
		const float InDuration,  /** <= 0 means loop */
		const FVector& Center,
		const FVector& ForwardDir,
		const FVfxSnapshotCullOption& InCullShapeOption,
		const bool bProjectUV,
		const float ProjectScale,
		const float ProjectRollFactor,
		const bool bDebugDraw,
		const TArray<FName>& InTargetMaterialSlotNames,
		UVfxMaterialParamsData* InMaterialParamData = nullptr,
		const bool bOverrideMaterialParamDuration = true, // if bOverrideMaterialParamDuration == true, control Fade in/out through curve
		UNiagaraSystem* InNiagaraSystem = nullptr, /** Optional Niagara FX attached to generated mesh */
		const bool bDiscardBackFace = false, /** Experimental */
		const float DiscardBackFaceDotLimit = 0.1,
		const bool bBuildParticleSamplingRegionByUVRange = false,
		const FName ParticleSamplingRegionName = NAME_None,
		const FVector2f& ParticleSamplingRegionByUV_X = FVector2f(),
		const FVector2f& ParticleSamplingRegionByUV_Y = FVector2f()
	);

	UFUNCTION(BlueprintCallable)
	UMeshComponent* GetInstance (int64 InKey) const;

	UFUNCTION(BlueprintCallable)
	bool IsInstanceReady (int64 InKey);

	UFUNCTION(BlueprintCallable)
	void DestroyInstance (int64 InKey);

	UFUNCTION(BlueprintCallable)
	UMeshComponent* GetSourceComponent() const;

	// ActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnUnregister() override;
	// ~ActorComponent

private:
	UPROPERTY(Transient)
	EVfxSnapshotSourceMeshType SourceMeshType;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> SourceMeshComponent;

	UPROPERTY(Transient)
	FVfxSnapshotManagerSkeletalMesh SKManager;

	UPROPERTY(Transient)
	FVfxSnapshotManagerStaticMesh SMManager;

	UPROPERTY(Transient)
	FVfxSnapshotManagerGroom GroomManager;
};


/**************************************************************************************************
*
*	FVfxSnapshotManagerOrganizer
*
***/

USTRUCT()
struct VISUALEFFECTS_API FVfxSnapshotManagerOrganizer
{
	GENERATED_USTRUCT_BODY()

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UVfxSnapshotManagerComponent>> NameToManager;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMeshComponent>, TObjectPtr<UVfxSnapshotManagerComponent>> MeshToManager;
	
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVfxSnapshotManagerComponent>> Managers;
	
	UPROPERTY(Transient)
	TArray<FName> Names;

	TWeakObjectPtr<AActor> Owner;

public:
	UVfxSnapshotManagerComponent* Find(FName InName);
	UVfxSnapshotManagerComponent* Find(UMeshComponent* InMeshComponent);
	UVfxSnapshotManagerComponent* FindOrCreate(FName InName, UMeshComponent* InMeshComponent);
	void Remove(FName InName);
	void Remove(UMeshComponent* USkeletalMeshComponent);
	void Clear();
	const TArray<TObjectPtr<UVfxSnapshotManagerComponent>>& GetManagers() const { return Managers; }
	const TArray<FName>& GetNames() const { return Names; }

	FVfxSnapshotManagerOrganizer ()
		: Owner(nullptr)
	{
	}
	explicit FVfxSnapshotManagerOrganizer (AActor* InOwner)
		: Owner(InOwner)
	{
	}
};
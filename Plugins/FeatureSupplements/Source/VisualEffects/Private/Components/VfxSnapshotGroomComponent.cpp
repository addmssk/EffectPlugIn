#include "Components/VfxSnapshotGroomComponent.h"

#include "HairStrandsInterface.h"
#include "HairStrandsVertexFactory.h"
#include "HairCardsVertexFactory.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"


/**************************************************************************************************
*
*   FSnapshotGroomSceneProxy
* 
*	Copyed and Modified from 'FHairStrandsSceneProxy' at GroomComponent.cpp (UE 5.7.3)
*   
*
***/

class FSnapshotGroomSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	
	inline static int32 GetMaterialIndexWithFallback(int32 SlotIndex)
	{
		// Default policy: if no slot index has been bound, fallback on slot 0. If there is no
		// slot, the material will fallback on the default material.
		return SlotIndex != INDEX_NONE ? SlotIndex : 0;
	}
	
	inline static uint32 GetPointToVertexCount()
	{
		return GetHairStrandsUsesTriangleStrips() ? HAIR_POINT_TO_VERTEX_FOR_TRISTRP : HAIR_POINT_TO_VERTEX_FOR_TRILIST;
	}

	inline static EPrimitiveType GetPrimitiveType(EHairGeometryType In)
	{
		return (In == EHairGeometryType::Strands && GetHairStrandsUsesTriangleStrips()) ? PT_TriangleStrip : PT_TriangleList;
	}

	FRenderCurveResourceData* CurveResourceData = nullptr;

	virtual FRenderCurveResourceData* GetRenderCurveResourceData() override
	{
		return CurveResourceData;
	}

	FSnapshotGroomSceneProxy(UVfxSnapshotGroomComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		// Forcing primitive uniform as we don't support robustly GPU scene data
		bVFRequiresPrimitiveUniformBuffer = true;
		bCastDeepShadow = true;

		UGroomComponent* GroomComponet = Component->SourceComponent.Get();
		check(GroomComponet);
		UGroomAsset* GroomAsset = GroomComponet->GroomAsset;
		check(GroomAsset);
		check(GroomAsset->GetNumHairGroups() > 0);

		const uint32 Cnt = GroomComponet->GetGroupCount();
		HairGroupInstances.Reserve(Cnt);
		for (uint32 Idx = 0; Idx < Cnt; ++Idx)
		{
			FHairGroupInstance* Inst = GroomComponet->GetGroupInstance(Idx);
			HairGroupInstances.Add(Inst);
		}

		HairGroupMaterialProxies.SetNum(HairGroupInstances.Num());

		ComponentId = GroomComponet->GetPrimitiveSceneId().PrimIDValue;
		bAlwaysHasVelocity = false;

		check(HairGroupInstances.Num());

		const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
		const EShaderPlatform ShaderPlatform = GetScene().GetShaderPlatform();

		const int32 GroupCount = GroomAsset->GetNumHairGroups();
		check(GroomAsset->GetHairGroupsPlatformData().Num() == HairGroupInstances.Num());
		for (int32 GroupIt=0; GroupIt<GroupCount; GroupIt++)
		{
			const bool bIsVisible = GroomAsset->GetHairGroupsInfo()[GroupIt].bIsVisible;

			FHairGroupPlatformData& InGroupDataNonConst = GroomAsset->GetHairGroupsPlatformData()[GroupIt];
			const FHairGroupPlatformData& InGroupData = GroomAsset->GetHairGroupsPlatformData()[GroupIt];
			FHairGroupInstance* HairInstance = HairGroupInstances[GroupIt];


			// (Experimental) For now only use the resource of the first group
			CurveResourceData = nullptr;

			// Material - Strands
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands, ShaderPlatform))
			{
				const int32 SlotIndex = GroomAsset->GetMaterialIndex(GroomAsset->GetHairGroupsRendering()[GroupIt].MaterialSlotName);
				const UMaterialInterface* Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Strands);
				HairGroupMaterialProxies[GroupIt].Strands = Material ? Material->GetRenderProxy() : nullptr;
			}

			// Material - Cards
			HairGroupMaterialProxies[GroupIt].Cards.Init(nullptr, InGroupData.Cards.LODs.Num());
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards, ShaderPlatform))
			{
				uint32 CardsLODIndex = 0;
				for (const FHairGroupPlatformData::FCards::FLOD& LOD : InGroupData.Cards.LODs)
				{
					if (LOD.IsValid())
					{
						// Material
						int32 SlotIndex = INDEX_NONE;
						for (const FHairGroupsCardsSourceDescription& Desc : GroomAsset->GetHairGroupsCards())
						{
							if (Desc.GroupIndex == GroupIt && Desc.LODIndex == CardsLODIndex)
							{
								SlotIndex = GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
								break;
							}
						}
						const UMaterialInterface* Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Cards);
						HairGroupMaterialProxies[GroupIt].Cards[CardsLODIndex] = Material ? Material->GetRenderProxy() : nullptr;
					}
					++CardsLODIndex;
				}
			}

			// Material - Meshes
			HairGroupMaterialProxies[GroupIt].Meshes.Init(nullptr, InGroupData.Meshes.LODs.Num());
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, ShaderPlatform))
			{
				uint32 MeshesLODIndex = 0;
				for (const FHairGroupPlatformData::FMeshes::FLOD& LOD : InGroupData.Meshes.LODs)
				{
					if (LOD.IsValid())
					{
						// Material
						int32 SlotIndex = INDEX_NONE;
						for (const FHairGroupsMeshesSourceDescription& Desc : Component->GroomAsset->GetHairGroupsMeshes())
						{
							if (Desc.GroupIndex == GroupIt && Desc.LODIndex == MeshesLODIndex)
							{
								SlotIndex = Component->GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
								break;
							}
						}
						const UMaterialInterface* Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Meshes);
						HairGroupMaterialProxies[GroupIt].Meshes[MeshesLODIndex] = Material ? Material->GetRenderProxy() : nullptr;
					}
					++MeshesLODIndex;
				}
			}
		}
	}

	virtual ~FSnapshotGroomSceneProxy()
	{
	}
	
	/**
	 *	Called when the rendering thread adds the proxy to the scene.
	 *	This function allows for generating renderer-side resources.
	 *	Called in the rendering thread.
	 */
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) 
	{
		FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);

		// Assume added by SrcGroomComponent
#if 0
		// Register the data to the scene
		FSceneInterface& LocalScene = GetScene();
		for (TRefCountPtr<FHairGroupInstance>& Instance : HairGroupInstances)
		{
			if (Instance->IsValid() || Instance->Strands.ClusterResource)
			{
				check(Instance->HairGroupPublicData != nullptr);
				Instance->AddRef();
				Instance->Debug.Proxy = this;
				LocalScene.AddHairStrands(Instance);
			}
		}
#endif
	}

	/**
	 *	Called when the rendering thread removes the proxy from the scene.
	 *	This function allows for removing renderer-side resources.
	 *	Called in the rendering thread.
	 */
	virtual void DestroyRenderThreadResources() override
	{
		FPrimitiveSceneProxy::DestroyRenderThreadResources();
		
		// Assume SrcGroomComponent will do
#if 0
		// Unregister the data to the scene
		FSceneInterface& LocalScene = GetScene();
		for (TRefCountPtr<FHairGroupInstance>& Instance : HairGroupInstances)
		{
			if (Instance->IsValid() || Instance->Strands.ClusterResource)
			{
				check(Instance->GetRefCount() > 0);
				LocalScene.RemoveHairStrands(Instance);
				Instance->Debug.Proxy = nullptr;
				Instance->Release();
			}
		}
#endif
	}

	virtual void OnTransformChanged(FRHICommandListBase& RHICmdList) override
	{
#if 0
		const FTransform RigidLocalToWorld = FTransform(GetLocalToWorld());
		for (TRefCountPtr<FHairGroupInstance>& Instance : HairGroupInstances)
		{
			Instance->Debug.RigidPreviousLocalToWorld = Instance->Debug.RigidCurrentLocalToWorld;
			Instance->Debug.RigidCurrentLocalToWorld = RigidLocalToWorld;
		}
#endif
	}

	FORCEINLINE bool UseProxyLocalToWorld(const FHairGroupInstance* Instance) const
	{
		return (Instance->BindingType != EHairBindingType::Skinning);
	}

#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return false; }
	virtual bool IsRayTracingStaticRelevant() const override { return false; }
	virtual bool HasRayTracingRepresentation() const override { return false; }

	virtual void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override
	{
		return;
	}
#endif

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		const EShaderPlatform Platform = ViewFamily.GetShaderPlatform();
		if (!IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform) &&
			!IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform) &&
			!IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, Platform))
		{
			return;
		}

		if (HairGroupInstances.Num() == 0)
		{
			return;
		}

		const uint32 GroupCount = HairGroupInstances.Num();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_HairStrandsSceneProxy_GetDynamicMeshElements);

		// Need information back from the rendering thread to knwo which representation to use (strands/cards/mesh)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FSceneView* View = Views[ViewIndex];
			if (View->bIsReflectionCapture)
			{
				continue;
			}

			if ((IsShadowCast(View) || IsShown(View)) && (VisibilityMap & (1 << ViewIndex)))
			{
				for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
				{
					check(HairGroupInstances[GroupIt]->GetRefCount() > 0);

					if (FMeshBatch* MeshBatch = CreateMeshBatch(ViewFamily, Collector, HairGroupInstances[GroupIt], GroupIt))
					{
						Collector.AddMesh(ViewIndex, *MeshBatch);
					}
					else
					{
						continue;
					}

				#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					// Render bounds
					RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				#endif
				}
			}
		}
	}

	FMeshBatch* CreateMeshBatch(
		const FSceneViewFamily& ViewFamily,
		FMeshElementCollector& Collector,
		const FHairGroupInstance* Instance,
		uint32 GroupIndex
	) const
	{
		const EHairGeometryType GeometryType = Instance->GeometryType;
		if (GeometryType == EHairGeometryType::NoneGeometry)
		{
			return nullptr;
		}

		check(Instance->GetRefCount());

		const int32 IntLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
		const bool bIsVisible = Instance->HairGroupPublicData->GetLODVisibility();

		const FVertexFactory* VertexFactory = nullptr;
		FIndexBuffer* IndexBuffer = nullptr;
		const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
		const ERHIFeatureLevel::Type FeatureLevel = ViewFamily.GetFeatureLevel();

		uint32 NumPrimitive = 0;
		uint32 HairVertexCount = 0;
		uint32 MaxVertexIndex = 0;
		bool bUseCulling = false;
		bool bWireframe = false;
		EPrimitiveIdMode PrimitiveIdMode = PrimID_Num;
		if (GeometryType == EHairGeometryType::Meshes)
		{
			if (!Instance->Meshes.IsValid(IntLODIndex))
			{
				return nullptr;
			}
			VertexFactory = (FVertexFactory*)Instance->Meshes.LODs[IntLODIndex].GetVertexFactory();
			check(VertexFactory);
			PrimitiveIdMode = Instance->Meshes.LODs[IntLODIndex].GetVertexFactory()->GetPrimitiveIdMode(FeatureLevel);
			HairVertexCount = Instance->Meshes.LODs[IntLODIndex].RestResource->GetPrimitiveCount() * 3;
			MaxVertexIndex = HairVertexCount;
			NumPrimitive = HairVertexCount / 3;
			IndexBuffer = &Instance->Meshes.LODs[IntLODIndex].RestResource->IndexBuffer;
			bUseCulling = false;
			if (MaterialRenderProxy == nullptr)
			{
				MaterialRenderProxy = HairGroupMaterialProxies[GroupIndex].Meshes[IntLODIndex];
			}
			bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			if (!Instance->Cards.IsValid(IntLODIndex))
			{
				return nullptr;
			}

			VertexFactory = (FVertexFactory*)Instance->Cards.LODs[IntLODIndex].GetVertexFactory();
			check(VertexFactory);
			PrimitiveIdMode = Instance->Meshes.LODs[IntLODIndex].GetVertexFactory()->GetPrimitiveIdMode(FeatureLevel);
			HairVertexCount = Instance->Cards.LODs[IntLODIndex].RestResource->GetPrimitiveCount() * 3;
			MaxVertexIndex = HairVertexCount;
			NumPrimitive = HairVertexCount / 3;
			IndexBuffer = &Instance->Cards.LODs[IntLODIndex].RestResource->RestIndexBuffer;
			bUseCulling = false;
			if (MaterialRenderProxy == nullptr)
			{
				MaterialRenderProxy = HairGroupMaterialProxies[GroupIndex].Cards[IntLODIndex];
			}
			bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		}
		else // if (GeometryType == EHairGeometryType::Strands)
		{
			VertexFactory = (FVertexFactory*)Instance->Strands.VertexFactory;
			PrimitiveIdMode = Instance->Strands.VertexFactory->GetPrimitiveIdMode(FeatureLevel);
			HairVertexCount = Instance->HairGroupPublicData->GetActiveStrandsPointCount();
			MaxVertexIndex = HairVertexCount * GetPointToVertexCount();
			bUseCulling = Instance->Strands.bCullingEnable;
			NumPrimitive = bUseCulling ? 0 : HairVertexCount * HAIR_POINT_TO_TRIANGLE;
			if (MaterialRenderProxy == nullptr)
			{
				MaterialRenderProxy = HairGroupMaterialProxies[GroupIndex].Strands;
			}
			bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		}

		if (MaterialRenderProxy == nullptr || !bIsVisible)
		{
			return nullptr;
		}

		// Invalid primitive setup. This can happens when the (procedural) resources are not ready.
		if (NumPrimitive == 0 && !bUseCulling)
		{
			return nullptr;
		}

		if (bWireframe)
		{
			FMaterialRenderProxy* ColoredMaterialRenderProxy = new FColoredMaterialRenderProxy( GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL, FLinearColor(1.f, 0.5f, 0.f));
			Collector.RegisterOneFrameMaterialProxy(ColoredMaterialRenderProxy);
			MaterialRenderProxy = ColoredMaterialRenderProxy;
		}

		// Draw the mesh.
		FMeshBatch& Mesh = Collector.AllocateMesh();

		const bool bUseCardsOrMeshes = GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;
		Mesh.CastShadow = bUseCardsOrMeshes;
#if RHI_RAYTRACING
		Mesh.CastRayTracedShadow = bUseCardsOrMeshes && bCastDynamicShadow;
#endif
		Mesh.bUseForMaterial = bUseCardsOrMeshes;
		Mesh.bUseForDepthPass = bUseCardsOrMeshes;
		Mesh.SegmentIndex = 0;
		#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Mesh.VisualizeLODIndex = IntLODIndex;
		#endif

		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = VertexFactory;
		Mesh.MaterialRenderProxy = MaterialRenderProxy;
		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity = GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;

		FPrimitiveSceneInfo* PrimSceneInfo = GetPrimitiveSceneInfo();
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(PrimSceneInfo, bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

		const bool bUseProxy = UseProxyLocalToWorld(Instance);

		FMatrix CurrentLocalToWorld = bUseProxy ? GetLocalToWorld() : Instance->GetCurrentLocalToWorld().ToMatrixWithScale();
		PreviousLocalToWorld = bUseProxy ? PreviousLocalToWorld : Instance->GetPreviousLocalToWorld().ToMatrixWithScale();

		// Band-aid to avoid invalid velociy vector when switching LOD skinned <-> rigid
		if (Instance->HairGroupPublicData->VFInput.bHasLODSwitch && Instance->HairGroupPublicData->VFInput.bHasLODSwitchBindingType)
		{
			PreviousLocalToWorld = CurrentLocalToWorld;
		}

		// Update primitive uniform buffer
		{
			// Use default SceneProxy builder values
			FPrimitiveUniformShaderParametersBuilder Builder;
			BuildUniformShaderParameters(Builder);

			// Override transforms and the local bound.
			// The original local bound relative to the component local to world transform. If we override the local to world transform, 
			// we need to recompute the local bound relative to this new transform. It is important that the new local bound is correct 
			// as otherwise the GPUScene (which use bot the local to world transform and the local bound for culling purpose) will issue 
			// incorrect visibility test.
			FBoxSphereBounds NewLocalBound = GetLocalBounds();
			if (!bUseProxy)
			{
				const FTransform InvLocalToWorld = Instance->GetCurrentLocalToWorld().Inverse();
				const FBoxSphereBounds OriginalWorldBound = GetBounds();
				NewLocalBound = OriginalWorldBound.TransformBy(InvLocalToWorld);
			}
			Builder
				.LocalToWorld(CurrentLocalToWorld)
				.PreviousLocalToWorld(PreviousLocalToWorld)
				.LocalBounds(NewLocalBound)
				.OutputVelocity(bOutputVelocity)
				.UseVolumetricLightmap(false);

			// Create primitive uniform buffer
			FRHICommandListBase& RHICmdList = Collector.GetRHICommandList();
			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.UniformBuffer.BufferUsage = UniformBuffer_SingleFrame;
			DynamicPrimitiveUniformBuffer.UniformBuffer.SetContents(RHICmdList, Builder.Build());
			DynamicPrimitiveUniformBuffer.UniformBuffer.InitResource(RHICmdList);
			BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer; // automatic copy to the gpu scene buffer
		}

		//primtiveid is set to 0
		BatchElement.FirstIndex = 0;
		BatchElement.NumInstances = 1;
		BatchElement.PrimitiveIdMode = PrimitiveIdMode;
		if (bUseCulling)
		{
			BatchElement.NumPrimitives = 0;
			BatchElement.IndirectArgsBuffer = bUseCulling ? Instance->HairGroupPublicData->GetDrawIndirectBuffer().Buffer->GetRHI() : nullptr;
			BatchElement.IndirectArgsOffset = 0;
		}
		else
		{
			BatchElement.NumPrimitives = NumPrimitive;
			BatchElement.IndirectArgsBuffer = nullptr;
			BatchElement.IndirectArgsOffset = 0;
		}

		// Setup our vertex factor custom data
		BatchElement.VertexFactoryUserData = const_cast<void*>(reinterpret_cast<const void*>(Instance->HairGroupPublicData));

		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = MaxVertexIndex;
		BatchElement.UserData = reinterpret_cast<void*>(uint64(ComponentId));
		Mesh.ReverseCulling = bUseCardsOrMeshes ? IsLocalToWorldDeterminantNegative() : false;
		Mesh.bDisableBackfaceCulling = GeometryType == EHairGeometryType::Strands;
		Mesh.Type = GetPrimitiveType(GeometryType);
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.bCanApplyViewModeOverrides = false;
		Mesh.BatchHitProxyId = PrimSceneInfo->DefaultDynamicHitProxyId;

		return &Mesh;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		// When path tracing is enabled force DrawRelevance if not visible in main view ('hidden in game'), 
		// but visible in shadow 'hidden shadow') so that raytracing geometry is created/updated correctly
		const bool bPathtracing = View->Family->EngineShowFlags.PathTracing;
		const bool bIsShown = IsShown(View);
		const bool bForceDrawRelevance = bPathtracing && (!bIsShown && (IsShadowCast(View) || bAffectIndirectLightingWhileHidden));
		const bool bVisible = View->Family->EngineShowFlags.Hair;

		bool bUseCardsOrMesh = false;
		for (const TRefCountPtr<FHairGroupInstance>& Instance : HairGroupInstances)
		{
			check(Instance->GetRefCount());
			const EHairGeometryType GeometryType = Instance->GeometryType;
			bUseCardsOrMesh = bUseCardsOrMesh || GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;
		}

		FPrimitiveViewRelevance Result;

		// Special pass for hair strands geometry (not part of the base pass, and shadowing is handlded in a custom fashion). When cards rendering is enabled we reusethe base pass
		Result.bDrawRelevance		= bVisible && (bIsShown || bForceDrawRelevance);
		Result.bRenderInMainPass	= bUseCardsOrMesh && ShouldRenderInMainPass();
		Result.bShadowRelevance		= IsShadowCast(View);
		Result.bDynamicRelevance	= bUseCardsOrMesh;
		Result.bRenderCustomDepth	= ShouldRenderCustomDepth();
		Result.bVelocityRelevance	= Result.bRenderInMainPass && bUseCardsOrMesh;
		Result.bUsesLightingChannels= GetLightingChannelMask() != GetDefaultLightingChannelMask();

		// Selection only
		#if WITH_EDITOR
		{
			Result.bEditorStaticSelectionRelevance = true;
		}
		#endif
		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		// Override the MaterialRelevance output
		Result.bHairStrands = bIsShown || bForceDrawRelevance;
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

	TArray<TRefCountPtr<FHairGroupInstance>> HairGroupInstances;

	// Cache the material proxy to avoid race condition, when groom component's proxy is recreated, 
	// while another one is currently in flight for drawing.
	struct FHairGroupMaterialProxy
	{
		const FMaterialRenderProxy* Strands = nullptr;
		TArray<const FMaterialRenderProxy*> Cards;
		TArray<const FMaterialRenderProxy*> Meshes;
	};
	TArray<FHairGroupMaterialProxy> HairGroupMaterialProxies;
	
private:
	uint32 ComponentId = 0;
	FMaterialRelevance MaterialRelevance;
};


/**************************************************************************************************
*
*   UVfxSnapshotGroomComponent
*
***/

UVfxSnapshotGroomComponent::UVfxSnapshotGroomComponent (const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaterialEffectManager(true)
	, bUseDefaultIfIncompatible(false)
{
	bUseAttachParentBound = true;
	
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Strands_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairDefaultMaterial.HairDefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cards_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairCardsDefaultMaterial.HairCardsDefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Meshes_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairMeshesDefaultMaterial.HairMeshesDefaultMaterial"));

	Strands_DefaultMaterial = Strands_DefaultMaterialRef.Object;
	Cards_DefaultMaterial = Cards_DefaultMaterialRef.Object;
	Meshes_DefaultMaterial = Meshes_DefaultMaterialRef.Object;
}

UVfxSnapshotGroomComponent::~UVfxSnapshotGroomComponent ()
{
}

void UVfxSnapshotGroomComponent::SetUseDefaultIfIncompatible(bool InVal)
{
	if (bUseDefaultIfIncompatible == InVal)
	{
		return;
	}

	bUseDefaultIfIncompatible = InVal;
	
	MarkRenderStateDirty();
}

void UVfxSnapshotGroomComponent::BeginDestroy()
{
	if (IsRenderStateCreated())
	{
		DestroyRenderState_Concurrent();
	}

	Super::BeginDestroy();
}

void UVfxSnapshotGroomComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsRenderStateCreated())
	{
		DestroyRenderState_Concurrent();
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UVfxSnapshotGroomComponent::OnRegister()
{
	Super::OnRegister();

	SrcRenderStateDirtyCallback = UActorComponent::MarkRenderStateDirtyEvent.AddUObject(
		this, &UVfxSnapshotGroomComponent::OnMarkRenderStateDirty
	);
}

void UVfxSnapshotGroomComponent::OnUnregister()
{
	if (IsRenderStateCreated())
	{
		DestroyRenderState_Concurrent();
	}
	ClearQueuedBorrowRenderRecources();

	if (SrcRenderStateDirtyCallback.IsValid())
	{
		UActorComponent::MarkRenderStateDirtyEvent.Remove(SrcRenderStateDirtyCallback);
		SrcRenderStateDirtyCallback.Reset();
	}
	
	MaterialEffectManager.Uninitialize(false);

	Super::OnUnregister();
}

void UVfxSnapshotGroomComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();

	//if (SourceComponent != GetAttachParent())
	//{
	//	SetVisibility(false);
	//}
}

FBoxSphereBounds UVfxSnapshotGroomComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (SourceComponent)
	{
		return SourceComponent->CalcBounds(LocalToWorld);
	}
	
	return Super::CalcBounds(LocalToWorld);
}

void UVfxSnapshotGroomComponent::OnMarkRenderStateDirty(UActorComponent& Component)
{
	if (SourceComponent != &Component)
	{
		return;
	}

	if (IsRenderStateCreated())
	{
		DestroyRenderState_Concurrent();
	}

	QueueBorrowRenderRecources();
}

void UVfxSnapshotGroomComponent::QueueBorrowRenderRecources()
{
	if (BorrowRenderRecourcesTimer.IsValid() == false)
	{
		if (UWorld* World = GetWorld())
		{
			BorrowRenderRecourcesTimer = World->GetTimerManager().SetTimerForNextTick(
				this, &UVfxSnapshotGroomComponent::BorrowRenderRecources
			);
		}
	}
}

void UVfxSnapshotGroomComponent::ClearQueuedBorrowRenderRecources()
{
	if (BorrowRenderRecourcesTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BorrowRenderRecourcesTimer);
		}
		BorrowRenderRecourcesTimer.Invalidate();
	}
}

void UVfxSnapshotGroomComponent::BorrowRenderRecources()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVfxSnapshotGroomComponent::BorrowRenderRecources);

	BorrowRenderRecourcesTimer.Invalidate();

	UGroomComponent* Src = SourceComponent.Get();
	if (IsValid(Src) == false)
	{
		return;
	}

	if (Src->IsRenderStateDirty() || Src->IsRenderStateCreated() == false)
	{
		QueueBorrowRenderRecources();
		return;
	}

	if (IsRenderStateCreated())
	{
		DestroyRenderState_Concurrent();
	}

	CreateRenderState_Concurrent(nullptr);
}

void UVfxSnapshotGroomComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
}

void UVfxSnapshotGroomComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();
}

void UVfxSnapshotGroomComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
}

FPrimitiveSceneProxy* UVfxSnapshotGroomComponent::CreateSceneProxy()
{
	return new FSnapshotGroomSceneProxy(this);
}

/* Return the material slot index corresponding to the material name */
int32 UVfxSnapshotGroomComponent::GetMaterialIndex(FName MaterialSlotName) const
{
	if (GroomAsset)
	{
		return GroomAsset->GetMaterialIndex(MaterialSlotName);
	}

	return INDEX_NONE;
}

bool UVfxSnapshotGroomComponent::IsMaterialSlotNameValid(FName MaterialSlotName) const
{
	return GetMaterialIndex(MaterialSlotName) != INDEX_NONE;
}

TArray<FName> UVfxSnapshotGroomComponent::GetMaterialSlotNames() const
{
	TArray<FName> MaterialNames;
	if (GroomAsset)
	{
		MaterialNames = GroomAsset->GetMaterialSlotNames();
	}

	return MaterialNames;
}

int32 UVfxSnapshotGroomComponent::GetNumMaterials() const
{
	if (GroomAsset)
	{
		return FMath::Max(GroomAsset->GetHairGroupsMaterials().Num(), 1);
	}
	return 1;
}

UMaterialInterface* UVfxSnapshotGroomComponent::GetMaterial(int32 ElementIndex, EHairGeometryType GeometryType) const
{
	UMaterialInterface* OverrideMaterial = Super::GetMaterial(ElementIndex);

	bool bUseHairDefaultMaterial = false;

	if (!OverrideMaterial && GroomAsset && ElementIndex < GroomAsset->GetHairGroupsMaterials().Num())
	{
		if (UMaterialInterface* Material = GroomAsset->GetHairGroupsMaterials()[ElementIndex].Material)
		{
			OverrideMaterial = Material;
		}
		else if (bUseDefaultIfIncompatible)
		{
			bUseHairDefaultMaterial = true;
		}
	}

	struct FLocalUtils
	{
		static bool IsHairMaterialCompatible(UMaterialInterface* MaterialInterface, EShaderPlatform InShaderPlatform, EHairGeometryType GeometryType)
		{
			if (MaterialInterface)
			{
				if (GeometryType != Strands)
				{
					return true;
				}
				const FMaterialRelevance Relevance = MaterialInterface->GetRelevance_Concurrent(InShaderPlatform);
				const bool bIsRelevanceInitialized = Relevance.Raw != 0;
				if (bIsRelevanceInitialized && !Relevance.bHairStrands)
				{
					return false;
				}
				if (!MaterialInterface->GetShadingModels().HasShadingModel(MSM_Hair) && GeometryType == EHairGeometryType::Strands)
				{
					return false;
				}
				if (!IsOpaqueOrMaskedBlendMode(*MaterialInterface) && GeometryType == EHairGeometryType::Strands)
				{
					return false;
				}
			}
			else
			{
				return false;
			}
	
			return true;
		}

	};

	if (bUseDefaultIfIncompatible)
	{
		const EShaderPlatform ShaderPlatform = GetScene() ? GetScene()->GetShaderPlatform() : EShaderPlatform::SP_NumPlatforms;
		if (ShaderPlatform != EShaderPlatform::SP_NumPlatforms && FLocalUtils::IsHairMaterialCompatible(OverrideMaterial, ShaderPlatform, GeometryType) == false)
		{
			bUseHairDefaultMaterial = true;
		}
	}

	if (bUseHairDefaultMaterial)
	{
		if (GeometryType == EHairGeometryType::Strands)
		{
			OverrideMaterial = Strands_DefaultMaterial;
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			OverrideMaterial = Cards_DefaultMaterial;
		}
		else if (GeometryType == EHairGeometryType::Meshes)
		{
			OverrideMaterial = Meshes_DefaultMaterial;
		}
	}

	return OverrideMaterial;
}

EHairGeometryType UVfxSnapshotGroomComponent::GetMaterialGeometryType(int32 ElementIndex) const
{
	if (!GroomAsset)
	{
		// If we don't know, enforce strands, as it has the most requirement.
		return EHairGeometryType::Strands;
	}

	const EShaderPlatform Platform = GetScene() ? GetScene()->GetShaderPlatform() : EShaderPlatform::SP_NumPlatforms;
	for (uint32 GroupIt = 0, GroupCount = GroomAsset->GetHairGroupsRendering().Num(); GroupIt < GroupCount; ++GroupIt)
	{
		// Material - Strands
		const FHairGroupPlatformData& InGroupData = GroomAsset->GetHairGroupsPlatformData()[GroupIt];
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform))
		{
			const int32 SlotIndex = GroomAsset->GetMaterialIndex(GroomAsset->GetHairGroupsRendering()[GroupIt].MaterialSlotName);
			if (SlotIndex == ElementIndex)
			{
				return EHairGeometryType::Strands;
			}
		}

		// Material - Cards
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform))
		{
			uint32 CardsLODIndex = 0;
			for (const FHairGroupPlatformData::FCards::FLOD& LOD : InGroupData.Cards.LODs)
			{
				if (LOD.IsValid())
				{
					// Material
					int32 SlotIndex = INDEX_NONE;
					for (const FHairGroupsCardsSourceDescription& Desc : GroomAsset->GetHairGroupsCards())
					{
						if (Desc.GroupIndex == GroupIt && Desc.LODIndex == CardsLODIndex)
						{
							SlotIndex = GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
							break;
						}
					}
					if (SlotIndex == ElementIndex)
					{
						return EHairGeometryType::Cards;
					}
				}
				++CardsLODIndex;
			}
		}

		// Material - Meshes
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, Platform))
		{
			uint32 MeshesLODIndex = 0;
			for (const FHairGroupPlatformData::FMeshes::FLOD& LOD : InGroupData.Meshes.LODs)
			{
				if (LOD.IsValid())
				{
					// Material
					int32 SlotIndex = INDEX_NONE;
					for (const FHairGroupsMeshesSourceDescription& Desc : GroomAsset->GetHairGroupsMeshes())
					{
						if (Desc.GroupIndex == GroupIt && Desc.LODIndex == MeshesLODIndex)
						{
							SlotIndex = GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
							break;
						}
					}
					if (SlotIndex == ElementIndex)
					{
						return EHairGeometryType::Meshes;
					}
				}
				++MeshesLODIndex;
			}
		}
	}
	// If we don't know, enforce strands, as it has the most requirement.
	return EHairGeometryType::Strands;
}

void UVfxSnapshotGroomComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	Super::SetMaterial(ElementIndex, Material);
}

UMaterialInterface* UVfxSnapshotGroomComponent::GetMaterial(int32 ElementIndex) const
{
	const EHairGeometryType GeometryType = GetMaterialGeometryType(ElementIndex);
	return GetMaterial(ElementIndex, GeometryType);
}

void UVfxSnapshotGroomComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	UMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

	if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands))
	{
		OutMaterials.Add(Strands_DefaultMaterial);
	}

	if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards))
	{
		OutMaterials.Add(Cards_DefaultMaterial);
	}
	if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes))
	{
		OutMaterials.Add(Meshes_DefaultMaterial);
	}
}

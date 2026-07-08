#pragma once

#include "CoreMinimal.h"
#include "GroomComponent.h"
#include "GroomInstance.h"
#include "Material/VfxMaterialEffectManager.h"
#include "VfxSnapshotGroomComponent.generated.h"

class UGroomAsset;

/**************************************************************************************************
*
*   UVfxSnapshotGroomComponent
*
***/

UCLASS()
class VISUALEFFECTS_API UVfxSnapshotGroomComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UVfxSnapshotGroomComponent ();
	
	void SetUseDefaultIfIncompatible(bool InVal);

	//~ Begin UActorComponent Interface.
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void BeginDestroy() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnAttachmentChanged() override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.
	
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.
	
	EHairGeometryType GetMaterialGeometryType(int32 ElementIndex) const;
	UMaterialInterface* GetMaterial(int32 ElementIndex, EHairGeometryType GeometryType) const;
	//~ Begin UPrimitiveComponent Interface
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual int32 GetMaterialIndex(FName MaterialSlotName) const override;
	virtual TArray<FName> GetMaterialSlotNames() const override;
	virtual bool IsMaterialSlotNameValid(FName MaterialSlotName) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual int32 GetNumMaterials() const override;
	//~ End UPrimitiveComponent Interface

	UPROPERTY(VisibleAnywhere, Transient)
	TObjectPtr<UGroomComponent> SourceComponent;

	UPROPERTY(VisibleAnywhere, Transient)
	TObjectPtr<UGroomAsset> GroomAsset;

	UPROPERTY(VisibleAnywhere, Transient)
	FVfxMaterialEffectManager MaterialEffectManager;

private:
	void OnMarkRenderStateDirty(UActorComponent& Component);
	
	void QueueBorrowRenderRecources();
	void ClearQueuedBorrowRenderRecources();
	void BorrowRenderRecources();
	
	friend class FSnapshotGroomSceneProxy;
	
	void CheckHairStrandsUsage(int32 ElementIndex) const;

private:
	FTimerHandle BorrowRenderRecourcesTimer;

	FDelegateHandle SrcRenderStateDirtyCallback;
	TArray<TRefCountPtr<FHairGroupInstance>> HairGroupInstances;

	UPROPERTY(Transient)
	bool bUseDefaultIfIncompatible;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Strands_DefaultMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Cards_DefaultMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Meshes_DefaultMaterial;
};

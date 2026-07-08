#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Material/VfxMaterialEffectManager.h"
#include "VfxSnapshotStaticMeshComponent.generated.h"


/**************************************************************************************************
*
*   UVfxSnapshotStaticMeshComponent
*
***/

UCLASS()
class VISUALEFFECTS_API UVfxSnapshotStaticMeshComponent : public UStaticMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UVfxSnapshotStaticMeshComponent ();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(VisibleAnywhere, Transient)
	FVfxMaterialEffectManager MaterialEffectManager;
};
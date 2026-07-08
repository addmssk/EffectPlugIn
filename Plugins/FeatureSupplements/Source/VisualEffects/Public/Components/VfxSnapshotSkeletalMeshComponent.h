#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Material/VfxMaterialEffectManager.h"
#include "VfxSnapshotSkeletalMeshComponent.generated.h"


/**************************************************************************************************
*
*   UVfxSnapshotSkeletalMeshComponent
*
***/

UCLASS()
class VISUALEFFECTS_API UVfxSnapshotSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UVfxSnapshotSkeletalMeshComponent ();

	virtual void TickComponent (float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	UPROPERTY(VisibleAnywhere, Transient)
	FVfxMaterialEffectManager MaterialEffectManager;
};
#pragma once

#include "CoreMinimal.h"
#include "Components/SkinnedMeshComponent.h"
#include "Material/VfxMaterialEffectManager.h"
#include "VfxSnapshotSkinnedMeshComponent.generated.h"


/**************************************************************************************************
*
*   UVfxSnapshotSkinnedMeshComponent
*
***/

UCLASS()
class VISUALEFFECTS_API UVfxSnapshotSkinnedMeshComponent : public USkinnedMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UVfxSnapshotSkinnedMeshComponent ();

	virtual void OnRegister() override;
	virtual void TickComponent (float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	UPROPERTY(VisibleAnywhere, Transient, Category = Status)
	TWeakObjectPtr<USkinnedMeshComponent> SourcePoseComponent;

	UPROPERTY(VisibleAnywhere, Transient)
	FVfxMaterialEffectManager MaterialEffectManager;
};
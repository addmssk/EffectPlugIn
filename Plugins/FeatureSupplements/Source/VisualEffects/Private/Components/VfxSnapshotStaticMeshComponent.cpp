#include "Components/VfxSnapshotStaticMeshComponent.h"


/**************************************************************************************************
*
*   UVfxSnapshotStaticMeshComponent
*
***/

UVfxSnapshotStaticMeshComponent::UVfxSnapshotStaticMeshComponent (const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaterialEffectManager(true)
{
	bUseAttachParentBound = true;
}

UVfxSnapshotStaticMeshComponent::~UVfxSnapshotStaticMeshComponent ()
{
}

void UVfxSnapshotStaticMeshComponent::OnRegister()
{
	Super::OnRegister();
}

void UVfxSnapshotStaticMeshComponent::OnUnregister()
{
	MaterialEffectManager.Uninitialize(false);

	Super::OnUnregister();
}

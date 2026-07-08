#include "Components/VfxSnapshotSkeletalMeshComponent.h"


/**************************************************************************************************
*
*   UVfxSnapshotSkeletalMeshComponent
*
***/

UVfxSnapshotSkeletalMeshComponent::UVfxSnapshotSkeletalMeshComponent (const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaterialEffectManager(true)
{
	bComponentUseFixedSkelBounds = true;
	bPerBoneMotionBlur = false;
	bUseAttachParentBound = true;
	bUseBoundsFromLeaderPoseComponent = true;
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 1.f / 30.f;
}

UVfxSnapshotSkeletalMeshComponent::~UVfxSnapshotSkeletalMeshComponent ()
{
}

void UVfxSnapshotSkeletalMeshComponent::TickComponent (float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MaterialEffectManager.Tick(DeltaTime);
}
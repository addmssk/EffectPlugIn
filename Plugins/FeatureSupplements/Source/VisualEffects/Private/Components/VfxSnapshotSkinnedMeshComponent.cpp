#include "Components/VfxSnapshotSkinnedMeshComponent.h"


/**************************************************************************************************
*
*   UVfxSnapshotSkinnedMeshComponent
*
***/

UVfxSnapshotSkinnedMeshComponent::UVfxSnapshotSkinnedMeshComponent (const FObjectInitializer& ObjectInitializer)
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

UVfxSnapshotSkinnedMeshComponent::~UVfxSnapshotSkinnedMeshComponent ()
{
}

void UVfxSnapshotSkinnedMeshComponent::OnRegister()
{
	if (USkinnedMeshComponent* SourceComp = SourcePoseComponent.Get())
	{
		SetLeaderPoseComponent(SourceComp->LeaderPoseComponent.IsValid() ? SourceComp->LeaderPoseComponent.Get() : SourceComp);
	}

	Super::OnRegister();
}

void UVfxSnapshotSkinnedMeshComponent::TickComponent (float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (USkinnedMeshComponent* SourceComp = SourcePoseComponent.Get())
	{
		if (bRecentlyRendered && SourceComp->bRecentlyRendered == false)
		{
			// Do animation update even if master is hidden
			SourceComp->SetLastRenderTime(GetLastRenderTime());
		}

		USkinnedMeshComponent* MasterComp = SourceComp->LeaderPoseComponent.IsValid() ? SourceComp->LeaderPoseComponent.Get() : SourceComp;
		SetLeaderPoseComponent(MasterComp);
	}

	MaterialEffectManager.Tick(DeltaTime);
}
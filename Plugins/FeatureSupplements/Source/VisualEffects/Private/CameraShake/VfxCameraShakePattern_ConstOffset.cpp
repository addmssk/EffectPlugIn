


#include "CameraShake/VfxCameraShakePattern_ConstOffset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VfxCameraShakePattern_ConstOffset)

UVfxCameraShakePattern_ConstOffset::UVfxCameraShakePattern_ConstOffset(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	Location = FVector::ZeroVector;
	Rotation = FRotator::ZeroRotator;
	FOV = 0.f;
}

void UVfxCameraShakePattern_ConstOffset::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	Super::StartShakePatternImpl(Params);

	if (!Params.bIsRestarting)
	{

	}
}

void UVfxCameraShakePattern_ConstOffset::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	OutResult.Location = Location;
	OutResult.Rotation = Rotation;
	OutResult.FOV = FOV;

	const float BlendWeight = State.Update(Params.DeltaTime);
	OutResult.ApplyScale(BlendWeight);
}

void UVfxCameraShakePattern_ConstOffset::ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	OutResult.Location = Location;
	OutResult.Rotation = Rotation;
	OutResult.FOV = FOV;

	const float BlendWeight = State.Scrub(Params.AbsoluteTime);
	OutResult.ApplyScale(BlendWeight);
}


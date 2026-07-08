

#pragma once

#include "CoreMinimal.h"
#include "Shakes/SimpleCameraShakePattern.h"
#include "VfxCameraShakePattern_ConstOffset.generated.h"

#define UE_API VISUALEFFECTS_API

UCLASS()
class UE_API UVfxCameraShakePattern_ConstOffset : public USimpleCameraShakePattern
{
	GENERATED_BODY()
	
public:	
	UVfxCameraShakePattern_ConstOffset(const FObjectInitializer& ObjInit);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FOV;

private:
	
	// UCameraShakePattern interface
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
};

#undef UE_API
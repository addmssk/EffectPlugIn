#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "VfxVolumeDecisionBaseParam.generated.h"


USTRUCT(BlueprintType)
struct FVfxVolumeDecisionBaseParam
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector3f Origin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector3f ForwardDirection;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Roll;

	FVfxVolumeDecisionBaseParam ()
		: Origin(FVector3f::ZeroVector)
		, ForwardDirection(FVector3f::ForwardVector)
		, Roll(0.f)
	{
	}
};
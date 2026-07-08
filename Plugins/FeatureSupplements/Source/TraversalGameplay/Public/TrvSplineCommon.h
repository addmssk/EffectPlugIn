

#pragma once

#include "CoreMinimal.h"
#include "TrvSplineCommon.generated.h"


USTRUCT()
struct FTrvSplineSegment
{
	GENERATED_USTRUCT_BODY()

	FVector ForwardVector;
	FVector UpVector;
	FVector SideVector;
	FVector Center;
	float RightMaxOffset;
	float LeftMaxOffset;
	float Distance;

	uint8 DebugInfo;

	/*~ Begin TStructOpsTypeTraits implementation */
	inline bool Serialize(FArchive& Ar)
	{
		Ar << ForwardVector;
		Ar << UpVector;
		Ar << SideVector;
		Ar << Center;
		Ar << RightMaxOffset;
		Ar << LeftMaxOffset;
		Ar << Distance;

		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FTrvSplineSegment> : public TStructOpsTypeTraitsBase2<FTrvSplineSegment>
{
	enum
	{
		WithSerializer = true,
	};
};
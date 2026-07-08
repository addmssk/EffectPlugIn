

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrvCustomMoveArrangeComponent.generated.h"

class ACharacter;
class UMotionWarpingComponent;


/**************************************************************************************************
*
*	Structures
*
***/

USTRUCT(BlueprintType)
struct FTrvCustomMoveAsyncConcaveAnalysisOptions
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CheckStartOffset = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CheckStep = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SurfaceHeightDifferenceTolerance = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseSphereTrace = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SphereTraceRadius = 5.f;
};


USTRUCT(BlueprintType)
struct FTrvCustomMoveAsyncConcaveAnalysisResult
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSuccess;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float GapLength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HeightDifference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector GapStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector GapEnd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FQuat MotionWarpDirection;

	inline void Reset()
	{
		bSuccess = false;
		Distance = 0.f;
		GapLength = 0.f;
		HeightDifference = 0.f;
		GapStart = FVector::ZeroVector;
		GapEnd = FVector::ZeroVector;
		MotionWarpDirection = FQuat::Identity;
	}

	FTrvCustomMoveAsyncConcaveAnalysisResult()
	{
		Reset();
	}
};


USTRUCT(BlueprintType)
struct FTrvCustomMoveAsyncConvexAnalysisOptions
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HeightCheckStartOffset = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HeightCheckMaxLength = 290.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 HeightCheckGranularity = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float HeightCheckAceeptableSlope = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DepthCheckStartOffset = 5.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DepthCheckMaxLength = 400.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 DepthCheckGranularity = 15;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DepthCheckAceeptableSlope = 0.2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float VerticalRayStartOffset = 80.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float VerticalRayEndOffset = 100.f;

	/** return motionwarp direction return rule. return InvDir if 'obtacle height' < x, blend Indir and Hit not  X < 'obtacle height' < Y, return Hit Normal Y <  'obtacle height'*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D InvDirToHitNormalBlendRangeByHeight = FVector2D(60.f, 180.f);
};


USTRUCT(BlueprintType)
struct FTrvCustomMoveAsyncConvexAnalysisResult
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSuccess;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Depth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector FrontLedgeLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector BackLedgeLocation;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BackFloorStartDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BackFloorHeightDifference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PeakHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PeakRatio;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FQuat MotionWarpDirection;

	inline void Reset()
	{
		bSuccess = false;
		Depth = 0.f;
		Height = 0.f;
		Distance = 0.f;
		FrontLedgeLocation = FVector::ZeroVector;
		BackLedgeLocation = FVector::ZeroVector;
		BackFloorStartDistance = 0.f;
		BackFloorHeightDifference = 0.f;
		PeakHeight = 0.f;
		PeakRatio = 0.5f;
		MotionWarpDirection = FQuat::Identity;
	}

	FTrvCustomMoveAsyncConvexAnalysisResult()
	{
		Reset();
	}
};


/**************************************************************************************************
*
*	UTrvCustomMoveArrangeComponent
*
***/

UCLASS(notplaceable, BlueprintType, Blueprintable)
class TRAVERSALGAMEPLAY_API UTrvCustomMoveArrangeComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UTrvCustomMoveArrangeComponent(const FObjectInitializer& ObjectInitializer);
	
	// UActorComponent~
	virtual void BeginPlay() override;
	virtual void OnUnregister() override;
	// ~UActorComponent

public:
	UFUNCTION(Blueprintcallable)
	void QueueAsyncConvexAnalysis(
		float InCheckFarDistance,
		FVector InDirection,
		const bool InbForceSynchronous,
		const FTrvCustomMoveAsyncConvexAnalysisOptions& InConvexDetectOptions,
		const bool InbDrawDebug = false
	);
	
	UFUNCTION(Blueprintcallable)
	bool HasQueuedAsyncConvexAnalysis(FVector2D InValidHeightRange = FVector2D(-10.0f, 10.0f));

	UFUNCTION(Blueprintcallable)
	bool ConsumeAsyncConvexAnalysis(
		float InSpeedLowerLimit,
		const bool bDebugDraw,
		float& OutSpeed,
		FTrvCustomMoveAsyncConvexAnalysisResult& OutConvexResult
	);

	UFUNCTION(Blueprintcallable)
	void QueueAsyncConcaveAnalysis(
		float InCheckFarDistance,
		FVector InDirection,
		const bool InbForceSynchronous,
		const FTrvCustomMoveAsyncConcaveAnalysisOptions& InConcaveDetectOptions,
		const bool InbDrawDebug = false
	);

	UFUNCTION(Blueprintcallable)
	bool HasQueuedAsyncConcaveAnalysis(FVector2D InValidHeightRange = FVector2D(-10.0f, 10.0f));

	UFUNCTION(Blueprintcallable)
	bool ConsumeAsyncConcaveAnalysis(
		float InSpeedLowerLimit,
		const bool bDebugDraw,
		float& OutSpeed,
		FTrvCustomMoveAsyncConcaveAnalysisResult& OutConcaveResult
	);

protected:
	struct FQueuedJabValidationContext
	{
		FVector Position;
		FVector Direction;
		float InputCheckDistance;
		float AdditionalCheckDistance;
	};
	FQueuedJabValidationContext AsyncConcaveAnalysisVaidationContext;
	FQueuedJabValidationContext AsyncConvexAnalysisVaidationContext;

	bool IsValidAnalysisTask(
		const void* TaskPtr,
		const FVector2D& InValidHeightRange,
		const FQueuedJabValidationContext& Context,
		const TCHAR* DebugStr
	) const;
	void ClearAsyncConcaveAnalysisTask();
	void ClearAsyncConvexAnalysisTask();

protected:
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	TObjectPtr<UMotionWarpingComponent> OwnerMotionWarpingComponent;

	void* AsyncConcaveAnalysisTask;
	uint64 AsyncConcaveAnalaysisQueuedCycle;

	void* AsyncConvexAnalysisTask;
	uint64 AsyncConvexAnalaysisQueuedCycle;
};

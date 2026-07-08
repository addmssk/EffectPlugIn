


#include "Character/TrvCustomMoveArrangeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "MotionWarpingComponent.h"
#include "DrawDebugHelpers.h"
#include "TrvUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvCustomMoveArrangeComponent)


static float gTrvCustomMoveAsyncConcaveAnalysisMaxFreshSeconds = 1.f / 10.f;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncConcaveAnalysisMaxFreshSeconds(
	TEXT("msk.cfs.CustomMove.AsyncConcaveAnalysisMaxFreshSeconds"),
	gTrvCustomMoveAsyncConcaveAnalysisMaxFreshSeconds,
	TEXT(""),
	ECVF_Default
);

static float gTrvCustomMoveAsyncConvexAnalysisMaxFreshSeconds = 5.f / 10.f;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncConvexAnalysisMaxFreshSeconds(
	TEXT("msk.cfs.CustomMove.AsyncConvexAnalysisMaxFreshSeconds"),
	gTrvCustomMoveAsyncConvexAnalysisMaxFreshSeconds,
	TEXT(""),
	ECVF_Default
);

static float gTrvCustomMoveAsyncAnalysisDirTolerance = 5.f;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncAnalysisDirTolerance(
	TEXT("msk.cfs.CustomMove.AsyncAnalysisDirTolerance"),
	gTrvCustomMoveAsyncAnalysisDirTolerance,
	TEXT(""),
	ECVF_Default
);

static float gTrvCustomMoveAsyncAnalysisAcceptableAdditionalDistance = 20.f;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncAnalysisAcceptableAdditionalDistance(
	TEXT("msk.cfs.CustomMove.AsyncAnalysisAcceptableAdditionalDistance"),
	gTrvCustomMoveAsyncAnalysisAcceptableAdditionalDistance,
	TEXT(""),
	ECVF_Default
);

static float gTrvCustomAsyncAnalysiskMoveDebugDrawDuration = 3.f;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncAnalysisDebugDrawDuration(
	TEXT("msk.cfs.CustomMove.AsyncAnalysisDebugDrawDuration"),
	gTrvCustomAsyncAnalysiskMoveDebugDrawDuration,
	TEXT(""),
	ECVF_Default
);

static int32 gTrvCustomMoveAsyncAnalysisEnableLog = 0;
static FAutoConsoleVariableRef CVarTrvCustomMoveAsyncAnalysisEnableLog(
	TEXT("msk.cfs.CustomMove.AsyncAnalysisEnableLog"),
	gTrvCustomMoveAsyncAnalysisEnableLog,
	TEXT(""),
	ECVF_Default
);

#define ANALYSIS_LOG(_FormatString_, ...) if (gTrvCustomMoveAsyncAnalysisEnableLog != 0) { UE_LOG(LogTrvGeneral, Log, _FormatString_, ##__VA_ARGS__); }

const ECollisionChannel kParkourBlockTraceChannel = ECollisionChannel::ECC_GameTraceChannel15;
const ECollisionChannel kParkourTraceChannel = ECollisionChannel::ECC_GameTraceChannel16;

struct FTrvAsyncAnalaysisTaskCommonContext
{
	int64 FrameCounter;
	UTrvCustomMoveArrangeComponent* OwnerComp;
	float CheckFarDistance;
	FVector Direction;
	FVector Position;
	FRotator ControlRotation;
	float CapsuleRadius;
	float CapsuleHalfHeight;
	UWorld* World;
	TArray<AActor*, TInlineAllocator<4>> IgnoreActors;

	FTrvAsyncAnalaysisTaskCommonContext(UTrvCustomMoveArrangeComponent* InOwner)
		: OwnerComp(InOwner)
	{
	}
};


/**************************************************************************************************
*
*	Local Functions
*
***/

inline bool IsValidAsyncAnalysisResult(float& OutDistance2D, const FVector& InResultDir, const float InDirDotTolerance, const FVector& TargetPosition, const FVector& CurrentPosition)
{
	FVector ToTarget = TargetPosition - CurrentPosition;
	const float ToTargetDist2DSQ = ToTarget.SizeSquared2D();
	if (ToTargetDist2DSQ < 100.f)
	{
		return false;
	}

	const float ToTargetDist2D = FMath::Sqrt(ToTargetDist2DSQ);

	const FVector ToGapStartDir = FVector(ToTarget.X / ToTargetDist2D, ToTarget.Y / ToTargetDist2D, 0.0);
	if (FVector::DotProduct(InResultDir, ToGapStartDir) < InDirDotTolerance)
	{
		return false;
	}

	OutDistance2D = ToTargetDist2D;
	return true;
}

inline bool IsPakourBlocked(const FTrvAsyncAnalaysisTaskCommonContext& InCommonContext, const FCollisionQueryParams& InCollisonQueryParam)
{
	FCollisionShape CollisionShape;
	CollisionShape.SetCapsule(InCommonContext.CapsuleRadius * 0.5f, InCommonContext.CapsuleHalfHeight * 0.5f);

	FVector RayOrigin = InCommonContext.Position;
	FVector RaytEnd = RayOrigin + InCommonContext.Direction * InCommonContext.CheckFarDistance;
	FHitResult PreventCheck;
	bool bResult = InCommonContext.World->SweepSingleByChannel(
		PreventCheck, RayOrigin, RaytEnd, FQuat::Identity, kParkourBlockTraceChannel, CollisionShape, InCollisonQueryParam
	);
	if (bResult && PreventCheck.IsValidBlockingHit())
	{
		FHitResult AllowCheck;
		bResult = InCommonContext.World->SweepSingleByChannel(
			AllowCheck, RayOrigin, RaytEnd, FQuat::Identity, kParkourTraceChannel, CollisionShape, InCollisonQueryParam
		);
		if (AllowCheck.IsValidBlockingHit())
		{
			 return PreventCheck.Distance < AllowCheck.Distance;
		}
		else
		{
			return true;
		}
	}
	
	return false;
}


/**************************************************************************************************
*
*	FTrvAsyncConcaveAnalysisTask
*
***/

class FTrvAsyncConcaveAnalysisTask : public FNonAbandonableTask
{
public:
	explicit FTrvAsyncConcaveAnalysisTask (UTrvCustomMoveArrangeComponent* InOwner)
		: CommonContext(InOwner)
		, bDebugDrawed(false)
	{
	}

	~FTrvAsyncConcaveAnalysisTask ();

	void DoWork ();

	void DebugDraw(UWorld* InWorld);

	FORCEINLINE TStatId GetStatId () const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(TrvAsyncConcaveAnalysisTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	FTrvAsyncAnalaysisTaskCommonContext CommonContext;

	FTrvCustomMoveAsyncConcaveAnalysisOptions ConcaveOptions;

	FTrvCustomMoveAsyncConcaveAnalysisResult ConcaveResult;

	FHitResult ForwardRay;
	TArray<FHitResult> VerticalRays;

	bool bDebugDrawed;

private:
	enum EAnalysisStep
	{
		None,
		FindingGapStart,
		FindingGapLength,
		StartValidatingGapEnd,
		EndValidatingGapEnd = StartValidatingGapEnd + 3,
	};
};

FTrvAsyncConcaveAnalysisTask::~FTrvAsyncConcaveAnalysisTask ()
{
	
}

void FTrvAsyncConcaveAnalysisTask::DoWork ()
{
	ConcaveResult.Reset();
	bDebugDrawed = false;

	static const FName sQueryName = "AsyncConcaveAnalysis";
	FCollisionQueryParams CollisonQueryParam = FCollisionQueryParams(sQueryName, false);
	for (AActor* IgnoreActor : CommonContext.IgnoreActors)
	{
		CollisonQueryParam.AddIgnoredActor(IgnoreActor);
	}

	if (IsPakourBlocked(CommonContext, CollisonQueryParam))
	{
		return;
	}
	
	const FVector& Dir = CommonContext.Direction;
	const FVector StartTrace = Dir * ConcaveOptions.CheckStartOffset + CommonContext.Position;

	// Check obstacle in front of character. ignore small surface height difference
	{
		FVector EndTrace = Dir * CommonContext.CheckFarDistance + StartTrace;
		
#if 0
		FCollisionShape CollisionShape;
		CollisionShape.SetCapsule(CapsuleRadius, CapsuleHalfHeight - SurfaceHeightDifferenceTolerance);

		const bool bSweepResult = CommonContext.World->SweepSingleByChannel(ForwardRay, StartTrace, EndTrace, FQuat::Identity, kParkourTraceChannel, CollisionShape, CollisonQueryParam);
#else
		const bool bSweepResult = CommonContext.World->LineTraceSingleByChannel(ForwardRay, StartTrace, EndTrace, kParkourTraceChannel, CollisonQueryParam);
#endif

		if (ForwardRay.bBlockingHit || ForwardRay.bStartPenetrating)
		{
			return;
		}
	}

	const int32 LinetraceCount = FMath::CeilToInt32(CommonContext.CheckFarDistance / ConcaveOptions.CheckStep);
	const float RayEndDistance = CommonContext.CapsuleHalfHeight + ConcaveOptions.SurfaceHeightDifferenceTolerance;

	//TArray<FHitResult, TMemStackAllocator<>> VerticalRayCasts;
	VerticalRays.Reset();
	int32 HitCount = 0;
	{
		VerticalRays.SetNum(LinetraceCount);
		FHitResult* DestHitResults = VerticalRays.GetData();
	
		if (ConcaveOptions.bUseSphereTrace)
		{
			FCollisionShape CollisionShape;
			CollisionShape.SetSphere(ConcaveOptions.SphereTraceRadius);

			for (int32 Idx = 0; Idx < LinetraceCount; ++Idx)
			{
				FVector RayOrigin = StartTrace + Dir * (float)Idx * ConcaveOptions.CheckStep;
				FVector RaytEnd = RayOrigin - FVector::UpVector * RayEndDistance;
				bool bResult = CommonContext.World->SweepSingleByChannel(*DestHitResults, RayOrigin, RaytEnd, FQuat::Identity, kParkourTraceChannel, CollisionShape, CollisonQueryParam);

				HitCount += bResult? 1 : 0;

				++DestHitResults;
			}
		}
		else
		{
			for (int32 Idx = 0; Idx < LinetraceCount; ++Idx)
			{
				FVector RayOrigin = StartTrace + Dir * (float)Idx * ConcaveOptions.CheckStep;
				FVector RaytEnd = RayOrigin - FVector::UpVector * RayEndDistance;
				bool bResult = CommonContext.World->LineTraceSingleByChannel(*DestHitResults, RayOrigin, RaytEnd, kParkourTraceChannel, CollisonQueryParam);

				HitCount += bResult? 1 : 0;

				++DestHitResults;
			}
		}
	}

	if (HitCount < 3)
	{
		return;
	}

	ConcaveResult.GapStart = FVector::ZeroVector;
	ConcaveResult.GapEnd = FVector::ZeroVector;

	const float AllowedStartDistance = CommonContext.CapsuleHalfHeight - ConcaveOptions.SurfaceHeightDifferenceTolerance;

	uint8 AnalysisStep = EAnalysisStep::None;
	for (const FHitResult& CurrResult : VerticalRays)
	{
		if (AnalysisStep == EAnalysisStep::None)
		{
			if (CurrResult.Time < 1.f)
			{
				if (CurrResult.Distance < AllowedStartDistance)
				{
					ANALYSIS_LOG(TEXT("no space to prepare jump. Convex Shape found"));
					break;
				}
				
				ConcaveResult.GapStart = CurrResult.ImpactPoint;
				AnalysisStep = EAnalysisStep::FindingGapStart;
				continue;
			}
			else
			{
				ANALYSIS_LOG(TEXT("no space to prepare jump. In Air"));
				break; // Failed
			}
		}
		else if (AnalysisStep == EAnalysisStep::FindingGapStart)
		{
			if (CurrResult.Time < 1.f)
			{
				if (CurrResult.Distance < AllowedStartDistance)
				{
					ANALYSIS_LOG(TEXT("no space to prepare jump. Convex Shape found"));
					break;
				}
				ConcaveResult.GapStart = CurrResult.ImpactPoint;
			}
			else
			{
				AnalysisStep = EAnalysisStep::FindingGapLength;
				continue;
			}
		}
		else if (AnalysisStep == EAnalysisStep::FindingGapLength)
		{
			if (CurrResult.Time < 1.f)
			{
				ConcaveResult.GapEnd = CurrResult.ImpactPoint;
				AnalysisStep = EAnalysisStep::StartValidatingGapEnd;
			}

			continue;
		}
		else
		{
			if (CurrResult.Time < 1.f)
			{
				float ZDiff = CurrResult.ImpactPoint.Z - ConcaveResult.GapEnd.Z;
				if (ZDiff > -ConcaveOptions.SurfaceHeightDifferenceTolerance && ZDiff < ConcaveOptions.SurfaceHeightDifferenceTolerance)
				{
					++AnalysisStep;
				}
				else if (ZDiff > 0.f)
				{
					ConcaveResult.GapEnd = CurrResult.ImpactPoint;
					AnalysisStep = EAnalysisStep::StartValidatingGapEnd;
				}

				if (AnalysisStep >= EAnalysisStep::EndValidatingGapEnd)
				{
					ConcaveResult.bSuccess = true;
					break;
				}
			}
			else
			{
				ANALYSIS_LOG(TEXT("no space to land jump. In Air"));
				break;
			}
		}

	}

	if (ConcaveResult.bSuccess == false)
	{
		if (AnalysisStep >= EAnalysisStep::StartValidatingGapEnd)
		{
			ANALYSIS_LOG(TEXT("no space to land jump. Too short"));
		}
		return;
	}

	ConcaveResult.Distance = (ConcaveResult.GapStart - StartTrace).Size2D();
	ConcaveResult.MotionWarpDirection = (-Dir).ToOrientationQuat();
	ConcaveResult.GapLength = (ConcaveResult.GapEnd - ConcaveResult.GapStart).Size2D();
	ConcaveResult.HeightDifference = ConcaveResult.GapEnd.Z - ConcaveResult.GapStart.Z;
}

void FTrvAsyncConcaveAnalysisTask::DebugDraw(UWorld* InWorld)
{
	if (bDebugDrawed)
	{
		return;
	}
	bDebugDrawed = true;

	if (InWorld)
	{
		const float DrawDuration = gTrvCustomAsyncAnalysiskMoveDebugDrawDuration;
		{
			if (ForwardRay.bBlockingHit)
			{
				DrawDebugLine(InWorld, ForwardRay.TraceStart, ForwardRay.TraceEnd, FColor::Red, false, DrawDuration);
				DrawDebugSphere(InWorld, ForwardRay.ImpactPoint, 10, 16, FColor::Red, false, DrawDuration);
			}
			else
			{
				DrawDebugLine(InWorld, ForwardRay.TraceStart, ForwardRay.TraceEnd, FColor::Green, false, DrawDuration);
			}
		}

		for (const FHitResult& Curr : VerticalRays)
		{
			if (Curr.bBlockingHit)
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Green, false, DrawDuration);
				DrawDebugSphere(InWorld, Curr.ImpactPoint, 10, 16, FColor::Red, false, DrawDuration);
			}
			else
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Red, false, DrawDuration);
			}
		}

		if (ConcaveResult.bSuccess)
		{
			//DrawDebugSphere(InWorld, ConcaveResult.GapStart, 50, 16, FColor::Cyan, false, DrawDuration);
			//DrawDebugSphere(InWorld, ConcaveResult.GapEnd, 50, 16, FColor::Cyan, false, DrawDuration);

			DrawDebugCoordinateSystem(InWorld, ConcaveResult.GapStart, ConcaveResult.MotionWarpDirection.Rotator(), 50, false, DrawDuration, 0, 5);
			DrawDebugCoordinateSystem(InWorld, ConcaveResult.GapEnd, ConcaveResult.MotionWarpDirection.Rotator(), 50, false, DrawDuration, 0, 5);
		}
	}
}


/**************************************************************************************************
*
*	FTrvAsyncConvexAnalysisTask
*
***/

class FTrvAsyncConvexAnalysisTask : public FNonAbandonableTask
{
public:
	explicit FTrvAsyncConvexAnalysisTask (UTrvCustomMoveArrangeComponent* InOwner)
		: CommonContext(InOwner)
		, bDebugDrawed(false)
	{
	}

	~FTrvAsyncConvexAnalysisTask ();

	void DoWork ();

	void DebugDraw(UWorld* InWorld);

	FORCEINLINE TStatId GetStatId () const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(TrvAsyncConvexAnalysisTask, STATGROUP_ThreadPoolAsyncTasks);
	}
	
	FTrvAsyncAnalaysisTaskCommonContext CommonContext;

	FTrvCustomMoveAsyncConvexAnalysisOptions ConvexOptions;

	FTrvCustomMoveAsyncConvexAnalysisResult ConvexResult;

	TArray<FHitResult> HorizontalRays;
	TArray<FHitResult> VerticalRays;

	bool bDebugDrawed;
};

FTrvAsyncConvexAnalysisTask::~FTrvAsyncConvexAnalysisTask ()
{
	
}

void FTrvAsyncConvexAnalysisTask::DoWork ()
{
	ConvexResult.Reset();
	bDebugDrawed = false;

	static const FName sQueryName = "AsyncConvexAnalysis";
	FCollisionQueryParams CollisonQueryParam = FCollisionQueryParams(sQueryName, false);
	for (AActor* IgnoreActor : CommonContext.IgnoreActors)
	{
		CollisonQueryParam.AddIgnoredActor(IgnoreActor);
	}

	if (IsPakourBlocked(CommonContext, CollisonQueryParam))
	{
		return;
	}

	int32 HeightFrontLedgeHitResultIndex = INDEX_NONE;
	int32 PossibleFrontLedgeHitResultIndex = INDEX_NONE;
	int32 PossibleBackLedgeHitResultIndex = INDEX_NONE;
	int32 PossiblePeakHitResultIndex = INDEX_NONE;
	
	const FVector& Dir = CommonContext.Direction;
	FVector UpVector = FVector::UpVector;

	// Check Height
	FVector HorizontalRayStartPosition = CommonContext.Position;
	{
		HorizontalRayStartPosition.Z += ConvexOptions.HeightCheckStartOffset - CommonContext.CapsuleHalfHeight;

		FVector RayEndOffset = Dir * CommonContext.CheckFarDistance;

		const float Step = (ConvexOptions.HeightCheckMaxLength - ConvexOptions.HeightCheckStartOffset) / (float)ConvexOptions.HeightCheckGranularity;
		
		int32 HitCount = 0;
		HorizontalRays.Reset();
		HorizontalRays.SetNum(ConvexOptions.HeightCheckGranularity);
		FHitResult* DestHitResults = HorizontalRays.GetData();
		FVector RayOrigin = HorizontalRayStartPosition;
		for (int32 Idx = 0; Idx < ConvexOptions.HeightCheckGranularity; ++Idx)
		{
			FVector RayEnd = RayOrigin + RayEndOffset;
			bool bResult = CommonContext.World->LineTraceSingleByChannel(*DestHitResults, RayOrigin, RayEnd, kParkourTraceChannel, CollisonQueryParam);

			HitCount += DestHitResults->IsValidBlockingHit()? 1 : 0;

			++DestHitResults;
			RayOrigin.Z += Step;
		}

		if (HitCount < 2)
		{
			ANALYSIS_LOG(TEXT("FTrvAsyncParkourAnalysisTask. Failed. no horizontal hits"));
			return;
		}
		
#if 0
		if (HitCount < 4)
		{
			float SelectedDist = TNumericLimits<float>::Max();
			for (const FHitResult& Curr : HorizontalRays)
			{
				if (Curr.IsValidBlockingHit() &&
					(SelectedDist >= Curr.Distance || FMath::IsNearlyEqual(SelectedDist, Curr.Distance, 5.f))
				)
				{
					SelectedDist = FMath::Min(Curr.Distance, SelectedDist);
					HeightFrontLedgeHitResultIndex = &Curr - HorizontalRays.GetData();
				}
			}
		}
		else
#endif
		{
			struct FPreliminaryData
			{
				float Distance;
				float DistanceDiff;
				float HeightDiff;
				int32 HitIndex;
			};
			TArray<FPreliminaryData> PreliminaryData;

			float PrevDist = -1.f;
			float PrevHeight = -1.f;
			PreliminaryData.Reserve(HitCount);
			for (const FHitResult& Curr : HorizontalRays)
			{
				if (Curr.IsValidBlockingHit() == false)
				{
					continue;
				}

				if (PreliminaryData.Num() == 0)
				{
					FPreliminaryData& Dest = PreliminaryData.AddDefaulted_GetRef();
					Dest.Distance = Curr.Distance;
					Dest.DistanceDiff = 0.f;
					Dest.HeightDiff = 0.f;
					Dest.HitIndex = &Curr - HorizontalRays.GetData();

					PrevDist = Curr.Distance;
					PrevHeight = 0.f;
				}
				else
				{
					const float CurrHeight = (Curr.TraceStart - HorizontalRayStartPosition).Size();

					FPreliminaryData& Dest = PreliminaryData.AddDefaulted_GetRef();
					Dest.Distance = Curr.Distance;
					Dest.DistanceDiff = Curr.Distance - PrevDist;
					Dest.HeightDiff = CurrHeight - PrevHeight;
					Dest.HitIndex = &Curr - HorizontalRays.GetData();

					PrevDist = Curr.Distance;
					PrevHeight = CurrHeight;
				}
			}

			float PrevDistance = TNumericLimits<float>::Max();
			for (int32 Idx = 1; Idx < PreliminaryData.Num(); ++Idx)
			{
				const FPreliminaryData& Curr = PreliminaryData[Idx];
				float Slope = Curr.DistanceDiff / Curr.HeightDiff;
				if (ConvexOptions.HeightCheckAceeptableSlope < FMath::Abs(Slope)) // How vertical it is. Hueristic
				{
					continue;
				}

				if (HeightFrontLedgeHitResultIndex == INDEX_NONE)
				{
					if (PrevDistance > Curr.Distance)
					{
						PrevDistance = Curr.Distance;
						HeightFrontLedgeHitResultIndex = Curr.HitIndex;
					}
				}
				else
				{
					const int32 StepDiff = Curr.HitIndex - HeightFrontLedgeHitResultIndex;
					if (StepDiff < 2) // Hueristic. ignore distance diff for very next hit
					{
						PrevDistance = Curr.Distance;
						HeightFrontLedgeHitResultIndex = Curr.HitIndex;
					}
					else if (PrevDistance > Curr.Distance)
					{
						PrevDistance = Curr.Distance;
						HeightFrontLedgeHitResultIndex = Curr.HitIndex;
					}
				}
			}
		}
	}

	if (HeightFrontLedgeHitResultIndex == INDEX_NONE)
	{
		ANALYSIS_LOG(TEXT("FTrvAsyncParkourAnalysisTask. Failed. can not find front ledge"));
		return;
	}

	
	// Check Depth
	{
		const FHitResult& PossibleFrontLedgeHit = HorizontalRays[HeightFrontLedgeHitResultIndex];
		FVector StartPosition = PossibleFrontLedgeHit.ImpactPoint;
		StartPosition.Z += ConvexOptions.VerticalRayStartOffset;
		StartPosition += Dir * ConvexOptions.DepthCheckStartOffset;

		FVector RayEndOffset = -UpVector * (ConvexOptions.VerticalRayStartOffset + ConvexOptions.VerticalRayEndOffset);

		const float Step = (ConvexOptions.DepthCheckMaxLength - ConvexOptions.DepthCheckStartOffset) / (float)ConvexOptions.DepthCheckGranularity;
		
		int32 HitCount = 0;
		VerticalRays.Reset();
		VerticalRays.SetNum(ConvexOptions.DepthCheckGranularity);
		FHitResult* DestHitResults = VerticalRays.GetData();
		FVector RayOrigin = StartPosition;
		FVector StepVector = Step * Dir;
		for (int32 Idx = 0; Idx < ConvexOptions.DepthCheckGranularity; ++Idx)
		{
			RayOrigin = StartPosition + float(Idx) *StepVector;
			FVector RayEnd = RayOrigin + RayEndOffset;
			bool bResult = CommonContext.World->LineTraceSingleByChannel(*DestHitResults, RayOrigin, RayEnd, kParkourTraceChannel, CollisonQueryParam);

			HitCount += DestHitResults->IsValidBlockingHit()? 1 : 0;

			++DestHitResults;
		}

		if (HitCount == 0)
		{
			ANALYSIS_LOG(TEXT("FTrvAsyncParkourAnalysisTask. Failed. depth identification"));
			return;
		}
		else
		{
			struct FPreliminaryData
			{
				float Distance;
				float DistanceDiff;
				float DepthDiff;
				int32 HitIndex;
			};
			TArray<FPreliminaryData> PreliminaryData;

			float PrevDist = -1.f;
			float PrevDepth = -1.f;
			PreliminaryData.Reserve(HitCount);
			for (const FHitResult& Curr : VerticalRays)
			{
				if (Curr.IsValidBlockingHit() == false)
				{
					continue;
				}

				if (PreliminaryData.Num() == 0)
				{
					FPreliminaryData& Dest = PreliminaryData.AddDefaulted_GetRef();
					Dest.Distance = Curr.Distance;
					Dest.DistanceDiff = 0.f;
					Dest.DepthDiff = 0.f;
					Dest.HitIndex = &Curr - VerticalRays.GetData();

					PrevDist = Curr.Distance;
					PrevDepth = 0.f;
				}
				else
				{
					const float CurrDepth = (Curr.TraceStart - StartPosition).Size();

					FPreliminaryData& Dest = PreliminaryData.AddDefaulted_GetRef();
					Dest.Distance = Curr.Distance;
					Dest.DistanceDiff = Curr.Distance - PrevDist;
					Dest.DepthDiff = CurrDepth - PrevDepth;
					Dest.HitIndex = &Curr - VerticalRays.GetData();

					PrevDist = Curr.Distance;
					PrevDepth = CurrDepth;
				}
			}
			
			const FPreliminaryData& FirstData = PreliminaryData[0];
			
			PossibleFrontLedgeHitResultIndex = FirstData.HitIndex;
			PossibleBackLedgeHitResultIndex = PossibleFrontLedgeHitResultIndex;

			int32 PreliminaryDataIndex = 1;
			for (; PreliminaryDataIndex < PreliminaryData.Num(); ++PreliminaryDataIndex)
			{				
				const FPreliminaryData& Curr = PreliminaryData[PreliminaryDataIndex];
				float Slope = Curr.DistanceDiff / Curr.DepthDiff;
				if (ConvexOptions.DepthCheckAceeptableSlope < FMath::Abs(Slope)) // How Horizontal it is. Hueristic
				{
					break;
				}
				
				PossibleBackLedgeHitResultIndex = Curr.HitIndex;
			}
						
			float PrevDistance = TNumericLimits<float>::Max();
			for (const FPreliminaryData& Curr : PreliminaryData)
			{
				if (Curr.HitIndex >= PossibleBackLedgeHitResultIndex)
				{
					break;
				}

				if (Curr.Distance < PrevDistance)
				{
					PrevDistance = Curr.Distance;
					PossiblePeakHitResultIndex = Curr.HitIndex;
				}
			}

			// Find possible back floor
			if ((PossibleBackLedgeHitResultIndex + 3) < VerticalRays.Num())
			{
				int32 BackFloorStartIndex = PossibleBackLedgeHitResultIndex + 1;
				do
				{
					const FHitResult& CheckStart = VerticalRays[BackFloorStartIndex];
					if (CheckStart.bStartPenetrating)
					{
						break;
					}
					else
					{
						int32 BackFloorIndex = BackFloorStartIndex + 1;
						for (; BackFloorIndex < VerticalRays.Num(); ++BackFloorIndex)
						{
							const FHitResult& Curr = VerticalRays[BackFloorIndex];

							if (Curr.bBlockingHit != CheckStart.bBlockingHit ||
								FMath::IsNearlyEqual(Curr.Distance, CheckStart.Distance, 10.f) == false)
							{
								break;
							}
						}

						if ((BackFloorIndex - BackFloorStartIndex) < 2)
						{
							BackFloorStartIndex = BackFloorIndex;
						}
						else
						{
							const FHitResult& BackLedge = VerticalRays[PossibleBackLedgeHitResultIndex];

							ConvexResult.BackFloorStartDistance = (CheckStart.TraceStart - BackLedge.TraceStart).Size();
							if (CheckStart.bBlockingHit == false)
							{
								ConvexResult.BackFloorHeightDifference = -400.f;
							}
							else
							{
								ConvexResult.BackFloorHeightDifference = CheckStart.ImpactPoint.Z - BackLedge.ImpactPoint.Z;
							}

							break;
						}
					}
				}
				while(BackFloorStartIndex < VerticalRays.Num());
			}

		}
	}

	ConvexResult.bSuccess = true;

	const FHitResult& HeightHit = HorizontalRays[HeightFrontLedgeHitResultIndex];
	const FHitResult& FrontHit = VerticalRays[PossibleFrontLedgeHitResultIndex];
	const FHitResult& BackHit = VerticalRays[PossibleBackLedgeHitResultIndex];
	
	ConvexResult.Distance = HeightHit.Distance;
	ConvexResult.Depth = (BackHit.TraceStart - FrontHit.TraceStart).Size();
	ConvexResult.Height = FrontHit.ImpactPoint.Z - HorizontalRayStartPosition.Z + ConvexOptions.HeightCheckStartOffset;
	ConvexResult.FrontLedgeLocation = FrontHit.ImpactPoint;
	ConvexResult.BackLedgeLocation = BackHit.ImpactPoint;

	if (PossiblePeakHitResultIndex != INDEX_NONE)
	{
		const FHitResult& PeakHit = VerticalRays[PossiblePeakHitResultIndex];
		ConvexResult.PeakHeight = PeakHit.ImpactPoint.Z - HorizontalRayStartPosition.Z + ConvexOptions.HeightCheckStartOffset;
		ConvexResult.PeakRatio = (ConvexResult.Depth > FLT_EPSILON)?
			(PeakHit.ImpactPoint - ConvexResult.FrontLedgeLocation).Size() / ConvexResult.Depth :
			0.0f;
	}
	else
	{
		ConvexResult.PeakHeight = 0.f;
		ConvexResult.PeakRatio = 0.0f;
	}

	const FVector InvDir = -Dir;
	const FVector HitNormal = HeightHit.ImpactNormal.GetSafeNormal2D();

	const float Base = ConvexOptions.InvDirToHitNormalBlendRangeByHeight.Y - ConvexOptions.InvDirToHitNormalBlendRangeByHeight.X;
	float Ratio = (ConvexResult.Height - ConvexOptions.InvDirToHitNormalBlendRangeByHeight.X) / Base;
	Ratio = FMath::Clamp(Ratio, 0.f, 1.f);

	ConvexResult.MotionWarpDirection = FQuat::Slerp(InvDir.ToOrientationQuat(), HitNormal.ToOrientationQuat(), Ratio);
}

void FTrvAsyncConvexAnalysisTask::DebugDraw(UWorld* InWorld)
{
	if (bDebugDrawed)
	{
		return;
	}
	bDebugDrawed = true;

	if (InWorld)
	{
		const float DrawDuration = gTrvCustomAsyncAnalysiskMoveDebugDrawDuration;
		
		for (const FHitResult& Curr : HorizontalRays)
		{
			if (Curr.bBlockingHit)
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Green, false, DrawDuration);
				DrawDebugSphere(InWorld, Curr.ImpactPoint, 10, 16, FColor::Red, false, DrawDuration);
			}
			else
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Red, false, DrawDuration);
			}
		}

		for (const FHitResult& Curr : VerticalRays)
		{
			if (Curr.bBlockingHit)
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Green, false, DrawDuration);
				DrawDebugSphere(InWorld, Curr.ImpactPoint, 10, 16, FColor::Red, false, DrawDuration);
			}
			else
			{
				DrawDebugLine(InWorld, Curr.TraceStart, Curr.TraceEnd, FColor::Red, false, DrawDuration);
			}
		}

		if (ConvexResult.bSuccess)
		{
			DrawDebugCoordinateSystem(InWorld, ConvexResult.FrontLedgeLocation, ConvexResult.MotionWarpDirection.Rotator(), 50, false, DrawDuration, 0, 5);
			DrawDebugSphere(InWorld, ConvexResult.BackLedgeLocation, 30, 16, FColor::Cyan, false, DrawDuration);
		}
	}
}


/**************************************************************************************************
*
*	UTrvCustomMoveArrangeComponent
*
***/

UTrvCustomMoveArrangeComponent::UTrvCustomMoveArrangeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	AsyncConcaveAnalysisTask = nullptr;
	AsyncConvexAnalysisTask = nullptr;
}

void UTrvCustomMoveArrangeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		OwnerCharacter = Character;
		OwnerMotionWarpingComponent = Cast<UMotionWarpingComponent>(Character->FindComponentByClass(UMotionWarpingComponent::StaticClass()));
	}
}

void UTrvCustomMoveArrangeComponent::OnUnregister()
{
	ClearAsyncConvexAnalysisTask();
	ClearAsyncConcaveAnalysisTask();

	Super::OnUnregister();
}

void UTrvCustomMoveArrangeComponent::QueueAsyncConvexAnalysis(
	float InCheckFarDistance,
	FVector InDirection,
	const bool InbForceSynchronous,
	const FTrvCustomMoveAsyncConvexAnalysisOptions& InConvexDetectOptions,
	const bool InbDrawDebug
)
{
	ACharacter* Character = OwnerCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}
	APlayerController* CurrController = Cast<APlayerController>(Character->Controller);
	if (CurrController == nullptr)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp->MovementMode != EMovementMode::MOVE_Walking)
	{
		return;
	}

	FAsyncTask<FTrvAsyncConvexAnalysisTask>* AsyncTaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConvexAnalysisTask>*>(AsyncConvexAnalysisTask);
	if (AsyncTaskPtr == nullptr)
	{
		AsyncTaskPtr = new FAsyncTask<FTrvAsyncConvexAnalysisTask>(this);
		AsyncConvexAnalysisTask = AsyncTaskPtr;
	}
	else
	{
		AsyncTaskPtr->EnsureCompletion(false);
		check(AsyncTaskPtr->IsIdle());

	}
	//ClearAsyncParkourAnalysisTask();

	//FAsyncTask<FTrvAsyncConvexAnalysisTask>* NewAsyncTask = new FAsyncTask<FTrvAsyncConvexAnalysisTask>(this);
	//AsyncParkourAnalysisTask = NewAsyncTask;
	FTrvAsyncConvexAnalysisTask& Task = AsyncTaskPtr->GetTask();
	
	AsyncConvexAnalaysisQueuedCycle = FPlatformTime::Cycles64();

	UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
	Task.CommonContext.Direction = InDirection.GetSafeNormal2D();
	Task.CommonContext.Position = CapsuleComp->GetComponentLocation();
	Task.CommonContext.ControlRotation = CurrController->GetControlRotation();
	Task.CommonContext.CapsuleRadius = CapsuleComp->GetScaledCapsuleRadius();
	Task.CommonContext.CapsuleHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
	Task.CommonContext.FrameCounter = GFrameCounter;
	Task.CommonContext.World = GetWorld();
	Task.CommonContext.IgnoreActors.Reset();
	Task.CommonContext.IgnoreActors.Add(Character);

	const float AdditionalCheckDistance = MoveComp->GetMaxSpeed() * gTrvCustomMoveAsyncConvexAnalysisMaxFreshSeconds;
	Task.CommonContext.CheckFarDistance = InCheckFarDistance + AdditionalCheckDistance;
	
	Task.ConvexResult.Reset();
	Task.ConvexOptions = InConvexDetectOptions;

	AsyncConvexAnalysisVaidationContext.Position = Task.CommonContext.Position;
	AsyncConvexAnalysisVaidationContext.Direction = Task.CommonContext.Direction;
	AsyncConvexAnalysisVaidationContext.InputCheckDistance = InCheckFarDistance;
	AsyncConvexAnalysisVaidationContext.AdditionalCheckDistance = AdditionalCheckDistance;

	if (InbForceSynchronous)
	{
		AsyncTaskPtr->StartSynchronousTask();
	}
	else
	{
		AsyncTaskPtr->StartBackgroundTask();
	}

	if (InbDrawDebug)
	{
		UWorld* World = GetWorld();
		const float DrawDuration = gTrvCustomAsyncAnalysiskMoveDebugDrawDuration;
		DrawDebugLine(World, Task.CommonContext.Position, Task.CommonContext.Position + Task.CommonContext.Direction * 50, FColor::Yellow, false, DrawDuration);
		DrawDebugSphere(World, Task.CommonContext.Position, 10, 16, FColor::Yellow, false, DrawDuration);
	}

	ANALYSIS_LOG(TEXT("QueueAsyncParkourAnalysis. %llu"), GFrameCounter);
}

bool UTrvCustomMoveArrangeComponent::HasQueuedAsyncConvexAnalysis(FVector2D InValidHeightRange)
{
#if 1
	return AsyncConvexAnalysisTask != nullptr &&
		IsValidAnalysisTask(AsyncConvexAnalysisTask, InValidHeightRange, AsyncConvexAnalysisVaidationContext, TEXT("Parkour"));
#else
	const uint64 Diff = FPlatformTime::Cycles64() - AsyncConvexAnalaysisQueuedCycle;
	return AsyncConvexAnalysisTask != nullptr &&
		FPlatformTime::ToSeconds64(Diff) < gTrvCustomMoveAsyncGapJumpAnalysisMaxFreshSeconds &&
		IsValidAnalysisTask(AsyncConvexAnalysisTask, InValidHeightRange, AsyncConvexAnalysisVaidationContext, TEXT("Parkour"));
#endif
}

bool UTrvCustomMoveArrangeComponent::ConsumeAsyncConvexAnalysis(
	float InSpeedLowerLimit,
	const bool bDebugDraw,
	float& OutSpeed,
	FTrvCustomMoveAsyncConvexAnalysisResult& OutConvexResult
)
{
	if (AsyncConvexAnalysisTask == nullptr)
	{
		return false;
	}

	FAsyncTask<FTrvAsyncConvexAnalysisTask>* AsyncTaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConvexAnalysisTask>*>(AsyncConvexAnalysisTask);
	if (AsyncTaskPtr->IsDone() == false)
	{
		AsyncTaskPtr->EnsureCompletion();
	}

	const FTrvAsyncConvexAnalysisTask& Task = AsyncTaskPtr->GetTask();
	
	if (bDebugDraw)
	{
		FTrvAsyncConvexAnalysisTask& TaskUnConst = AsyncTaskPtr->GetTask();
		TaskUnConst.DebugDraw(GetWorld());
	}

	ACharacter* Character = OwnerCharacter.Get();
	if (Character == nullptr)
	{
		return false;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp->MovementMode != EMovementMode::MOVE_Walking)
	{
		return false;
	}
	float Speed2DSQ = MoveComp->Velocity.SizeSquared2D();
	if (Speed2DSQ < FMath::Square(InSpeedLowerLimit))
	{
		return false;
	}

	const FVector OldDirection = Task.CommonContext.Direction;
	const FVector CurrVelocity = MoveComp->Velocity.GetSafeNormal2D();
	const float DirDotTolerance = FMath::Cos(FMath::DegreesToRadians(gTrvCustomMoveAsyncAnalysisDirTolerance));
	if (FVector::DotProduct(OldDirection, CurrVelocity) < DirDotTolerance)
	{
		return false;
	}

	UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
	FVector CurrLoc = CapsuleComp->GetComponentLocation();
	
	OutSpeed = MoveComp->Velocity.Size2D();

	if (Task.ConvexResult.bSuccess)
	{
		float Distance = 0.f;
		if (false == IsValidAsyncAnalysisResult(Distance, OldDirection, DirDotTolerance, Task.ConvexResult.FrontLedgeLocation, CurrLoc))
		{
			return false;
		}

		OutConvexResult = Task.ConvexResult;
		OutConvexResult.Distance = Distance;

		return OutConvexResult.bSuccess;
	}

	return false;
}

void UTrvCustomMoveArrangeComponent::QueueAsyncConcaveAnalysis(
	float InCheckFarDistance,
	FVector InDirection,
	const bool InbForceSynchronous,
	const FTrvCustomMoveAsyncConcaveAnalysisOptions& InConcaveDetectOptions,
	const bool InbDrawDebug
)
{
	ACharacter* Character = OwnerCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}
	APlayerController* CurrController = Cast<APlayerController>(Character->Controller);
	if (CurrController == nullptr)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp->MovementMode != EMovementMode::MOVE_Walking)
	{
		return;
	}

	FAsyncTask<FTrvAsyncConcaveAnalysisTask>* AsyncTaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConcaveAnalysisTask>*>(AsyncConcaveAnalysisTask);
	if (AsyncTaskPtr == nullptr)
	{
		AsyncTaskPtr = new FAsyncTask<FTrvAsyncConcaveAnalysisTask>(this);
		AsyncConcaveAnalysisTask = AsyncTaskPtr;
	}
	else
	{
		AsyncTaskPtr->EnsureCompletion(false);
		check(AsyncTaskPtr->IsIdle());

	}
	//ClearAsyncGapJumpAnalysisTask();

	//FAsyncTask<FTrvAsyncConcaveAnalysisTask>* NewAsyncTask = new FAsyncTask<FTrvAsyncConcaveAnalysisTask>(this);
	//AsyncGapJumpAnalysisTask = NewAsyncTask;
	FTrvAsyncConcaveAnalysisTask& Task = AsyncTaskPtr->GetTask();
	
	AsyncConcaveAnalaysisQueuedCycle = FPlatformTime::Cycles64();

	UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
	Task.CommonContext.Direction = InDirection.GetSafeNormal2D();
	Task.CommonContext.Position = CapsuleComp->GetComponentLocation();
	Task.CommonContext.ControlRotation = CurrController->GetControlRotation();
	Task.CommonContext.CapsuleRadius = CapsuleComp->GetScaledCapsuleRadius();
	Task.CommonContext.CapsuleHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
	Task.CommonContext.FrameCounter = GFrameCounter;
	Task.CommonContext.World = GetWorld();
	Task.CommonContext.IgnoreActors.Reset();
	Task.CommonContext.IgnoreActors.Add(Character);

	const float AdditionalCheckDistance = MoveComp->GetMaxSpeed() * gTrvCustomMoveAsyncConcaveAnalysisMaxFreshSeconds;
	Task.CommonContext.CheckFarDistance = InCheckFarDistance + AdditionalCheckDistance;

	Task.ConcaveResult.Reset();

	Task.ConcaveOptions = InConcaveDetectOptions;

	AsyncConcaveAnalysisVaidationContext.Position = Task.CommonContext.Position;
	AsyncConcaveAnalysisVaidationContext.Direction = Task.CommonContext.Direction;
	AsyncConcaveAnalysisVaidationContext.InputCheckDistance = InCheckFarDistance;
	AsyncConcaveAnalysisVaidationContext.AdditionalCheckDistance = AdditionalCheckDistance;

	if (InbForceSynchronous)
	{
		AsyncTaskPtr->StartSynchronousTask();
	}
	else
	{
		AsyncTaskPtr->StartBackgroundTask();
	}
	
	if (InbDrawDebug)
	{
		UWorld* World = GetWorld();
		const float DrawDuration = gTrvCustomAsyncAnalysiskMoveDebugDrawDuration;
		DrawDebugLine(World, Task.CommonContext.Position, Task.CommonContext.Position + Task.CommonContext.Direction * 50, FColor::Yellow, false, DrawDuration);
		DrawDebugSphere(World, Task.CommonContext.Position, 10, 16, FColor::Yellow, false, DrawDuration);
	}

	ANALYSIS_LOG(TEXT("QueueAsyncConcaveAnalysis. %llu"), GFrameCounter);
}

bool UTrvCustomMoveArrangeComponent::HasQueuedAsyncConcaveAnalysis(FVector2D InValidHeightRange)
{
#if 1
	return AsyncConcaveAnalysisTask != nullptr &&
		IsValidAnalysisTask(AsyncConcaveAnalysisTask, InValidHeightRange, AsyncConcaveAnalysisVaidationContext, TEXT("GapJump"));
#else
	const uint64 Diff = FPlatformTime::Cycles64() - AsyncConcaveAnalaysisQueuedCycle;
	return AsyncConcaveAnalysisTask != nullptr &&
		FPlatformTime::ToSeconds64(Diff) < gTrvCustomMoveAsyncGapJumpAnalysisMaxFreshSeconds &&
		IsValidAnalysisTask(AsyncConcaveAnalysisTask, InValidHeightRange,AsyncConcaveAnalysisVaidationContext, TEXT("GapJump"));
#endif
}

bool UTrvCustomMoveArrangeComponent::IsValidAnalysisTask(
	const void* TaskPtr,
	const FVector2D& InValidHeightRange,
	const FQueuedJabValidationContext& Context,
	const TCHAR* DebugStr
) const
{
	if (TaskPtr == nullptr)
	{
		return false;
	}

	ACharacter* Character = OwnerCharacter.Get();
	if (Character == nullptr)
	{
		return false;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp == nullptr)
	{
		return false;
	}

	const FVector& OldDirection = Context.Direction;
	FVector CurrDirection = FVector::ForwardVector;
	if (MoveComp->Velocity.SizeSquared2D() > 100.f)
	{
		CurrDirection = MoveComp->Velocity.GetSafeNormal2D();
	}
	else
	{
		CurrDirection = Character->GetActorForwardVector().GetSafeNormal2D();
	}

	const float DirDotTolerance = FMath::Cos(FMath::DegreesToRadians(gTrvCustomMoveAsyncAnalysisDirTolerance));
	if (FVector::DotProduct(OldDirection, CurrDirection) < DirDotTolerance)
	{
		return false;
	}

	UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
	FVector CurrLoc = CapsuleComp->GetComponentLocation();

	FVector FromStart = CurrLoc - Context.Position;
	const float FromStartDist2DSQ = FromStart.SizeSquared2D();
	const float ValidDistanceLimit = Context.AdditionalCheckDistance + gTrvCustomMoveAsyncAnalysisAcceptableAdditionalDistance;
	if (FromStartDist2DSQ > FMath::Square(ValidDistanceLimit))
	{
		ANALYSIS_LOG(TEXT("IsValidAnalysisTask<%s>. Failed Distance %.2f"), DebugStr, FMath::Sqrt(FromStartDist2DSQ));
		return false;
	}

	if (FromStart.Z < InValidHeightRange.X || FromStart.Z > InValidHeightRange.Y)
	{
		ANALYSIS_LOG(TEXT("IsValidAnalysisTask<%s>. Failed Height %.2f"), DebugStr, FromStart.Z);
		return false;
	}
	
	const float FromStartDist2D = FMath::Sqrt(FromStartDist2DSQ);

	const FVector ToGapStartDir = FVector(FromStart.X / FromStartDist2D, FromStart.Y / FromStartDist2D, 0.0);
	if (FVector::DotProduct(OldDirection, ToGapStartDir) < DirDotTolerance)
	{
		ANALYSIS_LOG(TEXT("IsValidAnalysisTask<%s>. Failed dir"), DebugStr);
		return false;
	}

	return true;
}

bool UTrvCustomMoveArrangeComponent::ConsumeAsyncConcaveAnalysis(
	float InSpeedLowerLimit,
	const bool bDebugDraw,
	float& OutSpeed,
	FTrvCustomMoveAsyncConcaveAnalysisResult& OutConcaveResult
)
{
	if (AsyncConcaveAnalysisTask == nullptr)
	{
		return false;
	}

	FAsyncTask<FTrvAsyncConcaveAnalysisTask>* AsyncTaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConcaveAnalysisTask>*>(AsyncConcaveAnalysisTask);
	if (AsyncTaskPtr->IsDone() == false)
	{
		AsyncTaskPtr->EnsureCompletion();
	}

	const FTrvAsyncConcaveAnalysisTask& Task = AsyncTaskPtr->GetTask();
	
	if (bDebugDraw)
	{
		FTrvAsyncConcaveAnalysisTask& TaskUnConst = AsyncTaskPtr->GetTask();
		TaskUnConst.DebugDraw(GetWorld());
	}

	if (Task.ConcaveResult.bSuccess == false)
	{
		return false;
	}

	ACharacter* Character = OwnerCharacter.Get();
	if (Character == nullptr)
	{
		return false;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (MoveComp->MovementMode != EMovementMode::MOVE_Walking)
	{
		return false;
	}
	float Speed2DSQ = MoveComp->Velocity.SizeSquared2D();
	if (Speed2DSQ < FMath::Square(InSpeedLowerLimit))
	{
		return false;
	}

	const FVector OldDirection = Task.CommonContext.Direction;
	const FVector CurrVelocity = MoveComp->Velocity.GetSafeNormal2D();
	const float DirDotTolerance = FMath::Cos(FMath::DegreesToRadians(gTrvCustomMoveAsyncAnalysisDirTolerance));
	if (FVector::DotProduct(OldDirection, CurrVelocity) < DirDotTolerance)
	{
		return false;
	}
	
	UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
	FVector CurrLoc = CapsuleComp->GetComponentLocation();

	float Distance = 0.f;
	if (false == IsValidAsyncAnalysisResult(Distance, OldDirection, DirDotTolerance, Task.ConcaveResult.GapStart, CurrLoc))
	{
		return false;
	}

	OutSpeed = MoveComp->Velocity.Size2D();
	OutConcaveResult = Task.ConcaveResult;
	OutConcaveResult.Distance = Distance;
	return OutConcaveResult.bSuccess;
}

void UTrvCustomMoveArrangeComponent::ClearAsyncConcaveAnalysisTask()
{
	if (AsyncConcaveAnalysisTask == nullptr)
	{
		return;
	}

	FAsyncTask<FTrvAsyncConcaveAnalysisTask>* TaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConcaveAnalysisTask>*>(AsyncConcaveAnalysisTask);
	AsyncConcaveAnalysisTask = nullptr;
	TaskPtr->EnsureCompletion();
	delete TaskPtr;
}

void UTrvCustomMoveArrangeComponent::ClearAsyncConvexAnalysisTask()
{
	if (AsyncConvexAnalysisTask == nullptr)
	{
		return;
	}

	FAsyncTask<FTrvAsyncConvexAnalysisTask>* TaskPtr = reinterpret_cast<FAsyncTask<FTrvAsyncConvexAnalysisTask>*>(AsyncConvexAnalysisTask);
	AsyncConvexAnalysisTask = nullptr;
	TaskPtr->EnsureCompletion();
	delete TaskPtr;
}

#undef ANALYSIS_LOG
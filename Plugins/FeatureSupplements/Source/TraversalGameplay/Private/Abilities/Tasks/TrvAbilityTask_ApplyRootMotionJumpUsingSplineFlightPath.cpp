


#include "Abilities/Tasks/TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemLog.h"
#include "Net/UnrealNetwork.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"

#include "TrvSplineFlightPath.h"
#include "TrvUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath)


/**************************************************************************************************
*
*	LocalFunctions
*
***/

struct FTrvRootMotionUtils
{
	static float EvaluateFloatCurveAtFraction(const UCurveFloat& Curve, const float Fraction)
	{
		float MinCurveTime(0.f);
		float MaxCurveTime(1.f);

		Curve.GetTimeRange(MinCurveTime, MaxCurveTime);
		return Curve.GetFloatValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), Fraction));
	}

	static FVector EvaluateVectorCurveAtFraction(const UCurveVector& Curve, const float Fraction)
	{
		float MinCurveTime(0.f);
		float MaxCurveTime(1.f);

		Curve.GetTimeRange(MinCurveTime, MaxCurveTime);
		return Curve.GetVectorValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), Fraction));
	}

};


/**************************************************************************************************
*
*	FTrvRootMotionSource_JumpUsingSplineFlightPath
*
***/

FTrvRootMotionSource_JumpUsingSplineFlightPath::FTrvRootMotionSource_JumpUsingSplineFlightPath()
	: BlendInDuration(0.2f)
	, bDisableTimeout(false)
	, SplineFlightPath(nullptr)
	, TimeMappingCurve(nullptr)
	, SavedHalfwayLocation(FVector::ZeroVector)
{
	// Don't allow partial end ticks. Jump forces are meant to provide velocity that
	// carries through to the end of the jump, and if we do partial ticks at the very end,
	// it means the provided velocity can be significantly reduced on the very last tick,
	// resulting in lost momentum. This is not desirable for jumps.
	Settings.SetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
}

bool FTrvRootMotionSource_JumpUsingSplineFlightPath::IsTimeOutEnabled() const
{
	if (bDisableTimeout)
	{
		return false;
	}
	return FRootMotionSource::IsTimeOutEnabled();
}

FRootMotionSource* FTrvRootMotionSource_JumpUsingSplineFlightPath::Clone() const
{
	FTrvRootMotionSource_JumpUsingSplineFlightPath* CopyPtr = new FTrvRootMotionSource_JumpUsingSplineFlightPath(*this);
	return CopyPtr;
}

bool FTrvRootMotionSource_JumpUsingSplineFlightPath::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}

	// We can cast safely here since in FRootMotionSource::Matches() we ensured ScriptStruct equality
	const FTrvRootMotionSource_JumpUsingSplineFlightPath* OtherCast = static_cast<const FTrvRootMotionSource_JumpUsingSplineFlightPath*>(Other);

	return bDisableTimeout == OtherCast->bDisableTimeout &&
		SplineFlightPath == OtherCast->SplineFlightPath &&
		TimeMappingCurve == OtherCast->TimeMappingCurve;
}

bool FTrvRootMotionSource_JumpUsingSplineFlightPath::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
	// Check that it matches
	if (!FRootMotionSource::MatchesAndHasSameState(Other))
	{
		return false;
	}

	return true; // JumpForce has no unique state
}

bool FTrvRootMotionSource_JumpUsingSplineFlightPath::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	if (!FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup))
	{
		return false;
	}

	return true; // JumpForce has no unique state other than Time which is handled by FRootMotionSource
}


void FTrvRootMotionSource_JumpUsingSplineFlightPath::Init(class UCharacterMovementComponent* InMoveComp)
{	
	MaxDistance = SplineFlightPath->GetFlightPathLength();
	StartPerpendicularBias = 0.f;
	StartWorldPosition = InMoveComp->UpdatedComponent->GetComponentLocation();

	StartPerpendicularBias = SplineFlightPath->CaclculateNearestStartPerpendicularBias(StartWorldPosition);
}

void FTrvRootMotionSource_JumpUsingSplineFlightPath::PrepareRootMotion(
	float SimulationTime, 
	float MovementTickTime,
	const ACharacter& Character, 
	const UCharacterMovementComponent& MoveComponent
)
{
	RootMotionParams.Clear();

	if (Duration > UE_SMALL_NUMBER && MovementTickTime > UE_SMALL_NUMBER && SimulationTime > UE_SMALL_NUMBER)
	{
		const FVector ActorLoc = MoveComponent.UpdatedComponent->GetComponentLocation();

		float NextSimulationTime = (GetTime() + SimulationTime);
		float TargetTimeFraction = NextSimulationTime / Duration;
		
		float FromStartPositionBlendRatioForTarget = 1.f;
		if (BlendInDuration > FLT_EPSILON)
		{
			if (BlendInDuration >= NextSimulationTime)
			{
				FromStartPositionBlendRatioForTarget = NextSimulationTime / BlendInDuration;
			}
		}

		// If we're beyond specified duration, we need to re-map times so that
		// we continue our desired ending velocity
		if (TargetTimeFraction > 1.f)
		{
			float TimeFractionPastAllowable = TargetTimeFraction - 1.0f;
			TargetTimeFraction -= TimeFractionPastAllowable;
		}

		float TargetMoveFraction = TargetTimeFraction;
		
		if (TimeMappingCurve)
		{
			TargetMoveFraction  = FTrvRootMotionUtils::EvaluateFloatCurveAtFraction(*TimeMappingCurve, TargetMoveFraction);
		}

		float TargetPerpendicularBias = FMath::Lerp(StartPerpendicularBias, 0.f, TargetMoveFraction);

		FVector TargetLocation = ActorLoc;
		const FVector CurrForwardVector = MoveComponent.UpdatedComponent->GetComponentQuat().GetAxisX();
		FVector TargetForwardVector = CurrForwardVector;
		{
			float TargetMovedDistance = MaxDistance * TargetMoveFraction;

			FVector TargetPathSegmentForwardVector;
			FVector TargetPathSegmentUpVector;
			FVector TargetPathSegmentSideVector;
			FVector TargetPathSegmentCenter;
			float TargetPathSegmentRightMaxOffset;
			float TargetPathSegmentLeftMaxOffset;
			bool bValidTargetPath = SplineFlightPath->GetFlightPathPoint(
				TargetMovedDistance,
				TargetPathSegmentForwardVector,
				TargetPathSegmentUpVector,
				TargetPathSegmentSideVector,
				TargetPathSegmentCenter,
				TargetPathSegmentRightMaxOffset,
				TargetPathSegmentLeftMaxOffset
			);
			if (bValidTargetPath)
			{
				float TargetOffset = FMath::Clamp(TargetPerpendicularBias, TargetPathSegmentLeftMaxOffset, TargetPathSegmentRightMaxOffset);
				TargetLocation = TargetPathSegmentCenter + TargetPathSegmentSideVector * TargetOffset;
			
				if (TargetPathSegmentForwardVector.SizeSquared2D() > FLT_EPSILON)
				{
					TargetPathSegmentForwardVector.Z = 0.f;
					TargetPathSegmentForwardVector.Normalize();
				}
				TargetForwardVector = FMath::Lerp(TargetForwardVector, TargetPathSegmentForwardVector, FromStartPositionBlendRatioForTarget);
			}
			TargetLocation = FMath::Lerp(ActorLoc, TargetLocation, FromStartPositionBlendRatioForTarget);
			
		}

		const FVector Force = (TargetLocation - ActorLoc) / MovementTickTime;
		FQuat DeltaRot = FQuat::FindBetweenNormals(CurrForwardVector, TargetForwardVector);

		// Debug
#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() != 0)
		{
			static const TConsoleVariableData<float>* CVarDebugRootMotionSourcesLifetime = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("p.RootMotion.DebugSourceLifeTime"));
			const float DebugLifetime = CVarDebugRootMotionSourcesLifetime? CVarDebugRootMotionSourcesLifetime->GetValueOnGameThread() : 3.f;

			// Actor
			DrawDebugCapsule(Character.GetWorld(), ActorLoc, Character.GetSimpleCollisionHalfHeight(), Character.GetSimpleCollisionRadius(), FQuat::Identity, FColor::Yellow, true, DebugLifetime);
			
			// Current Target
			DrawDebugCapsule(Character.GetWorld(), TargetLocation, Character.GetSimpleCollisionHalfHeight(), Character.GetSimpleCollisionRadius(), FQuat::Identity, FColor::Green, true, DebugLifetime);

			// Force
			DrawDebugLine(Character.GetWorld(), ActorLoc, ActorLoc+Force, FColor::Blue, true, DebugLifetime);
			
			// Curr Target Rotation
			DrawDebugLine(Character.GetWorld(), TargetLocation, TargetLocation+TargetForwardVector*300, FColor::Red, true, DebugLifetime);

			// Destination point
			FVector DestPathSegmentForwardVector;
			FVector DestPathSegmentUpVector;
			FVector DestPathSegmentSideVector;
			FVector DestPathSegmentCenter;
			float DestPathSegmentRightMaxOffset;
			float DestPathSegmentLeftMaxOffset;
			SplineFlightPath->GetFlightPathPoint(
				MaxDistance,
				DestPathSegmentForwardVector,
				DestPathSegmentUpVector,
				DestPathSegmentSideVector,
				DestPathSegmentCenter,
				DestPathSegmentRightMaxOffset,
				DestPathSegmentLeftMaxOffset
			);

			DrawDebugCapsule(Character.GetWorld(), DestPathSegmentCenter, Character.GetSimpleCollisionHalfHeight(), Character.GetSimpleCollisionRadius(), FQuat::Identity, FColor::White, true, DebugLifetime);

			UE_LOG(LogTrvGeneral,
				VeryVerbose,
				TEXT("FTrvRootMotionSource_JumpUsingSplineFlightPath %s %s preparing from %f to %f from (%s) to (%s) resulting force %s"), 
				Character.GetLocalRole() == ROLE_AutonomousProxy ? TEXT("AUTONOMOUS") : TEXT("AUTHORITY"),
				Character.bClientUpdating ? TEXT("UPD") : TEXT("NOR"),
				GetTime(), GetTime() + SimulationTime, 
				*ActorLoc.ToString(), *TargetLocation.ToString(), 
				*Force.ToString());

			{
				FString AdjustedDebugString = FString::Printf(
					TEXT("    FTrvRootMotionSource_JumpUsingSplineFlightPath::Prep Force(%s) SimTime(%.3f) MoveTime(%.3f) EndP(%.3f)"),
					*Force.ToCompactString(), SimulationTime, MovementTickTime, TargetMoveFraction
				);
				RootMotionSourceDebug::PrintOnScreen(Character, AdjustedDebugString);
			}
		}
#endif

		const FTransform NewTransform(DeltaRot, Force);
		RootMotionParams.Set(NewTransform);
	}
	else
	{
		checkf(Duration > UE_SMALL_NUMBER, TEXT("FTrvRootMotionSource_JumpUsingSplineFlightPath prepared with invalid duration."));
	}

	SetTime(GetTime() + SimulationTime);
}

bool FTrvRootMotionSource_JumpUsingSplineFlightPath::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}

	Ar << bDisableTimeout;
	Ar << SplineFlightPath;
	Ar << TimeMappingCurve;

	bOutSuccess = true;
	return true;
}

UScriptStruct* FTrvRootMotionSource_JumpUsingSplineFlightPath::GetScriptStruct() const
{
	return FTrvRootMotionSource_JumpUsingSplineFlightPath::StaticStruct();
}

FString FTrvRootMotionSource_JumpUsingSplineFlightPath::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FTrvRootMotionSource_JumpUsingSplineFlightPath %s"), LocalID, *InstanceName.GetPlainNameString());
}

void FTrvRootMotionSource_JumpUsingSplineFlightPath::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SplineFlightPath);
	Collector.AddReferencedObject(TimeMappingCurve);

	FRootMotionSource::AddReferencedObjects(Collector);
}


/**************************************************************************************************
*
*	UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath
*
***/

UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	SplineFlightPath = nullptr;
	TimeMappingCurve = nullptr;
	bHasLanded = false;
}

UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath* UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::TrvApplyRootMotionJumpUsingSplineFlightPath(
	UGameplayAbility* OwningAbility,
	FName TaskInstanceName,
	ATrvSplineFlightPath* Path,
	float BlendInDuration,
	float Duration,
	float MinimumLandedTriggerTime,
	bool bFinishOnLanded,
	ERootMotionFinishVelocityMode VelocityOnFinishMode,
	FVector SetVelocityOnFinish,
	float ClampVelocityOnFinish,
	UCurveFloat* TimeMappingCurve
)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Duration(Duration);

	UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath* MyTask = NewAbilityTask<UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath>(OwningAbility, TaskInstanceName);

	MyTask->ForceName = TaskInstanceName;
	MyTask->BlendInDuration = FMath::Max(BlendInDuration, 0.f);
	MyTask->Duration = FMath::Max(Duration, KINDA_SMALL_NUMBER); // No zero duration
	MyTask->MinimumLandedTriggerTime = MinimumLandedTriggerTime * Duration; // MinimumLandedTriggerTime is normalized
	MyTask->bFinishOnLanded = bFinishOnLanded;
	MyTask->FinishVelocityMode = VelocityOnFinishMode;
	MyTask->FinishSetVelocity = SetVelocityOnFinish;
	MyTask->FinishClampVelocity = ClampVelocityOnFinish;
	MyTask->SplineFlightPath = Path;
	MyTask->TimeMappingCurve = TimeMappingCurve;
	MyTask->SharedInitAndApply();

	return MyTask;
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::Activate()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActor());
	if (Character)
	{
		Character->LandedDelegate.AddDynamic(this, &UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::OnLandedCallback);
	}
	SetWaitingOnAvatar();
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::OnLandedCallback(const FHitResult& Hit)
{
	bHasLanded = true;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActor());
	if (Character && Character->bClientUpdating)
	{
		// If in a move replay, we just mark that we landed so that next tick we trigger landed
	}
	else
	{
		// TriggerLanded immediately if we're past time allowed, otherwise it'll get caught next valid tick
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		if (CurrentTime >= (StartTime+MinimumLandedTriggerTime))
		{
			TriggerLanded();
		}
	}
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::TriggerLanded()
{
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnLanded.Broadcast();
	}

	if (bFinishOnLanded)
	{
		Finish();
	}
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::SharedInitAndApply()
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (ASC && ASC->AbilityActorInfo->MovementComponent.IsValid())
	{
		MovementComponent = Cast<UCharacterMovementComponent>(ASC->AbilityActorInfo->MovementComponent.Get());
		StartTime = GetWorld()->GetTimeSeconds();
		EndTime = StartTime + Duration;

		ATrvSplineFlightPath* PathPtr = SplineFlightPath.Get();
		if (IsValid(PathPtr))
		{
			if (MovementComponent.IsValid())
			{
				ForceName = ForceName.IsNone() ? FName("ApplyRootMotionJumpUsingSplineFlightPath") : ForceName;
				TSharedPtr<FTrvRootMotionSource_JumpUsingSplineFlightPath> JumpForce = MakeShared<FTrvRootMotionSource_JumpUsingSplineFlightPath>();
				JumpForce->InstanceName = ForceName;
				JumpForce->AccumulateMode = ERootMotionAccumulateMode::Override;
				JumpForce->Priority = 500;
				JumpForce->BlendInDuration = BlendInDuration;
				JumpForce->Duration = Duration;
				JumpForce->bDisableTimeout = bFinishOnLanded; // If we finish on landed, we need to disable force's timeout
				JumpForce->SplineFlightPath = PathPtr;
				JumpForce->TimeMappingCurve = TimeMappingCurve;
				JumpForce->FinishVelocityParams.Mode = FinishVelocityMode;
				JumpForce->FinishVelocityParams.SetVelocity = FinishSetVelocity;
				JumpForce->FinishVelocityParams.ClampVelocity = FinishClampVelocity;
				JumpForce->Init(MovementComponent.Get());
				RootMotionSourceID = MovementComponent->ApplyRootMotionSource(JumpForce);
			}
		}
		else
		{
			ABILITY_LOG(
				Warning,
				TEXT("UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath called in Ability %s with null SplineFlightPath; Task Instance Name %s."), 
				Ability ? *Ability->GetName() : TEXT("NULL"), 
				*InstanceName.ToString()
			);
		}
	}
	else
	{
		ABILITY_LOG(
			Warning,
			TEXT("UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath called in Ability %s with null MovementComponent; Task Instance Name %s."), 
			Ability ? *Ability->GetName() : TEXT("NULL"), 
			*InstanceName.ToString()
		);
	}
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::Finish()
{
	bIsFinished = true;

	if (!bIsSimulating)
	{
		AActor* MyActor = GetAvatarActor();
		if (MyActor)
		{
			MyActor->ForceNetUpdate();
			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnFinish.Broadcast();
			}
		}
		EndTask();
	}
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::TickTask(float DeltaTime)
{
	if (bIsFinished)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	if (bHasLanded && CurrentTime >= (StartTime+MinimumLandedTriggerTime))
	{
		TriggerLanded();
		return;
	}

	Super::TickTask(DeltaTime);

	AActor* MyActor = GetAvatarActor();
	if (MyActor)
	{
		const bool bTimedOut = HasTimedOut();

		if (!bFinishOnLanded && bTimedOut)
		{
			// Task has finished
			Finish();
		}
	}
	else
	{
		bIsFinished = true;
		EndTask();
	}
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, BlendInDuration);
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, Duration);
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, MinimumLandedTriggerTime);
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, bFinishOnLanded);
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, SplineFlightPath);
	DOREPLIFETIME(UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath, TimeMappingCurve);
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::PreDestroyFromReplication()
{
	bIsFinished = true;
	EndTask();
}

void UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::OnDestroy(bool AbilityIsEnding)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActor());
	if (Character)
	{
		Character->LandedDelegate.RemoveDynamic(this, &UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath::OnLandedCallback);
	}

	if (MovementComponent.IsValid())
	{
		MovementComponent->RemoveRootMotionSourceByID(RootMotionSourceID);
	}

	Super::OnDestroy(AbilityIsEnding);
}


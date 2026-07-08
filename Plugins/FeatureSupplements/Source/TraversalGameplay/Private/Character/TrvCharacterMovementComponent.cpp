// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/TrvCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "TrvSplineFlightPath.h"
#include "TrvGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvCharacterMovementComponent)

DECLARE_CYCLE_STAT(TEXT("TrvChar RootMotionSource Apply"), STAT_TrvCharacterMovementRootMotionSourceApply, STATGROUP_Character);

static int32 gTrvCharMoveRootMotionOnlyEnableSweep = 0;
static FAutoConsoleVariableRef CVarTrvCharMoveRootMotionOnlyEnableSweep(
	TEXT("msk.cfs.CharMove.RootMotionOnly.EnableSweep"),
	gTrvCharMoveRootMotionOnlyEnableSweep,
	TEXT(""),
	ECVF_Default
);


/**************************************************************************************************
*
*   UTrvCharacterMovementComponent
*
***/

UTrvCharacterMovementComponent::UTrvCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetAutoSlidableFloorAngleStart(15);
	SetAutoSlidableFloorAngleEnd(60);
	SlidableMaxSteerAngle = 30;

	BrakingDecelerationSliding = 300;
	SlidingFriction = 0.7;
	MaxSlidingSpeed = 600;
	MaxSlidingAcceleration = 1024;

	MinAnalogSlidingSpeed = 1024;

	SlidingAccelDirection = 0.f;
	LastSlidingSlopeDirection = FVector::ForwardVector;

	FollowFlightPathMaxBiasSpeed = 500.f;

	AutoSlidableComponentTag = NAME_None;

	bIgnoreAnimRootMotionPrecedenceOverRootMotionSource = false;
	bOrientRotationToMovementByVelocity = false;

	bUseFallingZSpeedLimit = false;
	FallingZSpeedLimit = 600.f;
}

void UTrvCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

FRotator UTrvCharacterMovementComponent::GetDeltaRotation(float DeltaTime) const
{
	return Super::GetDeltaRotation(DeltaTime);
}

float UTrvCharacterMovementComponent::GetMaxSpeed() const
{
#if 1
	switch(MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		return IsCrouching() ? MaxWalkSpeedCrouched : MaxWalkSpeed;
	case MOVE_Falling:
		return MaxWalkSpeed;
	case MOVE_Swimming:
		return MaxSwimSpeed;
	case MOVE_Flying:
		return MaxFlySpeed;
	case MOVE_Custom:
		if (ETrvCustomMovementMode(CustomMovementMode) == ETrvCustomMovementMode::Slide)
		{
			return MaxSlidingSpeed;
		}
		return MaxCustomMovementSpeed;
	case MOVE_None:
	default:
		return 0.f;
	}

#else
	return Super::GetMaxSpeed();
#endif
}

bool UTrvCharacterMovementComponent::IsMovingOnGround() const
{
#if 1
	return Super::IsMovingOnGround();
#else
	return UpdatedComponent &&
		((MovementMode == MOVE_Walking) ||
			(MovementMode == MOVE_NavWalking) || 
			IsSlideMovementMode()
		);
#endif
}

void UTrvCharacterMovementComponent::ApplyRootMotionToVelocity(float deltaTime)
{
#if 0
	Super::ApplyRootMotionToVelocity(deltaTime);
#else
	SCOPE_CYCLE_COUNTER(STAT_TrvCharacterMovementRootMotionSourceApply);

	// Animation root motion is distinct from root motion sources right now and takes precedence
	if ((bIgnoreAnimRootMotionPrecedenceOverRootMotionSource == false || CurrentRootMotion.HasOverrideVelocity() == false) &&
		HasAnimRootMotion() && deltaTime > 0.f)
	{
		Velocity = ConstrainAnimRootMotionVelocity(AnimRootMotionVelocity, Velocity);
		if (IsFalling())
		{
			Velocity += ProjectToGravityFloor(DecayingFormerBaseVelocity);
		}
		return;
	}

	const FVector OldVelocity = Velocity;

	bool bAppliedRootMotion = false;

	// Apply override velocity
	if( CurrentRootMotion.HasOverrideVelocity() )
	{
		CurrentRootMotion.AccumulateOverrideRootMotionVelocity(deltaTime, *CharacterOwner, *this, Velocity);
		if (IsFalling())
		{
			Velocity += CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? ProjectToGravityFloor(DecayingFormerBaseVelocity) : DecayingFormerBaseVelocity;
		}
		bAppliedRootMotion = true;

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			FString AdjustedDebugString = FString::Printf(TEXT("ApplyRootMotionToVelocity HasOverrideVelocity Velocity(%s)"),
				*Velocity.ToCompactString());
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif
	}

	// Next apply additive root motion
	if( CurrentRootMotion.HasAdditiveVelocity() )
	{
		CurrentRootMotion.LastPreAdditiveVelocity = Velocity; // Save off pre-additive Velocity for restoration next tick
		CurrentRootMotion.AccumulateAdditiveRootMotionVelocity(deltaTime, *CharacterOwner, *this, Velocity);
		CurrentRootMotion.bIsAdditiveVelocityApplied = true; // Remember that we have it applied
		bAppliedRootMotion = true;

#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnGameThread() == 1)
		{
			FString AdjustedDebugString = FString::Printf(TEXT("ApplyRootMotionToVelocity HasAdditiveVelocity Velocity(%s) LastPreAdditiveVelocity(%s)"),
				*Velocity.ToCompactString(), *CurrentRootMotion.LastPreAdditiveVelocity.ToCompactString());
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif
	}

	// Switch to Falling if we have vertical velocity from root motion so we can lift off the ground
	const FVector AppliedVelocityDelta = Velocity - OldVelocity;
	if( bAppliedRootMotion && GetGravitySpaceZ(AppliedVelocityDelta) != 0.f && IsMovingOnGround() )
	{
		float LiftoffBound;
		if( CurrentRootMotion.LastAccumulatedSettings.HasFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck) )
		{
			// Sensitive bounds - "any positive force"
			LiftoffBound = UE_SMALL_NUMBER;
		}
		else
		{
			// Default bounds - the amount of force gravity is applying this tick
			LiftoffBound = FMath::Max(-GetGravityZ() * deltaTime, UE_SMALL_NUMBER);
		}

		if(GetGravitySpaceZ(AppliedVelocityDelta) > LiftoffBound )
		{
			SetMovementMode(MOVE_Falling);
		}
	}
#endif
}

float UTrvCharacterMovementComponent::GetMinAnalogSpeed() const
{
#if 1
	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
		return MinAnalogWalkSpeed;
	case MOVE_Custom:
		if (ETrvCustomMovementMode(CustomMovementMode) == ETrvCustomMovementMode::Slide)
		{
			return MinAnalogSlidingSpeed;
		}
	default:
		return 0.f;
	};
#else
	return Super::GetMinAnalogSpeed();
#endif
}

float UTrvCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
#if 1
	switch (MovementMode)
	{
		case MOVE_Walking:
		case MOVE_NavWalking:
			return BrakingDecelerationWalking;
		case MOVE_Falling:
			return BrakingDecelerationFalling;
		case MOVE_Swimming:
			return BrakingDecelerationSwimming;
		case MOVE_Flying:
			return BrakingDecelerationFlying;
		case MOVE_Custom:
			if (ETrvCustomMovementMode(CustomMovementMode) == ETrvCustomMovementMode::Slide)
			{
				return BrakingDecelerationSliding;
			}
			return 0.f;
		case MOVE_None:
		default:
			return 0.f;
	}
#else
	return Super::GetMaxBrakingDeceleration();
#endif
}

bool UTrvCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (MovementMode == MOVE_Falling)
	{
		return IsSlidableFloor(Hit);
	}
	else if (IsSlideMovementMode())
	{
		return IsSlidableFloor(Hit);
	}

	return Super::IsWalkable(Hit);
}

bool UTrvCharacterMovementComponent::CanAttemptJump() const
{
	// Same as UCharacterMovementComponent's implementation but without the crouch check
	return IsJumpAllowed() &&
		(IsMovingOnGround() ||
		IsFalling() ||  // Falling included for double-jump and non-zero jump hold time, but validated by character.
		IsSlideMovementMode()
		);
}

bool UTrvCharacterMovementComponent::CanStepUp(const FHitResult& Hit) const
{
	return Super::CanStepUp(Hit);
}

void UTrvCharacterMovementComponent::StartNewPhysics(float deltaTime, int32 Iterations)
{
	Super::StartNewPhysics(deltaTime, Iterations);
}

void UTrvCharacterMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	Super::ProcessLanded(Hit, remainingTime, Iterations);
}

void UTrvCharacterMovementComponent::SetPostLandedPhysics(const FHitResult& Hit)
{
	if (NeedSlideMovementOnLanded(Hit))
	{
		MovementMode = MOVE_Custom;
		CustomMovementMode = (uint8)ETrvCustomMovementMode::Slide;
		return;
	}
	
	Super::SetPostLandedPhysics(Hit);
}

void UTrvCharacterMovementComponent::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
	Super::MoveAlongFloor(InVelocity, DeltaSeconds, OutStepDownResult);
}

float UTrvCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	FVector Normal(InNormal);
	const FVector::FReal NormalZ = GetGravitySpaceZ(Normal);
	if (IsMovingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (NormalZ > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
		else if (NormalZ < -UE_KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (GetGravitySpaceZ(FloorNormal) < 1.f - UE_DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}
				
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
	}
	else if (IsSlideMovementMode())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (NormalZ > 0.f)
		{
			if (!IsAutoSlidableFloor(Hit))
			{
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
		else if (NormalZ < -UE_KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (GetGravitySpaceZ(FloorNormal) < 1.f - UE_DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}
				
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
	}

	return UPawnMovementComponent::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

void UTrvCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	Super::PhysCustom(deltaTime, Iterations);

	switch (ETrvCustomMovementMode(CustomMovementMode))
	{
		case ETrvCustomMovementMode::RootMotionOnly:
		{
			if (gTrvCharMoveRootMotionOnlyEnableSweep == 0)
			{
				RestorePreAdditiveRootMotionVelocity();

				ApplyRootMotionToVelocity(deltaTime);

				Iterations++;
				bJustTeleported = false;

				FVector OldLocation = UpdatedComponent->GetComponentLocation();
				const FVector Adjusted = Velocity * deltaTime;
				FHitResult Hit(1.f);
				SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), false, Hit);
			}
			else
			{
				PhysFlying(deltaTime, Iterations);
			}
		}
		break;
		case ETrvCustomMovementMode::Slide:
		{
			PhysSliding(deltaTime, Iterations);
		}
		break;
		case ETrvCustomMovementMode::FollowFlightPath:
		{
			FollowFlightPath(deltaTime, Iterations);
		}
		break;
	};
}

FVector UTrvCharacterMovementComponent::ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const
{
	return Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity, CurrentVelocity);
}

FRotator UTrvCharacterMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const
{
	if (bOrientRotationToMovementByVelocity)
	{
		const float SpeedSQ = Velocity.SizeSquared();
		if (SpeedSQ > UE_KINDA_SMALL_NUMBER)
		{
			FVector Dir = Velocity / FMath::Sqrt(SpeedSQ);
			return Dir.ToOrientationRotator();
		}

		return CurrentRotation;
	}

	return Super::ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRotation);
}

void UTrvCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (IsFollowFlightPathMovementMode())
	{
		return;
	}

	Super::PhysicsRotation(DeltaTime);
}

FVector UTrvCharacterMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	if (bUseFallingZSpeedLimit == false)
	{
		return Super::NewFallVelocity(InitialVelocity, Gravity, DeltaTime);
	}

	FVector Result = InitialVelocity;

	if (DeltaTime > 0.f)
	{
		// Apply gravity.
		Result += Gravity * DeltaTime;

		// Apply limit
		FVector GravityDirVelocity = GetGravitySpaceComponentZ(Result);
		if (GravityDirVelocity.SizeSquared() > FMath::Square(FallingZSpeedLimit))
		{
			Result -= GravityDirVelocity;
			Result += GetGravityDirection() * FallingZSpeedLimit;
		}

		// Don't exceed terminal velocity.
		const float TerminalLimit = FMath::Abs(GetPhysicsVolume()->TerminalVelocity);
		if (Result.SizeSquared() > FMath::Square(TerminalLimit))
		{
			const FVector GravityDir = Gravity.GetSafeNormal();
			if ((Result | GravityDir) > TerminalLimit)
			{
				Result = FVector::PointPlaneProject(Result, FVector::ZeroVector, GravityDir) + GravityDir * TerminalLimit;
			}
		}
	}

	return Result;
}

void UTrvCharacterMovementComponent::SetAutoSlidableFloorAngleStart(float InAngle)
{
	AutoSlidableFloorAngleStart = InAngle;
	AutoSlidableFloorMaxZ = FMath::Cos(FMath::DegreesToRadians(AutoSlidableFloorAngleStart));
}

void UTrvCharacterMovementComponent::SetAutoSlidableFloorAngleEnd(float InAngle)
{
	AutoSlidableFloorAngleEnd = InAngle;
	AutoSlidableFloorMinZ = FMath::Cos(FMath::DegreesToRadians(AutoSlidableFloorAngleEnd));
}

bool UTrvCharacterMovementComponent::HasAutoSlidableTag(const FHitResult& InFloorHit) const
{
	if (AutoSlidableComponentTag.IsNone() == false)
	{
		if (UPrimitiveComponent* HitComp = InFloorHit.GetComponent())
		{
			return HitComp->ComponentHasTag(AutoSlidableComponentTag);
		}

		return false;
	}

	return true;
}

bool UTrvCharacterMovementComponent::IsSlidableFloor (const FHitResult& InFloorHit) const
{
	if (!InFloorHit.IsValidBlockingHit())
	{
		return false;
	}

	const FVector::FReal ImpactNormalZ = GetGravitySpaceZ(InFloorHit.ImpactNormal);
	if (ImpactNormalZ < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	return ImpactNormalZ > AutoSlidableFloorMinZ;
}

bool UTrvCharacterMovementComponent::NeedSlideMovementOnLanded(const FHitResult& InFloorHit) const
{
	if (!InFloorHit.IsValidBlockingHit())
	{
		return false;
	}

	const FVector::FReal ImpactNormalZ = GetGravitySpaceZ(InFloorHit.ImpactNormal);
	if (ImpactNormalZ < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (HasAutoSlidableTag(InFloorHit) == false)
	{
		return false;
	}

	return ImpactNormalZ > AutoSlidableFloorMinZ && ImpactNormalZ < AutoSlidableFloorMaxZ;
}

bool UTrvCharacterMovementComponent::IsAutoSlidableFloor(const FHitResult& InFloorHit) const
{
	if (!InFloorHit.IsValidBlockingHit())
	{
		return false;
	}

	const FVector::FReal ImpactNormalZ = GetGravitySpaceZ(InFloorHit.ImpactNormal);
	if (ImpactNormalZ < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	return ImpactNormalZ > AutoSlidableFloorMinZ && ImpactNormalZ < AutoSlidableFloorMaxZ;
}

float UTrvCharacterMovementComponent::GetSlideAcceleratioScale(const float InZ) const
{
	float Base = FMath::Max(1, (AutoSlidableFloorMaxZ - AutoSlidableFloorMinZ));

	return 1.f - FMath::Clamp((InZ - AutoSlidableFloorMinZ) / Base, 0.f, 1.f);
}

bool UTrvCharacterMovementComponent::IsSlideMovementMode() const
{
	return MovementMode == EMovementMode::MOVE_Custom && ETrvCustomMovementMode(CustomMovementMode) == ETrvCustomMovementMode::Slide;
}

bool UTrvCharacterMovementComponent::IsFollowFlightPathMovementMode() const
{
	return MovementMode == EMovementMode::MOVE_Custom && ETrvCustomMovementMode(CustomMovementMode) == ETrvCustomMovementMode::FollowFlightPath;
}

void UTrvCharacterMovementComponent::PhysSliding(float DeltaSeconds, int32 Iterations)
{
	if (DeltaSeconds < MIN_TICK_TIME)
	{
		return;
	}

	SlidingAccelDirection = 0.f;
	bForceNextFloorCheck = true;
	CurrentFloor.Clear();
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false, nullptr);

	if (IsSlidableFloor(CurrentFloor.HitResult) == false && Velocity.IsNearlyZero())
	{
		SetMovementMode(EMovementMode::MOVE_Falling);
		StartNewPhysics(DeltaSeconds, Iterations);
		return;
	}

	// Constraint Acceleration to Move Tangent 
	const FVector InverseGravityDir = -GetGravityDirection();
	const FVector OldAccel = Acceleration;
	const bool bZeroAccel = FMath::IsNearlyZero(OldAccel.SizeSquared());

	float remainingTime = DeltaSeconds;
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{	
		Iterations++;
		float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		bJustTeleported = false;

		// Caculate Acceleration
		FVector RollAxis = FVector::ZeroVector;
		FVector PitchAxis = FVector::ZeroVector;
		const float FloorNormalDot = FVector::DotProduct(InverseGravityDir, CurrentFloor.HitResult.ImpactNormal);
		if (FMath::IsNearlyEqual(FloorNormalDot, 1.0f) == false)
		{
			PitchAxis = InverseGravityDir ^ CurrentFloor.HitResult.ImpactNormal;
			PitchAxis.Normalize();
			RollAxis = PitchAxis ^ CurrentFloor.HitResult.ImpactNormal;
			LastSlidingSlopeDirection = RollAxis;
		}
		else
		{
			LastSlidingSlopeDirection = UpdatedComponent->GetComponentTransform().GetUnitAxis(EAxis::X);
		}
		
		if (false == bZeroAccel)
		{
			if (FMath::IsNearlyEqual(FloorNormalDot, 1.0f))
			{
				Acceleration = FVector::ZeroVector;
			}
			else
			{
				Acceleration = FVector::VectorPlaneProject(OldAccel, CurrentFloor.HitResult.ImpactNormal);
				if (FMath::IsNearlyZero(Acceleration.SizeSquared()) == false)
				{
					Acceleration.Normalize();
					float AccelDot = FVector::DotProduct(RollAxis, Acceleration);
					if (AccelDot < FMath::Cos(FMath::DegreesToRadians(SlidableMaxSteerAngle)))
					{
						FVector RotationAxisAndAngle = CurrentFloor.HitResult.ImpactNormal;
						RotationAxisAndAngle *= FMath::DegreesToRadians(SlidableMaxSteerAngle);
						SlidingAccelDirection = SlidableMaxSteerAngle;
						if (FVector::DotProduct(PitchAxis, Acceleration) < 0.f)
						{
							RotationAxisAndAngle *= -1.0;
							SlidingAccelDirection = -SlidableMaxSteerAngle;
						}
						FQuat Rot = FQuat::MakeFromRotationVector(RotationAxisAndAngle);
						Acceleration = Rot.RotateVector(RollAxis);
					}
				}
			}
		}
		else if (FloorNormalDot < AutoSlidableFloorMaxZ && FloorNormalDot > AutoSlidableFloorMinZ)
		{
			Acceleration = RollAxis;
		}

		Acceleration *= GetSlideAcceleratioScale(FloorNormalDot) * MaxSlidingAcceleration;

		const FVector OldVelocityWithRootMotion = Velocity;

		RestorePreAdditiveRootMotionVelocity();

		const FVector OldVelocity = Velocity;
		
		// Apply acceleration
		const float MaxDecel = GetMaxBrakingDeceleration();
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			CalcVelocity(timeTick, SlidingFriction, false, MaxDecel);
		}
		
		ApplyRootMotionToVelocity(timeTick);
		if (IsSlideMovementMode() == false)
		{
			StartNewPhysics(remainingTime+timeTick, Iterations-1);
			return;
		}

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		if ( bZeroDelta )
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
			else if (IsSlideMovementMode() == false)
			{
				// pawn ended up in a different mode, probably due to the step-up-and-over flow
				// let's refund the estimated unused time (if any) and keep moving in the new mode
				const float DesiredDist = Delta.Size();
				if (DesiredDist > UE_KINDA_SMALL_NUMBER)
				{
					const float ActualDist = ProjectToGravityFloor(UpdatedComponent->GetComponentLocation() - OldLocation).Size();
					remainingTime += timeTick * (1.f - FMath::Min(1.f,ActualDist/DesiredDist));
				}
				StartNewPhysics(remainingTime,Iterations);
				return;
			}
		}

		// Update floor.
		// StepUp might have already done it for us.
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
		}
		
		// Validate the floor check
		if (CurrentFloor.IsWalkableFloor())
		{
			// TODO
#if 0
			HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
			if (IsMovingOnGround())
			{
				// If still walking, then fall. If not, assume the user set a different mode they want to keep.
				StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
			}
			return;
#endif

			AdjustFloorHeight();
			SetBaseFromFloor(CurrentFloor);
		}
		else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
		{
			// The floor check failed because it started in penetration
			// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
			FHitResult Hit(CurrentFloor.HitResult);
			Hit.TraceEnd = Hit.TraceStart + MAX_FLOOR_DIST * -GetGravityDirection();
			const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
			ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
			bForceNextFloorCheck = true;
		}

		// check if just entered water
		if ( IsSwimming() )
		{
			StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
			return;
		}

		if ((!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating) ||
			(Velocity.IsNearlyZero() && Acceleration.IsNearlyZero())
		)
		{
			SetMovementMode(EMovementMode::MOVE_Falling);
			StartNewPhysics(DeltaSeconds, Iterations);
			return;
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}
}

void UTrvCharacterMovementComponent::UTrvCharacterMovementComponent::ExecuteFollowFlightPath(ATrvSplineFlightPath* InPath, float InBlendInTime, float InMoveDuration)
{
	if (InPath == nullptr)
	{
		return;
	}

	float PathLength = InPath->GetFlightPathLength();
	if (PathLength < 1)
	{
		return;
	}

	FlightPathToFollow = InPath;

	FMemory::Memzero(FollowFlightPathContext);
	FollowFlightPathContext.TotalLength = PathLength;
	FollowFlightPathContext.MoveDuration = FMath::Max(0.1f, InMoveDuration);
	FollowFlightPathContext.MoveRemainSecs = FollowFlightPathContext.MoveDuration;
	FollowFlightPathContext.BlendInDuration = FMath::Max(0.1f, InBlendInTime);
	FollowFlightPathContext.BlendInRemainSecs = FollowFlightPathContext.BlendInDuration;
	FollowFlightPathContext.DefinedSpeed = PathLength / FollowFlightPathContext.MoveDuration;
	FollowFlightPathContext.CurrPerpendicularBias = InPath->CaclculateNearestStartPerpendicularBias(UpdatedComponent->GetComponentLocation());

	SetMovementMode(MOVE_Custom, (uint8)ETrvCustomMovementMode::FollowFlightPath);
}

void UTrvCharacterMovementComponent::FollowFlightPath(float DeltaSeconds, int32 Iterations)
{
	if (DeltaSeconds < MIN_TICK_TIME)
	{
		return;
	}
	
	if (::IsValid(FlightPathToFollow) == false)
	{
		FlightPathToFollow = nullptr;
		SetMovementMode(EMovementMode::MOVE_Falling);
		StartNewPhysics(DeltaSeconds, Iterations);
		return;
	}
	
	FollowFlightPathAccelDirection = 0.f;
	const FVector OldAccel = Acceleration;
	const bool bZeroAccel = FMath::IsNearlyZero(OldAccel.SizeSquared());

	float remainingTime = DeltaSeconds;
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{	
		Iterations++;
		float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		
		FollowFlightPathContext.MoveRemainSecs -= timeTick;
		FollowFlightPathContext.CurrRatio = FMath::Clamp(FollowFlightPathContext.MoveRemainSecs / FollowFlightPathContext.MoveDuration, 0.f, 1.f);
		FollowFlightPathContext.CurrRatio = 1.f - FollowFlightPathContext.CurrRatio;

		if (FMath::IsNearlyEqual(FollowFlightPathContext.CurrRatio, 1.f))
		{
			FlightPathToFollow = nullptr;
			SetMovementMode(EMovementMode::MOVE_Falling);
			StartNewPhysics(DeltaSeconds, Iterations);
			return;
		}

		float MovedDistance = FollowFlightPathContext.TotalLength * FollowFlightPathContext.CurrRatio;
		FVector PathSegmentForwardVector;
		FVector PathSegmentUpVector;
		FVector PathSegmentSideVector;
		FVector PathSegmentCenter;
		float PathSegmentRightMaxOffset;
		float PathSegmentLeftMaxOffset;
		bool bValidPath = FlightPathToFollow->GetFlightPathPoint(
			MovedDistance,
			PathSegmentForwardVector,
			PathSegmentUpVector,
			PathSegmentSideVector,
			PathSegmentCenter,
			PathSegmentRightMaxOffset,
			PathSegmentLeftMaxOffset
		);

		if (bValidPath == false)
		{
			FlightPathToFollow = nullptr;
			SetMovementMode(EMovementMode::MOVE_Falling);
			StartNewPhysics(DeltaSeconds, Iterations);
			return;
		}
		
		FollowFlightPathAccelDirection = 0.f;
		if (false == bZeroAccel)
		{
			FVector PathPalneProjectedAccel = FVector::VectorPlaneProject(OldAccel, PathSegmentUpVector);
			if (FMath::IsNearlyZero(PathPalneProjectedAccel.SizeSquared()) == false)
			{
				PathPalneProjectedAccel.Normalize();
				FollowFlightPathAccelDirection = FVector::DotProduct(PathPalneProjectedAccel, PathSegmentSideVector);
			}
		}
		
		float NextBiasCandidate = FollowFlightPathContext.CurrPerpendicularBias + FollowFlightPathAccelDirection * FollowFlightPathMaxBiasSpeed * timeTick;
		NextBiasCandidate = FMath::Clamp(NextBiasCandidate, PathSegmentLeftMaxOffset, PathSegmentRightMaxOffset);

		FollowFlightPathContext.CurrPerpendicularBias = NextBiasCandidate;
		FVector PathLocation = PathSegmentCenter + PathSegmentSideVector * NextBiasCandidate;
		
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		FVector NextLocation = FMath::VInterpTo(OldLocation, PathLocation, timeTick, 5.f);

		FollowFlightPathContext.BlendInRemainSecs -= timeTick;

		Velocity = PathSegmentForwardVector * FollowFlightPathContext.DefinedSpeed;

		const FVector Delta = NextLocation - OldLocation;
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, PathSegmentForwardVector.ToOrientationQuat(), false, Hit);
	}
}

#if WITH_EDITOR
void UTrvCharacterMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged == nullptr)
	{
		return;
	}
	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UTrvCharacterMovementComponent, AutoSlidableFloorAngleEnd))
	{
		AutoSlidableFloorAngleEnd = FMath::Max(GetWalkableFloorAngle(), AutoSlidableFloorAngleEnd);
		AutoSlidableFloorAngleEnd = FMath::Max(AutoSlidableFloorAngleStart + 1, AutoSlidableFloorAngleEnd);
		AutoSlidableFloorMinZ = FMath::Cos(FMath::DegreesToRadians(AutoSlidableFloorAngleEnd));
	}
	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UTrvCharacterMovementComponent, AutoSlidableFloorAngleStart))
	{
		AutoSlidableFloorAngleStart = FMath::Min(AutoSlidableFloorAngleEnd - 1, AutoSlidableFloorAngleStart);
		AutoSlidableFloorMaxZ = FMath::Cos(FMath::DegreesToRadians(AutoSlidableFloorAngleStart));
	}
}
#endif // WITH_EDITOR

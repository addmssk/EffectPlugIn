// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"

#include "TrvCharacterMovementComponent.generated.h"

#define UE_API TRAVERSALGAMEPLAY_API

UENUM(BlueprintType)
enum class ETrvCustomMovementMode : uint8
{
	None,
	RootMotionOnly,
	Slide,
	FollowFlightPath,

	MAX UMETA(Hidden)
};

class ATrvSplineFlightPath;


/**************************************************************************************************
*
*   UTrvCharacterMovementComponent
*
***/

UCLASS(MinimalAPI, Config = Game)
class UTrvCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UE_API UTrvCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	//~UMovementComponent interface
	UE_API virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	UE_API virtual float GetMaxSpeed() const override;
	UE_API virtual bool IsMovingOnGround() const override;
	//~End of UMovementComponent interface
	
	//~UCharacterMovementComponent interface
	UE_API virtual void ApplyRootMotionToVelocity(float deltaTime) override;
	UE_API virtual float GetMinAnalogSpeed() const override;
	UE_API virtual float GetMaxBrakingDeceleration() const override;
	UE_API virtual bool IsWalkable(const FHitResult& Hit) const override;
	UE_API virtual bool CanAttemptJump() const override;
	UE_API virtual bool CanStepUp(const FHitResult& Hit) const override;
	UE_API virtual void StartNewPhysics(float deltaTime, int32 Iterations) override;
	UE_API virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	UE_API virtual void SetPostLandedPhysics(const FHitResult& Hit) override;
	UE_API virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult = NULL) override;
	UE_API virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;
	UE_API virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	UE_API virtual FVector ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const override;
	UE_API virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
	UE_API virtual void PhysicsRotation(float DeltaTime) override;
	UE_API virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	//~End of UCharacterMovementComponent interface
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TraversalGameplay")
	bool bIgnoreAnimRootMotionPrecedenceOverRootMotionSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TraversalGameplay")
	bool bOrientRotationToMovementByVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(EditCondition="bIgnoreAnimRootMotionPrecedenceOverRootMotionSource"))
	FVector PossibleRootMotionSourceDestination;

public:
	void SetAutoSlidableFloorAngleStart(float InAngle);
	void SetAutoSlidableFloorAngleEnd(float InAngle);

	UFUNCTION(BlueprintCallable, Category="Sliding")
	bool HasAutoSlidableTag(const FHitResult& InFloorHit) const;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMin="0", ClampMin="0"))
	float BrakingDecelerationSliding;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMin="0", ClampMin="0"))
	float SlidingFriction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMin="0", ClampMin="0", ForceUnits="cm/s"))
	float MaxSlidingSpeed;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMax="1.0", UiMin="0.0", ClampMax="1.0", ClampMin="0.0", ForceUnits="cm/s"))
	float MinAnalogSlidingSpeed;
	
	// Maybe constraint by MaxAcceleration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMin="0", ClampMin="0"))
	float MaxSlidingAcceleration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding")
	FName AutoSlidableComponentTag;

	// Less steepper then this have no acceleraton to slide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMax="90", UiMin="0", ClampMax="90", ClampMin="0"))
	float AutoSlidableFloorAngleStart;

	// More steepper then this trigger MovementMode change to Fall. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMax="90", UiMin="0", ClampMax="90", ClampMin="0"))
	float AutoSlidableFloorAngleEnd;

	// Floor less steepper then this only apply decceleration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sliding", meta=(UiMax="90", UiMin="0", ClampMax="90", ClampMin="0"))
	float SlidableMaxSteerAngle;

	UPROPERTY(BlueprintReadOnly, Transient)
	float SlidingAccelDirection;

	UPROPERTY(BlueprintReadOnly, Transient)
	FVector LastSlidingSlopeDirection;

public:
	UFUNCTION(BlueprintCallable, Category="FollowFlightPath")
	void ExecuteFollowFlightPath(ATrvSplineFlightPath* InPath, float InBlendInTime, float InMoveDuration);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FollowFlightPathMaxBiasSpeed;

	UPROPERTY(BlueprintReadOnly, Transient)
	float FollowFlightPathAccelDirection;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character Movement: Jumping / Falling")
	bool bUseFallingZSpeedLimit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character Movement: Jumping / Falling")
	float FallingZSpeedLimit;

protected:
	UE_API virtual void InitializeComponent() override;

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	float AutoSlidableFloorMinZ;

	UPROPERTY()
	float AutoSlidableFloorMaxZ;

private:
	void PhysSliding(float DeltaSeconds, int32 Iterations);

	bool IsSlidableFloor(const FHitResult& InFloorHit) const;
	bool NeedSlideMovementOnLanded(const FHitResult& InFloorHit) const;
	bool IsAutoSlidableFloor(const FHitResult& InFloorHit) const;
	float GetSlideAcceleratioScale(const float InZ) const;

	inline bool IsSlideMovementMode() const;

private:
	void FollowFlightPath(float DeltaSeconds, int32 Iterations);

	inline bool IsFollowFlightPathMovementMode() const;

	UPROPERTY(Transient)
	TObjectPtr<ATrvSplineFlightPath> FlightPathToFollow;

	struct FFollowFlightPathContext
	{
		float CurrRatio;
		float CurrPerpendicularBias;
		float TotalLength;
		float DefinedSpeed;

		float MoveDuration;
		float MoveRemainSecs;

		float BlendInDuration;
		float BlendInRemainSecs;
	};
	FFollowFlightPathContext FollowFlightPathContext;
};

#undef UE_API

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotion_Base.h"
#include "GameFramework/RootMotionSource.h"
#include "TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath.generated.h"

#define UE_API TRAVERSALGAMEPLAY_API

class ATrvSplineFlightPath;


/**************************************************************************************************
*
*	FTrvRootMotionSource_JumpUsingSplineFlightPath
*   Based on FRootMotionSource_JumpForce
*
***/

USTRUCT()
struct FTrvRootMotionSource_JumpUsingSplineFlightPath : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

	UE_API FTrvRootMotionSource_JumpUsingSplineFlightPath();

	virtual ~FTrvRootMotionSource_JumpUsingSplineFlightPath() {}
	
	UPROPERTY()
	float BlendInDuration;

	UPROPERTY()
	bool bDisableTimeout;
	
	UPROPERTY()
	TObjectPtr<ATrvSplineFlightPath> SplineFlightPath;

	UPROPERTY()
	TObjectPtr<UCurveFloat> TimeMappingCurve;

	FVector SavedHalfwayLocation;

	float MaxDistance;
	float StartPerpendicularBias;
	FVector StartWorldPosition;

	void Init(class UCharacterMovementComponent* InMoveComp);

	UE_API virtual bool IsTimeOutEnabled() const override;

	UE_API virtual FRootMotionSource* Clone() const override;

	UE_API virtual bool Matches(const FRootMotionSource* Other) const override;

	UE_API virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;

	UE_API virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;

	UE_API virtual void PrepareRootMotion(
		float SimulationTime, 
		float MovementTickTime,
		const ACharacter& Character, 
		const UCharacterMovementComponent& MoveComponent
		) override;

	UE_API virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits<FTrvRootMotionSource_JumpUsingSplineFlightPath> : public TStructOpsTypeTraitsBase2<FTrvRootMotionSource_JumpUsingSplineFlightPath>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};


/**************************************************************************************************
*
*	UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath
*   Based on UAbilityTask_ApplyRootMotionJumpForce
*
***/

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FApplyRootMotionJumpUsingSplineFlightPathDelegate);

UCLASS(MinimalAPI)
class UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath : public UAbilityTask_ApplyRootMotion_Base
{
	GENERATED_UCLASS_BODY()
	
public:	
	UPROPERTY(BlueprintAssignable)
	FApplyRootMotionJumpUsingSplineFlightPathDelegate OnFinish;

	UPROPERTY(BlueprintAssignable)
	FApplyRootMotionJumpUsingSplineFlightPathDelegate OnLanded;

	UFUNCTION(BlueprintCallable, Category="Ability|Tasks")
	UE_API void Finish();

	UFUNCTION()
	UE_API void OnLandedCallback(const FHitResult& Hit);
	
	/** use path to character's movement */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UE_API UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath* TrvApplyRootMotionJumpUsingSplineFlightPath(
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
	);
		
	UE_API virtual void Activate() override;

	/** Tick function for this task, if bTickingTask == true */
	UE_API virtual void TickTask(float DeltaTime) override;

	UE_API virtual void PreDestroyFromReplication() override;
	UE_API virtual void OnDestroy(bool AbilityIsEnding) override;

protected:

	UE_API virtual void SharedInitAndApply() override;

	/**
	* Work-around for OnLanded being called during bClientUpdating in movement replay code
	* Don't want to trigger our Landed logic during a replay, so we wait until next frame
	* If we don't, we end up removing root motion from a replay root motion set instead
	* of the real one
	*/
	UE_API void TriggerLanded();

protected:
	UPROPERTY(Replicated)
	float BlendInDuration;

	UPROPERTY(Replicated)
	float Duration;

	UPROPERTY(Replicated)
	float MinimumLandedTriggerTime;

	UPROPERTY(Replicated)
	bool bFinishOnLanded;
	
	UPROPERTY(Replicated)
	TObjectPtr<ATrvSplineFlightPath> SplineFlightPath;

	/** 
	 *  Maps real time to movement fraction curve to affect the speed of the
	 *  movement through the path
	 *  Curve X is 0 to 1 normalized real time (a fraction of the duration)
	 *  Curve Y is 0 to 1 is what percent of the move should be at a given X
	 *  Default if unset is a 1:1 correspondence
	 */
	UPROPERTY(Replicated)
	TObjectPtr<UCurveFloat> TimeMappingCurve;

	bool bHasLanded;
};

#undef UE_API
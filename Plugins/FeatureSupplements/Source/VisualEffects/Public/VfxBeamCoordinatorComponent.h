

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VfxBeamCoordinatorComponent.generated.h"

class UVfxBeamCoordinatorComponent;


UENUM(BlueprintType)
enum class EVfxBeamTargetInteractionType : uint8
{
	Assemble,
	Dismantle,
	Scan,

	MAX UMETA(hidden)
};


/**************************************************************************************************
*
*	VfxBeamTargetInterface
*
***/

UINTERFACE(BlueprintType, MinimalAPI)
class UVfxBeamTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class VISUALEFFECTS_API IVfxBeamTargetInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	void OnBeamTargetInteractionStart(UVfxBeamCoordinatorComponent* InComp, int32 InId, EVfxBeamTargetInteractionType InType);
	virtual void OnBeamTargetInteractionStart_Implementation(UVfxBeamCoordinatorComponent* InComp, int32 InId, EVfxBeamTargetInteractionType InType)
	PURE_VIRTUAL(IVfxBeamTargetInterface::OnBeamTargetInteractionStart_Implementation, );

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	void OnBeamTargetInteractionEnd(UVfxBeamCoordinatorComponent* InComp);
	virtual void OnBeamTargetInteractionEnd_Implementation(UVfxBeamCoordinatorComponent* InComp)
	PURE_VIRTUAL(IVfxBeamTargetInterface::OnBeamTargetInteractionEnd_Implementation, );

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	void OnUpdateProgress(float InRatio);
	virtual void OnUpdateProgress_Implementation(float InRatio)
	PURE_VIRTUAL(IVfxBeamTargetInterface::OnUpdateProgress_Implementation, );

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	FVector GetWorkingWorldPosition(EAxis::Type InAxis);
	virtual FVector GetWorkingWorldPosition_Implementation(EAxis::Type InAxis)
	PURE_VIRTUAL(IVfxBeamTargetInterface::GetWorkingWorldPosition_Implementation, return FVector::ZeroVector;);
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	FVector GetWorkingWorldDirection(EAxis::Type InAxis);
	virtual FVector GetWorkingWorldDirection_Implementation(EAxis::Type InAxis)
	PURE_VIRTUAL(IVfxBeamTargetInterface::GetWorkingWorldDirection_Implementation, return FVector::ZeroVector;);
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	float GetWorkingWidth(EAxis::Type InAxis);
	virtual float GetWorkingWidth_Implementation(EAxis::Type InAxis)
	PURE_VIRTUAL(IVfxBeamTargetInterface::GetWorkingWidth_Implementation, return 100.f;);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BeamCoordinator")
	float GetWorkingProgress();
	virtual float GetWorkingProgress_Implementation()
	PURE_VIRTUAL(IVfxBeamTargetInterface::GetWorkingProgress_Implementation, return 0.f;);
};


USTRUCT(BlueprintType)
struct FVfxBeamTargetObject
{
	GENERATED_BODY()

	UPROPERTY(Transient)
    TObjectPtr<UObject> Object;
	
	EVfxBeamTargetInteractionType Type;
	bool bStarted;
	float RemainDelay;
	float RemainDuration;
	float Duration;

	FVfxBeamTargetObject()
		: Object(nullptr)
		, Type(EVfxBeamTargetInteractionType::Assemble)
		, bStarted(false)
		, RemainDelay(0.f)
		, RemainDuration(1)
		, Duration(1)
	{
	}
};


/**************************************************************************************************
*
*   UVfxBeamCoordinatorComponent
*
***/

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class VISUALEFFECTS_API UVfxBeamCoordinatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UVfxBeamCoordinatorComponent(const FObjectInitializer& ObjectInitializer);

public:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	bool HasBeamTarget(int32 InID);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	float GetProgress(int32 InID);
	
	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	float GetElapsedSecs(int32 InID);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	float GetDuration(int32 InID);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	FVector GetTargetLocation(int32 InID, EAxis::Type InAxis = EAxis::Z);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	FVector GetTargetMoveDirection(int32 InID, EAxis::Type InAxis = EAxis::Z);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	float GetTargetWidth(int32 InID, EAxis::Type InAxis = EAxis::Z);

	UFUNCTION(BlueprintCallable, Category = "BeamCoordinator")
	int32 StartInteracting(UObject* InObj, EVfxBeamTargetInteractionType InType, float InDelay, float InDuration);

	DECLARE_DELEGATE_FourParams(FInteractionStartCallback, UObject*, EVfxBeamTargetInteractionType, int32, float);
	FInteractionStartCallback OnInteractionStart;

	DECLARE_DELEGATE_TwoParams(FInteractionEndCallback, EVfxBeamTargetInteractionType, int32);
	FInteractionEndCallback OnInteractionEnd;

protected:
	UPROPERTY(Transient)
	TMap<int32, FVfxBeamTargetObject> Targets;

	struct FLastQuery
	{
		float WorldTime;
		float Progress;
		float ElapsedSecs;
		float Duration;
		float TargetWidth[4]; //EAxis
		FVector TargetLocation[4]; //EAxis
		FVector TargetMoveDirection[4]; //EAxis
	};
	TMap<int32, FLastQuery> QueryHistory;

	FRWLock MapLock;
	
	struct FReadLookScoped
	{
		FReadLookScoped(FRWLock& InLock)
			: Lock(InLock)
		{
			Lock.ReadLock();
		}
		~FReadLookScoped()
		{
			Lock.ReadUnlock();
		}
		FRWLock& Lock;
	};

	void ClearTimerCallback();
	void EnsureTimerCallback();
	void OnTimerCallback();
	FTimerHandle CallbackHandle;

	static int32 sIDGenerator;
};

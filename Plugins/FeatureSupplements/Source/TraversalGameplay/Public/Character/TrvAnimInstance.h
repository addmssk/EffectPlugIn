#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"
#include "TrvAnimInstance.generated.h"


class ACharacter;
class UCharacterMovementComponent;
class UAbilitySystemComponent;


/**************************************************************************************************
*
*   UTrvAnimInstance
*
***/

UCLASS(transient)
class TRAVERSALGAMEPLAY_API UTrvAnimInstance : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

	// UAnimInstance ~
	virtual APawn* TryGetPawnOwner() const override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	// ~UAnimInstance
	
#if WITH_EDITOR
	// UObject ~
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	// ~ UObject
#endif // WITH_EDITOR

public:	
	UFUNCTION(BlueprintCallable)
	void DisableControlRig(float InBlendDuration = 0.2f);
	
	UFUNCTION(BlueprintCallable)
	void EnableControlRig(float InBlendDuration = 0.2f);

	UFUNCTION(BlueprintCallable)
	void SetParkourFrontLedgeTransform(const FTransform& InTransform);

	UFUNCTION(BlueprintCallable)
	const FTransform& GetParkourFrontLedgeTransform() const;

	UFUNCTION(BlueprintCallable)
	void SetParkourBackLedgeTransform(const FTransform& InTransform);

	UFUNCTION(BlueprintCallable)
	const FTransform& GetParkourBackLedgeTransform() const;
	
	virtual void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

protected:
	UPROPERTY(BlueprintReadOnly, Transient)
	TObjectPtr<ACharacter> CharacterActor;

	UPROPERTY(BlueprintReadOnly, Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovement;

	FAlphaBlend ControlRigAlphaBlend;
	
	UPROPERTY(Transient, BlueprintReadOnly)
	FTransform ParkourFrontLedgeTransform;
	
	UPROPERTY(Transient, BlueprintReadOnly)
	FTransform ParkourBackLedgeTransform;

	UPROPERTY(Transient, BlueprintReadOnly)
	float ControlRigAlpha;

	UPROPERTY(EditAnywhere, meta=(UIMin="0.1", ClampMin="0.1"))
	float ControlRigBlendDuration;

	// Gameplay tags that can be mapped to blueprint variables. The variables will automatically update as the tags are added or removed.
	// These should be used instead of manually querying for the gameplay tags.
	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;
};

#include "Character/TrvAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemInterface.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvAnimInstance)


/**************************************************************************************************
*
*   UTrvAnimInstance
*
***/

UTrvAnimInstance::UTrvAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CharacterActor(nullptr)
	, CharacterMovement(nullptr)
	, ControlRigAlpha(1.f)
	, ControlRigBlendDuration(0.2f)
{
	ControlRigAlphaBlend.SetBlendTime(ControlRigBlendDuration);
	ControlRigAlphaBlend.SetValueRange(1, 1);
	ControlRigAlphaBlend.SetBlendOption(EAlphaBlendOption::Linear);
}

#if WITH_EDITOR
EDataValidationResult UTrvAnimInstance::IsDataValid(FDataValidationContext& Context) const
{
	Super::IsDataValid(Context);

	GameplayTagPropertyMap.IsDataValid(this, Context);

	return ((Context.GetNumErrors() > 0) ? EDataValidationResult::Invalid : EDataValidationResult::Valid);
}
#endif // WITH_EDITOR

void UTrvAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	check(ASC);

	GameplayTagPropertyMap.Initialize(this, ASC);
}

APawn* UTrvAnimInstance::TryGetPawnOwner() const
{
	USkeletalMeshComponent* OwnerComponent = GetSkelMeshComponent();
	if (AActor* OwnerActor = OwnerComponent->GetOwner())
	{
		return Cast<APawn>(OwnerActor);
	}
	OwnerComponent = this->GetOwningComponent();
	if (AActor* OwnerActor = OwnerComponent->GetOwner())
	{
		return Cast<APawn>(OwnerActor);
	}
	return nullptr;
}

void UTrvAnimInstance::NativeInitializeAnimation()
{
	CharacterActor = Cast<ACharacter>(TryGetPawnOwner());
	if (CharacterActor)
	{
		if (IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(CharacterActor))
		{
			if (UAbilitySystemComponent* ASC = AbilitySystemInterface->GetAbilitySystemComponent())
			{
				InitializeWithAbilitySystem(ASC);
			}
		}

		CharacterMovement = CharacterActor->GetCharacterMovement();
	}
}

void UTrvAnimInstance::NativeUninitializeAnimation()
{
	CharacterActor = nullptr;
	CharacterMovement = nullptr;
}

void UTrvAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (ControlRigAlphaBlend.IsComplete() == false)
	{
		ControlRigAlphaBlend.Update(DeltaSeconds);
		ControlRigAlpha = ControlRigAlphaBlend.GetBlendedValue();
	}
}

void UTrvAnimInstance::DisableControlRig(float InBlendDuration)
{
	if (InBlendDuration < 0.01f)
	{
		ControlRigAlphaBlend.SetValueRange(0, 0);
		ControlRigAlpha = 0.f;
		return;
	}

	float Value = ControlRigAlphaBlend.GetBlendedValue();
	float Ratio = Value;

	ControlRigAlphaBlend.SetBlendTime(ControlRigBlendDuration * Ratio);
	ControlRigAlphaBlend.SetValueRange(ControlRigAlpha, 0.f);
}

void UTrvAnimInstance::EnableControlRig(float InBlendDuration)
{
	if (InBlendDuration < 0.01f)
	{
		ControlRigAlphaBlend.SetValueRange(1, 1);
		ControlRigAlpha = 1.f;
		return;
	}

	float Value = ControlRigAlphaBlend.GetBlendedValue();
	float Ratio = 1.f - Value;

	ControlRigAlphaBlend.SetBlendTime(ControlRigBlendDuration * Ratio);
	ControlRigAlphaBlend.SetValueRange(ControlRigAlpha, 1.f);
}

void UTrvAnimInstance::SetParkourFrontLedgeTransform(const FTransform& InTransform)
{
	ParkourFrontLedgeTransform = InTransform;
}

const FTransform& UTrvAnimInstance::GetParkourFrontLedgeTransform() const
{
	return ParkourFrontLedgeTransform;
}

void UTrvAnimInstance::SetParkourBackLedgeTransform(const FTransform& InTransform)
{
	ParkourBackLedgeTransform = InTransform;
}

const FTransform& UTrvAnimInstance::GetParkourBackLedgeTransform() const
{
	return ParkourBackLedgeTransform;
}

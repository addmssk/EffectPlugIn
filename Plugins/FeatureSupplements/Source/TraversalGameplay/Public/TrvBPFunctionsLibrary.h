// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"

#include "TrvBPFunctionsLibrary.generated.h"


class UGameplayAbility;
class UAbilitySystemComponent;
class UCharacterMovementComponent;
class UGameplayCueNotify_Static;


/**************************************************************************************************
*
*   UTrvBPFunctionsLibrary
*
***/

UCLASS()
class UTrvBPFunctionsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
	/** Gets the ability instance or CDO to cause this effect */
	UFUNCTION(BlueprintPure, Category = "Ability|TraversalGameplay", Meta = (DisplayName = "GetAbility"))
	static TRAVERSALGAMEPLAY_API const UGameplayAbility* EffectContextGetAbility(FGameplayEffectContextHandle EffectContext);

	/** Try Get the AbilitySystemComponent from Character actor if possible */
	UFUNCTION(BlueprintPure, Category = "Ability|TraversalGameplay", Meta = (DisplayName = "TryGetAbilitySystemFromCharacter"))
	static TRAVERSALGAMEPLAY_API UAbilitySystemComponent* CharacterGetAbilitySystemComponent(ACharacter* InCharacter);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ability|TraversalGameplay", meta = (DisplayName = "Give Ability And Activate Once with Source Object", ScriptName = "GiveAbilityAndActivateOnceWithSourceObject"))
	static TRAVERSALGAMEPLAY_API FGameplayAbilitySpecHandle GiveAbilityAndActivateOnceWithSourceObject(
		UAbilitySystemComponent* InAbilitySystemComponent,
		UObject* InSourceObject,
		TSubclassOf<UGameplayAbility> AbilityClass,
		int32 Level = 0,
		int32 InputID = -1
	);

	UFUNCTION(BlueprintCallable, Category = "TraversalGameplay")
	static TRAVERSALGAMEPLAY_API void SetAnimRootMotionTranslationScale(
		ACharacter* InActor,
		float InAnimRootMotionTranslationScale
	);

	UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ability|TraversalGameplay", meta = (DisplayName = "GetSourceObjectFromAbilitySpec"))
	static TRAVERSALGAMEPLAY_API UObject* GetSourceObjectFromAbilitySpec(UGameplayAbility* InAbility);

	/** Try fit curve until RMES accpetable */
	UFUNCTION(BlueprintPure, Category = "TraversalGameplay")
	static TRAVERSALGAMEPLAY_API TArray<float> FindBestCurveFit(
		const TArray<float>& InT,
		const TArray<float>& InValue,
		const int32 InMaxOrder = 5,
		const float InRMESAccpetable = 0.1f
	);

	UFUNCTION(BlueprintPure, Category = "TraversalGameplay", Meta = (DisplayName = "IsSlideMovementMode"))
	static TRAVERSALGAMEPLAY_API bool IsSlideMovementMode(UCharacterMovementComponent* InComponent);
	
	UFUNCTION(BlueprintPure, Category = "TraversalGameplay", Meta = (DisplayName = "IsFollowFlightPathMovementMode"))
	static TRAVERSALGAMEPLAY_API bool IsFollowFlightPathMovementMode(UCharacterMovementComponent* InComponent);

	UFUNCTION(BlueprintPure, Category = "TraversalGameplay")
	static TRAVERSALGAMEPLAY_API FGameplayTag GetGameplayCueTag(UGameplayCueNotify_Static* InGameplayCue);

	UFUNCTION(BlueprintPure, Category = "TraversalGameplay|Collision")
	static TRAVERSALGAMEPLAY_API ETraceTypeQuery GetTraceTypeQuery(ECollisionChannel InCollisionChannel);

	UFUNCTION(BlueprintPure, Category = "TraversalGameplay")
	static TRAVERSALGAMEPLAY_API int32 GetDedicatedVRamSizeInMB();
};

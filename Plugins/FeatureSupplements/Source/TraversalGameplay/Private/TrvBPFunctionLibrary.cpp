// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrvBPFunctionsLibrary.h"
#include "GameFramework/Character.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayCueNotify_Static.h"
#include "Character/TrvCharacterMovementComponent.h"
#include "TrvUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvBPFunctionsLibrary)


/**************************************************************************************************
*
*   UTrvBPFunctionsLibrary
*
***/

const UGameplayAbility* UTrvBPFunctionsLibrary::EffectContextGetAbility(FGameplayEffectContextHandle EffectContext)
{
	return Cast<UGameplayAbility>(EffectContext.GetAbility());
}

UAbilitySystemComponent* UTrvBPFunctionsLibrary::CharacterGetAbilitySystemComponent(ACharacter* InCharacter)
{
	if (IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(InCharacter))
	{
		return AbilitySystemInterface->GetAbilitySystemComponent();
	}

	return nullptr;
}

FGameplayAbilitySpecHandle UTrvBPFunctionsLibrary::GiveAbilityAndActivateOnceWithSourceObject(UAbilitySystemComponent* InAbilitySystemComponent, UObject* InSourceObject, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level, int32 InputID)
{
    if (IsValid(InAbilitySystemComponent) == false)
    {
        return FGameplayAbilitySpecHandle();
    }

    FGameplayAbilitySpec NewSpec = FGameplayAbilitySpec(AbilityClass, Level, InputID, InSourceObject);

    return InAbilitySystemComponent->GiveAbilityAndActivateOnce(NewSpec);
}

void UTrvBPFunctionsLibrary::SetAnimRootMotionTranslationScale(ACharacter* InActor, float InAnimRootMotionTranslationScale)
{
    if (InActor)
    {
        InActor->SetAnimRootMotionTranslationScale(InAnimRootMotionTranslationScale);
    }
}

UObject* UTrvBPFunctionsLibrary::GetSourceObjectFromAbilitySpec(UGameplayAbility* InAbility)
{
    if (IsValid(InAbility) == false)
    {
        return nullptr;
    }

    UAbilitySystemComponent* AbilityComp =  InAbility->GetAbilitySystemComponentFromActorInfo();
    if (AbilityComp == nullptr)
    {
        return nullptr;
    }

    FGameplayAbilitySpecHandle Handle = InAbility->GetCurrentAbilitySpecHandle();
    if (FGameplayAbilitySpec* Spec = AbilityComp->FindAbilitySpecFromHandle(Handle))
    {
        return Spec->SourceObject.Get();
    }

    return nullptr;
}

namespace TraversalGameplay
{
    // A helper to solve linear system Ax = B using Gaussian Elimination
    // A is a square matrix (flattened or vector of vectors), B is the result vector
    // Returns the coefficients (solution vector)
    TArray<float> SolveLinearSystem(TArray<TArray<float>> A, TArray<float> B) {
        int n = B.Num();

        // Forward Elimination
        for (int i = 0; i < n; i++) {
            // Pivot
            int maxRow = i;
            for (int k = i + 1; k < n; k++) {
                if (FMath::Abs(A[k][i]) > FMath::Abs(A[maxRow][i])) {
                    maxRow = k;
                }
            }

            Swap(A[i], A[maxRow]);
            Swap(B[i], B[maxRow]);

            // Eliminate
            for (int k = i + 1; k < n; k++) {
                float factor = A[k][i] / A[i][i];
                B[k] -= factor * B[i];
                for (int j = i; j < n; j++) {
                    A[k][j] -= factor * A[i][j];
                }
            }
        }

        // Back Substitution
        TArray<float> solution;
        solution.SetNumZeroed(n);
        for (int i = n - 1; i >= 0; i--) {
            float sum = 0;
            for (int j = i + 1; j < n; j++) {
                sum += A[i][j] * solution[j];
            }
            solution[i] = (B[i] - sum) / A[i][i];
        }
        return solution;
    }

    // ==========================================
    // 2. Polynomial Fitting Logic
    // ==========================================

    struct FitResult {
        int order;
        float rmse;
        TArray<float> coefficients; // [a0, a1, a2...] where y = a0 + a1*t + a2*t^2...
    };

    // Fits a polynomial of specific 'order' to data 'y' over parameter 't'
    FitResult FitPolynomial(const TArray<float>& t, const TArray<float>& y, int order) {
        int n = t.Num();
        int m = order + 1; // Number of coefficients

        // Build Normal Matrix (A) and Vector (B)
        // We are solving A * coeffs = B
        TArray<TArray<float>> A;
        A.SetNumZeroed(m);
        for (TArray<float>& Elem : A)
        {
            Elem.SetNumZeroed(m);
        }
        
        TArray<float> B;
        B.Init(m, 0.0);
        B.SetNumZeroed(m);

        // Precompute sums of powers of t to fill the matrix efficiently
        // Matrix A[row][col] is sum(t^(row+col))
        TArray<float> sum_t_powers;
        sum_t_powers.SetNumZeroed(2 * m);
        for (int i = 0; i < 2 * m; ++i) {
            for (float val : t) {
                sum_t_powers[i] += FMath::Pow(val, i);
            }
        }

        // Fill A
        for (int r = 0; r < m; ++r) {
            for (int c = 0; c < m; ++c) {
                A[r][c] = sum_t_powers[r + c];
            }
        }

        // Fill B (sum(y * t^row))
        for (int r = 0; r < m; ++r) {
            for (int i = 0; i < n; ++i) {
                B[r] += y[i] * FMath::Pow(t[i], r);
            }
        }

        // Solve
        TArray<float> coeffs = SolveLinearSystem(A, B);

        // Calculate RMSE (Error)
        float mse = 0.0;
        for(int i=0; i<n; ++i) {
            float predicted = 0.0;
            for(int j=0; j<m; ++j) {
                predicted += coeffs[j] * FMath::Pow(t[i], j);
            }
            mse += FMath::Pow(y[i] - predicted, 2);
        }
        float rmse = FMath::Sqrt(mse / n);

        return { order, rmse, coeffs };
    }

    // Automatically finds the best order by checking where error improvement drops off
    FitResult FindBestCurveFit(const TArray<float>& t, const TArray<float>& val, int maxOrder = 5, const float InRMESAccpetable = 0.1) {
        FitResult best = FitPolynomial(t, val, 1);
    
        // Threshold: if error improves by less than 20%, stop increasing order
        const float IMPROVEMENT_THRESHOLD = 0.20; 

        UE_LOG(LogTrvGeneral, Log, TEXT(" checking orders: 1 (err: %.4f)"), best.rmse);

        if (InRMESAccpetable >= best.rmse)
        {
            return best; 
        }

        for (int order = 2; order <= maxOrder; ++order) {
            FitResult current = FitPolynomial(t, val, order);
            UE_LOG(LogTrvGeneral, Log, TEXT(" checking orders: %d (err: %.4f)"), order, current.rmse);
        
            float improvement = (best.rmse - current.rmse) / best.rmse;

            if (InRMESAccpetable >= best.rmse)
            {
                return best; 
            }

            if (improvement < IMPROVEMENT_THRESHOLD) {
                UE_LOG(LogTrvGeneral, Log, TEXT("[Stop: Diminishing returns]"));
                return best; 
            }
        
            best = current; // Upgrade to the higher order
        }

        return best;
    }
}

TArray<float> UTrvBPFunctionsLibrary::FindBestCurveFit(
	const TArray<float>& InT,
	const TArray<float>& InValue,
	const int32 InMaxOrder,
	const float InRMESAccpetable
)
{
    TraversalGameplay::FitResult Ret = TraversalGameplay::FindBestCurveFit(InT, InValue, InMaxOrder, InRMESAccpetable);
    
    UE_LOG(LogTrvGeneral, Log, TEXT("Find Curve Fit Result Order : [%d]"), Ret.order);
    return MoveTemp(Ret.coefficients);
}

bool UTrvBPFunctionsLibrary::IsSlideMovementMode(UCharacterMovementComponent* InComponent)
{
    if (InComponent == nullptr)
    {
        return false;
    }

    return InComponent->MovementMode == MOVE_Custom && InComponent->CustomMovementMode == (uint8)ETrvCustomMovementMode::Slide;

}

bool UTrvBPFunctionsLibrary::IsFollowFlightPathMovementMode(UCharacterMovementComponent* InComponent)
{
    if (InComponent == nullptr)
    {
        return false;
    }

    return InComponent->MovementMode == MOVE_Custom && InComponent->CustomMovementMode == (uint8)ETrvCustomMovementMode::FollowFlightPath;
}

FGameplayTag UTrvBPFunctionsLibrary::GetGameplayCueTag(UGameplayCueNotify_Static* InGameplayCue)
{
    if (InGameplayCue)
    {
        return InGameplayCue->GameplayCueTag;
    }

    return FGameplayTag::EmptyTag;
}

ETraceTypeQuery UTrvBPFunctionsLibrary::GetTraceTypeQuery(ECollisionChannel InCollisionChannel)
{
    return UEngineTypes::ConvertToTraceType(InCollisionChannel);
}

int32 UTrvBPFunctionsLibrary::GetDedicatedVRamSizeInMB()
{
	FTextureMemoryStats TextureMemStats;
	RHIGetTextureMemoryStats(TextureMemStats);
    
 	return TextureMemStats.DedicatedVideoMemory / int64(1024 * 1024);
}

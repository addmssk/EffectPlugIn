#include "Material/VfxMaterialEffectManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

#include "../VfxUtils.h"


/**************************************************************************************************
*
*   Defines and Constants
* 
***/

#define ENABLE_EFFECTMANGER_LOG 0

DECLARE_CYCLE_STAT(TEXT("VfxMaterialEffectManager_Tick"), STAT_GMaterialEffectManager_Tick, STATGROUP_Character);

#if WITH_EDITOR
static int32 gMaterialEffectManagerUpdateDebugString = 0;
static FAutoConsoleVariableRef CVarMaterialEffectManagerUpdateDebugString(
	TEXT("msk.MaterialEffectManager.UpdateDebugString"),
	gMaterialEffectManagerUpdateDebugString,
	TEXT("Update Debug String per tick"),
	ECVF_Default
);
#endif

static const int32 sUtilitySystemLayerMaterialInternalID = TNumericLimits<int32>::Max();
static const int32 sUtilitySystemLayerInternalID = TNumericLimits<int32>::Max() - 1;

TArray<FVfxMaterialEffectManager::FScalarValueArray*> FVfxMaterialEffectManager::sScalarArrayPool;
TArray<FVfxMaterialEffectManager::FColorValueArray*> FVfxMaterialEffectManager::sColorArrayPool;
TArray<FVfxMaterialEffectManager::FTextureValueArray*> FVfxMaterialEffectManager::sTextureArrayPool;

struct FVfxMaterialParamChangeEffect
{
	FVfxMaterialParamChangeEffect ()
		: ID(0)
		, InternalID(-1)
		, State(FVfxMaterialEffectManager::EActiveEffectInternalState::None)
		, ElapsedTime(0.f)
		, PeriodTime(0.0f)
		, FadeInTime(0.0f)
		, FadeOutTime(0.0f)
		, bLooping(false)
		, bHasCurve(false)
		, FadeRate(1.0f)
	{
	}
	
	static TSharedPtr<FVfxMaterialParamChangeEffect> CreateSharable ();
	static void Destroy (FVfxMaterialParamChangeEffect* InPtr);

	struct DeleterForSharedPtr
	{
		FORCEINLINE void operator()(FVfxMaterialParamChangeEffect* InPtr) const
		{
			FVfxMaterialParamChangeEffect::Destroy(InPtr);
		}
	};

	uint32 ID;
	int32 InternalID;
	FVfxMaterialEffectManager::EActiveEffectInternalState State;

	float ElapsedTime;
	float PeriodTime;

	float FadeInTime;
	float FadeOutTime;
	bool bLooping;
	bool bHasCurve;
	float FadeRate;

	TArray<FName, TInlineAllocator<4>> MaterialSlotNames;

	const UVfxMaterialParamsData* Params;
};

static TVfxCommonStructPool<DEF_TYPE_SIZE(FVfxMaterialParamChangeEffect), 1024> sMaterialParamChangeEffectPool(TEXT("FVfxMaterialParamChangeEffect"));


TSharedPtr<FVfxMaterialParamChangeEffect> FVfxMaterialParamChangeEffect::CreateSharable ()
{
	return MakeShareable(sMaterialParamChangeEffectPool.New(), FVfxMaterialParamChangeEffect::DeleterForSharedPtr());
}

void FVfxMaterialParamChangeEffect::Destroy (FVfxMaterialParamChangeEffect* InPtr)
{
	sMaterialParamChangeEffectPool.Delete(InPtr);
}

bool FVfxMaterialEffectManager::RemoveAllMaterialInstanceDynamic (UMeshComponent* InComponent, bool bDoRenderDirty)
{
	if (InComponent == nullptr)
	{
		return false;
	}
	

	if (InComponent->OverrideMaterials.Num())
	{
		bool bRemoved = false;
		
		TObjectPtr<UMaterialInterface>* Ptr = InComponent->OverrideMaterials.GetData();
		TObjectPtr<UMaterialInterface>* Term = Ptr + InComponent->OverrideMaterials.Num();
		for (; Ptr != Term; ++Ptr)
		{
			if (UMaterialInstanceDynamic* CurrMID = Cast<UMaterialInstanceDynamic>(*Ptr))
			{
				*Ptr = CurrMID->Parent;
				bRemoved = true;
			}
		}
		if (bDoRenderDirty && bRemoved)
		{
			InComponent->MarkRenderStateDirty();
		}

		return bRemoved;
	}

	return false;
}


/**************************************************************************************************
*
*   FVfxMaterialEffectManager
*
***/

FVfxMaterialEffectManager::FVfxMaterialEffectManager (bool OwnerIsSnapshot)
{
	InternalIdSource = 0;
	ActiveOverrideMaterialInternalID = -1;
	ActiveOverrideMaterialPriority = EVfxMaterialParamPriority::Layer0;
	ActiveOverrideMaterial = nullptr;

	bInited = false;
	bForceUpdateAll = false;
	bNeedNextTick = true;
	bNeedToCheckNextPriorityOverrideMaterial = false;
	bIsSnapshot = OwnerIsSnapshot;
}

FVfxMaterialEffectManager::~FVfxMaterialEffectManager ()
{
	Uninitialize(true);
}

void FVfxMaterialEffectManager::Initialize (UObject* InOwner, UPrimitiveComponent* InTargetComponent)
{
	if (InTargetComponent == nullptr)
	{
		return;
	}

	if (bInited == true)
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("%s : !!!!!!!!!!!!! Initialize again !!!!!!!!!!!"), ANSI_TO_TCHAR(__FUNCTION__));
		Uninitialize();
	}

	OwnerWeakPtr = InOwner;

	FVfxMaterialEffectManagerPrimitiveComponentInfo& NewComponentInfo = Components[Components.Add(InTargetComponent)];

	for (int32 i = 0; i < InTargetComponent->GetNumMaterials(); ++i)
	{		
		UMaterialInterface* OriginalMaterial = InTargetComponent->GetMaterial(i);
		NewComponentInfo.OriginalMaterials.Add(OriginalMaterial);
	}
	NewComponentInfo.MaterialSlotNames = InTargetComponent->GetMaterialSlotNames();
	bInited = true;

	for (int32 Idx = 0; Idx < PendingEffects.Num(); ++Idx)
	{
		FVfxMaterialEffectManagerPendingEffect& Curr = PendingEffects[Idx];

		CreateEffectInternal(
			Curr.InternalID,
			Curr.Params,
			Curr.EffectID,
			Curr.bOverrideTime,
			Curr.LifeTime,
			Curr.FadeInTime,
			Curr.FadeOutTime,
			Curr.bLoop,			
			Curr.ElapsedTime,
			Curr.MaterialSlotNames
		);
	}
	PendingEffects.Reset();
}

void FVfxMaterialEffectManager::AddComponent (UPrimitiveComponent* InComponent)
{
	if (bInited == false)
	{
		return;
	}

	if (Components.Contains(InComponent))
	{
		UE_LOG(LogVfxGeneral, Error, TEXT("%s : !!!!!!!!!!!!! Adding Same Component (%s) again !!!!!!!!!!!"),
			ANSI_TO_TCHAR(__FUNCTION__), *InComponent->GetName()
		);
		return;
	}
	
	FVfxMaterialEffectManagerPrimitiveComponentInfo& NewComponentInfo = Components[Components.Add(InComponent)];
	for (int32 i = 0; i < InComponent->GetNumMaterials(); ++i)
	{
		UMaterialInterface* OriginalMaterial = InComponent->GetMaterial(i);
		NewComponentInfo.OriginalMaterials.Add(OriginalMaterial);
	}
	NewComponentInfo.MaterialSlotNames = InComponent->GetMaterialSlotNames();

	if (bNeedNextTick)
	{
		ResetToOriginalMID(&NewComponentInfo);
	}
}

void FVfxMaterialEffectManager::RemoveComponent (UPrimitiveComponent* InComponent)
{
	if (bInited == false)
	{
		return;
	}

	FVfxMaterialEffectManagerPrimitiveComponentInfo* Ret = Components.Find(InComponent);
	if (Ret == nullptr)
	{
		return;
	}
	ClearComponentInfo(Ret);
	Components.Remove(InComponent);
}

const bool FVfxMaterialEffectManager::HasComponent(USceneComponent* InComponent)
{
    if (bInited == false)
    {
        return false;
    }

	if (InComponent == nullptr)
	{
		return false;
	}

	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
	{
		if (CurrCompoent.Component == InComponent)
		{
			return true;
		}
	}

	return false;
}

void FVfxMaterialEffectManager::ClearComponentInfo (FVfxMaterialEffectManagerPrimitiveComponentInfo* InInfo)
{
#if 0
	for (TPair<UObject*, FVfxMaterialEffectManagerOverridedMaterialCache>& CurrCache : InInfo->CachedOverridedMIDs)
	{
		for (UMaterialInstanceDynamic* CurrMID : CurrCache.Value.MIDs)
		{
			if (CurrMID != nullptr)
			{
				CurrMID->ConditionalBeginDestroy();
			}
		}
	}
	InInfo->CachedOverridedMIDs.Reset();
#endif
}

void FVfxMaterialEffectManager::Uninitialize (const bool bCalledByDestructor)
{
	if (bInited == false)
	{
		return;
	}

	OwnerWeakPtr.Reset();

	for (TPair<TTuple<TSoftObjectPtr<UTexture>, int32>,TSharedPtr<struct FStreamableHandle>>& CurrHandle : LoadHandleMap)
	{
		if (CurrHandle.Value.IsValid())
		{
			CurrHandle.Value->CancelHandle();
		}
	}
	LoadHandleMap.Reset();
	TextureReferenceHolder.Reset();

	if (bCalledByDestructor == false)
	{
		ResetToOrigin(); //
	}
	else
	{
		PendingEffects.Reset();
	}
	
	bInited = false;
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& Curr : Components)
	{
		ClearComponentInfo(&Curr);
	}
	Components.Reset();
	ActiveEffects.Reset();
	
	for (TPair<FName, FScalarLayers>& CurrParam : ActiveScalarParams)
	{
		FScalarLayers& CurrLayers = CurrParam.Value;
		for (int32 LayerIdx = 0; LayerIdx < (int32)EVfxMaterialParamPriority::Count; ++LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				Release(CurrParam.Value.Values[LayerIdx]);
			}
		}
	}
	ActiveScalarParams.Reset();
	
	for (TPair<FName, FColorLayers>& CurrParam : ActiveColorParams)
	{
		FColorLayers& CurrLayers = CurrParam.Value;
		for (int32 LayerIdx = 0; LayerIdx < (int32)EVfxMaterialParamPriority::Count; ++LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				Release(CurrParam.Value.Values[LayerIdx]);
			}
		}
	}
	ActiveColorParams.Reset();

	for (TPair<FName, FTextureLayers>& CurrParam : ActiveTextureParams)
	{
		FTextureLayers& CurrLayers = CurrParam.Value;
		for (int32 LayerIdx = 0; LayerIdx < (int32)EVfxMaterialParamPriority::Count; ++LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				Release(CurrParam.Value.Values[LayerIdx]);
			}
		}
	}
	ActiveTextureParams.Reset();
}

float FVfxMaterialEffectManager::AccumValue (FScalarLayers& InLayers, FScalarValueArray& InArray)
{
	// Curr : Last one standing
	if (InArray.Num())
	{
		FScalarValue& Curr = InArray.Last();

		if (Curr.Effect->State == EActiveEffectInternalState::FadeOut)
		{
			return FMath::Lerp(InLayers.CachedOrigialValue, Curr.FinalValue, Curr.Effect->FadeRate);
		}
		else
		{
			float TargetValue = Curr.BlendEndValue;
			if (Curr.CurveRef != nullptr)
			{
				const FRichCurve* Curve = Curr.CurveRef->GetRichCurveConst();
				if (Curr.Effect->bLooping)
				{
					float Parameter = Curr.bPlayCurveOnce?
						FMath::Clamp(Curr.Effect->ElapsedTime, 0.f, Curr.CurveMaxRange) :
						FMath::Fmod(Curr.Effect->ElapsedTime, Curr.CurveMaxRange);
					TargetValue = Curve->Eval(Parameter);

				}
				else
				{
					if (Curr.bNormalizedParamterization)
					{
						float Parameter = FMath::Clamp(Curr.Effect->ElapsedTime / Curr.Effect->PeriodTime, 0.f, 1.f);
						TargetValue = Curve->Eval(Parameter);
					}
					else
					{
						TargetValue = Curve->Eval(Curr.Effect->ElapsedTime);
					}
				}
			}

			if (Curr.Effect->State == EActiveEffectInternalState::FadeIn)
			{
				TargetValue = FMath::Lerp(Curr.BlendStartValue, TargetValue, Curr.Effect->FadeRate);
			}

			Curr.FinalValue = TargetValue;
			return Curr.FinalValue;
		}
	}

	// TODO : need Accumulaion?
#if 0
	for (FScalarValue& Curr : InArray)
	{
		if (Curr.bFadeEnabled)
		{
			Result = Curr.Effect->FadeRate * Curr.BlendEndValue;
		}
		else
		{
			Result = Curr.BlendEndValue;
		}
	}
#endif

	return InLayers.CachedOrigialValue;
}

FLinearColor FVfxMaterialEffectManager::AccumValue (FColorLayers& InLayers, FColorValueArray& InArray)
{
	// Curr : Last one standing
	if (InArray.Num())
	{
		FColorValue& Curr = InArray.Last();
		
		if (Curr.Effect->State == EActiveEffectInternalState::FadeOut)
		{
			return FMath::Lerp(InLayers.CachedOrigialValue, Curr.FinalValue, Curr.Effect->FadeRate);
		}
		else
		{
			FLinearColor TargetValue = Curr.BlendEndValue;
			if (Curr.CurveRef != nullptr)
			{
				if (Curr.Effect->bLooping)
				{
					float Parameter = Curr.bPlayCurveOnce?
						FMath::Clamp(Curr.Effect->ElapsedTime, 0.f, Curr.CurveMaxRange) :
						FMath::Fmod(Curr.Effect->ElapsedTime, Curr.CurveMaxRange);
					TargetValue = Curr.CurveRef->GetLinearColorValue(Parameter);
				}
				else
				{
					if (Curr.bNormalizedParamterization)
					{
						float Parameter = FMath::Clamp(Curr.Effect->ElapsedTime / Curr.Effect->PeriodTime, 0.f, 1.f);
						TargetValue = Curr.CurveRef->GetLinearColorValue(Parameter);
					}
					else
					{
						TargetValue = Curr.CurveRef->GetLinearColorValue(Curr.Effect->ElapsedTime);
					}
				}
			}

			if (Curr.Effect->State == EActiveEffectInternalState::FadeIn)
			{
				TargetValue = FMath::Lerp(Curr.BlendStartValue, TargetValue, Curr.Effect->FadeRate);
			}

			Curr.FinalValue = TargetValue;
			return Curr.FinalValue;
		}
	}
	
	// TODO : need Accum?
#if 0
	for (FColorValue& Curr : InArray)
	{
		Result = Curr.Value;
		//Curr.Effect->FadeRate;
	}
#endif

	return InLayers.CachedOrigialValue;
}

UTexture** FVfxMaterialEffectManager::AccumValue (FTextureLayers& InLayers, FTextureValueArray& InArray)
{
	// Curr : Last one standing
	if (InArray.Num())
	{
		FTextureValue& Curr = InArray.Last();
		return Curr.Values;
	}

#if 0
	for (FTextureValue& Curr : InArray)
	{
		Result = Curr.Value;
		//Curr.Effect->FadeRate;
	}
#endif

	return InLayers.CachedOrigialValues;
}

void FVfxMaterialEffectManager::Tick (const float DeltaSeconds)
{
	if (bInited == false)
	{
		for (int32 Idx = 0; Idx < PendingEffects.Num(); ++Idx)
		{
			FVfxMaterialEffectManagerPendingEffect& Curr = PendingEffects[Idx];
			Curr.ElapsedTime += DeltaSeconds;

			if (Curr.bLoop == false && Curr.ElapsedTime >= Curr.LifeTime)
			{
				PendingEffects.RemoveAt(Idx, 1, EAllowShrinking::No);
				--Idx;
				continue;
			}
		}
	}

	if (bNeedNextTick == false)
	{
#if WITH_EDITOR
		UpdateDebugInfo(true);
#endif
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_GMaterialEffectManager_Tick);
	
	bNeedNextTick = false;
	if (ActiveEffects.Num() != 0)
	{
		TArray<FVfxMaterialParamChangeEffectPtr, TInlineAllocator<6>> RemovedEffects;

		for (FVfxMaterialParamChangeEffectPtr& CurrEffect : ActiveEffects)
		{
			if (CurrEffect->State == EActiveEffectInternalState::During)
			{
				if (CurrEffect->bLooping == true)
				{
					if (CurrEffect->bHasCurve == true)
					{
						CurrEffect->ElapsedTime += DeltaSeconds;
						CurrEffect->ElapsedTime = FMath::Fmod(CurrEffect->ElapsedTime, 3600.f);
						
						bNeedNextTick = true;
					}
					
					continue;
				}

				if (CurrEffect->ElapsedTime >= (CurrEffect->PeriodTime - CurrEffect->FadeOutTime))
				{
					if (CurrEffect->FadeOutTime > 0.f)
					{
						CurrEffect->State = EActiveEffectInternalState::FadeOut;
					}
					else
					{
						RemovedEffects.Add(CurrEffect);
						continue;
					}
				}
			}
			
			bNeedNextTick = true;
			if (CurrEffect->State == EActiveEffectInternalState::None)
			{
				CurrEffect->State = (CurrEffect->FadeInTime > 0.0f)?
					EActiveEffectInternalState::FadeIn : EActiveEffectInternalState::During;
				if (CurrEffect->State == EActiveEffectInternalState::FadeIn)
				{
					CurrEffect->FadeRate = FMath::Min(1.0f, CurrEffect->ElapsedTime / CurrEffect->FadeInTime);
				}
			}
			else if (CurrEffect->State == EActiveEffectInternalState::FadeIn)
			{
				CurrEffect->FadeRate = FMath::Min(1.0f, CurrEffect->ElapsedTime / CurrEffect->FadeInTime);
				if (FMath::IsNearlyEqual(CurrEffect->FadeRate, 1.0f))
				{
					CurrEffect->State = (CurrEffect->bLooping || CurrEffect->ElapsedTime < CurrEffect->PeriodTime)?
						EActiveEffectInternalState::During : EActiveEffectInternalState::FadeOut;
				}
			}
			else if (CurrEffect->State == EActiveEffectInternalState::FadeOut)
			{
				float RemainTime = CurrEffect->PeriodTime - CurrEffect->ElapsedTime;
				if (RemainTime > 0)
				{
					CurrEffect->FadeRate = FMath::Clamp(RemainTime / CurrEffect->FadeOutTime, 0.f, 1.f);
				}
				else
				{
					RemovedEffects.Add(CurrEffect);
					continue;
				}
			}
			
			CurrEffect->ElapsedTime += DeltaSeconds;
		}

		if (RemovedEffects.Num() > 0)
		{
			for (FVfxMaterialParamChangeEffectPtr& CurrEffect : RemovedEffects)
			{
				RemoveEffect(CurrEffect, true);
			}
		}
	}

	if (bIsSnapshot && ActiveEffects.Num() == 0)
	{
		return;
	}

	TryChangeNextPriorityMaterial();

	for (TPair<FName, FScalarLayers>& CurrParam : ActiveScalarParams)
	{
		FScalarLayers& CurrLayers = CurrParam.Value;
		FScalarValueArray* ActiveLayer = nullptr;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				ActiveLayer = CurrParam.Value.Values[LayerIdx];
				break;
			}
		}

		if (ActiveLayer != nullptr && ActiveLayer->Num())
		{
			float NewValue = AccumValue(CurrLayers, *ActiveLayer);
			if (NewValue != CurrLayers.LastValue || bForceUpdateAll)
			{
				for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
				{
					for (UMaterialInstanceDynamic* MID : CurrCompoent.CurrentMIDs)
					{
						if (MID)
						{
						#if ENABLE_EFFECTMANGER_LOG
							UE_LOG(LogVfxGeneral, Error, TEXT("SetScalar : %.2f"), NewValue);
						#endif
							MID->SetScalarParameterValue(CurrParam.Key, NewValue);
						}
					}
				}
			}
			CurrLayers.LastValue = NewValue;
		}
		else if (bForceUpdateAll || CurrLayers.bNeedPush)
		{
			for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
			{
				for (UMaterialInstanceDynamic* MID : CurrCompoent.CurrentMIDs)
				{
					if (MID)
					{
					#if ENABLE_EFFECTMANGER_LOG
						UE_LOG(LogVfxGeneral, Error, TEXT("SetScalar (L) : %.2f"), CurrLayers.CachedOrigialValue);
					#endif
						MID->SetScalarParameterValue(CurrParam.Key, CurrLayers.CachedOrigialValue);
						CurrLayers.LastValue = CurrLayers.CachedOrigialValue;
					}
				}
			}
		}

		CurrLayers.bNeedPush = false;
	}

	for (TPair<FName, FColorLayers>& CurrParam : ActiveColorParams)
	{
		FColorLayers& CurrLayers = CurrParam.Value;
		FColorValueArray* ActiveLayer = nullptr;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				ActiveLayer = CurrParam.Value.Values[LayerIdx];
				break;
			}
		}

		if (ActiveLayer != nullptr && ActiveLayer->Num())
		{
			FLinearColor NewValue = AccumValue(CurrLayers, *ActiveLayer);
			if (NewValue != CurrLayers.LastValue || bForceUpdateAll)
			{
				for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
				{
					for (UMaterialInstanceDynamic* MID : CurrCompoent.CurrentMIDs)
					{
						if (MID)
						{
							MID->SetVectorParameterValue(CurrParam.Key, NewValue);
						}
					}
				}
			}
			CurrLayers.LastValue = NewValue;
		}
		else if (bForceUpdateAll || CurrLayers.bNeedPush)
		{
			for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
			{
				for (UMaterialInstanceDynamic* MID : CurrCompoent.CurrentMIDs)
				{
					if (MID)
					{
						MID->SetVectorParameterValue(CurrParam.Key, CurrLayers.CachedOrigialValue);
						CurrLayers.LastValue = CurrLayers.CachedOrigialValue;
					}
				}
			}
		}

		CurrLayers.bNeedPush = false;
	}

	for (TPair<FName, FTextureLayers>& CurrParam : ActiveTextureParams)
	{
		FTextureLayers& CurrLayers = CurrParam.Value;
		FTextureValueArray* ActiveLayer = nullptr;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr)
			{
				ActiveLayer = CurrParam.Value.Values[LayerIdx];
				break;
			}
		}

		if (ActiveLayer != nullptr && ActiveLayer->Num())
		{
			UTexture** NewValue = AccumValue(CurrLayers, *ActiveLayer);
			bool bDifferentTextures = (
				0 != FMemory::Memcmp(NewValue, CurrLayers.LastValues, sizeof(UTexture*)*FTextureValue::TEX_ARRAY_MAX)
			);
			if (bDifferentTextures || bForceUpdateAll)
			{
				for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
				{
					for (int32 MatIdx = 0; MatIdx <  CurrCompoent.CurrentMIDs.Num(); ++ MatIdx)
					{
						UMaterialInstanceDynamic* MID = CurrCompoent.CurrentMIDs[MatIdx];
						if (MID)
						{
							if (NewValue[MatIdx] != nullptr)
							{
								MID->SetTextureParameterValue(CurrParam.Key, NewValue[MatIdx]);
							}
						}
					}
				}
			}
			FMemory::Memcpy(CurrLayers.LastValues, NewValue, sizeof(UTexture*)*FTextureValue::TEX_ARRAY_MAX);
		}
		else if (bForceUpdateAll || CurrLayers.bNeedPush)
		{
			for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrCompoent : Components)
			{
				for (int32 MatIdx = 0; MatIdx <  CurrCompoent.CurrentMIDs.Num(); ++ MatIdx)
				{
					UMaterialInstanceDynamic* MID = CurrCompoent.CurrentMIDs[MatIdx];
					if (MID)
					{
						if (CurrLayers.CachedOrigialValues[MatIdx] != nullptr)
						{
							MID->SetTextureParameterValue(CurrParam.Key, CurrLayers.CachedOrigialValues[MatIdx]);
						}
					}
					CurrLayers.LastValues[MatIdx] = CurrLayers.CachedOrigialValues[MatIdx];
				}
			}
		}

		CurrLayers.bNeedPush = false;
	}

	bForceUpdateAll = false;
	
#if WITH_EDITOR
	UpdateDebugInfo(false);
#endif
}

#if WITH_EDITOR
void FVfxMaterialEffectManager::UpdateDebugInfo (bool bSkipTicking)
{
	DebugInfo.Reset();

	if (gMaterialEffectManagerUpdateDebugString == 0)
	{
		return;
	}
	
	TCHAR Buffer[512];

	FCString::Snprintf(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("Is Ticking(%s) \n"), bSkipTicking? TEXT("N") : TEXT("Y"));
	DebugInfo += Buffer;

	FCString::Snprintf(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("Active Effect Count (%d) \n\n"), ActiveEffects.Num());
	DebugInfo += Buffer;

	DebugInfo += TEXT("Scalar Params Begin \n");
	for (TPair<FName, FScalarLayers>& CurrParam : ActiveScalarParams)
	{
		bool bActive = false;
		float FadeRate = 1.f;
		int32 State = -1;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr && CurrParam.Value.Values[LayerIdx]->Num())
			{
				FadeRate = CurrParam.Value.Values[LayerIdx]->Last().Effect->FadeRate;
				State = (int32)CurrParam.Value.Values[LayerIdx]->Last().Effect->State;
				bActive = true;
				break;
			}
		}

		FCString::Snprintf(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("Name(%s), Value(%.2f), Original(%.2f), Active(%s, %d, %.2f) \n"),
			*CurrParam.Key.ToString(), CurrParam.Value.LastValue, CurrParam.Value.CachedOrigialValue,
			bActive? TEXT("Y") : TEXT("N"), State, FadeRate
		
		);
		DebugInfo += Buffer;
	}
	DebugInfo += TEXT("Scalar Params End \n\n");

	DebugInfo += TEXT("Color Params Begin \n");
	for (TPair<FName, FColorLayers>& CurrParam : ActiveColorParams)
	{
		bool bActive = false;
		float FadeRate = 1.f;
		int32 State = -1;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr && CurrParam.Value.Values[LayerIdx]->Num())
			{
				FadeRate = CurrParam.Value.Values[LayerIdx]->Last().Effect->FadeRate;
				State = (int32)CurrParam.Value.Values[LayerIdx]->Last().Effect->State;
				bActive = true;
				break;
			}
		}
		
		FCString::Snprintf(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("Name(%s), Value(%s), Original(%s), Active(%s, %d, %.2f) \n"),
			*CurrParam.Key.ToString(), *CurrParam.Value.LastValue.ToString(), *CurrParam.Value.CachedOrigialValue.ToString(),
			bActive? TEXT("Y") : TEXT("N"), State, FadeRate
		
		);
		DebugInfo += Buffer;
	}
	DebugInfo += TEXT("Color Params End \n\n");
	
	DebugInfo += TEXT("Texture Params Begin \n");
	for (TPair<FName, FTextureLayers>& CurrParam : ActiveTextureParams)
	{
		bool bActive = false;
		for (int32 LayerIdx = (int32)EVfxMaterialParamPriority::Count - 1; LayerIdx >= 0; --LayerIdx)
		{
			if (CurrParam.Value.Values[LayerIdx] != nullptr && CurrParam.Value.Values[LayerIdx]->Num())
			{
				bActive = true;
				break;
			}
		}

		UTexture* RepresentitiveLastTex = CurrParam.Value.LastValues[0];
		UTexture* RepresentitiveCacheTex = CurrParam.Value.CachedOrigialValues[0];
		
		FCString::Snprintf(Buffer, UE_ARRAY_COUNT(Buffer), TEXT("Name(%s), Value(%s), Original(%s), Active(%s) \n"),
			*CurrParam.Key.ToString(),
			(RepresentitiveLastTex != nullptr && RepresentitiveLastTex->IsValidLowLevel())? *RepresentitiveLastTex->GetName() : TEXT("None"),
			(RepresentitiveCacheTex != nullptr && RepresentitiveCacheTex->IsValidLowLevel())? *RepresentitiveCacheTex->GetName() : TEXT("None"),
			bActive? TEXT("Y") : TEXT("N")
		
		);
		DebugInfo += Buffer;
	}
	DebugInfo += TEXT("Texture Params End \n\n");
}
#endif

int32 FVfxMaterialEffectManager::CreateEffect (
	const UVfxMaterialParamsData* InParam,
	uint32 ID,
	bool bOverrideTime,
	float Time,
	float FadeInTime,
	float FadeOutTime,
	bool bLoop,
	const TArray<FName>& MaterialSlotNames
)
{
	TArrayView<FName> MaterialSlotNamesView = MakeArrayView((FName*)MaterialSlotNames.GetData(), MaterialSlotNames.Num());

	const int32 NewInternalID = ++InternalIdSource;
	if (CreateEffectInternal(NewInternalID, InParam, ID, bOverrideTime, Time, FadeInTime, FadeOutTime, bLoop, 0.f, MaterialSlotNamesView))
	{
		return NewInternalID;
	}

	if (bInited == false)
	{
		AddToPendingEffect(NewInternalID, InParam, ID, bOverrideTime, Time, FadeInTime, FadeOutTime, bLoop, MaterialSlotNamesView);
		return NewInternalID;
	}

	return -1;
}

bool FVfxMaterialEffectManager::CreateEffectInternal (
	int32 NewInternalID,
	const UVfxMaterialParamsData* InParam,
	uint32 EffectID,
	bool bOverrideTime,
	float Time,
	float FadeInTime,
	float FadeOutTime,
	bool bLoop,
	float TimeOffset,
	const TArrayView<FName>& MaterialSlotNames
)
{
	if (bInited == false)
	{
		return false;
	}
#if ENABLE_EFFECTMANGER_LOG
	UE_LOG(LogVfxGeneral, Error, TEXT("CreateEffectInternal : %d, override(%d), Value(%d)"), NewInternalID, Param->Param.OverrideMaterial ? 1 : 0, Param->Param.Scalars.Num());
#endif

	FVfxMaterialParamChangeEffectPtr NewEffect = FVfxMaterialParamChangeEffect::CreateSharable();
	NewEffect->ID = EffectID;
	NewEffect->InternalID = NewInternalID;
	if (bOverrideTime == false)
	{
		NewEffect->bLooping = InParam->Param.bIsLoop;
		NewEffect->FadeInTime = InParam->Param.FadeInTime;
		NewEffect->FadeOutTime = InParam->Param.FadeOutTime;
		NewEffect->PeriodTime = (NewEffect->bLooping) ? 1000 : InParam->Param.LifeTime;
	}
	else
	{
		NewEffect->bLooping = bLoop;
		NewEffect->FadeInTime = FadeInTime;
		NewEffect->FadeOutTime = FadeOutTime;
		NewEffect->PeriodTime = (bLoop) ? 1000 : Time;
	}

	NewEffect->Params = InParam;
	NewEffect->ElapsedTime = TimeOffset;
	NewEffect->MaterialSlotNames.Reset();
	NewEffect->MaterialSlotNames.Append(MaterialSlotNames);
	ActiveEffects.Push(NewEffect);
	bNeedNextTick = true;

	TryChangeMaterial(InParam, NewInternalID, InParam->Param.bNeedOriginalTextures, NewEffect->MaterialSlotNames);
	
	int32 TotalMaterialCount = 0;
	int32 UpdatableMIDCount = 0;
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		TotalMaterialCount += CurrComponent.OriginalMaterials.Num();
		UpdatableMIDCount += CurrComponent.CurrentMIDs.Num();
	}
	if (UpdatableMIDCount != TotalMaterialCount)
	{
		ResetToOriginalMIDs();
	}

	UpdateActiveParams(NewEffect, InParam);

	return true;
}

void FVfxMaterialEffectManager::AddToPendingEffect (
	int32 NewInternalID,
	const UVfxMaterialParamsData* InParam,
	uint32 EffectID,
	bool bOverrideTime,
	float Time,
	float FadeInTime,
	float FadeOutTime,
	bool bLoop,
	const TArrayView<FName>& MaterialSlotNames
)
{
	FVfxMaterialEffectManagerPendingEffect& NewPending = PendingEffects.AddDefaulted_GetRef();
	NewPending.Params = InParam;
	NewPending.EffectID = EffectID;
	NewPending.bOverrideTime = bOverrideTime;
	NewPending.LifeTime = Time;
	NewPending.FadeInTime = FadeInTime;
	NewPending.FadeOutTime = FadeOutTime;
	NewPending.bLoop = bLoop;

	NewPending.InternalID = NewInternalID;
	NewPending.ElapsedTime = 0.f;

	NewPending.MaterialSlotNames.Reset();
	NewPending.MaterialSlotNames.Append(MaterialSlotNames);
}

void FVfxMaterialEffectManager::UpdateActiveParams (
	FVfxMaterialParamChangeEffectPtr& InEffect,
	const UVfxMaterialParamsData* InParam
)
{
	bNeedNextTick = true;
	for (const FVfxMaterialParamScalar& CurrParam : InParam->Param.Scalars)
	{
		FScalarLayers* Active = ActiveScalarParams.Find(CurrParam.ParamName);
		if (Active == nullptr)
		{
			Active = &ActiveScalarParams.Add(CurrParam.ParamName);
			Active->bIsNew = true;
			UpdateOriginalValue(CurrParam.ParamName, *Active);
		}

		if (Active->Values[(int32)InParam->Param.Priority] == nullptr)
		{
			Active->Values[(int32)InParam->Param.Priority] = AllocScalarArray();
			Active->PriorityArrayMask |= 1 << (int32)InParam->Param.Priority;
		}

		FScalarValue& NewValue = Active->Values[(int32)InParam->Param.Priority]->AddDefaulted_GetRef();
		NewValue.GivenID = InEffect->ID;
		NewValue.InternalID = InEffect->InternalID;
		NewValue.Effect = InEffect;
		NewValue.CurveRef = CurrParam.bUseCurve? &CurrParam.Curve : nullptr;
		if (NewValue.CurveRef)
		{
			NewValue.bPlayCurveOnce = CurrParam.bPlayCurveOnce;
			NewValue.bNormalizedParamterization = CurrParam.bUseNormalizedTime;
			InEffect->bHasCurve = true;
			float MinTime = 0.f;
			NewValue.CurveRef->GetRichCurveConst()->GetTimeRange(MinTime, NewValue.CurveMaxRange);
			NewValue.CurveMaxRange = FMath::Max(0.01f, NewValue.CurveMaxRange);
		}
		NewValue.BlendStartValue = Active->LastValue;
		NewValue.BlendEndValue = CurrParam.Value;
	}

	for (const FVfxMaterialParamColor& CurrParam : InParam->Param.Colors)
	{
		FColorLayers* Active = ActiveColorParams.Find(CurrParam.ParamName);
		if (Active == nullptr)
		{
			Active = &ActiveColorParams.Add(CurrParam.ParamName);
			Active->bIsNew = true;
			UpdateOriginalValue(CurrParam.ParamName, *Active);
		}

		if (Active->Values[(int32)InParam->Param.Priority] == nullptr)
		{
			Active->Values[(int32)InParam->Param.Priority] = AllocColorArray();
			Active->PriorityArrayMask |= 1 << (int32)InParam->Param.Priority;
		}

		FColorValue& NewValue = Active->Values[(int32)InParam->Param.Priority]->AddDefaulted_GetRef();
		NewValue.GivenID = InEffect->ID;
		NewValue.InternalID = InEffect->InternalID;
		NewValue.Effect = InEffect;
		NewValue.CurveRef = CurrParam.bUseCurve? &CurrParam.Curve : nullptr;
		if (NewValue.CurveRef)
		{
			NewValue.bPlayCurveOnce = CurrParam.bPlayCurveOnce;
			NewValue.bNormalizedParamterization = CurrParam.bUseNormalizedTime;
			InEffect->bHasCurve = true;
			float MinTime = 0.f;
			if (NewValue.CurveRef->ExternalCurve != nullptr)
			{
				NewValue.CurveRef->ExternalCurve->GetTimeRange(MinTime, NewValue.CurveMaxRange);
				NewValue.CurveMaxRange = FMath::Max(0.01f, NewValue.CurveMaxRange);
			}
			else
			{
				for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(NewValue.CurveRef->ColorCurves); ++Idx)
				{
					float MaxTime = 0.f;
					NewValue.CurveRef->ColorCurves[Idx].GetTimeRange(MinTime, MaxTime);
				}

				NewValue.CurveMaxRange = FMath::Max(0.01f, NewValue.CurveMaxRange);
			}
		}
		 
		NewValue.BlendStartValue = Active->LastValue;
		NewValue.BlendEndValue = CurrParam.Value;
	}

	for (const FVfxMaterialParamTexture& CurrParam : InParam->Param.Textures)
	{
		FTextureLayers* Active = ActiveTextureParams.Find(CurrParam.ParamName);
		if (Active == nullptr)
		{
			Active = &ActiveTextureParams.Add(CurrParam.ParamName);
			Active->bIsNew = true;
			UpdateOriginalValue(CurrParam.ParamName, *Active);
		}

		if (Active->Values[(int32)InParam->Param.Priority] == nullptr)
		{
			Active->Values[(int32)InParam->Param.Priority] = AllocTextureArray();
			Active->PriorityArrayMask |= 1 << (int32)InParam->Param.Priority;
		}

		FTextureValueArray& CurrArray = *Active->Values[(int32)InParam->Param.Priority];

		check(CurrParam.TargetMaterialIndex < FTextureValue::TEX_ARRAY_MAX);

		bool bIsNew = true;
		for (FTextureValue& CurrValues : CurrArray)
		{
			if (CurrValues.InternalID == InEffect->InternalID)
			{
				bIsNew = false;
				check(CurrValues.Effect == InEffect);
				CurrValues.Values[CurrParam.TargetMaterialIndex] = CurrParam.Value;
				break;
			}
		}

		if (bIsNew)
		{
			FTextureValue& NewValue = CurrArray.AddDefaulted_GetRef();
			NewValue.GivenID = InEffect->ID;
			NewValue.InternalID = InEffect->InternalID;
			NewValue.Effect = InEffect;
			NewValue.Values[CurrParam.TargetMaterialIndex] = CurrParam.Value;
		}
	}
}

void FVfxMaterialEffectManager::ResetToOrigin()
{
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		if (::IsValid(CurrComponent.Component) == false)
		{
			continue;
		}

		int32 MatIdx = 0;
		for (UMaterialInterface* OriginalMat : CurrComponent.OriginalMaterials)
		{
			CurrComponent.Component->SetMaterial(MatIdx, OriginalMat);
			++MatIdx;
		}
	}
}

void FVfxMaterialEffectManager::ResetToOriginalMID (FVfxMaterialEffectManagerPrimitiveComponentInfo* InInfo)
{
	if (IsGarbageCollecting())
	{
#if !UE_BUILD_SHIPPING
		ensure(IsGarbageCollecting() == false);
#endif
		UE_LOG(LogVfxGeneral, Error, TEXT("%s : Can not create MID while GCing"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	InInfo->CurrentMIDs.Reset();	
	if (::IsValid(InInfo->Component) == false)
	{
		return;
	}

	int32 MatIdx = -1;
	for (UMaterialInterface* CurrMat : InInfo->OriginalMaterials)
	{
		++MatIdx;

		if (CurrMat == nullptr)
		{
			InInfo->CurrentMIDs.Add(nullptr);
			continue;
		}

		UMaterialInstanceDynamic* CurrMID = Cast<UMaterialInstanceDynamic>(CurrMat);
		if (CurrMID == nullptr)
		{
			CurrMID = UMaterialInstanceDynamic::Create(CurrMat, InInfo->Component);
			InInfo->OriginalMaterials[MatIdx] = CurrMID;
		}
		ensure(CurrMID);
		if (CurrMID != nullptr)
		{
			InInfo->Component->SetMaterial(MatIdx, CurrMID);
		}
		InInfo->CurrentMIDs.Add(CurrMID);
	}
}

void FVfxMaterialEffectManager::ResetToOriginalMIDs ()
{
	if (bIsSnapshot)
	{
		if (ActiveEffects.Num() == 0)
		{
			return;
		}
	}

	CacheOverridedMaterials();

	bForceUpdateAll = true;
	bNeedNextTick = true;
	
	ActiveOverrideMaterial = nullptr;
	ActiveOverrideMaterialPriority = EVfxMaterialParamPriority::Layer0;
	ActiveOverrideMaterialInternalID = -1;

	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		ResetToOriginalMID(&CurrComponent);
	}

	UpdateOriginalValueAll();
}

void FVfxMaterialEffectManager::TryChangeMaterial (
	const UVfxMaterialParamsData* InNewParam,
	int32 InInternalID,
	bool bTexCopy,
	const TArrayView<FName>& MaterialSlotNames
)
{
	if (bInited == false)
	{
		return;
	}

	if (ActiveOverrideMaterial != nullptr && ActiveOverrideMaterialPriority > InNewParam->Param.Priority)
	{
		return;
	}

	if (nullptr == InNewParam->Param.OverrideMaterial)
	{
		return;
	}
	
	if (IsGarbageCollecting())
	{
#if !UE_BUILD_SHIPPING
		ensure(IsGarbageCollecting() == false);
#endif
		UE_LOG(LogVfxGeneral, Error, TEXT("%s : Can not create MID while GCing"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	CacheOverridedMaterials();
	
	bForceUpdateAll = true;
	bNeedNextTick = true;

	ActiveOverrideMaterial = InNewParam->Param.OverrideMaterial;
	ActiveOverrideMaterialPriority = InNewParam->Param.Priority;
	ActiveOverrideMaterialInternalID = InInternalID;

	struct FLocalUtils
	{
		static UMaterialInstanceDynamic* CreateMID(
			UMaterialInterface* ActiveOverrideMaterial,
			UMaterialInterface* OriginMat,
			UPrimitiveComponent* Outer,
			const UVfxMaterialParamsData* InNewParam,
			bool bTexCopy
		)
		{
			UMaterialInstanceDynamic* NewMID = nullptr;
			UTexture* DiffuseTex = nullptr;
			UTexture* NormalTex = nullptr;
			UTexture* RMESTex = nullptr;
			
			if (InNewParam->Param.bSkipIfBaseColorTexNotExist && bTexCopy)
			{
				if (OriginMat->GetTextureParameterValue(VfxMaterialParams::BaseTexture, DiffuseTex))
				{
					NewMID = UMaterialInstanceDynamic::Create(ActiveOverrideMaterial, Outer);
					NewMID->SetTextureParameterValue(VfxMaterialParams::BaseTexture, DiffuseTex);

					if (OriginMat->GetTextureParameterValue(VfxMaterialParams::NormalTexture, NormalTex))
					{
						NewMID->SetTextureParameterValue(VfxMaterialParams::NormalTexture, NormalTex);
					}
					if (OriginMat->GetTextureParameterValue(VfxMaterialParams::RMESTexture, RMESTex))
					{
						NewMID->SetTextureParameterValue(VfxMaterialParams::RMESTexture, RMESTex);
					}
				}
			}
			else
			{
				NewMID = UMaterialInstanceDynamic::Create(ActiveOverrideMaterial, Outer);
				if (bTexCopy)
				{
					if (OriginMat->GetTextureParameterValue(VfxMaterialParams::BaseTexture, DiffuseTex))
					{
						NewMID->SetTextureParameterValue(VfxMaterialParams::BaseTexture, DiffuseTex);
					}
					if (OriginMat->GetTextureParameterValue(VfxMaterialParams::NormalTexture, NormalTex))
					{
						NewMID->SetTextureParameterValue(VfxMaterialParams::NormalTexture, NormalTex);
					}
					if (OriginMat->GetTextureParameterValue(VfxMaterialParams::RMESTexture, RMESTex))
					{
						NewMID->SetTextureParameterValue(VfxMaterialParams::RMESTexture, RMESTex);
					}
				}
			}

			return NewMID;
		}
	};

	const bool bHasMaterialSlotFilter = MaterialSlotNames.Num() != 0;

	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		CurrComponent.CurrentMIDs.Reset();

		if (::IsValid(CurrComponent.Component) == false)
		{
			continue;
		}

		if (CurrComponent.OriginalMaterials.Num() == 0)
		{
			continue;
		}

		if (bHasMaterialSlotFilter)
		{
			const int32 MatNum = CurrComponent.OriginalMaterials.Num();
			for (int32 MatIdx = 0; MatIdx < MatNum; ++MatIdx)
			{
				const FName& MaterialSlotName = CurrComponent.MaterialSlotNames[MatIdx];
				const bool bMatch = MaterialSlotNames.Contains(MaterialSlotName);
				if (bMatch)
				{
					UMaterialInterface* OriginMat = CurrComponent.OriginalMaterials[MatIdx];
					if (OriginMat != nullptr)
					{
						UMaterialInstanceDynamic* NewMID = FLocalUtils::CreateMID(
							ActiveOverrideMaterial, OriginMat, CurrComponent.Component,
							InNewParam, bTexCopy
						);

						if (NewMID != nullptr)
						{
							CurrComponent.Component->SetMaterial(MatIdx, NewMID);
							CurrComponent.CurrentMIDs.Add(NewMID);
						}
						else
						{
							CurrComponent.CurrentMIDs.Add(nullptr);
						}
					}
					else
					{
						CurrComponent.CurrentMIDs.Add(nullptr);
					}
				}
				else
				{
					CurrComponent.CurrentMIDs.Add(nullptr);
				}
			}

			continue;
		}

		FVfxMaterialEffectManagerOverridedMaterialCache* CachedMIDs = CurrComponent.CachedOverridedMIDs.Find(ActiveOverrideMaterial);
		if (CachedMIDs)
		{
			const int32 MatCount = FMath::Min(CurrComponent.Component->GetNumMaterials(), CachedMIDs->MIDs.Num());
			for (int32 MatIdx = 0; MatIdx < MatCount; ++MatIdx)
			{
				UMaterialInstanceDynamic* CurrMID = CachedMIDs->MIDs[MatIdx];
				if (CurrMID != nullptr)
				{
					CurrComponent.Component->SetMaterial(MatIdx, CurrMID);
				}
				CurrComponent.CurrentMIDs.Add(CurrMID);
			}
		}
		else
		{
			int32 MatIdx = 0;
			for (UMaterialInterface* OriginMat : CurrComponent.OriginalMaterials)
			{
				if (OriginMat != nullptr)
				{
					UMaterialInstanceDynamic* NewMID = FLocalUtils::CreateMID(
						ActiveOverrideMaterial, OriginMat, CurrComponent.Component,
						InNewParam, bTexCopy
					);

					if (NewMID != nullptr)
					{
						CurrComponent.Component->SetMaterial(MatIdx, NewMID);
						CurrComponent.CurrentMIDs.Add(NewMID);
					}
					else
					{
						CurrComponent.CurrentMIDs.Add(nullptr);
					}
				}
				else
				{
					CurrComponent.CurrentMIDs.Add(nullptr);
				}

				++MatIdx;
			}
		}
	}

	UpdateOriginalValueAll();
}

void FVfxMaterialEffectManager::CacheOverridedMaterials ()
{
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		if (CurrComponent.CurrentMIDs.Num() != 0 && ActiveOverrideMaterial != nullptr)
		{
			if (CurrComponent.CachedOverridedMIDs.Contains(ActiveOverrideMaterial) == false)
			{			
				FVfxMaterialEffectManagerOverridedMaterialCache& Added = CurrComponent.CachedOverridedMIDs.Add(ActiveOverrideMaterial);
				Added.MIDs.Append(CurrComponent.CurrentMIDs);
			}
		}
	}
}

void FVfxMaterialEffectManager::TryChangeNextPriorityMaterial ()
{
	if (bNeedToCheckNextPriorityOverrideMaterial)
	{
		int32 CurrPriority = -1;
		FVfxMaterialParamChangeEffectPtr Candidate;
		// Last one Stand
		//for (FVfxMaterialParamChangeEffectPtr& CurrEffect : ActiveEffects)
		for (int32 Idx = ActiveEffects.Num() - 1; Idx >= 0; --Idx)
		{
			FVfxMaterialParamChangeEffectPtr& CurrEffect = ActiveEffects[Idx];
			if (CurrEffect->Params->Param.OverrideMaterial != nullptr && CurrPriority < (int32)CurrEffect->Params->Param.Priority)
			{
				CurrPriority = (int32)CurrEffect->Params->Param.Priority;
				Candidate = CurrEffect;
			}
		}

		if (Candidate.IsValid())
		{
			TryChangeMaterial(Candidate->Params, Candidate->InternalID, Candidate->Params->Param.bNeedOriginalTextures, Candidate->MaterialSlotNames);
		}
		else
		{
			ResetToOriginalMIDs();
		}

		bNeedToCheckNextPriorityOverrideMaterial = false;
	}
}

void FVfxMaterialEffectManager::RemoveEffect (FVfxMaterialParamChangeEffectPtr InEffect, const bool bSkipChangeMaterial)
{
#if ENABLE_EFFECTMANGER_LOG
	UE_LOG(LogVfxGeneral, Error, TEXT("RemoveEffect : %d, override(%d), Value(%d)"), InEffect->InternalID, InEffect->Param->Param.OverrideMaterial? 1 : 0, InEffect->Param->Param.Scalars.Num());
#endif

	if (ActiveOverrideMaterialInternalID == InEffect->InternalID)
	{
		if (bSkipChangeMaterial == false)
		{
			ResetToOriginalMIDs();
		}
		else
		{
			CacheOverridedMaterials();

			bForceUpdateAll = true;
			bNeedNextTick = true;
	
			ActiveOverrideMaterial = nullptr;
			ActiveOverrideMaterialPriority = EVfxMaterialParamPriority::Layer0;
			ActiveOverrideMaterialInternalID = -1;
		}

		bNeedToCheckNextPriorityOverrideMaterial = true;
	}

	int32 InternalID = InEffect->InternalID;

	{
		for (const FVfxMaterialParamScalar& CurrParam : InEffect->Params->Param.Scalars)
		{
			FScalarLayers* Active = ActiveScalarParams.Find(CurrParam.ParamName);
			if (Active != nullptr)
			{
				if (Active->Values[(int32)InEffect->Params->Param.Priority] != nullptr)
				{
					FScalarValueArray& CurrAr = *Active->Values[(int32)InEffect->Params->Param.Priority];
					int32 Idx = 0;
					for (FScalarValue& Curr : CurrAr)
					{
						if (Curr.InternalID == InternalID)
						{
							//CurrAr.RemoveAtSwap(Idx, 1, false);
							// Due to Laat one standing AccumRule
							CurrAr.RemoveAt(Idx, 1, EAllowShrinking::No);
							Active->bNeedPush = true;
							bNeedNextTick = true;
							break;
						}
						++Idx;
					}

					if (CurrAr.Num() == 0)
					{
						Active->Values[(int32)InEffect->Params->Param.Priority] = nullptr;
						Release(&CurrAr);
						Active->PriorityArrayMask &= ~(1 << (int32)InEffect->Params->Param.Priority);

						if (Active->PriorityArrayMask == 0)
						{
							Active->bIsNew = true;
						}
					}
				}
			}
		}
		
		for (const FVfxMaterialParamColor& CurrParam : InEffect->Params->Param.Colors)
		{
			FColorLayers* Active = ActiveColorParams.Find(CurrParam.ParamName);
			if (Active != nullptr)
			{
				if (Active->Values[(int32)InEffect->Params->Param.Priority] != nullptr)
				{
					FColorValueArray& CurrAr = *Active->Values[(int32)InEffect->Params->Param.Priority];
					int32 Idx = 0;
					for (FColorValue& Curr : CurrAr)
					{
						if (Curr.InternalID == InternalID)
						{
							//CurrAr.RemoveAtSwap(Idx, 1, false);
							// Due to Laat one standing AccumRule
							CurrAr.RemoveAt(Idx, 1, EAllowShrinking::No);
							Active->bNeedPush = true;
							bNeedNextTick = true;
							break;
						}
						++Idx;
					}

					if (CurrAr.Num() == 0)
					{
						Active->Values[(int32)InEffect->Params->Param.Priority] = nullptr;
						Release(&CurrAr);
						Active->PriorityArrayMask &= ~(1 << (int32)InEffect->Params->Param.Priority);

						if (Active->PriorityArrayMask == 0)
						{
							Active->bIsNew = true;
						}
					}
				}	
			}
		}
		
		for (const FVfxMaterialParamTexture& CurrParam : InEffect->Params->Param.Textures)
		{
			FTextureLayers* Active = ActiveTextureParams.Find(CurrParam.ParamName);
			if (Active != nullptr)
			{
				if (Active->Values[(int32)InEffect->Params->Param.Priority] != nullptr)
				{
					FTextureValueArray& CurrAr = *Active->Values[(int32)InEffect->Params->Param.Priority];
					int32 Idx = 0;
					for (FTextureValue& Curr : CurrAr)
					{
						if (Curr.InternalID == InternalID)
						{
							//CurrAr.RemoveAtSwap(Idx, 1, false);
							// Due to Laat one standing AccumRule
							CurrAr.RemoveAt(Idx, 1, EAllowShrinking::No);
							Active->bNeedPush = true;
							bNeedNextTick = true;
							break;
						}
						++Idx;
					}

					if (CurrAr.Num() == 0)
					{
						Active->Values[(int32)InEffect->Params->Param.Priority] = nullptr;
						Release(&CurrAr);
						Active->PriorityArrayMask &= ~(1 << (int32)InEffect->Params->Param.Priority);

						if (Active->PriorityArrayMask == 0)
						{
							Active->bIsNew = true;
						}
					}
				}	
			}
		}
	}

	bNeedNextTick = true;
	ActiveEffects.Remove(InEffect);
}

void FVfxMaterialEffectManager::UpdateOriginalValueAll ()
{
	for (TPair<FName, FScalarLayers>& CurrParam : ActiveScalarParams)
	{
		UpdateOriginalValue(CurrParam.Key, CurrParam.Value);
	}

	for (TPair<FName, FColorLayers>& CurrParam : ActiveColorParams)
	{
		UpdateOriginalValue(CurrParam.Key, CurrParam.Value);
	}

	for (TPair<FName, FTextureLayers>& CurrParam : ActiveTextureParams)
	{
		UpdateOriginalValue(CurrParam.Key, CurrParam.Value);
	}
}

void FVfxMaterialEffectManager::UpdateOriginalValue (const FName& InName, FScalarLayers& InDest)
{
	float OriginalValue = InDest.bIsNew? 0.f : InDest.LastValue;
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		for (UMaterialInstanceDynamic* CurrMID : CurrComponent.CurrentMIDs)
		{
			if (CurrMID == nullptr)
			{
				continue;
			}
			if (CurrMID->Parent->GetScalarParameterValue(InName, OriginalValue))
			{
				break;
			}
		}
	}

	InDest.CachedOrigialValue = OriginalValue;
	if (InDest.bIsNew)
	{
		InDest.LastValue = OriginalValue;
		InDest.bIsNew = false;
	}
}

void FVfxMaterialEffectManager::UpdateOriginalValue (const FName& InName, FColorLayers& InDest)
{
	FLinearColor OriginalValue = InDest.bIsNew? FLinearColor::Gray : InDest.LastValue;
	
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		for (UMaterialInstanceDynamic* CurrMID : CurrComponent.CurrentMIDs)
		{
			if (CurrMID == nullptr)
			{
				continue;
			}
			if (CurrMID->Parent->GetVectorParameterValue(InName, OriginalValue))
			{
				break;
			}
		}
	}
	
	InDest.CachedOrigialValue = OriginalValue;
	if (InDest.bIsNew)
	{
		InDest.LastValue = OriginalValue;
		InDest.bIsNew = false;
	}
}

void FVfxMaterialEffectManager::UpdateOriginalValue (const FName& InName, FTextureLayers& InDest)
{
	UTexture* OriginalValue[FTextureValue::TEX_ARRAY_MAX];
	if (InDest.bIsNew)
	{
		FMemory::Memzero(OriginalValue);
	}
	else
	{
		FMemory::Memcpy(OriginalValue, InDest.LastValues, sizeof(UTexture*) * FTextureValue::TEX_ARRAY_MAX);
	}
	
	for (FVfxMaterialEffectManagerPrimitiveComponentInfo& CurrComponent : Components)
	{
		check(CurrComponent.CurrentMIDs.Num() <= FTextureValue::TEX_ARRAY_MAX);
		for (int32 MatIdx = 0; MatIdx < CurrComponent.CurrentMIDs.Num(); ++MatIdx)
		{
			UMaterialInstanceDynamic* CurrMID = CurrComponent.CurrentMIDs[MatIdx];
			if (CurrMID == nullptr)
			{
				continue;
			}
			if (CurrMID->Parent->GetTextureParameterValue(InName, OriginalValue[MatIdx]) == false)
			{
				OriginalValue[MatIdx] = nullptr;
			}
		}
	}
	
	FMemory::Memcpy(InDest.CachedOrigialValues, OriginalValue, sizeof(UTexture*) * FTextureValue::TEX_ARRAY_MAX);
	if (InDest.bIsNew)
	{
		FMemory::Memcpy(InDest.LastValues, OriginalValue, sizeof(UTexture*) * FTextureValue::TEX_ARRAY_MAX);
		InDest.bIsNew = false;
	}
}

void FVfxMaterialEffectManager::KillEffectByInternalID (int32 InInternalId)
{
	if (InInternalId == -1)
	{
		return;
	}

	// TODO : maybe MAP required, in case of multiple Material effect coexist
	for (FVfxMaterialParamChangeEffectPtr& CurrEffect : ActiveEffects)
	{
		if (CurrEffect->InternalID == InInternalId)
		{
			RemoveEffect(CurrEffect, false);
			return;
		}
	}

	for (int32 Idx = 0; Idx < PendingEffects.Num(); ++Idx)
	{
		FVfxMaterialEffectManagerPendingEffect& Curr = PendingEffects[Idx];
		if (Curr.InternalID == InInternalId)
		{
			PendingEffects.RemoveAt(Idx, 1, EAllowShrinking::No);
			return;
		}
	}
}

void FVfxMaterialEffectManager::KillEffectByID (uint32 InId, const bool bAll)
{
	// TODO : maybe MAP required, in case of multiple Material effect coexist
	for (int32 Idx = ActiveEffects.Num() - 1; Idx >= 0; --Idx)
	{
		FVfxMaterialParamChangeEffectPtr& CurrEffect = ActiveEffects[Idx];
		if (CurrEffect->ID == InId)
		{
			RemoveEffect(CurrEffect, false);
			if (bAll == false)
			{
				return;
			}
		}
	}

	for (int32 Idx = 0; Idx < PendingEffects.Num(); ++Idx)
	{
		FVfxMaterialEffectManagerPendingEffect& Curr = PendingEffects[Idx];
		if (Curr.EffectID == InId)
		{
			PendingEffects.RemoveAt(Idx, 1, EAllowShrinking::No);
			--Idx;
			if (bAll == false)
			{
				return;
			}
		}
	}
}

void FVfxMaterialEffectManager::KillEffectByLayer (EVfxMaterialParamPriority InLayer, const bool bAll)
{
	for (int32 Idx = ActiveEffects.Num() - 1; Idx >= 0; --Idx)
	{
		FVfxMaterialParamChangeEffectPtr& CurrEffect = ActiveEffects[Idx];
		if (CurrEffect->Params->Param.Priority == InLayer)
		{
			RemoveEffect(CurrEffect, false);
			if (bAll == false)
			{
				return;
			}
		}
	}

	for (int32 Idx = 0; Idx < PendingEffects.Num(); ++Idx)
	{
		FVfxMaterialEffectManagerPendingEffect& Curr = PendingEffects[Idx];
		if (Curr.Params->Param.Priority == InLayer)
		{
			PendingEffects.RemoveAt(Idx, 1, EAllowShrinking::No);
			--Idx;
			if (bAll == false)
			{
				return;
			}
		}
	}
}

void FVfxMaterialEffectManager::KillAllEffects ()
{
	PendingEffects.Reset();

	for (int32 Idx = ActiveEffects.Num() - 1; Idx >= 0; --Idx)
	{
		FVfxMaterialParamChangeEffectPtr& CurrEffect = ActiveEffects[Idx];
		RemoveEffect(CurrEffect, true);
	}

	TryChangeNextPriorityMaterial();

	bForceUpdateAll = true;
	bNeedNextTick = true;
}

bool FVfxMaterialEffectManager::HasEffect (int32 InInternalId)
{
	if (InInternalId == -1)
	{
		return false;
	}

	// TODO : maybe MAP required, in case of multiple Material effect coexist
	for (FVfxMaterialParamChangeEffectPtr& CurrEffect : ActiveEffects)
	{
		if (CurrEffect->InternalID == InInternalId)
		{
			return true;
		}
	}

	for (FVfxMaterialEffectManagerPendingEffect& CurrPending : PendingEffects)
	{
		if (CurrPending.InternalID == InInternalId)
		{
			return true;
		}
	}

	return false;
}

FVfxMaterialEffectManager::FScalarValueArray* FVfxMaterialEffectManager::AllocScalarArray ()
{
	if (sScalarArrayPool.Num())
	{
		return sScalarArrayPool.Pop();
	}

	return new FScalarValueArray;
}

void FVfxMaterialEffectManager::Release (FScalarValueArray* InPtr)
{
	InPtr->Reset();
	sScalarArrayPool.Add(InPtr);
}

FVfxMaterialEffectManager::FColorValueArray* FVfxMaterialEffectManager::AllocColorArray ()
{
	if (sColorArrayPool.Num())
	{
		return sColorArrayPool.Pop();
	}

	return new FColorValueArray;
}

void FVfxMaterialEffectManager::Release (FColorValueArray* InPtr)
{
	InPtr->Reset();
	sColorArrayPool.Add(InPtr);
}

FVfxMaterialEffectManager::FTextureValueArray* FVfxMaterialEffectManager::AllocTextureArray ()
{
	if (sTextureArrayPool.Num())
	{
		return sTextureArrayPool.Pop();
	}

	return new FTextureValueArray;
}

void FVfxMaterialEffectManager::Release (FTextureValueArray* InPtr)
{
	InPtr->Reset();
	sTextureArrayPool.Add(InPtr);
}
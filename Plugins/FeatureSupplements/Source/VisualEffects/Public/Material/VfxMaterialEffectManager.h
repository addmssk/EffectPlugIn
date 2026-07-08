#pragma once
#include "CoreMinimal.h"
#include "Material/VfxMaterialParams.h"
#include "VfxMaterialEffectManager.generated.h"

class UMeshComponent;
class UTexture;
class UVfxMaterialParamsData;

namespace VfxMaterialParams
{
	static const FName BaseTexture = TEXT("BaseColor_Tex");
	static const FName NormalTexture = TEXT("Normal_Tex");
	static const FName RMESTexture = TEXT("Mask_RMES");

	static const FName Opacity_Internal = TEXT("Opacity_Int");
};

typedef TSharedPtr<struct FVfxMaterialParamChangeEffect> FVfxMaterialParamChangeEffectPtr;


USTRUCT()
struct FVfxMaterialEffectManagerOverridedMaterialCache
{
	GENERATED_BODY();

	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> MIDs;
};

USTRUCT()
struct FVfxMaterialEffectManagerPendingEffect
{
	GENERATED_BODY();

	UPROPERTY(Transient)
	const UVfxMaterialParamsData* Params;

	uint32 EffectID;
	bool bOverrideTime;
	float LifeTime;
	float FadeInTime;
	float FadeOutTime;
	bool bLoop;

	int32 InternalID;
	float ElapsedTime;

	TArray<FName, TInlineAllocator<4>> MaterialSlotNames;

	FVfxMaterialEffectManagerPendingEffect()
		: Params(nullptr)
		, EffectID(0)
		, bOverrideTime(false)
		, LifeTime(1.f)
		, FadeInTime(0.2f)
		, FadeOutTime(0.2f)
		, bLoop(1.f)
		, InternalID(-1)
		, ElapsedTime(0.f)
	{
	}
};

USTRUCT()
struct FVfxMaterialEffectManagerPrimitiveComponentInfo
{
	GENERATED_BODY();

	UPROPERTY(Transient)
	UPrimitiveComponent* Component;

	UPROPERTY(Transient)
	TArray<UMaterialInterface*> OriginalMaterials;
	
	UPROPERTY(Transient)
	TArray<FName> MaterialSlotNames;

	UPROPERTY(VisibleAnywhere, Transient)
	TArray<UMaterialInstanceDynamic*> CurrentMIDs;
	
	UPROPERTY(VisibleAnywhere, Transient)
	TMap<UObject*, FVfxMaterialEffectManagerOverridedMaterialCache> CachedOverridedMIDs;

	FVfxMaterialEffectManagerPrimitiveComponentInfo ()
		: Component(nullptr)
	{
	}
	FVfxMaterialEffectManagerPrimitiveComponentInfo (UPrimitiveComponent* InComponent)
		: Component(InComponent)
	{
	}

	inline bool operator== (const FVfxMaterialEffectManagerPrimitiveComponentInfo& Rhs) const
	{
		return Component == Rhs.Component;
	}
	inline bool operator!= (const FVfxMaterialEffectManagerPrimitiveComponentInfo& Rhs) const
	{
		return Component != Rhs.Component;
	}
	inline friend uint32 GetTypeHash (const FVfxMaterialEffectManagerPrimitiveComponentInfo& InValue)
	{
		return GetTypeHash(InValue.Component);
	}
};


USTRUCT()
struct VISUALEFFECTS_API FVfxMaterialEffectManager
{
	 GENERATED_BODY();

public:
	FVfxMaterialEffectManager (bool OwnerIsSnapshot = false);
	~FVfxMaterialEffectManager ();

	const bool IsInitialized () const { return bInited; }
	void Initialize (UObject* InOwner, UPrimitiveComponent* InTargetComponent);
	void AddComponent (UPrimitiveComponent* InComponent);
	void RemoveComponent (UPrimitiveComponent* InComponent);
	const bool HasComponent(USceneComponent* InComponent);
	void Uninitialize (const bool bCalledByDestructor = false);
	void Tick (const float DeltaSeconds);

	int32 CreateEffect (
		const UVfxMaterialParamsData* InParam,
		uint32 ID,
		bool bOverrideTime,
		float Time,
		float FadeInTime,
		float FadeOutTime,
		bool bLoop,
		const TArray<FName>& MaterialSlotNames
	); // Return InternalID. -1 means failed;

	void KillEffectByInternalID (int32 InInternalId);
	void KillEffectByID (uint32 InId, const bool bAll);
	void KillEffectByLayer (EVfxMaterialParamPriority InLayer, const bool bAll);
	void KillAllEffects ();

	bool HasEffect (int32 InInternalId);

	void IdentifySnapshotFX () { bIsSnapshot = true; }

	UPrimitiveComponent* GetFirstTargetComponent () const { return Components.Num()? Components.begin()->Component : nullptr; }

	static bool RemoveAllMaterialInstanceDynamic (UMeshComponent* InComponent, bool bDoRenderDirty); // return true when removed
	
private:
	void ResetToOrigin ();
	void ClearComponentInfo (FVfxMaterialEffectManagerPrimitiveComponentInfo* InInfo);
	inline void ResetToOriginalMID (FVfxMaterialEffectManagerPrimitiveComponentInfo* InInfo);

	friend struct FVfxMaterialParamChangeEffect;
	enum class EActiveEffectInternalState : uint8
	{
		None = 0,
		FadeIn,
		FadeOut,
		During,
	};

	UPROPERTY(VisibleAnywhere, Transient)
	TSet<FVfxMaterialEffectManagerPrimitiveComponentInfo> Components;
	
	TArray<FVfxMaterialParamChangeEffectPtr> ActiveEffects;

	bool bInited;
	bool bForceUpdateAll;
	bool bNeedNextTick;
	bool bNeedToCheckNextPriorityOverrideMaterial;

	bool bIsSnapshot;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, meta=(MultiLine="true"))
	FString DebugInfo;
#endif

	UPROPERTY(VisibleAnywhere, Transient)
	int32 ActiveOverrideMaterialInternalID;
	
	UPROPERTY(VisibleAnywhere, Transient)
	EVfxMaterialParamPriority ActiveOverrideMaterialPriority;
	
	UPROPERTY(VisibleAnywhere, Transient)
	UMaterialInterface* ActiveOverrideMaterial;

	UPROPERTY(VisibleAnywhere, Transient)
	int32 InternalIdSource;

	struct FValue
	{
		uint32 GivenID;
		int32 InternalID;

		FVfxMaterialParamChangeEffectPtr Effect;

		FValue ()
			: GivenID(-1)
			, InternalID(-1)
		{
		}
	};

	struct FScalarValue : public FValue
	{
		float BlendStartValue;
		float BlendEndValue;
		float FinalValue;
		const FRuntimeFloatCurve* CurveRef;
		bool bPlayCurveOnce;
		bool bNormalizedParamterization;
		float CurveMaxRange;

		FScalarValue ()
			: FValue()
			, BlendStartValue(0.f)
			, BlendEndValue(0.f)
			, FinalValue(0.f)
			, CurveRef(nullptr)
			, bPlayCurveOnce(false)
			, bNormalizedParamterization(true)
			, CurveMaxRange(1.f)
		{

		}
	};

	struct FColorValue : public FValue
	{
		FLinearColor BlendStartValue;
		FLinearColor BlendEndValue;
		FLinearColor FinalValue;
		const FRuntimeCurveLinearColor* CurveRef;
		bool bPlayCurveOnce;
		bool bNormalizedParamterization;
		float CurveMaxRange;

		FColorValue ()
			: FValue()
			, BlendStartValue(FLinearColor::Black)
			, BlendEndValue(FLinearColor::Black)
			, FinalValue(FLinearColor::Black)
			, CurveRef(nullptr)
			, bPlayCurveOnce(false)
			, bNormalizedParamterization(true)
			, CurveMaxRange(1.f)
		{
		}
	};

	struct FTextureValue : public FValue
	{
		enum
		{
			TEX_ARRAY_MAX = 6
		};

		UTexture* Values[TEX_ARRAY_MAX];

		FTextureValue ()
			: FValue()
		{
			FMemory::Memzero(Values);
		}
	};

	typedef TArray<FScalarValue, TInlineAllocator<5>> FScalarValueArray;
	typedef TArray<FColorValue, TInlineAllocator<5>> FColorValueArray;
	typedef TArray<FTextureValue, TInlineAllocator<5>> FTextureValueArray;

	struct FLayerCommon
	{
		FLayerCommon ()
			: PriorityArrayMask(0)
			, bNeedPush(false)
			, bIsNew(true)
		{
		}
		
		uint16 PriorityArrayMask;
		bool bNeedPush;
		bool bIsNew;
	};

	struct FScalarLayers : public FLayerCommon
	{
		FScalarLayers () 
			: FLayerCommon()
			, CachedOrigialValue(-12543)
			, LastValue(-12543)
		{ FMemory::Memzero(Values); }

		float CachedOrigialValue;
		float LastValue;
		
		FScalarValueArray* Values[(int32)EVfxMaterialParamPriority::Count];
	};
	struct FColorLayers : public FLayerCommon
	{
		FColorLayers ()
			: FLayerCommon()
			, CachedOrigialValue(-1,-1,-1)
			, LastValue(-1,-1,-1)
		{ FMemory::Memzero(Values); }

		FLinearColor CachedOrigialValue;
		FLinearColor LastValue;
		FColorValueArray* Values[(int32)EVfxMaterialParamPriority::Count];
	};
	struct FTextureLayers : public FLayerCommon
	{
		FTextureLayers ()
			: FLayerCommon()
		{ 
			FMemory::Memzero(CachedOrigialValues);
			FMemory::Memzero(LastValues);
			FMemory::Memzero(Values);
		}

		UTexture* CachedOrigialValues[FTextureValue::TEX_ARRAY_MAX];  // TODO : WeakPtr?
		UTexture* LastValues[FTextureValue::TEX_ARRAY_MAX];           // TODO : WeakPtr?
		FTextureValueArray* Values[(int32)EVfxMaterialParamPriority::Count];
	};

	TMap<FName, FScalarLayers> ActiveScalarParams;
	TMap<FName, FColorLayers> ActiveColorParams;
	TMap<FName, FTextureLayers> ActiveTextureParams;

	TMap<TTuple<TSoftObjectPtr<UTexture>, int32>,TSharedPtr<struct FStreamableHandle>> LoadHandleMap;

	UPROPERTY(Transient)
	TArray<UTexture*> TextureReferenceHolder;

	TWeakObjectPtr<UObject> OwnerWeakPtr;

	UPROPERTY(Transient)
	TArray<FVfxMaterialEffectManagerPendingEffect> PendingEffects;
	
private:
	bool CreateEffectInternal (
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
	);
	void AddToPendingEffect (
		int32 NewInternalID,
		const UVfxMaterialParamsData* InParam,
		uint32 EffectID,
		bool bOverrideTime,
		float Time,
		float FadeInTime,
		float FadeOutTime,
		bool bLoop,
		const TArrayView<FName>& MaterialSlotNames
	);
	void UpdateActiveParams (FVfxMaterialParamChangeEffectPtr& InEffect, const UVfxMaterialParamsData* InParam);
	void TryChangeMaterial (
		const UVfxMaterialParamsData* InNewParam,
		int32 InInternalID,
		bool bTexCopy,
		const TArrayView<FName>& MaterialSlotNames
	);
	void ResetToOriginalMIDs ();
	void CacheOverridedMaterials ();
	void TryChangeNextPriorityMaterial ();

	void RemoveEffect (FVfxMaterialParamChangeEffectPtr InEffect, const bool bSkipChangeMaterial);

#if WITH_EDITOR
	void UpdateDebugInfo (bool bSkipTicking);
#endif

	void UpdateOriginalValueAll ();
	inline void UpdateOriginalValue (const FName& InName, FScalarLayers& InDest);
	inline void UpdateOriginalValue (const FName& InName, FColorLayers& InDest);
	inline void UpdateOriginalValue (const FName& InName, FTextureLayers& InDest);

	inline float AccumValue (FScalarLayers& InLayers, FScalarValueArray& InArray);
	inline FLinearColor AccumValue (FColorLayers& InLayers, FColorValueArray& InArray);
	inline UTexture** AccumValue (FTextureLayers& InLayers, FTextureValueArray& InArray);
	
private:
	static FScalarValueArray* AllocScalarArray ();
	static void Release (FScalarValueArray* InPtr);
	static FColorValueArray* AllocColorArray ();
	static void Release (FColorValueArray* InPtr);
	static FTextureValueArray* AllocTextureArray ();
	static void Release (FTextureValueArray* InPtr);

	static TArray<FScalarValueArray*> sScalarArrayPool;
	static TArray<FColorValueArray*> sColorArrayPool;
	static TArray<FTextureValueArray*> sTextureArrayPool;
};
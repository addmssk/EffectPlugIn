#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "VfxMaterialParams.generated.h"

class UParticleSystem;
class UMaterialInstance;
class UNiagaraSystem;
class UTexture;

UENUM()
enum class EVfxMaterialParamPriority : uint8
{
	Layer0,
	Layer1,
	Layer2,
	Layer3,
	Layer4,
	
	System,

	Count UMETA(hidden)
};

USTRUCT()
struct FVfxMaterialParamScalar
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere)
	FName ParamName;

	UPROPERTY(EditAnywhere)
	bool bUseCurve;
	
	UPROPERTY(EditAnywhere, meta=(EditCondition="false==bUseCurve"))
	float Value;

	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	bool bPlayCurveOnce;
	
	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	bool bUseNormalizedTime;

	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	FRuntimeFloatCurve Curve;

	FVfxMaterialParamScalar ()
		: ParamName(NAME_None)
		, bUseCurve(false)
		, Value(0.f)
		, bPlayCurveOnce(false)
		, bUseNormalizedTime(true)
	{
	}
};

USTRUCT()
struct FVfxMaterialParamColor
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere)
	FName ParamName;

	UPROPERTY(EditAnywhere)
	bool bUseCurve;

	UPROPERTY(EditAnywhere, meta=(EditCondition="false==bUseCurve"))
	FLinearColor Value;
	
	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	bool bPlayCurveOnce;
	
	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	bool bUseNormalizedTime;

	UPROPERTY(EditAnywhere, meta=(EditCondition="bUseCurve"))
	FRuntimeCurveLinearColor Curve;

	FVfxMaterialParamColor ()
		: ParamName(NAME_None)
		, bUseCurve(false)
		, Value(FLinearColor::White)
		, bPlayCurveOnce(false)
		, bUseNormalizedTime(true)
	{
	}
};

USTRUCT()
struct FVfxMaterialParamTexture
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere)
	FName ParamName;
	
	UPROPERTY(EditAnywhere)
	UTexture* Value;

	UPROPERTY(Transient)
	int32 TargetMaterialIndex;

	FVfxMaterialParamTexture ()
		: Value(nullptr)
		, TargetMaterialIndex(0)
	{
	}
};

USTRUCT()
struct FVfxMaterialParams
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, meta=(EditCondition="bIsLoop==false", UIMin="0.01", ClampMin="0.01"))
	float LifeTime;
	
	UPROPERTY(EditAnywhere, meta=(UIMin="0.0", ClampMin="0.0"))
	float FadeInTime;

	UPROPERTY(EditAnywhere, meta=(UIMin="0.0", ClampMin="0.0"))
	float FadeOutTime;

	UPROPERTY(EditAnywhere)
	bool bIsLoop;

	UPROPERTY(EditAnywhere)
	EVfxMaterialParamPriority Priority;
	
	/** Caution : Expensive. This will ignore Component's Tick rate.  */
	UPROPERTY(EditAnywhere)
	bool bForceApplyImmediately;

	UPROPERTY(EditAnywhere)
	class UMaterialInterface* OverrideMaterial;

	/** Copy original material's textures. vaild texture names are 'BaseColor_Tex', 'Normal_Tex', 'Mask_RMES'. */
	UPROPERTY(EditAnywhere, meta=(EditCondition="OverrideMaterial!=nullptr"))
	bool bNeedOriginalTextures;

	/** Skip creating snapshot component if original component is hidden */
	UPROPERTY(EditAnywhere, meta=(EditCondition="bNeedOriginalTextures"))
	bool bSkipIfBaseColorTexNotExist;

	/** Duplicate Component and Overdraw over original mesh. (add draw call) */
	UPROPERTY(EditAnywhere, Category=Snapshot)
	bool bIsSnapshot;

	/** Skip creating snapshot component if original component is hidden */
	UPROPERTY(EditAnywhere, Category=Snapshot, meta=(EditCondition="bIsSnapshot"))
	bool bDisableSnapshotIfSourceIsHidden;

	/** Duplicate Component will maintain copyed pose and keep owner's world position at that time, if this option off and */
	UPROPERTY(EditAnywhere, Category=Snapshot, meta=(EditCondition="bIsSnapshot"))
	bool bSyncAnimationAndPosition;

	/** Provide three vector params. 'HitLocation', 'HitNormal', 'HitUV_V' to materials in snapshot component */
	UPROPERTY(EditAnywhere, Category=Snapshot, meta=(EditCondition="bIsSnapshot"))
	bool bNeedHitTransformMaterialParams;

	/**     */
	UPROPERTY(EditAnywhere, Category=Snapshot, meta=(EditCondition="bIsSnapshot && bNeedHitTransformMaterialParams"))
	bool bInverseHitNormal;
	
	UPROPERTY(EditAnywhere)
	TArray<FVfxMaterialParamScalar> Scalars;
	
	UPROPERTY(EditAnywhere)
	TArray<FVfxMaterialParamColor> Colors;
	
	UPROPERTY(EditAnywhere)
	TArray<FVfxMaterialParamTexture> Textures;

	inline bool IsEmpty () const
	{
		return OverrideMaterial == nullptr && Scalars.Num() == 0 && Colors.Num() == 0 && Textures.Num() == 0;
	}

	void Reset();

	FVfxMaterialParams ()
		: LifeTime(1.f)
		, FadeInTime(0.f)
		, FadeOutTime(0.f)
		, bIsLoop(false)
		, Priority(EVfxMaterialParamPriority::Layer0)
		, bForceApplyImmediately(false)
		, OverrideMaterial(nullptr)
		, bNeedOriginalTextures(true)
		, bSkipIfBaseColorTexNotExist(false)
		, bIsSnapshot(false)
		, bDisableSnapshotIfSourceIsHidden(true)
		, bSyncAnimationAndPosition(true)
		, bNeedHitTransformMaterialParams(false)
		, bInverseHitNormal(false)
	{
	}
};


UCLASS(BlueprintType)
class VISUALEFFECTS_API UVfxMaterialParamsData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FVfxMaterialParams Param;
};
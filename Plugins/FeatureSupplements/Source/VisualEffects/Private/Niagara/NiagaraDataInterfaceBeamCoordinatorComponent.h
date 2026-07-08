// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceBeamCoordinatorComponent.generated.h"

#define UE_API VISUALEFFECTS_API

class UVfxBeamCoordinatorComponent;

UENUM()
enum class ENDIBeamCoordinatorComponentSourceMode : uint8
{
	/**
	The default binding mode first we look for a valid binding on the ActorOrComponentParameter.
	- Use "Source" when specified (either set explicitly or via blueprint with Set Niagara Skeletal Mesh Component).
	If this it no valid we then look at the SourceActor.
	If these both fail we are bound to nothing.
	*/
	Default,

	/**	Only use "Source" (either set explicitly or via blueprint with Set Niagara Skeletal Mesh Component). */
	Source,

	/**
	We will first look at the attach parent.
	If this is not valid we fallback to the Default binding mode.
	*/
	AttachParent
};


/**************************************************************************************************
*
*   UNiagaraDataInterfaceBeamCoordinatorComponent
* 
*   See : UNiagaraDataInterfaceActorComponent, UNiagaraDataInterfaceSkeletalMesh
*
***/

UCLASS(EditInlineNew, Category = "VisualEffects", CollapseCategories, meta = (DisplayName = "BeamCoordinator Component Interface"), MinimalAPI)
class UNiagaraDataInterfaceBeamCoordinatorComponent : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(uint32,		Valid)
		SHADER_PARAMETER(float,			Progress)
		SHADER_PARAMETER(float,			ElapsedSecs)
		SHADER_PARAMETER(float,			Duration)
		SHADER_PARAMETER(float,			TargetWidth)
		SHADER_PARAMETER(FVector3f,		TargetLocation)
		SHADER_PARAMETER(FVector3f,		TargetMoveDirection)
	END_SHADER_PARAMETER_STRUCT();

public:
	/** Controls how we find the actor / component we want to bind to. */
	UPROPERTY(EditAnywhere, Category = "ActorComponent")
	ENDIBeamCoordinatorComponentSourceMode SourceMode = ENDIBeamCoordinatorComponentSourceMode::Default;

	/** WorkingPos Query Axis */
	UPROPERTY(EditAnywhere, Category = "ActorComponent")
	TEnumAsByte<EAxis::Type> Axis;

	/** User parameter binding to use, overrides SourceActor.  Can be set by Blueprint, etc. */
	UPROPERTY(EditAnywhere, Category = "ActorComponent")
	FNiagaraUserParameterBinding ActorOrComponentParameter;

	/** When this option is disabled, we use the previous frame's data for the skeletal mesh and can often issue the simulation early. This greatly
	reduces overhead and allows the game thread to run faster, but comes at a tradeoff if the dependencies might leave gaps or other visual artifacts.*/
	UPROPERTY(EditAnywhere, Category = "Performance")
	bool bRequireCurrentFrameData = true;
	
	//UObject Interface
	UE_API virtual void PostInitProperties() override;
	//UObject Interface End

	UE_API class UActorComponent* ResolveComponent(FNiagaraSystemInstance* SystemInstance, const void* PerInstanceData) const;
	
	UE_API void SetSourceComponentFromBlueprints(UVfxBeamCoordinatorComponent* ComponentToUse, int32 InObjectID);

	//UNiagaraDataInterface Interface
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }

	UE_API virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
#if WITH_EDITORONLY_DATA
	UE_API virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	UE_API virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	UE_API virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	UE_API virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif
	UE_API virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	UE_API virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	UE_API virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	UE_API virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	UE_API virtual int32 PerInstanceDataSize() const override;
	UE_API virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;

	UE_API virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	virtual bool HasTickGroupPrereqs() const override { return true; }
	UE_API virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override;

	UE_API virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	UE_API virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	virtual bool HasPreSimulateTick() const override { return true; }
	//UNiagaraDataInterface Interface

protected:
#if WITH_EDITORONLY_DATA
	UE_API virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	
	// Bind/unbind delegates to release references to the source actor & component.
	UE_API void UnbindSourceDelegates();
	UE_API void BindSourceDelegates();

	UFUNCTION()
	UE_API void OnSourceEndPlay(AActor* InSource, EEndPlayReason::Type Reason);

	/** The source component from which to sample. Takes precedence over the direct mesh. Not exposed to the user, only indirectly accessible from blueprints. */
	UPROPERTY(Transient)
	TObjectPtr<UVfxBeamCoordinatorComponent> SourceComponent;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SoftSourceActor;
	
	UPROPERTY(Transient)
	int32 ObjectID;

private:
	UE_API void VMGetProgress(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetElapsedSecs(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetDuration(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetTargetWidth(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetTargetLocation(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetTargetMoveDirection(FVectorVMExternalFunctionContext& Context);
	UE_API void VMGetTargetLocationAndMoveDirection(FVectorVMExternalFunctionContext& Context);
};


#undef UE_API
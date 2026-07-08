// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceBeamCoordinatorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraTypes.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraWorldManager.h"

#include "Components/SceneComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "ShaderCompilerCore.h"
#include "Misc/LargeWorldRenderPosition.h"

#include "VfxBeamCoordinatorComponent.h"
#include "VfxUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceBeamCoordinatorComponent)

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceBeamCoordinatorComponent"

struct FNiagaraBeamCoordinatorDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

namespace NDIBeamCoordinatorComponentLocal
{
	static const TCHAR* TemplateShaderFile = TEXT("/Plugin/FeatureSupplements/NiagaraDataInterfaceBeamCoordinatorComponentTemplate.ush");

	static const FName	GetProgressName(TEXT("GetProgress"));
	static const FName	GetElapsedSecsName(TEXT("GetElapsedSecs"));
	static const FName	GetDurationName(TEXT("GetDuration"));
	static const FName	GetTargetWidthName(TEXT("GetTargetWidth"));
	static const FName	GetTargetLocationName(TEXT("GetTargetLocation"));
	static const FName	GetTargetMoveDirectionName(TEXT("GetTargetMoveDirection"));
	static const FName	GetTargetLocationAndMoveDirectionName(TEXT("GetTargetLocationAndMoveDirection"));

	struct FInstanceData_GameThread
	{
		FNiagaraParameterDirectBinding<UObject*>	UserParamBinding;
		bool										bCachedValid = false;
		float										CachedProgress = 0.5f;
		float										CachedElapsedSecs = 0.5f;
		float										CachedDuration = 1.0f;
		float										CachedTargetWidth = 100.0f;
		FVector3f									CachedTargetLocation = FVector3f::ZeroVector;
		FVector3f									CachedTargetMoveDirection = FVector3f::ForwardVector;

		// our use of UserParamBinding can occur within CalculateTickGroup which occurs before we tick our parameter stores.  This
		// can lead to a stale UObject reference being accessed (if the actor we're pointing at is deleted).  For now we cache
		// the results during PerInstanceTick and re-use the result (if it remains valid) for calculating the tick group.
		TWeakObjectPtr<UActorComponent>				CachedActorForCalcTickGroup;
	};

	struct FGameToRenderInstanceData
	{
		bool		bCachedValid = false;
		float		CachedProgress = 0.5f;
		float		CachedElapsedSecs = 0.5f;
		float		CachedDuration = 1.0f;
		float		CachedTargetWidth = 100.0f;
		FVector3f	CachedTargetLocation = FVector3f::ZeroVector;
		FVector3f	CachedTargetMoveDirection = FVector3f::ForwardVector;
	};

	struct FInstanceData_RenderThread
	{
		bool		bCachedValid = false;
		float		CachedProgress = 0.5f;
		float		CachedElapsedSecs = 0.5f;
		float		CachedDuration = 1.0f;
		float		CachedTargetWidth = 100.0f;
		FVector3f	CachedTargetLocation = FVector3f::ZeroVector;
		FVector3f	CachedTargetMoveDirection = FVector3f::ForwardVector;
	};

	struct FNDIProxy : public FNiagaraDataInterfaceProxy
	{
		virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FGameToRenderInstanceData); }

		static void ProvidePerInstanceDataForRenderThread(void* InDataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
		{
			const FInstanceData_GameThread* InstanceData = reinterpret_cast<FInstanceData_GameThread*>(PerInstanceData);
			FGameToRenderInstanceData* DataForRenderThread = reinterpret_cast<FGameToRenderInstanceData*>(InDataForRenderThread);
			DataForRenderThread->bCachedValid = InstanceData->bCachedValid;
			DataForRenderThread->CachedProgress = InstanceData->CachedProgress;
			DataForRenderThread->CachedElapsedSecs = InstanceData->CachedElapsedSecs;
			DataForRenderThread->CachedDuration = InstanceData->CachedDuration;
			DataForRenderThread->CachedTargetWidth = InstanceData->CachedTargetWidth;
			DataForRenderThread->CachedTargetLocation = InstanceData->CachedTargetLocation;
			DataForRenderThread->CachedTargetMoveDirection = InstanceData->CachedTargetMoveDirection;
		}

		virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
		{
			FGameToRenderInstanceData* InstanceDataFromGT = reinterpret_cast<FGameToRenderInstanceData*>(PerInstanceData);

			FInstanceData_RenderThread& InstanceData = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);
			InstanceData.bCachedValid = InstanceDataFromGT->bCachedValid;
			InstanceData.CachedProgress = InstanceDataFromGT->CachedProgress;
			InstanceData.CachedElapsedSecs = InstanceDataFromGT->CachedElapsedSecs;
			InstanceData.CachedDuration = InstanceDataFromGT->CachedDuration;
			InstanceData.CachedTargetWidth = InstanceDataFromGT->CachedTargetWidth;
			InstanceData.CachedTargetLocation = InstanceDataFromGT->CachedTargetLocation;
			InstanceData.CachedTargetMoveDirection = InstanceDataFromGT->CachedTargetMoveDirection;
		}

		TMap<FNiagaraSystemInstanceID, FInstanceData_RenderThread> SystemInstancesToInstanceData_RT;
	};
}

//////////////////////////////////////////////////////////////////////////
// Data Interface
UNiagaraDataInterfaceBeamCoordinatorComponent::UNiagaraDataInterfaceBeamCoordinatorComponent(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new NDIBeamCoordinatorComponentLocal::FNDIProxy());

	FNiagaraTypeDefinition Def(UObject::StaticClass());
	ActorOrComponentParameter.Parameter.SetType(Def);

	Axis = EAxis::X;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

UActorComponent* UNiagaraDataInterfaceBeamCoordinatorComponent::ResolveComponent(FNiagaraSystemInstance* SystemInstance, const void* PerInstanceData) const
{
	using namespace NDIBeamCoordinatorComponentLocal;

	if (SourceMode == ENDIBeamCoordinatorComponentSourceMode::Source ||
		SourceMode == ENDIBeamCoordinatorComponentSourceMode::Default)
	{
		if (UVfxBeamCoordinatorComponent* Comp = SourceComponent.Get())
		{
			return Comp;
		}
	}

	if (SourceMode == ENDIBeamCoordinatorComponentSourceMode::AttachParent)
	{
		if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
		{
			return AttachComponent;
		}
	}
	
	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	if (UObject* ObjectBinding = InstanceData->UserParamBinding.GetValue())
	{
		if (UActorComponent* ComponentBinding = Cast<UActorComponent>(ObjectBinding))
		{
			return ComponentBinding;
		}
		else if (AActor* ActorBinding = Cast<AActor>(ObjectBinding))
		{
			return ActorBinding->GetRootComponent();
		}
	}

	return nullptr;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::SetSourceComponentFromBlueprints(
	UVfxBeamCoordinatorComponent* ComponentToUse,
	int32 InObjectID
)
{
	UnbindSourceDelegates();
	SourceComponent = ComponentToUse;
	SoftSourceActor = ComponentToUse ? ComponentToUse->GetOwner() : nullptr;
	ObjectID = InObjectID;
	BindSourceDelegates();
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceBeamCoordinatorComponent::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	using namespace NDIBeamCoordinatorComponentLocal;
	FNiagaraFunctionSignature DefaultSig;
	DefaultSig.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("BeamCoordinatorComponent"));
	DefaultSig.bMemberFunction = true;
	DefaultSig.bRequiresContext = false;
	DefaultSig.bSupportsGPU = true;
	DefaultSig.SetFunctionVersion(FNiagaraBeamCoordinatorDIFunctionVersion::LatestVersion);
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetProgressName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Progress"));
		FunctionSignature.SetDescription(LOCTEXT("GetProgress", "Returns the current Progress from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetElapsedSecsName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ElapsedSecs"));
		FunctionSignature.SetDescription(LOCTEXT("GetElapsedSecs", "Returns the ElapsedSecs from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetDurationName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Duration"));
		FunctionSignature.SetDescription(LOCTEXT("GetDuration", "Returns the Duration from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetTargetWidthName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("TargetWidth"));
		FunctionSignature.SetDescription(LOCTEXT("GetTargetWidth", "Returns current TargetWidth from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetTargetLocationName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TargetLocation"));
		FunctionSignature.SetDescription(LOCTEXT("GetTargetLocation", "Returns the TargetLocation from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetTargetMoveDirectionName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TargetMoveDirection"));
		FunctionSignature.SetDescription(LOCTEXT("GetTargetMoveDirection", "Returns the TargetMoveDirection from the BeamCoordinator if valid."));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.Add_GetRef(DefaultSig);
		FunctionSignature.Name = GetTargetLocationAndMoveDirectionName;
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TargetLocation"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TargetMoveDirection"));
		FunctionSignature.SetDescription(LOCTEXT("GetTargetLocationAndMoveDirection", "Returns TargetLocation and TargetMoveDirection from the BeamCoordinator if valid."));
	}
}
void UNiagaraDataInterfaceBeamCoordinatorComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceBeamCoordinatorComponent, SourceComponent) &&
		SourceMode != ENDIBeamCoordinatorComponentSourceMode::Default &&
		SourceMode != ENDIBeamCoordinatorComponentSourceMode::Source)
	{
		// Clear out any source that is set to prevent unnecessary references, since we won't even consider them
		SourceComponent = nullptr;
		SoftSourceActor = nullptr;
		ObjectID = INDEX_NONE;
	}
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}
	
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceBeamCoordinatorComponent, SourceComponent) &&
		SourceMode != ENDIBeamCoordinatorComponentSourceMode::Default &&
		SourceMode != ENDIBeamCoordinatorComponentSourceMode::Source)
	{
		// Disable "Source" if it won't be considered
		return false;
	}

	return true;
}
#endif

void UNiagaraDataInterfaceBeamCoordinatorComponent::BindSourceDelegates()
{
	if (AActor* Source = SoftSourceActor.Get())
	{
		Source->OnEndPlay.AddDynamic(this, &UNiagaraDataInterfaceBeamCoordinatorComponent::OnSourceEndPlay);
	}
	else if (SourceComponent)
	{
		UE_CLOG(!UObjectBaseUtility::IsGarbageEliminationEnabled(), 
			LogVfxGeneral, Warning, TEXT("%s: Unable to bind OnEndPlay for actor-less source component %s, this may extend the lifetime of the component"), 
			*GetFullName(), *SourceComponent->GetPathName());
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::UnbindSourceDelegates()
{
	if (AActor* Source = SoftSourceActor.Get())
	{
		Source->OnEndPlay.RemoveAll(this);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::OnSourceEndPlay(AActor* InSource, EEndPlayReason::Type Reason)
{
	// Increment change id in case we're able to find a new source component 
	UnbindSourceDelegates();
	SoftSourceActor = nullptr;
	SourceComponent = nullptr;
	ObjectID = INDEX_NONE;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	using namespace NDIBeamCoordinatorComponentLocal;
	if (BindingInfo.Name == GetProgressName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetProgress(Context); });
	}
	else if (BindingInfo.Name == GetElapsedSecsName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetElapsedSecs(Context); });
	}
	else if (BindingInfo.Name == GetDurationName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetDuration(Context); });
	}
	else if (BindingInfo.Name == GetTargetWidthName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetTargetWidth(Context); });
	}
	else if (BindingInfo.Name == GetTargetLocationName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetTargetLocation(Context); });
	}
	else if (BindingInfo.Name == GetTargetMoveDirectionName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetTargetMoveDirection(Context); });
	}
	else if (BindingInfo.Name == GetTargetLocationAndMoveDirectionName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetTargetLocationAndMoveDirection(Context); });
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceBeamCoordinatorComponent::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	bSuccess &= InVisitor->UpdateShaderFile(NDIBeamCoordinatorComponentLocal::TemplateShaderFile);
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};
	AppendTemplateHLSL(OutHLSL, NDIBeamCoordinatorComponentLocal::TemplateShaderFile, TemplateArgs);
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	using namespace NDIBeamCoordinatorComponentLocal;
	return (FunctionInfo.DefinitionName == GetProgressName) ||
		(FunctionInfo.DefinitionName == GetElapsedSecsName) ||
		(FunctionInfo.DefinitionName == GetDurationName) ||
		(FunctionInfo.DefinitionName == GetTargetWidthName) ||
		(FunctionInfo.DefinitionName == GetTargetLocationName) ||
		(FunctionInfo.DefinitionName == GetTargetMoveDirectionName) ||
		(FunctionInfo.DefinitionName == GetTargetLocationAndMoveDirectionName);
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	return false;
}
#endif

void UNiagaraDataInterfaceBeamCoordinatorComponent::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	using namespace NDIBeamCoordinatorComponentLocal;

	FNDIProxy& DIProxy = Context.GetProxy<FNDIProxy>();
	FInstanceData_RenderThread& InstanceData = DIProxy.SystemInstancesToInstanceData_RT.FindChecked(Context.GetSystemInstanceID());

	FShaderParameters* ShaderParameters = Context.GetParameterNestedStruct<FShaderParameters>();
	ShaderParameters->Valid		= InstanceData.bCachedValid ? 1 : 0;
	ShaderParameters->Progress	= InstanceData.CachedProgress;
	ShaderParameters->ElapsedSecs	= InstanceData.CachedElapsedSecs;
	ShaderParameters->Duration	= InstanceData.CachedDuration;
	ShaderParameters->TargetWidth	= InstanceData.CachedTargetWidth;
	ShaderParameters->TargetLocation	= InstanceData.CachedTargetLocation;
	ShaderParameters->TargetMoveDirection = InstanceData.CachedTargetMoveDirection;
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	FInstanceData_GameThread* InstanceData = new (PerInstanceData) FInstanceData_GameThread;
	InstanceData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), ActorOrComponentParameter.Parameter);
	InstanceData->CachedActorForCalcTickGroup = ResolveComponent(SystemInstance, InstanceData);

	return true;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	InstanceData->~FInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(RemoveProxy)
	(
		[RT_Proxy=GetProxyAs<FNDIProxy>(), InstanceID=SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
		{
			RT_Proxy->SystemInstancesToInstanceData_RT.Remove(InstanceID);
		}
	);
}

int32 UNiagaraDataInterfaceBeamCoordinatorComponent::PerInstanceDataSize() const
{
	return sizeof(NDIBeamCoordinatorComponentLocal::FInstanceData_GameThread);
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	check(SystemInstance);
	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	if (!InstanceData)
	{
		return true;
	}

	InstanceData->bCachedValid = false;
	InstanceData->CachedProgress = 0.5f;
	InstanceData->CachedElapsedSecs = 0.5f;
	InstanceData->CachedDuration = 1.0f;
	InstanceData->CachedTargetWidth = 100.0f;
	InstanceData->CachedTargetLocation = FVector3f::ZeroVector;
	InstanceData->CachedTargetMoveDirection = FVector3f::ForwardVector;

	UActorComponent* ActorComponent = ResolveComponent(SystemInstance, PerInstanceData);
	if (ActorComponent)
	{
		if (UVfxBeamCoordinatorComponent* BeamCoordinatorComp = Cast<UVfxBeamCoordinatorComponent>(ActorComponent) )
		{
			InstanceData->bCachedValid = BeamCoordinatorComp->HasBeamTarget(ObjectID);
			if (InstanceData->bCachedValid)
			{
				InstanceData->CachedProgress = BeamCoordinatorComp->GetProgress(ObjectID);
				InstanceData->CachedElapsedSecs = BeamCoordinatorComp->GetElapsedSecs(ObjectID);
				InstanceData->CachedDuration = BeamCoordinatorComp->GetDuration(ObjectID);
				InstanceData->CachedTargetWidth = BeamCoordinatorComp->GetTargetWidth(ObjectID);
				InstanceData->CachedTargetLocation = (FVector3f)BeamCoordinatorComp->GetTargetLocation(ObjectID, Axis);
				InstanceData->CachedTargetMoveDirection = (FVector3f)BeamCoordinatorComp->GetTargetMoveDirection(ObjectID, Axis);
			}
		}
	}

	if (bRequireCurrentFrameData)
	{
		InstanceData->CachedActorForCalcTickGroup = ActorComponent;
	}

	return false;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	NDIBeamCoordinatorComponentLocal::FNDIProxy::ProvidePerInstanceDataForRenderThread(DataForRenderThread, PerInstanceData, SystemInstance);
}

ETickingGroup UNiagaraDataInterfaceBeamCoordinatorComponent::CalculateTickGroup(const void* PerInstanceData) const
{
	if ( bRequireCurrentFrameData )
	{
		const NDIBeamCoordinatorComponentLocal::FInstanceData_GameThread* InstanceData = (NDIBeamCoordinatorComponentLocal::FInstanceData_GameThread*)PerInstanceData;
		const UActorComponent* ActorComponent = InstanceData ? InstanceData->CachedActorForCalcTickGroup.Get() : nullptr;
		if (ActorComponent)
		{
			ETickingGroup FinalTickGroup = FMath::Max(ActorComponent->PrimaryComponentTick.TickGroup, ActorComponent->PrimaryComponentTick.EndTickGroup);
			//-TODO: Do we need to do this?
			//if ( USkeletalMeshComponent* SkelMeshComponent = Cast<USkeletalMeshComponent>(ActorComponent) )
			//{
			//	if (SkelMeshComponent->bBlendPhysics)
			//	{
			//		FinalTickGroup = FMath::Max(FinalTickGroup, TG_EndPhysics);
			//	}
			//}
			FinalTickGroup = FMath::Clamp(ETickingGroup(FinalTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);
			return FinalTickGroup;
		}
	}
	return NiagaraFirstTickGroup;
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceBeamCoordinatorComponent* OtherTyped = CastChecked<const UNiagaraDataInterfaceBeamCoordinatorComponent>(Other);
	return OtherTyped->SourceComponent == SourceComponent		
		&& OtherTyped->ActorOrComponentParameter == ActorOrComponentParameter
		&& OtherTyped->SourceMode == SourceMode
		&& OtherTyped->Axis == Axis
		&& OtherTyped->SoftSourceActor == SoftSourceActor
		&& OtherTyped->ObjectID == ObjectID
		&& OtherTyped->bRequireCurrentFrameData == bRequireCurrentFrameData;
}

bool UNiagaraDataInterfaceBeamCoordinatorComponent::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceBeamCoordinatorComponent* OtherTyped = CastChecked<UNiagaraDataInterfaceBeamCoordinatorComponent>(Destination);
	OtherTyped->SourceMode = SourceMode;
	OtherTyped->Axis = Axis;
	OtherTyped->SoftSourceActor = SoftSourceActor;
	OtherTyped->SourceComponent = SourceComponent;
	OtherTyped->ObjectID = ObjectID;
	OtherTyped->ActorOrComponentParameter = ActorOrComponentParameter;
	OtherTyped->bRequireCurrentFrameData = bRequireCurrentFrameData;
	return true;
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetProgress(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<float>	OutProgress(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutProgress.SetAndAdvance(InstanceData->CachedProgress);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetElapsedSecs(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<float>	OutElapsedSecs(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutElapsedSecs.SetAndAdvance(InstanceData->CachedElapsedSecs);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetDuration(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<float>	OutDuration(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutDuration.SetAndAdvance(InstanceData->CachedDuration);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetTargetWidth(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<float>	OutTargetWidth(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutTargetWidth.SetAndAdvance(InstanceData->CachedTargetWidth);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetTargetLocation(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<FVector3f>	OutPosition(Context);

	for (int32 i=0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutPosition.SetAndAdvance(InstanceData->CachedTargetLocation);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetTargetMoveDirection(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<FVector3f>	OutDirection(Context);

	for (int32 i=0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutDirection.SetAndAdvance(InstanceData->CachedTargetMoveDirection);
	}
}

void UNiagaraDataInterfaceBeamCoordinatorComponent::VMGetTargetLocationAndMoveDirection(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIBeamCoordinatorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<FVector3f>	OutPosition(Context);
	FNDIOutputParam<FVector3f>	OutDirection(Context);

	for (int32 i=0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutPosition.SetAndAdvance(InstanceData->CachedTargetLocation);
		OutDirection.SetAndAdvance(InstanceData->CachedTargetMoveDirection);
	}
}

#undef LOCTEXT_NAMESPACE


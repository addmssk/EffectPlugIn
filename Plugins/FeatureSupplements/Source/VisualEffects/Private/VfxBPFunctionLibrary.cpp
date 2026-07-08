// Copyright Epic Games, Inc. All Rights Reserved.

#include "VfxBPFunctionsLibrary.h"
#include "Engine/Engine.h"
#include "Niagara/NiagaraDataInterfaceBeamCoordinatorComponent.h"
#include "VfxBeamCoordinatorComponent.h"
#include "VfxUtils.h"
#include "NiagaraComponent.h"
#include "UObject/UObjectBaseUtility.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(VfxBPFunctionsLibrary)

class UMaterialInterface;


void UVfxBPFunctionsLibrary::OverrideSystemUserVariableBeamCoordinatorComponent(
	UNiagaraComponent* NiagaraSystem,
	const FString& OverrideName,
	UVfxBeamCoordinatorComponent* BeamCoordinatorComponent,
	int32 InObjectID
)
{
	if (!NiagaraSystem)
	{
		UE_LOG(LogVfxGeneral, Warning,
			TEXT("NiagaraSystem in \"Set Niagara Beam Coordinator Component\" is NULL, OverrideName \"%s\" and BeamCoordinatorComponent \"%s\", skipping."),
			*OverrideName, *GetFullNameSafe(BeamCoordinatorComponent)
		);
		return;
	}

	if (!BeamCoordinatorComponent)
	{
		UE_LOG(LogVfxGeneral, Warning,
			TEXT("BeamCoordinatorComponent in \"Set Niagara Beam Coordinator Component\" is NULL, OverrideName \"%s\" and NiagaraSystem \"%s\", skipping."),
			*OverrideName, *GetFullNameSafe(NiagaraSystem)
		);
		return;
	}

	UNiagaraDataInterfaceBeamCoordinatorComponent* Interface = GetBeamCoordinatorComponentDataInterface(NiagaraSystem, OverrideName);
	if (!Interface)
	{
		UE_LOG(LogVfxGeneral, Warning,
			TEXT("UNiagaraFunctionLibrary::OverrideSystemUserVariableBeamCoordinatorComponent: Did not find a matching Skeletal Mesh Data Interface variable named \"%s\" in the User variables of NiagaraSystem \"%s\" ."),
			*OverrideName, *GetFullNameSafe(NiagaraSystem)
		);
		return;
	}

	Interface->SetSourceComponentFromBlueprints(BeamCoordinatorComponent, InObjectID);
}

UNiagaraDataInterfaceBeamCoordinatorComponent* UVfxBPFunctionsLibrary::GetBeamCoordinatorComponentDataInterface(
	UNiagaraComponent* NiagaraSystem,
	const FString& OverrideName
)
{
	if (!NiagaraSystem)
	{
		return nullptr;
	}

	const FNiagaraParameterStore& OverrideParameters = NiagaraSystem->GetOverrideParameters();
	FNiagaraVariable Variable(FNiagaraTypeDefinition(UNiagaraDataInterfaceBeamCoordinatorComponent::StaticClass()), *OverrideName);

	const int32 Index = OverrideParameters.IndexOf(Variable);
	return Index != INDEX_NONE ? Cast<UNiagaraDataInterfaceBeamCoordinatorComponent>(OverrideParameters.GetDataInterface(Index)) : nullptr;
}
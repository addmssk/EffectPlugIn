// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "VfxBPFunctionsLibrary.generated.h"

class UNiagaraComponent;
class UVfxBeamCoordinatorComponent;
class UNiagaraDataInterfaceBeamCoordinatorComponent;

/**
 *
 */

UCLASS()
class UVfxBPFunctionsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
	/** Sets a beam coordinator component parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Beam Coordinator Component"))
	static VISUALEFFECTS_API void OverrideSystemUserVariableBeamCoordinatorComponent(
		UNiagaraComponent* NiagaraSystem,
		UPARAM(DisplayName = "Parameter Name") const FString& OverrideName,
		UVfxBeamCoordinatorComponent* BeamCoordinatorComponent,
		int32 InObjectID
	);
	
	/** Get the beam coordinator component data interface by name .*/
	static VISUALEFFECTS_API class UNiagaraDataInterfaceBeamCoordinatorComponent* GetBeamCoordinatorComponentDataInterface(
		UNiagaraComponent* NiagaraSystem, const FString& OverrideName
	);

};

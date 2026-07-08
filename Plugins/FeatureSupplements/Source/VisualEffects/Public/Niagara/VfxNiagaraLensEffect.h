

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraLensEffectBase.h"
#include "VfxNiagaraLensEffect.generated.h"

UCLASS(Blueprintable)
class VISUALEFFECTS_API AVfxNiagaraLensEffect : public ANiagaraLensEffectBase
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVfxNiagaraLensEffect(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	
	
};

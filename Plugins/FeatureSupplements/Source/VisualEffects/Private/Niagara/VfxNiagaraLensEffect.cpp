


#include "Niagara\VfxNiagaraLensEffect.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VfxNiagaraLensEffect)

// Sets default values
AVfxNiagaraLensEffect::AVfxNiagaraLensEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AVfxNiagaraLensEffect::BeginPlay()
{
	Super::BeginPlay();
	
	ActivateLensEffect();
}

// Called every frame
void AVfxNiagaraLensEffect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}



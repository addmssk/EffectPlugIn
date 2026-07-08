


#include "LevelSequence/TrvLevelSequenceActor.h"


// Sets default values
ATrvLevelSequenceActor::ATrvLevelSequenceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ATrvLevelSequenceActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATrvLevelSequenceActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}



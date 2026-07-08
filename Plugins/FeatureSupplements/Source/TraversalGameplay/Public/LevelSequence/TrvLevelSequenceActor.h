

#pragma once

#include "CoreMinimal.h"
#include "LevelSequenceActor.h"
#include "TrvLevelSequenceActor.generated.h"

UCLASS()
class TRAVERSALGAMEPLAY_API ATrvLevelSequenceActor : public ALevelSequenceActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATrvLevelSequenceActor(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	
	
};

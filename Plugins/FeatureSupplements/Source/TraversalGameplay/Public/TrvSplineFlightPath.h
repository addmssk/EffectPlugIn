

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrvSplineCommon.h"
#include "TrvSplineFlightPath.generated.h"

class UProceduralMeshComponent;
class USplineComponent;
class UMaterialInterface;


UCLASS()
class TRAVERSALGAMEPLAY_API ATrvSplineFlightPath : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATrvSplineFlightPath(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	// AActor~
	virtual void OnConstruction(const FTransform& Transform) override;
	// ~AActor

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable)
	void RefreshPaths();
#endif

	UFUNCTION(BlueprintCallable)
	float GetFlightPathLength() const;

	UFUNCTION(BlueprintCallable)
	bool GetFlightPathPoint(
		const float InDistance,
		FVector& OutForwardVector,
		FVector& OutUpVector,
		FVector& OutSideVector,
		FVector& OutCenterLocation,
		float& OutRightMaxOffset,
		float& OutLeftMaxOffset
	) const;

	float CaclculateNearestStartPerpendicularBias(const FVector& InStartPosition) const;
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DistancePerEachPathSample;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PathHalfWidth;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PathHalfWidthMultiplierForIgnoringCurvatureRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USplineComponent> SplineComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UProceduralMeshComponent> EditOnlyMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UMaterialInterface> EditOnlyMeshMaterial;
#endif

private:
	UPROPERTY()
	TArray<FTrvSplineSegment> Paths;
};

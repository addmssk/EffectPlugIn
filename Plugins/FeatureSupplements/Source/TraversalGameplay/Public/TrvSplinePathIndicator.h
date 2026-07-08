

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrvSplineCommon.h"
#include "TrvSplinePathIndicator.generated.h"

class UProceduralMeshComponent;
class USplineComponent;
class UMaterialInterface;


UCLASS()
class TRAVERSALGAMEPLAY_API ATrvSplinePathIndicator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATrvSplinePathIndicator(const FObjectInitializer& ObjectInitializer);
	
	virtual void Tick(float DeltaTime) override;
	
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	// AActor~
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	// ~AActor

public:	
	UFUNCTION(BlueprintCallable)
	void BuildSpline(const TArray<FVector>& ControlPoints);

	UFUNCTION(BlueprintCallable)
	void RefreshPaths();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(UIMin="10.0", ClampMin="10.0"))
	float TexCoordVWrappingDistance;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(UIMin="10.0", ClampMin="10.0"))
	float SegmentDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(UIMin="10.0", ClampMin="10.0"))
	float BaseHalfWidth;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(UIMin="1.0", ClampMin="1.0"))
	float HalfWidthMultiplierForIgnoringCurvatureRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh")
	TSoftObjectPtr<UMaterialInterface> MeshMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USplineComponent> SplineComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

private:
	UPROPERTY()
	TArray<FTrvSplineSegment> Segments;
};

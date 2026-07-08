


#include "TrvSplineFlightPath.h"
#include "ProceduralMeshComponent.h"
#include "Components/SplineComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TrvUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvSplineFlightPath)

// Sets default values
ATrvSplineFlightPath::ATrvSplineFlightPath(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	DistancePerEachPathSample = 100.f;
	PathHalfWidth = 100.f;
	PathHalfWidthMultiplierForIgnoringCurvatureRadius = 1.5f;

	SplineComponent = CreateDefaultSubobject<USplineComponent>("SplineComponent");
	SplineComponent->SetupAttachment(RootComponent);
	
#if WITH_EDITOR
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> DebugMaterial;
		FConstructorStatics()
			: DebugMaterial(TEXT("/FeatureSupplements/Materials/M_Cfs_FlightPathDebug.M_Cfs_FlightPathDebug"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	EditOnlyMeshMaterial = ConstructorStatics.DebugMaterial.Object;

	EditOnlyMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>("EditOnlyMesh");
	EditOnlyMeshComponent->bHiddenInGame = true;
	EditOnlyMeshComponent->SetupAttachment(SplineComponent);
#endif
}

// Called when the game starts or when spawned
void ATrvSplineFlightPath::BeginPlay()
{
	Super::BeginPlay();
	
}

#if WITH_EDITOR
void ATrvSplineFlightPath::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged == nullptr)
	{
		return;
	}

	bool bNeedReconstruct = PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplineFlightPath, DistancePerEachPathSample);
	bNeedReconstruct |= PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplineFlightPath, PathHalfWidth);
	bNeedReconstruct |= PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplineFlightPath, PathHalfWidthMultiplierForIgnoringCurvatureRadius);
	if (bNeedReconstruct)
	{
		RefreshPaths();
		Modify();
	}
}
#endif

void ATrvSplineFlightPath::OnConstruction(const FTransform& Transform)
{
#if WITH_EDITOR
	RefreshPaths();
#endif
}

// Called every frame
void ATrvSplineFlightPath::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

#if WITH_EDITOR
void ATrvSplineFlightPath::RefreshPaths()
{
#if WITH_EDITOR
	EditOnlyMeshComponent->ClearAllMeshSections();
#endif

	Paths.Reset();

	const float Length = SplineComponent->GetSplineLength();
	if (Length < DistancePerEachPathSample)
	{
		return;
	}

	const int32 MaxStep = 1 + FMath::CeilToInt32(Length / DistancePerEachPathSample);
	Paths.Reserve(MaxStep);

	FVector PrevDir = FVector::ZeroVector;
	float PrevDistance = -DistancePerEachPathSample;
	for (int32 Idx = 0; Idx < MaxStep; ++Idx)
	{
		float CurrDistance = FMath::Min(Length, float(Idx) * DistancePerEachPathSample);
		float Dist = CurrDistance - PrevDistance;
		if (Dist < 10.f)
		{
			break;
		}

		FTrvSplineSegment& NewSeg = Paths.AddDefaulted_GetRef();
		FSplineMeshBuildHelper::Add(
			PrevDir, NewSeg, SplineComponent,
			CurrDistance, Dist, PathHalfWidth, PathHalfWidthMultiplierForIgnoringCurvatureRadius
		);

		PrevDistance = CurrDistance;
	}

#if WITH_EDITOR
	EditOnlyMeshComponent->ClearAllMeshSections();

	
	TArray<FVector> Vertices;
	TArray<int32> Indices;
	TArray<FVector2D> UV;
	TArray<FColor> VertexColors;
	TArray<FVector> PlaceHolder;
	TArray<FProcMeshTangent> Tangents;

	for (const FTrvSplineSegment& CurrPoint : Paths)
	{
		Vertices.Add(CurrPoint.Center + CurrPoint.SideVector * CurrPoint.LeftMaxOffset);
		Vertices.Add(CurrPoint.Center + CurrPoint.SideVector * CurrPoint.RightMaxOffset);
		
		VertexColors.Add(FColor::Red);
		VertexColors.Add(FColor::Blue);

		if (Vertices.Num() > 2)
		{
			Indices.Add(Vertices.Num() - 4);
			Indices.Add(Vertices.Num() - 3);
			Indices.Add(Vertices.Num() - 1);

			Indices.Add(Vertices.Num() - 1);
			Indices.Add(Vertices.Num() - 2);
			Indices.Add(Vertices.Num() - 4);
		}
	}
	EditOnlyMeshComponent->CreateMeshSection(0, Vertices, Indices, PlaceHolder, UV, UV, UV, UV, VertexColors, Tangents, false);
	EditOnlyMeshComponent->SetMaterial(0, EditOnlyMeshMaterial.Get());
#endif
}
#endif

float ATrvSplineFlightPath::GetFlightPathLength() const
{
	return SplineComponent->GetSplineLength();
}

bool ATrvSplineFlightPath::GetFlightPathPoint(
	const float InDistance,
	FVector& OutForwardVector,
	FVector& OutUpVector,
	FVector& OutSideVector,
	FVector& OutCenterLocation,
	float& OutRightMaxOffset,
	float& OutLeftMaxOffset
) const
{
	if (Paths.Num() < 2)
	{
		return false;
	}

	int32 UpperBound = Algo::UpperBoundBy(
		Paths, InDistance,
		[](const FTrvSplineSegment& InPath)
		{
			return InPath.Distance;
		}
	);
	if (UpperBound == INDEX_NONE)
	{
		return false;
	}
	UpperBound = FMath::Min(Paths.Num() - 1, UpperBound);
	
	const FTransform& CompToWorld = SplineComponent->GetComponentTransform();
	const FTrvSplineSegment& UpperElem = Paths[UpperBound];
	if (UpperBound == 0)
	{
		OutForwardVector = CompToWorld.TransformVector(UpperElem.ForwardVector);
		OutUpVector = CompToWorld.TransformVector(UpperElem.UpVector);
		OutSideVector = CompToWorld.TransformVector(UpperElem.SideVector);
		OutCenterLocation = CompToWorld.TransformPosition(UpperElem.Center);
		OutRightMaxOffset = UpperElem.RightMaxOffset;
		OutLeftMaxOffset = UpperElem.LeftMaxOffset;
		return true;
	}

	const int32 LowerBound = UpperBound - 1;
	const FTrvSplineSegment& LowerElem = Paths[LowerBound];

	float Ratio = (InDistance - LowerElem.Distance) / (UpperElem.Distance - LowerElem.Distance);
	
	OutForwardVector = CompToWorld.TransformVector(FMath::Lerp(LowerElem.ForwardVector, UpperElem.ForwardVector, Ratio));
	OutUpVector = CompToWorld.TransformVector(FMath::Lerp(LowerElem.UpVector, UpperElem.UpVector, Ratio));
	OutSideVector = CompToWorld.TransformVector(FMath::Lerp(LowerElem.SideVector, UpperElem.SideVector, Ratio));
	OutCenterLocation = CompToWorld.TransformPosition(FMath::Lerp(LowerElem.Center, UpperElem.Center, Ratio));
	OutRightMaxOffset = FMath::Lerp(LowerElem.RightMaxOffset, UpperElem.RightMaxOffset, Ratio);
	OutLeftMaxOffset = FMath::Lerp(LowerElem.LeftMaxOffset, UpperElem.LeftMaxOffset, Ratio);

	return true;
}

float ATrvSplineFlightPath::CaclculateNearestStartPerpendicularBias(const FVector& InStartPosition) const
{
	float Bias = 0.f;

	// Find nearest start bias
	{
		FVector PathSegmentForwardVector;
		FVector PathSegmentUpVector;
		FVector PathSegmentSideVector;
		FVector PathSegmentCenter;
		float PathSegmentRightMaxOffset;
		float PathSegmentLeftMaxOffset;
		bool bValidPath = GetFlightPathPoint(
			0.f,
			PathSegmentForwardVector,
			PathSegmentUpVector,
			PathSegmentSideVector,
			PathSegmentCenter,
			PathSegmentRightMaxOffset,
			PathSegmentLeftMaxOffset
		);

		if (bValidPath)
		{
			FVector Origin = PathSegmentCenter - PathSegmentSideVector * PathSegmentLeftMaxOffset;
			FVector Dir = -PathSegmentSideVector;
			FVector ProjectedPoint;
			FMath::PointDistToLine(InStartPosition, Dir, Origin, ProjectedPoint);
	
			FVector ToProjected = ProjectedPoint - PathSegmentCenter;
			float SizeSQ = ToProjected.SizeSquared();
			if (SizeSQ > FLT_EPSILON)
			{
				float Size = FMath::Sqrt(SizeSQ);
				ToProjected /= Size;

				if (FVector::DotProduct(ToProjected, PathSegmentSideVector) > 0)
				{
					Bias = FMath::Clamp(Size, 0.f, PathSegmentRightMaxOffset);
				}
				else
				{
					Bias = FMath::Clamp(-Size, PathSegmentLeftMaxOffset, 0.f);
				}
			}
		}
	}

	return Bias;
}

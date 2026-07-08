


#include "TrvSplinePathIndicator.h"
#include "ProceduralMeshComponent.h"
#include "Components/SplineComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TrvUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TrvSplinePathIndicator)

// Sets default values
ATrvSplinePathIndicator::ATrvSplinePathIndicator(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	TexCoordVWrappingDistance = 100.f;
	SegmentDistance = 100.f;
	BaseHalfWidth = 100.f;
	HalfWidthMultiplierForIgnoringCurvatureRadius = 1.5f;

	SplineComponent = CreateDefaultSubobject<USplineComponent>("SplineComponent");
	SplineComponent->SetupAttachment(RootComponent);
	
	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>("Mesh");
	MeshComponent->SetupAttachment(SplineComponent);
}

void ATrvSplinePathIndicator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

#if WITH_EDITOR
void ATrvSplinePathIndicator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged == nullptr)
	{
		return;
	}

	bool bNeedReconstruct = PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplinePathIndicator, TexCoordVWrappingDistance);
	bNeedReconstruct |= PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplinePathIndicator, SegmentDistance);
	bNeedReconstruct |= PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplinePathIndicator, BaseHalfWidth);
	bNeedReconstruct |= PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ATrvSplinePathIndicator, HalfWidthMultiplierForIgnoringCurvatureRadius);
	if (bNeedReconstruct)
	{
		RefreshPaths();
		Modify();
	}
}
#endif

void ATrvSplinePathIndicator::BeginPlay()
{
	Super::BeginPlay();
	
}

void ATrvSplinePathIndicator::OnConstruction(const FTransform& Transform)
{
#if WITH_EDITOR
	RefreshPaths();
#endif
}

void ATrvSplinePathIndicator::BuildSpline(const TArray<FVector>& ControlPoints)
{
	if (ControlPoints.Num() < 2)
	{
		return;
	}

	TArray<FVector> Distilled;
	{
		FTransform ToWorld = RootComponent->GetComponentTransform();
		FTransform WorldToActor = ToWorld.Inverse();

		FVector Prev = WorldToActor.TransformPosition(ControlPoints[0]);
		Distilled.Add(Prev);
		for (int32 Idx = 1; Idx < ControlPoints.Num(); ++Idx)
		{
			const FVector Curr = WorldToActor.TransformPosition(ControlPoints[Idx]);
			FVector Delta = Prev - Curr;
			
			if (Delta.SizeSquared() > 100.f)
			{
				Distilled.Add(Curr);
			}
			Prev = Curr;
		}
	}

	if (Distilled.Num() < 2)
	{
		return;
	}

	SplineComponent->ClearSplinePoints();

	FVector PrevPos = FVector::ZeroVector;

	// Begin
	{
		const FVector CurrPoint = Distilled[0];
		const FVector NextPoint = Distilled[1];

		FSplinePoint NewPoint;
		NewPoint.InputKey = 0.f;
		NewPoint.Position = CurrPoint;
		NewPoint.Type = ESplinePointType::Curve;

		FVector ToNext = NextPoint - CurrPoint;
		ToNext.Normalize();

		NewPoint.ArriveTangent = -ToNext * 100.f;
		NewPoint.LeaveTangent = ToNext * 100.f;

		PrevPos = CurrPoint;

		SplineComponent->AddPoint(NewPoint, false);
	}

	// Loop
	for (int32 Idx = 1, EndNum = Distilled.Num() - 1; Idx < EndNum; ++Idx)
	{
		const FVector CurrPoint = Distilled[Idx];
		const FVector NextPoint = Distilled[Idx + 1];

		FSplinePoint NewPoint;
		NewPoint.InputKey = Idx;
		NewPoint.Position = CurrPoint;
		NewPoint.Type = ESplinePointType::Curve;
		
		FVector ToNext = NextPoint - PrevPos;
		ToNext.Normalize();

		NewPoint.ArriveTangent = -ToNext * 100.f;
		NewPoint.LeaveTangent = ToNext * 100.f;
		
		SplineComponent->AddPoint(NewPoint, false);

		PrevPos = CurrPoint;
	}

	// End
	{
		const FVector& CurrPoint = Distilled.Last();

		FSplinePoint NewPoint;
		NewPoint.InputKey = Distilled.Num() - 1;
		NewPoint.Position = CurrPoint;
		NewPoint.Type = ESplinePointType::Curve;

		FVector FromPrev = CurrPoint - PrevPos;
		FromPrev.Normalize();
				
		NewPoint.ArriveTangent = -FromPrev * 100.f;
		NewPoint.LeaveTangent = FromPrev * 100.f;
		 
		SplineComponent->AddPoint(NewPoint, false);
	}

	SplineComponent->UpdateSpline();

	RefreshPaths();
}

void ATrvSplinePathIndicator::RefreshPaths()
{
	MeshComponent->ClearAllMeshSections();

	Segments.Reset();

	const float Length = SplineComponent->GetSplineLength();
	if (Length < SegmentDistance)
	{
		return;
	}

	const int32 MaxStep = 1 + FMath::CeilToInt32(Length / SegmentDistance);
	Segments.Reserve(MaxStep);

	FVector PrevDir = FVector::ZeroVector;
	float PrevDistance = -SegmentDistance;
	for (int32 Idx = 0; Idx < MaxStep; ++Idx)
	{
		float CurrDistance = FMath::Min(Length, float(Idx) * SegmentDistance);
		float Dist = CurrDistance - PrevDistance;
		if (Dist < 10.f)
		{
			break;
		}

		FTrvSplineSegment& NewSeg = Segments.AddDefaulted_GetRef();
		FSplineMeshBuildHelper::Add(
			PrevDir, NewSeg, SplineComponent,
			CurrDistance, Dist, BaseHalfWidth, HalfWidthMultiplierForIgnoringCurvatureRadius
		);

		PrevDistance = CurrDistance;
	}

	MeshComponent->ClearAllMeshSections();

	TArray<FVector> Vertices;
	TArray<int32> Indices;
	TArray<FVector2D> UV0s;
	TArray<FVector2D> UV1s;
	TArray<FVector2D> UVPlaceHolder;
	TArray<FColor> VertexColors;
	TArray<FVector> PlaceHolder;
	TArray<FProcMeshTangent> Tangents;


	{
		const int32 ReserveNum = Segments.Num() * 4;
		Vertices.Reserve(ReserveNum);
		VertexColors.Reserve(ReserveNum);
		UV0s.Reserve(ReserveNum);
		UV1s.Reserve(ReserveNum);
	}

	// Initial
	{
		const FTrvSplineSegment& CurrSegment = Segments[0];
		Vertices.Add(CurrSegment.Center + CurrSegment.SideVector * CurrSegment.LeftMaxOffset);
		Vertices.Add(CurrSegment.Center + CurrSegment.SideVector * CurrSegment.RightMaxOffset);
		UV0s.Add(FVector2D(0.f, 0.f));
		UV0s.Add(FVector2D(1.f, 0.f));
		UV1s.Add(FVector2D(0.f, 0.f));
		UV1s.Add(FVector2D(1.f, 0.f));
	}
	
	float LastTexCoord0V = 0.f;
	float LastDistance = 0.f;
	
	for (int32 Idx = 1; Idx < Segments.Num(); ++Idx)
	{
		const FTrvSplineSegment& CurrSegment = Segments[Idx];
		float SegmentLength = CurrSegment.Distance - LastDistance;
		float TexCoord0V_Offset = SegmentLength / TexCoordVWrappingDistance;

		float TexCoord0V = LastTexCoord0V + TexCoord0V_Offset;
		float TexCoord1V = CurrSegment.Distance / Length;

		FVector2D UV0_L = FVector2D(0.f, 0.f);
		FVector2D UV0_R = FVector2D(1.f, 0.f);

		FVector Left = CurrSegment.Center + CurrSegment.SideVector * CurrSegment.LeftMaxOffset;
		FVector Right = CurrSegment.Center + CurrSegment.SideVector * CurrSegment.RightMaxOffset;

		Vertices.Add(Left);
		Vertices.Add(Right);
		UV0s.Add(FVector2D(0.f, TexCoord0V));
		UV0s.Add(FVector2D(1.f, TexCoord0V));
		UV1s.Add(FVector2D(0.f, TexCoord1V));
		UV1s.Add(FVector2D(1.f, TexCoord1V));
		
		Indices.Add(Vertices.Num() - 4);
		Indices.Add(Vertices.Num() - 3);
		Indices.Add(Vertices.Num() - 1);

		Indices.Add(Vertices.Num() - 1);
		Indices.Add(Vertices.Num() - 2);
		Indices.Add(Vertices.Num() - 4);

		// Next SegmentBegin

		TexCoord0V = FMath::Frac(TexCoord0V);

		Vertices.Add(Left);
		Vertices.Add(Right);
		UV0s.Add(FVector2D(0.f, TexCoord0V));
		UV0s.Add(FVector2D(1.f, TexCoord0V));
		UV1s.Add(FVector2D(0.f, TexCoord1V));
		UV1s.Add(FVector2D(1.f, TexCoord1V));

		LastTexCoord0V = TexCoord0V;
		LastDistance = CurrSegment.Distance;
	}

	MeshComponent->CreateMeshSection(0, Vertices, Indices, PlaceHolder, UV0s, UV1s, UVPlaceHolder, UVPlaceHolder, VertexColors, Tangents, false);
	MeshComponent->SetMaterial(0, MeshMaterial.Get());
}
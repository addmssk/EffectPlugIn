

#include "TrvUtils.h"
#include "Components/SplineComponent.h"


DEFINE_LOG_CATEGORY(LogTrvGeneral)


void FSplineMeshBuildHelper::Add(
	FVector& InOutPrevDir,
	FTrvSplineSegment& OutSegment,
	class USplineComponent* SplineComponent,
	const float InCurrDistance,
	const float InStepDist,
	const float InHalfWidth,
	const float InHalfWidthMultiplierForIgnoringCurvatureRadius
)
{
	const float StepDistSQ = InStepDist * InStepDist;

	FTransform CurrTransform = SplineComponent->GetTransformAtDistanceAlongSpline(InCurrDistance, ESplineCoordinateSpace::Local, true);

	OutSegment.Center = CurrTransform.GetLocation();
	OutSegment.Distance = InCurrDistance;
	OutSegment.ForwardVector = CurrTransform.GetRotation().GetAxisX();

	FVector D2 = OutSegment.ForwardVector - InOutPrevDir; // Second Derivative
	float ProjToTangent = FVector::DotProduct(D2, OutSegment.ForwardVector);
	FVector InnerDir = D2 - ProjToTangent * OutSegment.ForwardVector;
	InnerDir.Z = 0.f; // Ignore Z

	const float YScale = CurrTransform.GetScale3D().Y;
	const float HalfWidth = YScale * InHalfWidth;
		
	//FVector YAxis = CurrTransform.GetRotation().GetAxisY();
	FVector YAxis = FVector::CrossProduct(FVector::UpVector, OutSegment.ForwardVector);
	YAxis.Normalize();

	float SizeSQ = InnerDir.SizeSquared();
	if (SizeSQ < FLT_EPSILON)
	{
		OutSegment.RightMaxOffset = HalfWidth;
		OutSegment.LeftMaxOffset = -HalfWidth;
			
		OutSegment.SideVector = YAxis;
		OutSegment.DebugInfo = 0;
	}
	else
	{
		float Size = FMath::Sqrt(SizeSQ);
		InnerDir /= Size;
		float K = Size / StepDistSQ;
		float Radius = 1.f / K;
			
		if (Radius > (HalfWidth * InHalfWidthMultiplierForIgnoringCurvatureRadius))
		{
			OutSegment.RightMaxOffset = HalfWidth;
			OutSegment.LeftMaxOffset = -HalfWidth;
				
			OutSegment.SideVector = YAxis;
			OutSegment.DebugInfo = 0;
		}
		else
		{
			const float DotInnerAndY = FVector::DotProduct(YAxis, InnerDir);
			if (DotInnerAndY > 0.f)
			{
				OutSegment.RightMaxOffset = HalfWidth;
				OutSegment.LeftMaxOffset = -HalfWidth;
				if (Radius < HalfWidth)
				{
					OutSegment.RightMaxOffset = Radius;
				}
					
				OutSegment.SideVector = InnerDir;
				OutSegment.DebugInfo = 128;
			}
			else
			{
				OutSegment.RightMaxOffset = HalfWidth;
				OutSegment.LeftMaxOffset = -HalfWidth;
				if (Radius < HalfWidth)
				{
					OutSegment.LeftMaxOffset = -Radius;
				}
					
				OutSegment.SideVector = -InnerDir;
				OutSegment.DebugInfo = 255;
			}
		}
	}
		
	OutSegment.UpVector = FVector::CrossProduct(OutSegment.ForwardVector, OutSegment.SideVector);
	OutSegment.UpVector.Normalize();

	InOutPrevDir = OutSegment.ForwardVector;
}
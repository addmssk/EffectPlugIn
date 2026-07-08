
#pragma once
#include "CoreMinimal.h"
#include "TrvSplineCommon.h"

/**************************************************************************************************
*
*    Logs
*
***/

DECLARE_LOG_CATEGORY_EXTERN(LogTrvGeneral, Log, All);


/**************************************************************************************************
*
*    Helper
*
***/

struct FSplineMeshBuildHelper
{
	static void Add(
		FVector& InOutPrevDir,
		FTrvSplineSegment& OutSegment,
		class USplineComponent* SplineComponent,
		const float InCurrDistance,
		const float InStepDist,
		const float InHalfWidth,
		const float InHalfWidthMultiplierForIgnoringCurvatureRadius
	);
};
// PerfMonitorSubsystem.cpp

#include "PerfMonitorSubsystem.h"

#include "HAL/PlatformTime.h"
#include "UObject/UObjectHash.h"

namespace
{
	/** Default monitor: records the built-in metrics, registers no params or rules. */
	class FPerfCollectionOnlyMonitor final : public FPerfMonitorBase
	{
	protected:
		virtual void RegisterParams() override {}
		virtual void RegisterRules() override {}
	};
}

bool UPerfMonitorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Step aside when a project derived a subsystem from this class: the derived class
	// (which inherits this check and has no further children) hosts the monitor instead,
	// so only one monitor exists per GameInstance.
	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(GetClass(), DerivedClasses, /*bRecursive=*/false);
	return DerivedClasses.Num() == 0;
}

void UPerfMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Monitor = CreateMonitor();
	if (Monitor.IsValid())
	{
		Monitor->Initialize();
	}
}

void UPerfMonitorSubsystem::Deinitialize()
{
	Monitor.Reset(); // ~FPerfMonitorBase runs Shutdown()
	Super::Deinitialize();
}

TUniquePtr<FPerfMonitorBase> UPerfMonitorSubsystem::CreateMonitor()
{
	return MakeUnique<FPerfCollectionOnlyMonitor>();
}

double UPerfMonitorSubsystem::GetMetricLatest(FName MetricId) const
{
	return Monitor.IsValid() ? Monitor->GetMetrics().GetLatest(MetricId) : 0.0;
}

double UPerfMonitorSubsystem::GetMetricAverage(FName MetricId, float WindowSeconds) const
{
	if (!Monitor.IsValid())
	{
		return 0.0;
	}

	const FPerfMetricSeries* Series = Monitor->GetMetrics().Find(MetricId);
	if (!Series)
	{
		return 0.0;
	}

	const FPerfMetricSeries::FWindowStats Stats =
		Series->GetStats(TNumericLimits<int32>::Max(), WindowSeconds, FPlatformTime::Seconds());
	return (Stats.Num > 0) ? Stats.Avg : 0.0;
}

double UPerfMonitorSubsystem::GetParamEffective(FName ParamName) const
{
	const FPerfControlledParam* Param = Monitor.IsValid() ? Monitor->FindParam(ParamName) : nullptr;
	return Param ? Param->GetEffective() : 0.0;
}

FName UPerfMonitorSubsystem::GetParamBindingSource(FName ParamName) const
{
	if (const FPerfControlledParam* Param = Monitor.IsValid() ? Monitor->FindParam(ParamName) : nullptr)
	{
		if (const FPerfConstraint* Binding = Param->GetBindingConstraint())
		{
			return Binding->SourceId;
		}
	}
	return NAME_None;
}

int32 UPerfMonitorSubsystem::GetParamActiveReasonFlags(FName ParamName) const
{
	const FPerfControlledParam* Param = Monitor.IsValid() ? Monitor->FindParam(ParamName) : nullptr;
	return Param ? (int32)Param->GetActiveReasonFlags() : 0;
}

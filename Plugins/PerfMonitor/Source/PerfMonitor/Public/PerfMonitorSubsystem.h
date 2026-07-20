// PerfMonitorSubsystem.h
//
// GameInstance-lifetime host for a FPerfMonitorBase-derived monitor
// (cf. ULyraPerformanceStatSubsystem in the Lyra sample).

#pragma once

#include "CoreMinimal.h"
#include "PerfMonitorCore.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PerfMonitorSubsystem.generated.h"

/**
 * Hosts one monitor per GameInstance. GameInstance creation is the intended registration
 * point: GEngine exists by then, and multi-client PIE (several GameInstances in one
 * process) is already handled by the shared frame consumer, which suspends processing
 * while more than one monitor is registered.
 *
 * Projects can either:
 *   1. use this subsystem as-is - it runs a collection-only monitor (built-in frame /
 *      thermal / battery / memory metrics, no params or rules), or
 *   2. derive from it and override CreateMonitor() to supply their own monitor with
 *      project params and rules (see the usage example in PerfMonitorCore.h). The base
 *      subsystem steps aside automatically when a derived subsystem class exists.
 */
UCLASS()
class PERFMONITOR_API UPerfMonitorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	FPerfMonitorBase* GetMonitor() const { return Monitor.Get(); }

	// ---- metric queries (Lyra GetCachedStat analogue; ids from PerfMetricNames or project metrics) ----

	/** Most recent sample of a metric; 0 when the metric has no samples (e.g. unsupported platform). */
	UFUNCTION(BlueprintCallable, Category = "PerfMonitor")
	double GetMetricLatest(FName MetricId) const;

	/** Average over samples recorded in the last WindowSeconds; 0 when empty. */
	UFUNCTION(BlueprintCallable, Category = "PerfMonitor")
	double GetMetricAverage(FName MetricId, float WindowSeconds = 1.0f) const;

	// ---- controlled-param provenance queries ----

	/** Current effective value of a controlled param (baseline when nothing constrains it). */
	UFUNCTION(BlueprintCallable, Category = "PerfMonitor")
	double GetParamEffective(FName ParamName) const;

	/** SourceId of the constraint that decided the current value; NAME_None when the baseline decided it. */
	UFUNCTION(BlueprintCallable, Category = "PerfMonitor")
	FName GetParamBindingSource(FName ParamName) const;

	/** OR of EPerfReason bits across all constraints currently held on the param. */
	UFUNCTION(BlueprintCallable, Category = "PerfMonitor")
	int32 GetParamActiveReasonFlags(FName ParamName) const;

protected:
	/** Factory hook: override in a derived subsystem to supply the project monitor. */
	virtual TUniquePtr<FPerfMonitorBase> CreateMonitor();

	TUniquePtr<FPerfMonitorBase> Monitor;
};

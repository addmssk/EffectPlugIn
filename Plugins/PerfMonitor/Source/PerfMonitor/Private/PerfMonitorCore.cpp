// PerfMonitorCore.cpp
//
// See PerfMonitorCore.h for the architecture overview.
// Design doc: Artifacts/realtime_platform_metrics_plan.md

#include "PerfMonitorCore.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#if WITH_APPLICATION_CORE
#include "HAL/PlatformApplicationMisc.h"
#endif

#if PLATFORM_ANDROID
#include "Android/AndroidPlatformThermal.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogPerfMonitor, Log, All);

/**************************************************************************************************
*
*   Console variables
*
***/

// Devices refresh thermal data every ~7-10s and FAndroidPlatformThermal already rate-limits
// its JNI call to 1/s, so sampling faster than this is waste. (AndroidPlatformThermal.cpp:36-42)
static float GPerfMonitorThermalIntervalSec = 1.0f;
static FAutoConsoleVariableRef CVarPerfMonitorThermalInterval(
	TEXT("perf.Monitor.ThermalSampleIntervalSec"),
	GPerfMonitorThermalIntervalSec,
	TEXT("Interval for polling device temperature / thermal state."),
	ECVF_Default);

// FPlatformMemory::GetStats parses /proc on Android; never call it per frame.
static float GPerfMonitorMemoryIntervalSec = 2.0f;
static FAutoConsoleVariableRef CVarPerfMonitorMemoryInterval(
	TEXT("perf.Monitor.MemorySampleIntervalSec"),
	GPerfMonitorMemoryIntervalSec,
	TEXT("Interval for polling platform memory stats."),
	ECVF_Default);

// Battery level is 1% granular; session start/stop snapshots cover drain-rate needs.
static float GPerfMonitorBatteryIntervalSec = 30.0f;
static FAutoConsoleVariableRef CVarPerfMonitorBatteryInterval(
	TEXT("perf.Monitor.BatterySampleIntervalSec"),
	GPerfMonitorBatteryIntervalSec,
	TEXT("Interval for polling battery level / power source."),
	ECVF_Default);

static int32 GPerfMonitorWarmupFrames = 8;
static FAutoConsoleVariableRef CVarPerfMonitorWarmupFrames(
	TEXT("perf.Monitor.WarmupFrames"),
	GPerfMonitorWarmupFrames,
	TEXT("Frames to skip after a reset (level load spikes, foreground transitions)."),
	ECVF_Default);

/**************************************************************************************************
*
*   Built-in metric ids
*
***/

namespace PerfMetricNames
{
	const FName FrameTimeMs(TEXT("Frame.TimeMs"));
	const FName GameThreadMs(TEXT("Frame.GameThreadMs"));
	const FName RenderThreadMs(TEXT("Frame.RenderThreadMs"));
	const FName RHIThreadMs(TEXT("Frame.RHIThreadMs"));
	const FName GPUMs(TEXT("Frame.GPUMs"));
	const FName IdleMs(TEXT("Frame.IdleMs"));
	const FName DynResPercent(TEXT("Frame.DynResPercent"));
	const FName Hitch(TEXT("Frame.Hitch"));
	const FName GameThreadBound(TEXT("Frame.GameThreadBound"));
	const FName RenderThreadBound(TEXT("Frame.RenderThreadBound"));
	const FName RHIThreadBound(TEXT("Frame.RHIThreadBound"));
	const FName GPUBound(TEXT("Frame.GPUBound"));
	const FName FlushAsyncLoadMs(TEXT("Frame.FlushAsyncLoadMs"));
	const FName SyncLoadCount(TEXT("Frame.SyncLoadCount"));

	const FName DeviceTemperatureC(TEXT("Platform.DeviceTemperatureC"));
	const FName ThermalState(TEXT("Platform.ThermalState"));
	const FName ThermalStress10s(TEXT("Platform.ThermalStress10s"));
	const FName BatteryLevel(TEXT("Platform.BatteryLevel"));
	const FName OnBattery(TEXT("Platform.OnBattery"));
	const FName UsedPhysicalMB(TEXT("Platform.UsedPhysicalMB"));
	const FName AvailablePhysicalMB(TEXT("Platform.AvailablePhysicalMB"));
	const FName MemoryPressure(TEXT("Platform.MemoryPressure"));

	const FName TemperatureSeverityEvent(TEXT("Event.TemperatureSeverity"));
	const FName LowPowerModeEvent(TEXT("Event.LowPowerMode"));
	const FName MemoryTrimEvent(TEXT("Event.MemoryTrim"));
}

/**************************************************************************************************
*
*   FPerfMetricSeries
*
***/

double FPerfMetricSeries::Latest(double Default) const
{
	FPerfSample Sample;
	return TryGetRecent(0, Sample) ? Sample.Value : Default;
}

FPerfMetricSeries::FWindowStats FPerfMetricSeries::GetStats(int32 MaxSamples, double MaxAgeSeconds, double Now) const
{
	FWindowStats Stats;
	double Sum = 0.0;

	const int32 NumToScan = FMath::Min(MaxSamples, Num());
	for (int32 Age = 0; Age < NumToScan; ++Age)
	{
		FPerfSample Sample;
		if (!TryGetRecent(Age, Sample))
		{
			break;
		}
		if (MaxAgeSeconds > 0.0 && (Now - Sample.Timestamp) > MaxAgeSeconds)
		{
			break;
		}

		if (Stats.Num == 0)
		{
			Stats.Min = Sample.Value;
			Stats.Max = Sample.Value;
		}
		else
		{
			Stats.Min = FMath::Min(Stats.Min, Sample.Value);
			Stats.Max = FMath::Max(Stats.Max, Sample.Value);
		}
		Sum += Sample.Value;
		++Stats.Num;
	}

	if (Stats.Num > 0)
	{
		Stats.Avg = Sum / Stats.Num;
	}
	return Stats;
}

int32 FPerfMetricSeries::NumConsecutive(TFunctionRef<bool(double)> Pred) const
{
	int32 Num = 0;
	FPerfSample Sample;
	while (TryGetRecent(Num, Sample) && Pred(Sample.Value))
	{
		++Num;
	}
	return Num;
}

/**************************************************************************************************
*
*   FPerfMetricStore
*
***/

FPerfMetricSeries& FPerfMetricStore::FindOrRegister(FName Id, int32 Capacity)
{
	if (TUniquePtr<FPerfMetricSeries>* Existing = SeriesMap.Find(Id))
	{
		return **Existing;
	}
	return *SeriesMap.Emplace(Id, MakeUnique<FPerfMetricSeries>(Capacity));
}

void FPerfMetricStore::Record(FName Id, double Value, double Timestamp)
{
	FindOrRegister(Id, DefaultCapacity).Add(Value, Timestamp);
}

const FPerfMetricSeries* FPerfMetricStore::Find(FName Id) const
{
	const TUniquePtr<FPerfMetricSeries>* Found = SeriesMap.Find(Id);
	return Found ? Found->Get() : nullptr;
}

double FPerfMetricStore::GetLatest(FName Id, double Default) const
{
	const FPerfMetricSeries* Series = Find(Id);
	return Series ? Series->Latest(Default) : Default;
}

void FPerfMetricStore::ResetAll()
{
	for (auto& Pair : SeriesMap)
	{
		Pair.Value->Reset();
	}
}

/**************************************************************************************************
*
*   FPerfControlledParam
*
***/

FPerfControlledParam::FPerfControlledParam(FName InName, double InBaseline)
	: Name(InName)
	, Baseline(InBaseline)
	, CachedEffective(InBaseline)
	, BindingSource(NAME_None)
	, History(MaxHistory)
{
}

void FPerfControlledParam::SetBaseline(double NewBaseline, double Timestamp)
{
	if (Baseline == NewBaseline)
	{
		return;
	}
	Baseline = NewBaseline;
	Recompute(NAME_None, FAuditEntry::EAction::Baseline, Timestamp);
}

void FPerfControlledParam::Assert(const FPerfConstraint& Constraint)
{
	// Re-asserting from the same source updates in place; the set of causes stays deduplicated.
	Active.Add(Constraint.SourceId, Constraint);
	Recompute(Constraint.SourceId, FAuditEntry::EAction::Assert, Constraint.Timestamp);
}

void FPerfControlledParam::Retract(FName SourceId, double Timestamp)
{
	if (Active.Remove(SourceId) > 0)
	{
		Recompute(SourceId, FAuditEntry::EAction::Retract, Timestamp);
	}
}

void FPerfControlledParam::RetractAll(double Timestamp)
{
	if (Active.Num() > 0)
	{
		Active.Empty();
		Recompute(NAME_None, FAuditEntry::EAction::Retract, Timestamp);
	}
}

const FPerfConstraint* FPerfControlledParam::GetBindingConstraint() const
{
	return (BindingSource != NAME_None) ? Active.Find(BindingSource) : nullptr;
}

uint32 FPerfControlledParam::GetActiveReasonFlags() const
{
	uint32 Flags = EPerfReason::None;
	for (const auto& Pair : Active)
	{
		Flags |= Pair.Value.ReasonFlags;
	}
	return Flags;
}

void FPerfControlledParam::Recompute(FName CauseSource, FAuditEntry::EAction Action, double Timestamp)
{
	const double OldEffective = CachedEffective;

	double NewEffective = Baseline;
	FName NewBinding = NAME_None;

	// A Force overrides everything; among Forces the highest Priority wins, ties go to the latest.
	// (Same idea as the cvar ECVF_SetBy priority ladder.)
	const FPerfConstraint* WinningForce = nullptr;
	for (const auto& Pair : Active)
	{
		const FPerfConstraint& Constraint = Pair.Value;
		if (Constraint.Op != EPerfConstraintOp::Force)
		{
			continue;
		}
		if (!WinningForce
			|| Constraint.Priority > WinningForce->Priority
			|| (Constraint.Priority == WinningForce->Priority && Constraint.Timestamp > WinningForce->Timestamp))
		{
			WinningForce = &Constraint;
		}
	}

	if (WinningForce)
	{
		NewEffective = WinningForce->Value;
		NewBinding = WinningForce->SourceId;
	}
	else
	{
		// Fold caps (min) then floors (max); a floor wins a cap-floor conflict.
		for (const auto& Pair : Active)
		{
			const FPerfConstraint& Constraint = Pair.Value;
			if (Constraint.Op == EPerfConstraintOp::Cap && Constraint.Value < NewEffective)
			{
				NewEffective = Constraint.Value;
				NewBinding = Constraint.SourceId;
			}
		}
		for (const auto& Pair : Active)
		{
			const FPerfConstraint& Constraint = Pair.Value;
			if (Constraint.Op == EPerfConstraintOp::Floor && Constraint.Value > NewEffective)
			{
				NewEffective = Constraint.Value;
				NewBinding = Constraint.SourceId;
			}
		}
	}

	CachedEffective = NewEffective;
	BindingSource = NewBinding;

	// Audit every ledger operation, including no-op ones: an assert that did not move the
	// value still records that a cause was held while another constraint dominated.
	FAuditEntry Entry;
	Entry.Timestamp = Timestamp;
	Entry.SourceId = CauseSource;
	Entry.Action = Action;
	Entry.OldEffective = OldEffective;
	Entry.NewEffective = NewEffective;
	History.Add(Entry);

	if (NewEffective != OldEffective)
	{
		OnChanged.ExecuteIfBound(NewEffective, *this);
	}
}

/**************************************************************************************************
*
*   FPerfThresholdRule
*
***/

FPerfThresholdRule::FPerfThresholdRule(const FConfig& InConfig)
	: Config(InConfig)
{
}

void FPerfThresholdRule::Evaluate(FPerfMonitorBase& Monitor, double Now)
{
	if (!Config.TriggerPred)
	{
		return;
	}

	const FPerfMetricSeries* Series = Monitor.GetMetrics().Find(Config.MetricId);
	if (!Series || !Series->HasSample())
	{
		return;
	}

	if (Config.TriggerPred(Series->Latest()))
	{
		LastSatisfiedTime = Now;
	}

	if (!bAsserted)
	{
		// Enter fast: N consecutive samples over threshold.
		if (Series->NumConsecutive(Config.TriggerPred) >= Config.TriggerConsecutiveSamples)
		{
			FPerfConstraint Constraint;
			Constraint.SourceId = Config.SourceId;
			Constraint.ReasonFlags = Config.ReasonFlags;
			Constraint.Op = Config.Op;
			Constraint.Value = Config.ConstrainedValue;
			Constraint.Priority = Config.Priority;
			Constraint.Timestamp = Now;
			Monitor.AssertConstraint(Config.ParamName, Constraint);
			bAsserted = true;
		}
	}
	else if ((Now - LastSatisfiedTime) >= Config.ReleaseCooldownSeconds)
	{
		// Recover slow: predicate must stay false for the whole cooldown.
		Monitor.RetractConstraint(Config.ParamName, Config.SourceId);
		bAsserted = false;
	}
}

void FPerfThresholdRule::OnReset(FPerfMonitorBase& Monitor, double Now)
{
	// Metric history was just cleared. Keep the constraint but restart the cooldown clock,
	// otherwise a long background stay would retract on the first foreground frame.
	if (bAsserted)
	{
		LastSatisfiedTime = Now;
	}
}

/**************************************************************************************************
*
*   Shared frame consumer
*
*   One IPerformanceDataConsumer registered with the engine. Monitors register per
*   GameInstance; exactly one is expected in a real game. Multiple live monitors only
*   happen with multi-client PIE in the editor, where the clients share one process and
*   metrics / constraint actions would tangle - so processing is suspended (registration
*   stays) while more than one monitor is registered.
*
***/

class FPerfMonitorFrameConsumer final : public IPerformanceDataConsumer
{
public:
	virtual void StartCharting() override {}
	virtual void StopCharting() override {}
	virtual void ProcessFrame(const FFrameData& FrameData) override
	{
		if (Monitors.Num() != 1)
		{
			bSuspended |= (Monitors.Num() > 1);
			return;
		}
		if (bSuspended)
		{
			// Resuming after a multi-instance window: the surviving monitor's history is stale.
			Monitors[0]->ResetTransientState();
			bSuspended = false;
		}
		Monitors[0]->ProcessFrame(FrameData);
	}

	TArray<FPerfMonitorBase*> Monitors;
	bool bSuspended = false;
};

static TSharedPtr<FPerfMonitorFrameConsumer> GPerfMonitorFrameConsumer;

static void RegisterMonitorWithEngine(FPerfMonitorBase* Monitor)
{
	if (!GPerfMonitorFrameConsumer.IsValid())
	{
		if (!GEngine)
		{
			UE_LOG(LogPerfMonitor, Warning, TEXT("Initialize called before GEngine exists; frame data will not flow."));
			return;
		}
		GPerfMonitorFrameConsumer = MakeShared<FPerfMonitorFrameConsumer>();
		GEngine->AddPerformanceDataConsumer(GPerfMonitorFrameConsumer);
	}
	GPerfMonitorFrameConsumer->Monitors.AddUnique(Monitor);
}

static void UnregisterMonitorWithEngine(FPerfMonitorBase* Monitor)
{
	if (!GPerfMonitorFrameConsumer.IsValid())
	{
		return;
	}
	GPerfMonitorFrameConsumer->Monitors.Remove(Monitor);
	if (GPerfMonitorFrameConsumer->Monitors.Num() == 0)
	{
		if (GEngine)
		{
			GEngine->RemovePerformanceDataConsumer(GPerfMonitorFrameConsumer);
		}
		GPerfMonitorFrameConsumer.Reset();
	}
}

/**************************************************************************************************
*
*   FPerfMonitorBase
*
***/

FPerfMonitorBase::FPerfMonitorBase() = default;

FPerfMonitorBase::~FPerfMonitorBase()
{
	Shutdown();
}

void FPerfMonitorBase::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	RegisterMetrics();
	RegisterParams();
	RegisterRules();

	OnTemperatureChangeHandle = FCoreDelegates::OnTemperatureChange.AddRaw(this, &FPerfMonitorBase::HandleTemperatureChange);
	OnLowPowerModeHandle = FCoreDelegates::OnLowPowerMode.AddRaw(this, &FPerfMonitorBase::HandleLowPowerMode);
	OnMemoryTrimHandle = FCoreDelegates::GetMemoryTrimDelegate().AddRaw(this, &FPerfMonitorBase::HandleMemoryTrim);
	OnBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FPerfMonitorBase::HandleAppWillEnterBackground);
	OnForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FPerfMonitorBase::HandleAppHasEnteredForeground);

	RegisterMonitorWithEngine(this);
	ResetTransientState();
}

void FPerfMonitorBase::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}
	bInitialized = false;

	UnregisterMonitorWithEngine(this);

	FCoreDelegates::OnTemperatureChange.Remove(OnTemperatureChangeHandle);
	FCoreDelegates::OnLowPowerMode.Remove(OnLowPowerModeHandle);
	FCoreDelegates::GetMemoryTrimDelegate().Remove(OnMemoryTrimHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(OnBackgroundHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(OnForegroundHandle);
}

void FPerfMonitorBase::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}
	bEnabled = bInEnabled;
	if (bEnabled)
	{
		ResetTransientState();
	}
	// Disabling stops evaluation but keeps asserted constraints: the last decided values
	// (and their recorded causes) remain queryable and applied.
}

FPerfControlledParam& FPerfMonitorBase::FindOrRegisterParam(FName Name, double Baseline)
{
	if (TUniquePtr<FPerfControlledParam>* Existing = Params.Find(Name))
	{
		return **Existing;
	}
	return *Params.Emplace(Name, MakeUnique<FPerfControlledParam>(Name, Baseline));
}

FPerfControlledParam* FPerfMonitorBase::FindParam(FName Name)
{
	TUniquePtr<FPerfControlledParam>* Found = Params.Find(Name);
	return Found ? Found->Get() : nullptr;
}

void FPerfMonitorBase::AddRule(TUniquePtr<IPerfRule> Rule)
{
	if (Rule.IsValid())
	{
		Rules.Add(MoveTemp(Rule));
	}
}

void FPerfMonitorBase::AssertConstraint(FName ParamName, FPerfConstraint Constraint)
{
	FPerfControlledParam* Param = FindParam(ParamName);
	if (!Param)
	{
		UE_LOG(LogPerfMonitor, Warning, TEXT("AssertConstraint: unknown param '%s' (source '%s')"),
			*ParamName.ToString(), *Constraint.SourceId.ToString());
		return;
	}
	if (Constraint.Timestamp == 0.0)
	{
		Constraint.Timestamp = FPlatformTime::Seconds();
	}
	Param->Assert(Constraint);
}

void FPerfMonitorBase::RetractConstraint(FName ParamName, FName SourceId)
{
	if (FPerfControlledParam* Param = FindParam(ParamName))
	{
		Param->Retract(SourceId, FPlatformTime::Seconds());
	}
}

void FPerfMonitorBase::ResetTransientState()
{
	const double Now = FPlatformTime::Seconds();
	Metrics.ResetAll();
	WarmupFramesRemaining = FMath::Max(0, GPerfMonitorWarmupFrames);
	LastThermalSampleTime = TNumericLimits<double>::Lowest();
	LastMemorySampleTime = TNumericLimits<double>::Lowest();
	LastBatterySampleTime = TNumericLimits<double>::Lowest();
	for (TUniquePtr<IPerfRule>& Rule : Rules)
	{
		Rule->OnReset(*this, Now);
	}
}

void FPerfMonitorBase::ProcessFrame(const IPerformanceDataConsumer::FFrameData& FrameData)
{
	if (!bEnabled)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();

	// Events are drained even while gated so severity transitions are never lost.
	PumpEvents(Now);

	const bool bCollecting = IsApplicationForeground() && ShouldCollect();
	if (!bCollecting)
	{
		if (bWasCollecting)
		{
			ResetTransientState();
			bWasCollecting = false;
		}
		return;
	}
	bWasCollecting = true;

	if (WarmupFramesRemaining > 0)
	{
		--WarmupFramesRemaining;
		return;
	}

	RecordFrameMetrics(FrameData, Now);
	SamplePlatformMetrics(Now);
	CollectProjectMetrics(Now);

	for (TUniquePtr<IPerfRule>& Rule : Rules)
	{
		Rule->Evaluate(*this, Now);
	}

	OnFrameProcessed(FrameData);
}

void FPerfMonitorBase::RecordFrameMetrics(const IPerformanceDataConsumer::FFrameData& FrameData, double Now)
{
	using namespace PerfMetricNames;

	Metrics.Record(FrameTimeMs, FrameData.TrueDeltaSeconds * 1000.0, Now);
	Metrics.Record(GameThreadMs, FrameData.GameThreadTimeSeconds * 1000.0, Now);
	Metrics.Record(RenderThreadMs, FrameData.RenderThreadTimeSeconds * 1000.0, Now);
	Metrics.Record(RHIThreadMs, FrameData.RHIThreadTimeSeconds * 1000.0, Now);
	Metrics.Record(IdleMs, FrameData.IdleSeconds * 1000.0, Now);

	// GPU timing can be unavailable early on some RHIs; keep the series gap instead of a fake 0.
	if (FrameData.GPUTimeSeconds > 0.0)
	{
		Metrics.Record(GPUMs, FrameData.GPUTimeSeconds * 1000.0, Now);
	}

	Metrics.Record(DynResPercent, FrameData.DynamicResolutionScreenPercentage, Now);
	Metrics.Record(Hitch, (FrameData.HitchStatus != EFrameHitchType::NoHitch) ? 1.0 : 0.0, Now);

	// Engine's own bound judgements (FEnginePerformanceTargets thresholds) - free cross-check
	// against any custom detection a project adds on the raw thread times.
	Metrics.Record(GameThreadBound, FrameData.bGameThreadBound ? 1.0 : 0.0, Now);
	Metrics.Record(RenderThreadBound, FrameData.bRenderThreadBound ? 1.0 : 0.0, Now);
	Metrics.Record(RHIThreadBound, FrameData.bRHIThreadBound ? 1.0 : 0.0, Now);
	Metrics.Record(GPUBound, FrameData.bGPUBound ? 1.0 : 0.0, Now);

	// Loading pressure - lets rules tell a loading hitch from a real perf regression.
	Metrics.Record(FlushAsyncLoadMs, FrameData.FlushAsyncLoadingTime * 1000.0, Now);
	Metrics.Record(SyncLoadCount, (double)FrameData.SyncLoadCount, Now);
}

void FPerfMonitorBase::SamplePlatformMetrics(double Now)
{
	using namespace PerfMetricNames;

	// Unsupported values (-1 / EDeviceThermalState::Unsupported) are simply not recorded;
	// consumers see an absent/stale series instead of sentinel values.

	if (GPerfMonitorThermalIntervalSec > 0.0f && (Now - LastThermalSampleTime) >= GPerfMonitorThermalIntervalSec)
	{
		LastThermalSampleTime = Now;

		// Android: battery temperature (not SoC) - compare trends, not absolutes. iOS: -1.
		const float TemperatureC = FPlatformMisc::GetDeviceTemperature();
		if (TemperatureC >= 0.0f)
		{
			Metrics.Record(DeviceTemperatureC, TemperatureC, Now);
		}

		const EDeviceThermalState State = FPlatformMisc::GetDeviceThermalState();
		if (State != EDeviceThermalState::Unsupported)
		{
			Metrics.Record(ThermalState, (double)(int32)State, Now);
		}

#if PLATFORM_ANDROID
		// ADPF thermal headroom forecast; >= 1.0 means the current workload cannot be
		// sustained. The engine impl rate-limits and NaN-filters internally.
		const float Stress = FAndroidPlatformThermal::GetThermalStress(FAndroidPlatformThermal::TEN_SEC);
		if (Stress >= 0.0f)
		{
			Metrics.Record(ThermalStress10s, Stress, Now);
		}
#endif
	}

	if (GPerfMonitorMemoryIntervalSec > 0.0f && (Now - LastMemorySampleTime) >= GPerfMonitorMemoryIntervalSec)
	{
		LastMemorySampleTime = Now;

		const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		constexpr double BytesToMB = 1.0 / (1024.0 * 1024.0);
		Metrics.Record(UsedPhysicalMB, (double)Stats.UsedPhysical * BytesToMB, Now);
		Metrics.Record(AvailablePhysicalMB, (double)Stats.AvailablePhysical * BytesToMB, Now);
		Metrics.Record(MemoryPressure, (double)(int32)Stats.GetMemoryPressureStatus(), Now);
	}

	if (GPerfMonitorBatteryIntervalSec > 0.0f && (Now - LastBatterySampleTime) >= GPerfMonitorBatteryIntervalSec)
	{
		LastBatterySampleTime = Now;

		const int32 Level = FPlatformMisc::GetBatteryLevel();
		if (Level >= 0)
		{
			Metrics.Record(BatteryLevel, (double)Level, Now);
		}
		Metrics.Record(OnBattery, FPlatformMisc::IsRunningOnBattery() ? 1.0 : 0.0, Now);
	}
}

void FPerfMonitorBase::PumpEvents(double Now)
{
	using namespace PerfMetricNames;

	FPendingEvent Event;
	while (PendingEvents.Dequeue(Event))
	{
		switch (Event.Type)
		{
		case EPerfEventType::TemperatureSeverity:
			Metrics.Record(TemperatureSeverityEvent, (double)Event.Payload, Now);
			OnTemperatureSeverityChanged((FCoreDelegates::ETemperatureSeverity)Event.Payload);
			break;

		case EPerfEventType::LowPowerMode:
			Metrics.Record(LowPowerModeEvent, (double)Event.Payload, Now);
			OnLowPowerModeChanged(Event.Payload != 0);
			break;

		case EPerfEventType::MemoryTrim:
			++MemoryTrimCount;
			Metrics.Record(MemoryTrimEvent, (double)MemoryTrimCount, Now);
			OnMemoryTrim();
			break;
		}
	}
}

bool FPerfMonitorBase::IsApplicationForeground() const
{
#if PLATFORM_DESKTOP && WITH_APPLICATION_CORE
	return FPlatformApplicationMisc::IsThisApplicationForeground();
#else
	return bIsAppForeground.load(std::memory_order_relaxed);
#endif
}

// ---- delegate handlers: broadcast thread is not guaranteed, so enqueue only ----

void FPerfMonitorBase::HandleTemperatureChange(FCoreDelegates::ETemperatureSeverity Severity)
{
	PendingEvents.Enqueue({ EPerfEventType::TemperatureSeverity, (int32)Severity });
}

void FPerfMonitorBase::HandleLowPowerMode(bool bLowPower)
{
	PendingEvents.Enqueue({ EPerfEventType::LowPowerMode, bLowPower ? 1 : 0 });
}

void FPerfMonitorBase::HandleMemoryTrim()
{
	PendingEvents.Enqueue({ EPerfEventType::MemoryTrim, 0 });
}

void FPerfMonitorBase::HandleAppWillEnterBackground()
{
	bIsAppForeground.store(false, std::memory_order_relaxed);
}

void FPerfMonitorBase::HandleAppHasEnteredForeground()
{
	bIsAppForeground.store(true, std::memory_order_relaxed);
}

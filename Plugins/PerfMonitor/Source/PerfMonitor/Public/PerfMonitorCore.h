// PerfMonitorCore.h
//
// Project-agnostic base for a realtime performance / platform-metric monitoring system.
// Design doc: Artifacts/realtime_platform_metrics_plan.md
//
// Layers:
//   [Collectors]  FrameCollector (IPerformanceDataConsumer, per frame)
//                 PlatformSampler (thermal/battery/memory, per-interval polling)
//                 EventListener  (FCoreDelegates events, queued to game thread)
//   [Store]       FPerfMetricStore - per-metric ring buffers, window stats
//   [Ledger]      FPerfControlledParam - constraint-based value control with provenance
//   [Rules]       IPerfRule / FPerfThresholdRule - condition -> constraint assert/retract
//   [Base]        FPerfMonitorBase - owns everything; projects derive and register
//                 their own params/rules/metrics. No project-specific logic lives here.
//
// Threading: everything is consumed on the game thread. Platform event delegates may
// fire on other threads; they only enqueue into an MPSC queue drained per frame.
//
// -------------------------------------------------------------------------------------
// Usage example (project side):
//
//   class FMyGamePerfMonitor : public FPerfMonitorBase
//   {
//   protected:
//       virtual void RegisterParams() override
//       {
//           FPerfControlledParam& MatQuality = FindOrRegisterParam(TEXT("MaterialQuality"), /*Baseline=*/3.0);
//           MatQuality.OnChanged.BindLambda([](double NewValue, const FPerfControlledParam& Param)
//           {
//               // Apply. If this drives a cvar also touched by Scalability, mind ECVF_SetBy priority.
//               ApplyMaterialQuality((int32)NewValue);
//               if (const FPerfConstraint* Why = Param.GetBindingConstraint())
//               {
//                   UE_LOG(LogTemp, Log, TEXT("MaterialQuality=%d because %s"), (int32)NewValue, *Why->SourceId.ToString());
//               }
//           });
//       }
//
//       virtual void RegisterRules() override
//       {
//           FPerfThresholdRule::FConfig Cfg;
//           Cfg.SourceId                  = TEXT("Thermal.Severe");
//           Cfg.MetricId                  = PerfMetricNames::ThermalState;
//           Cfg.ParamName                 = TEXT("MaterialQuality");
//           Cfg.Op                        = EPerfConstraintOp::Cap;
//           Cfg.ConstrainedValue          = 1.0;
//           Cfg.ReasonFlags               = EPerfReason::Thermal;
//           Cfg.TriggerPred               = [](double V) { return V >= (double)EDeviceThermalState::Severe; };
//           Cfg.TriggerConsecutiveSamples = 2;      // thermal samples arrive ~1/s
//           Cfg.ReleaseCooldownSeconds    = 120.0;  // recover slowly
//           AddRule(MakeUnique<FPerfThresholdRule>(Cfg));
//       }
//
//       virtual bool ShouldCollect() const override { return /* project gates, e.g. actor count */ true; }
//   };
// -------------------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "ChartCreation.h"                          // IPerformanceDataConsumer
#include "Containers/Queue.h"
#include "GenericPlatform/GenericPlatformMisc.h"    // EDeviceThermalState
#include "Misc/CoreDelegates.h"

#include <atomic>

/**************************************************************************************************
*
*   Built-in metric ids
*
***/

namespace PerfMetricNames
{
	// Per-frame (from IPerformanceDataConsumer::FFrameData)
	extern PERFMONITOR_API const FName FrameTimeMs;
	extern PERFMONITOR_API const FName GameThreadMs;
	extern PERFMONITOR_API const FName RenderThreadMs;
	extern PERFMONITOR_API const FName RHIThreadMs;
	extern PERFMONITOR_API const FName GPUMs;
	extern PERFMONITOR_API const FName IdleMs;
	extern PERFMONITOR_API const FName DynResPercent;       // 0 when dynamic resolution is disabled
	extern PERFMONITOR_API const FName Hitch;               // 0/1, engine's EFrameHitchType judgement
	extern PERFMONITOR_API const FName GameThreadBound;     // 0/1, engine's FEnginePerformanceTargets judgement
	extern PERFMONITOR_API const FName RenderThreadBound;   // 0/1
	extern PERFMONITOR_API const FName RHIThreadBound;      // 0/1
	extern PERFMONITOR_API const FName GPUBound;            // 0/1
	extern PERFMONITOR_API const FName FlushAsyncLoadMs;
	extern PERFMONITOR_API const FName SyncLoadCount;

	// Periodic platform samples (recorded only when the platform supports them)
	extern PERFMONITOR_API const FName DeviceTemperatureC;  // Android: battery temperature. iOS: unsupported.
	extern PERFMONITOR_API const FName ThermalState;        // (int)EDeviceThermalState
	extern PERFMONITOR_API const FName ThermalStress10s;    // Android ADPF thermal headroom forecast, >=1.0 means throttling imminent
	extern PERFMONITOR_API const FName BatteryLevel;        // [0,100]
	extern PERFMONITOR_API const FName OnBattery;           // 0/1
	extern PERFMONITOR_API const FName UsedPhysicalMB;
	extern PERFMONITOR_API const FName AvailablePhysicalMB;
	extern PERFMONITOR_API const FName MemoryPressure;      // (int)FGenericPlatformMemoryStats::EMemoryPressureStatus

	// Events (recorded when the corresponding delegate fires)
	extern PERFMONITOR_API const FName TemperatureSeverityEvent; // (int)FCoreDelegates::ETemperatureSeverity
	extern PERFMONITOR_API const FName LowPowerModeEvent;        // 0/1
	extern PERFMONITOR_API const FName MemoryTrimEvent;          // running count
}

/**************************************************************************************************
*
*   FPerfMetricSeries / FPerfMetricStore
*
***/

struct FPerfSample
{
	double Value = 0.0;
	double Timestamp = 0.0;
};

/**
 * Fixed-capacity ring buffer. Age 0 == most recent. Appends are O(1) regardless of how
 * often the producer writes (metric samples per frame, ledger audits per assert).
 * Shared by FPerfMetricSeries and the FPerfControlledParam audit history.
 */
template <typename T>
class TPerfRingBuffer
{
public:
	explicit TPerfRingBuffer(int32 InCapacity)
	{
		Buffer.SetNum(FMath::Max(2, InCapacity));
	}

	// Explicitly copyable and movable so enclosing types (metric series, ledger)
	// keep their implicit copy/move operations available.
	TPerfRingBuffer(const TPerfRingBuffer&) = default;
	TPerfRingBuffer(TPerfRingBuffer&&) = default;
	TPerfRingBuffer& operator=(const TPerfRingBuffer&) = default;
	TPerfRingBuffer& operator=(TPerfRingBuffer&&) = default;

	void Add(const T& Item)
	{
		Head = (Head + 1) % Buffer.Num();
		Buffer[Head] = Item;
		Count = FMath::Min(Count + 1, Buffer.Num());
	}

	void Reset()
	{
		Head = -1;
		Count = 0;
	}

	int32 Num() const { return Count; }

	/** Returns false when Age is outside the recorded range (never reads unwritten slots). */
	bool TryGetRecent(int32 Age, T& Out) const
	{
		if (Age < 0 || Age >= Count)
		{
			return false;
		}
		Out = Buffer[(Head - Age + Buffer.Num()) % Buffer.Num()];
		return true;
	}

private:
	TArray<T> Buffer;
	int32 Head = -1;   // index of most recent item
	int32 Count = 0;
};

/** Ring buffer of timestamped samples plus window-stat queries. Age 0 == most recent. */
class PERFMONITOR_API FPerfMetricSeries
{
public:
	explicit FPerfMetricSeries(int32 InCapacity)
		: Samples(InCapacity)
	{
	}

	void Add(double Value, double Timestamp) { Samples.Add({ Value, Timestamp }); }
	void Reset() { Samples.Reset(); }

	int32 Num() const { return Samples.Num(); }
	bool HasSample() const { return Samples.Num() > 0; }

	bool TryGetRecent(int32 Age, FPerfSample& Out) const { return Samples.TryGetRecent(Age, Out); }
	double Latest(double Default = 0.0) const;

	struct FWindowStats
	{
		double Avg = 0.0;
		double Min = 0.0;
		double Max = 0.0;
		int32 Num = 0;
	};
	/** Stats over up to MaxSamples recent samples no older than MaxAgeSeconds (<=0 disables the age filter). */
	FWindowStats GetStats(int32 MaxSamples, double MaxAgeSeconds, double Now) const;

	/** Number of consecutive most-recent samples satisfying Pred. */
	int32 NumConsecutive(TFunctionRef<bool(double)> Pred) const;

private:
	TPerfRingBuffer<FPerfSample> Samples;
};

/** Registry of metric series keyed by id. Auto-registers on first Record. */
class PERFMONITOR_API FPerfMetricStore
{
public:
	FPerfMetricStore() = default;
	FPerfMetricStore(FPerfMetricStore&&) = default;
	FPerfMetricStore& operator=(FPerfMetricStore&&) = default;

	// TUniquePtr values make the store move-only. Copies are deleted here explicitly
	// so a copying use site fails on FPerfMetricStore itself instead of deep inside
	// Map.h.inl's TMap copy-assign instantiation.
	FPerfMetricStore(const FPerfMetricStore&) = delete;
	FPerfMetricStore& operator=(const FPerfMetricStore&) = delete;

	FPerfMetricSeries& FindOrRegister(FName Id, int32 Capacity);
	void Record(FName Id, double Value, double Timestamp);

	const FPerfMetricSeries* Find(FName Id) const;
	double GetLatest(FName Id, double Default = 0.0) const;

	/** Clears samples, keeps registrations. */
	void ResetAll();

	static constexpr int32 DefaultCapacity = 128;

private:
	TMap<FName, TUniquePtr<FPerfMetricSeries>> SeriesMap;
};

/**************************************************************************************************
*
*   Controlled-parameter ledger
*
*   Rules never Set values directly; they assert/retract tagged constraints. The effective
*   value is folded from the baseline plus all active constraints, so releasing one cause
*   recomputes correctly while others still hold, and "why is the value X" is answered by
*   the binding constraint. (Precedents: cvar ECVF_SetBy priority = value tagged with its
*   setter; Lyra GetEffectiveFrameRateLimit = min-fold of independent causes.)
*
***/

enum class EPerfConstraintOp : uint8
{
	Cap,    // effective <= Value
	Floor,  // effective >= Value
	Force,  // effective == Value (highest Priority wins; caps/floors ignored)
};

/** Base reason bits. Projects extend from ProjectBase upward. */
namespace EPerfReason
{
	enum Type : uint32
	{
		None      = 0,
		CPUBound  = 1u << 0,
		GPUBound  = 1u << 1,
		Thermal   = 1u << 2,
		Battery   = 1u << 3,
		Memory    = 1u << 4,
		Hitch     = 1u << 5,
		Loading   = 1u << 6,

		ProjectBase = 1u << 16,
	};
}

struct FPerfConstraint
{
	FName SourceId;                            // rule/system identifier, e.g. "Thermal.Severe"
	uint32 ReasonFlags = EPerfReason::None;    // cause bitmask, surfaced via GetActiveReasonFlags()
	EPerfConstraintOp Op = EPerfConstraintOp::Cap;
	double Value = 0.0;
	int32 Priority = 0;                        // Force-vs-Force conflict resolution
	double Timestamp = 0.0;                    // stamped by the monitor when 0
};

class PERFMONITOR_API FPerfControlledParam
{
public:
	DECLARE_DELEGATE_TwoParams(FOnEffectiveChanged, double /*NewEffective*/, const FPerfControlledParam&);

	FPerfControlledParam(FName InName, double InBaseline);

	FName GetName() const { return Name; }

	/** Call when the underlying user/device-profile setting changes. */
	void SetBaseline(double NewBaseline, double Timestamp);
	double GetBaseline() const { return Baseline; }

	void Assert(const FPerfConstraint& Constraint);
	void Retract(FName SourceId, double Timestamp);
	void RetractAll(double Timestamp);

	double GetEffective() const { return CachedEffective; }

	// ---- provenance queries ----
	/** The constraint that actually decided the current value; nullptr when the baseline decided it. */
	const FPerfConstraint* GetBindingConstraint() const;
	/** OR of all active causes - the "set of reasons" currently held, regardless of which one is binding. */
	uint32 GetActiveReasonFlags() const;
	const TMap<FName, FPerfConstraint>& GetActiveConstraints() const { return Active; }

	struct FAuditEntry
	{
		enum class EAction : uint8 { Assert, Retract, Baseline };
		double Timestamp = 0.0;
		FName SourceId;
		EAction Action = EAction::Assert;
		double OldEffective = 0.0;
		double NewEffective = 0.0;
	};
	int32 GetHistoryNum() const { return History.Num(); }
	/** Age 0 == most recent ledger operation, increasing Age walks back in time. */
	bool TryGetHistoryEntry(int32 Age, FAuditEntry& Out) const { return History.TryGetRecent(Age, Out); }

	/** Fired only when the effective value actually changes. Projects bind the apply-side here. */
	FOnEffectiveChanged OnChanged;

private:
	void Recompute(FName CauseSource, FAuditEntry::EAction Action, double Timestamp);

	FName Name;
	double Baseline;
	double CachedEffective;
	FName BindingSource;                       // NAME_None => baseline decided the value
	TMap<FName, FPerfConstraint> Active;       // the active "set of causes"; keyed by source so re-asserts update in place
	TPerfRingBuffer<FAuditEntry> History;

	static constexpr int32 MaxHistory = 64;
};

/**************************************************************************************************
*
*   Rules
*
***/

class FPerfMonitorBase;

class IPerfRule
{
public:
	virtual ~IPerfRule() = default;
	virtual FName GetSourceId() const = 0;
	/** Called once per frame on the game thread after metrics are updated. */
	virtual void Evaluate(FPerfMonitorBase& Monitor, double Now) = 0;
	/** Called when transient state resets (background/foreground, re-enable). */
	virtual void OnReset(FPerfMonitorBase& Monitor, double Now) {}
};

/**
 * Convenience rule: metric predicate held for N consecutive samples => assert constraint;
 * predicate false for ReleaseCooldownSeconds => retract. Entry-fast / recover-slow
 * hysteresis falls out of the two knobs.
 */
class PERFMONITOR_API FPerfThresholdRule : public IPerfRule
{
public:
	struct FConfig
	{
		FName SourceId;
		FName MetricId;
		FName ParamName;

		EPerfConstraintOp Op = EPerfConstraintOp::Cap;
		double ConstrainedValue = 0.0;
		uint32 ReasonFlags = EPerfReason::None;
		int32 Priority = 0;

		TFunction<bool(double)> TriggerPred;
		int32 TriggerConsecutiveSamples = 3;
		double ReleaseCooldownSeconds = 30.0;
	};

	explicit FPerfThresholdRule(const FConfig& InConfig);

	// IPerfRule
	virtual FName GetSourceId() const override { return Config.SourceId; }
	virtual void Evaluate(FPerfMonitorBase& Monitor, double Now) override;
	virtual void OnReset(FPerfMonitorBase& Monitor, double Now) override;

	bool IsAsserted() const { return bAsserted; }

private:
	FConfig Config;
	bool bAsserted = false;
	double LastSatisfiedTime = 0.0;
};

/**************************************************************************************************
*
*   FPerfMonitorBase
*
***/

class PERFMONITOR_API FPerfMonitorBase
{
public:
	FPerfMonitorBase();
	virtual ~FPerfMonitorBase();

	/** Registers the shared frame consumer and platform delegates, then calls the Register* hooks. */
	void Initialize();
	void Shutdown();

	void SetEnabled(bool bInEnabled);
	bool IsEnabled() const { return bEnabled; }

	FPerfMetricStore& GetMetrics() { return Metrics; }
	const FPerfMetricStore& GetMetrics() const { return Metrics; }

	FPerfControlledParam& FindOrRegisterParam(FName Name, double Baseline);
	FPerfControlledParam* FindParam(FName Name);

	void AddRule(TUniquePtr<IPerfRule> Rule);

	void AssertConstraint(FName ParamName, FPerfConstraint Constraint);
	void RetractConstraint(FName ParamName, FName SourceId);

	/** Clears metric history and rule state (constraints stay; rules re-evaluate on fresh data). */
	void ResetTransientState();

	/** Frame entry point; called by the shared consumer on the game thread. */
	void ProcessFrame(const IPerformanceDataConsumer::FFrameData& FrameData);

protected:
	// ---- project hooks ----
	virtual void RegisterMetrics() {}
	virtual void RegisterParams() = 0;
	virtual void RegisterRules() = 0;
	/** Extra project gates (e.g. minimum actor count). Collection pauses and history resets while false. */
	virtual bool ShouldCollect() const { return true; }
	/** Record project-specific metrics each frame. */
	virtual void CollectProjectMetrics(double Now) {}
	virtual void OnFrameProcessed(const IPerformanceDataConsumer::FFrameData& FrameData) {}
	virtual void OnTemperatureSeverityChanged(FCoreDelegates::ETemperatureSeverity Severity) {}
	virtual void OnLowPowerModeChanged(bool bLowPower) {}
	virtual void OnMemoryTrim() {}

private:
	void RecordFrameMetrics(const IPerformanceDataConsumer::FFrameData& FrameData, double Now);
	void SamplePlatformMetrics(double Now);
	void PumpEvents(double Now);
	bool IsApplicationForeground() const;

	// Platform delegate handlers (thread of broadcast is not guaranteed -> enqueue only)
	void HandleTemperatureChange(FCoreDelegates::ETemperatureSeverity Severity);
	void HandleLowPowerMode(bool bLowPower);
	void HandleMemoryTrim();
	void HandleAppWillEnterBackground();
	void HandleAppHasEnteredForeground();

	enum class EPerfEventType : uint8
	{
		TemperatureSeverity,
		LowPowerMode,
		MemoryTrim,
	};
	struct FPendingEvent
	{
		EPerfEventType Type = EPerfEventType::MemoryTrim;
		int32 Payload = 0;
	};

	FPerfMetricStore Metrics;
	TMap<FName, TUniquePtr<FPerfControlledParam>> Params;
	TArray<TUniquePtr<IPerfRule>> Rules;

	TQueue<FPendingEvent, EQueueMode::Mpsc> PendingEvents;
	std::atomic<bool> bIsAppForeground { true };

	bool bEnabled = true;
	bool bInitialized = false;
	bool bWasCollecting = false;
	int32 WarmupFramesRemaining = 0;
	int32 MemoryTrimCount = 0;

	double LastThermalSampleTime = TNumericLimits<double>::Lowest();
	double LastMemorySampleTime = TNumericLimits<double>::Lowest();
	double LastBatterySampleTime = TNumericLimits<double>::Lowest();

	FDelegateHandle OnTemperatureChangeHandle;
	FDelegateHandle OnLowPowerModeHandle;
	FDelegateHandle OnMemoryTrimHandle;
	FDelegateHandle OnBackgroundHandle;
	FDelegateHandle OnForegroundHandle;
};

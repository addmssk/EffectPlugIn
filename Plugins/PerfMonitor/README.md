# PerfMonitor

실시간 성능·플랫폼 지표 수집과 **원인 추적이 가능한 자동 품질 조절**을 위한 프로젝트 무관(base) Unreal Engine 플러그인.

엔진의 `IPerformanceDataConsumer` 프레임 데이터에 온도·배터리·메모리 등 플랫폼 특화 지표를 통합 수집하고, "성능 저하 → 옵션 다운" 류의 조치를 **제약 원장(constraint ledger)** 위에서 수행한다. 조절되고 있는 값(예: MaterialQuality)에 대해 *지금 이 값이 왜 이 값인지*를 언제든 조회할 수 있다.

> **지원 엔진 버전: UE 5.8 이상.**
> 5.8에서 정비된 열 API(`FPlatformMisc::GetDeviceTemperature()` / `GetDeviceThermalState()`)를 사용한다. 이전 버전에는 정의가 불명확한 `GetDeviceTemperatureLevel()`(5.8에서 deprecated)만 있으므로, 하위 버전 이식 시 `SamplePlatformMetrics()`의 열 샘플링 부분 수정이 필요하다.

## 특징

- **3계층 수집** — 매 프레임(엔진 프레임 데이터) / 주기 폴링(온도·배터리·메모리) / 이벤트(온도 등급 변화, 저전력 모드, 메모리 트림)
- **제약 원장** — 규칙이 값을 직접 Set하지 않고 원인 태그가 붙은 제약(Cap/Floor/Force)을 걸었다 풀었다 한다. 유효값은 활성 제약 집합에서 재계산되므로 복수 원인이 겹쳐도 복원 버그가 없고, 원인 조회가 구조의 부산물로 나온다
- **프로젝트 분리** — 무엇을 감지해 무엇을 조절할지는 전부 프로젝트 파생 클래스 몫. 플러그인에는 프로젝트 특화 로직이 없다
- **Shipping 안전** — 엔진 `STATS`/CSV 매크로에 의존하지 않는 자체 지표 저장소
- **PIE 멀티클라이언트 대응** — 한 프로세스에 GameInstance가 2개 이상이면(에디터 전용 상황) 등록만 유지하고 처리를 중단, 1개로 복귀 시 히스토리 리셋 후 재개

## 설치

1. 이 폴더를 프로젝트의 `Plugins/PerfMonitor`로 복사
2. `.uproject`에서 플러그인 활성화:
   ```json
   "Plugins": [ { "Name": "PerfMonitor", "Enabled": true } ]
   ```
3. 파생 클래스를 작성할 게임 모듈의 `Build.cs`에 의존성 추가:
   ```csharp
   PublicDependencyModuleNames.Add("PerfMonitor");
   ```

## 빠른 시작

### 1) 그대로 사용 — 수집만

플러그인을 켜면 `UPerfMonitorSubsystem`이 GameInstance마다 자동 생성되어 내장 지표를 수집한다. Blueprint/C++에서 바로 조회 가능:

```cpp
UPerfMonitorSubsystem* Perf = GetGameInstance()->GetSubsystem<UPerfMonitorSubsystem>();
double GameThreadMs = Perf->GetMetricAverage(PerfMetricNames::GameThreadMs, 1.0f);
double ThermalState = Perf->GetMetricLatest(PerfMetricNames::ThermalState);
```

### 2) 프로젝트 확장 — 감지와 조치

모니터를 파생해 제어 파라미터와 규칙을 등록하고, 서브시스템을 파생해 팩토리만 바꾼다:

```cpp
class FMyGamePerfMonitor : public FPerfMonitorBase
{
protected:
    virtual void RegisterParams() override
    {
        FPerfControlledParam& MatQuality = FindOrRegisterParam(TEXT("MaterialQuality"), /*Baseline=*/3.0);
        MatQuality.OnChanged.BindLambda([](double NewValue, const FPerfControlledParam& Param)
        {
            ApplyMaterialQuality((int32)NewValue);   // cvar 적용 시 ECVF_SetBy 우선순위 주의 (아래 참고)
        });
    }

    virtual void RegisterRules() override
    {
        FPerfThresholdRule::FConfig Cfg;
        Cfg.SourceId                  = TEXT("Thermal.Severe");
        Cfg.MetricId                  = PerfMetricNames::ThermalState;
        Cfg.ParamName                 = TEXT("MaterialQuality");
        Cfg.Op                        = EPerfConstraintOp::Cap;
        Cfg.ConstrainedValue          = 1.0;
        Cfg.ReasonFlags               = EPerfReason::Thermal;
        Cfg.TriggerPred               = [](double V) { return V >= (double)EDeviceThermalState::Severe; };
        Cfg.TriggerConsecutiveSamples = 2;      // 진입은 빠르게
        Cfg.ReleaseCooldownSeconds    = 120.0;  // 복귀는 느리게
        AddRule(MakeUnique<FPerfThresholdRule>(Cfg));
    }
};

UCLASS()
class UMyGamePerfSubsystem : public UPerfMonitorSubsystem
{
    GENERATED_BODY()
protected:
    virtual TUniquePtr<FPerfMonitorBase> CreateMonitor() override
    {
        return MakeUnique<FMyGamePerfMonitor>();
    }
};
```

파생 서브시스템이 존재하면 베이스 `UPerfMonitorSubsystem`은 자동으로 생성을 포기한다(`ShouldCreateSubsystem`) — GameInstance당 모니터는 항상 1개.

### 3) "왜 이 값인가" 조회

```cpp
// C++ : 현재 값을 결정한 제약과 걸려 있는 원인 전체
if (const FPerfConstraint* Why = Param.GetBindingConstraint())
{
    // Why->SourceId == "Thermal.Severe", Why->Timestamp == 걸린 시각
}
uint32 Reasons = Param.GetActiveReasonFlags();   // Thermal | Memory | ... 비트합

// Blueprint : 서브시스템 경유
Perf->GetParamEffective(TEXT("MaterialQuality"));         // 1.0
Perf->GetParamBindingSource(TEXT("MaterialQuality"));     // "Thermal.Severe" (baseline이면 None)
Perf->GetParamActiveReasonFlags(TEXT("MaterialQuality")); // EPerfReason 비트합
```

감사 이력은 `GetHistoryNum()` / `TryGetHistoryEntry(Age, Out)`로 최신→과거 순회(Assert/Retract/Baseline, 이전값→새값, 시각).

## 아키텍처

```
[Collectors]                          [Store]                [Consumers]
FrameCollector    ─ 매 프레임 ─┐
 (IPerformanceDataConsumer)    ├→  FPerfMetricStore     ┌→ 규칙(IPerfRule) → 제어 파라미터 원장
PlatformSampler   ─ 주기 폴링 ─┤   (지표별 링버퍼,      ├→ HUD / Blueprint 조회
 (온도/배터리/메모리)          │    창 통계)            └→ (훅) CSV / 텔레메트리
EventListener     ─ 델리게이트 ┘
```

- 모든 소비는 게임스레드. 플랫폼 델리게이트는 발생 스레드가 보장되지 않으므로 MPSC 큐에 넣고 프레임마다 드레인한다.
- 엔진에는 공유 frame consumer 1개만 등록되고 모니터들로 팬아웃된다.
- 규칙 평가는 히스테리시스 내장: N연속 샘플 초과로 진입, 쿨다운 동안 조용해야 해제.

## 내장 지표 (`PerfMetricNames`)

| 지표 | 내용 | 지원 |
|---|---|---|
| `Frame.TimeMs` `GameThreadMs` `RenderThreadMs` `RHIThreadMs` `GPUMs` `IdleMs` | 스레드/프레임 시간 | 전 플랫폼 |
| `Frame.Hitch`, `Frame.*Bound` | 엔진 자체 판정(히치, bound 플래그) | 전 플랫폼 |
| `Frame.DynResPercent` | 동적 해상도 비율 (조치 중복 방지용) | 전 플랫폼 |
| `Frame.FlushAsyncLoadMs` `SyncLoadCount` | 로딩 기인 스파이크 분리용 | 전 플랫폼 |
| `Platform.DeviceTemperatureC` | 수치 온도 | Android(배터리 온도), 기타 미지원 |
| `Platform.ThermalState` | `EDeviceThermalState` 등급 | Android / iOS |
| `Platform.ThermalStress10s` | ADPF 열 헤드룸 예측 (≥1.0 = 스로틀링 임박) | Android |
| `Platform.BatteryLevel` `OnBattery` | 배터리 | Android / iOS / Windows 등 |
| `Platform.UsedPhysicalMB` `AvailablePhysicalMB` `MemoryPressure` | 메모리 (`GetMemoryPressureStatus` 등급 포함) | 전 플랫폼 |
| `Event.TemperatureSeverity` `LowPowerMode` `MemoryTrim` | 이벤트 발생 기록 | 델리게이트 지원 플랫폼 |

**미지원 값은 기록되지 않는다** — 센티널(-1) 대신 시리즈에 샘플이 없다. 규칙은 샘플 없으면 침묵.

## 콘솔 변수

| CVar | 기본값 | 설명 |
|---|---|---|
| `perf.Monitor.ThermalSampleIntervalSec` | 1.0 | 온도/열 상태 폴링 주기 (기기 자체가 7~10s 주기로 갱신하므로 더 짧게는 무의미) |
| `perf.Monitor.MemorySampleIntervalSec` | 2.0 | 메모리 폴링 주기 (Android `GetStats`는 /proc 파싱 — 매 프레임 금지) |
| `perf.Monitor.BatterySampleIntervalSec` | 30.0 | 배터리 폴링 주기 |
| `perf.Monitor.WarmupFrames` | 8 | 리셋(포그라운드 복귀 등) 후 무시할 프레임 수 |

## 주의사항

- **cvar에 적용할 때** — `OnChanged`에서 `r.MaterialQualityLevel` 같은 cvar를 만지면 엔진 `ECVF_SetBy` 우선순위와 만난다. `SetByCode`로 덮으면 이후 Scalability(사용자 설정) 변경이 거부되므로 적용 우선순위를 명시적으로 선택하고, 사용자가 설정을 바꿀 때 `SetBaseline()`으로 기준값을 갱신할 것.
- **`GetBindingConstraint()` 반환 포인터**는 다음 Assert/Retract까지만 유효. 보관하려면 복사.
- **iOS는 수치 온도 없음** — 등급(`ThermalState`) 중심으로 규칙을 설계. Android의 수치 온도는 SoC가 아닌 **배터리 온도**이므로 기기 간 절대값 비교는 무의미.
- **모니터 등록 시점** — `Initialize()`는 GEngine 생성 이후여야 한다. 제공되는 서브시스템(GameInstance 생성 시점)을 쓰면 자동으로 만족된다.

## 파일 구성

```
PerfMonitor/
├── PerfMonitor.uplugin
├── README.md
└── Source/PerfMonitor/
    ├── PerfMonitor.Build.cs
    ├── Public/
    │   ├── PerfMonitorCore.h        # TPerfRingBuffer / MetricStore / 원장 / 규칙 / FPerfMonitorBase
    │   └── PerfMonitorSubsystem.h   # UGameInstanceSubsystem 호스트 (파생 예제 겸용)
    └── Private/
        ├── PerfMonitorCore.cpp
        ├── PerfMonitorSubsystem.cpp
        └── PerfMonitorModule.cpp
```

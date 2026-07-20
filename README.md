### 소개

Unreal Engine 5용 플러그인 샘플 (포트폴리오용, 개인프로젝트로 재구현).

두 개의 플러그인으로 구성됩니다.

- `Plugins/FeatureSupplements` — `TraversalGameplay`(캐릭터 이동/파쿠르/GAS)와
  `VisualEffects`(비주얼 이펙트/Niagara) 두 런타임 모듈로 구성된 게임플레이 보조 플러그인
  (최신 테스트 엔진 버전: **Unreal Engine 5.7.4**)
- `Plugins/PerfMonitor` — 실시간 성능·플랫폼 지표 수집과 원인 추적이 가능한
  자동 품질 조절을 위한 프로젝트 무관(base) 플러그인 (지원 엔진 버전: **UE 5.8 이상**)

## Plugins

### TraversalGameplay (Trv)

- **TrvCharacterMovementComponent** — `UCharacterMovementComponent`를 확장해 Slide, FollowFlightPath 등
  커스텀 이동 모드를 추가. 슬로프 기반/컴포넌트 태그 기반 슬라이드 감지, 스플라인 비행 경로 추종을 지원.
- **TrvCustomMoveArrangeComponent** — 파쿠르/장애물 판단을 위한 비동기 지형 스캐너.
  Convex(장애물/벽 넘기) / Concave(틈/구덩이) 분석 결과를 `FQuat` 목표 회전값으로 산출해
  `UMotionWarpingComponent`와 연동.
- **TrvSplineFlightPath / TrvSplinePathIndicator** — 스플라인 기반 비행 경로 정의 및 시각화,
  전용 GAS 어빌리티 태스크(`TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath`)로 루트모션 점프를
  스플라인에 고정.
- **TrvAnimInstance / TrvGameplayTags** — GAS 연동 애님 인스턴스, 파쿠르/이동모드 관련 네이티브 게임플레이 태그.

### VisualEffects (Vfx)

- **VfxSnapshotManagerComponent / SkeletalMeshCompositeUtils** — 스켈레탈/스태틱/그룸 메시의 런타임
  "스냅샷"(전체 덧그리기, 피격 부위 부분 그리기 등)을 생성. 멀티스레드 Octree 컬링으로 필요한 영역만
  잘라낸 메시를 만들고, Niagara 파티클 스폰용 샘플링 리전까지 자동 구성.
- **VfxMaterialEffectManager** — 레이어(우선순위) 기반으로 여러 머티리얼 파라미터 효과를 시간에 따라
  블렌딩/페이드하는 MID 드라이버.
- **VfxBeamCoordinatorComponent / NiagaraDataInterfaceBeamCoordinatorComponent** — 움직이는 두 액터 간
  Beam 연결(스캔, 조립/분해 등)을 CPU/GPU 양쪽 Niagara 파이프라인에 노출하는 커스텀 Data Interface.
- **VfxCameraShakePattern_ConstOffset / VfxNiagaraLensEffect** — 카메라 셰이크/렌즈 FX 확장 포인트.

### PerfMonitor

실시간 성능·플랫폼 지표 수집과 **원인 추적이 가능한 자동 품질 조절** 플러그인.
자세한 내용은 [Plugins/PerfMonitor/README.md](Plugins/PerfMonitor/README.md) 참고.

- **3계층 수집** — 매 프레임(엔진 `IPerformanceDataConsumer` 프레임 데이터) / 주기 폴링(온도·배터리·메모리) /
  이벤트(온도 등급 변화, 저전력 모드, 메모리 트림). 미지원 지표는 센티널 없이 기록 자체를 생략.
- **제약 원장(constraint ledger)** — 규칙이 값을 직접 Set하지 않고 원인 태그가 붙은 제약(Cap/Floor/Force)을
  걸었다 푸는 구조. 복수 원인이 겹쳐도 복원 버그가 없고, 조절 중인 값(예: MaterialQuality)에 대해
  *지금 왜 이 값인지*를 언제든 조회 가능.
- **프로젝트 분리** — 무엇을 감지해 무엇을 조절할지는 `FPerfMonitorBase`/`UPerfMonitorSubsystem` 파생 클래스 몫.
  플러그인 자체는 수집만으로도 동작(Blueprint/C++ 조회 지원).
- **지원 엔진 버전: UE 5.8 이상** — 5.8에서 정비된 열 API(`GetDeviceTemperature` / `GetDeviceThermalState`)를 사용.

## Knowledge

AI 도구를 활용한 구현 분석 및 개선방안 탐구 샘플. 실제 리팩터링 전 설계/성능 검토 기록입니다.

| 문서 | 내용 |
|---|---|
| [FeatureSupplementPlugin/CoreFeatureSupplement_Analysis.md](Knowledge/FeatureSupplementPlugin/CoreFeatureSupplement_Analysis.md) | 두 모듈 전반에 대한 1차 구조 분석 및 잠재 이슈 |
| [FeatureSupplementPlugin/FeatureSupplements_Analysis.md](Knowledge/FeatureSupplementPlugin/FeatureSupplements_Analysis.md) | 소스 전체(약 14,500줄)에 대한 상세 구조/의존성 분석, 개선 제안 |
| [FeatureSupplementPlugin/Snapshot_Performance_Analysis.md](Knowledge/FeatureSupplementPlugin/Snapshot_Performance_Analysis.md) | 스냅샷 시스템 런타임 성능 분석 (Octree 재생성, 메시 생성 비용 등 병목 지점과 개선안) |
| [FeatureSupplementPlugin/Snapshot_Scar_Design.md](Knowledge/FeatureSupplementPlugin/Snapshot_Scar_Design.md) | "칼자국(scar)" 국소 이펙트를 위한 설계 결정 — 스웹트 볼륨 선택 방식 채택 근거 |
| [FeatureSupplementPlugin/snapshot_manager_refactor_plan.md](Knowledge/FeatureSupplementPlugin/snapshot_manager_refactor_plan.md) | (제안 단계) 텍스처 기반 버텍스 데이터 스냅샷 대안 설계 — 미구현 |

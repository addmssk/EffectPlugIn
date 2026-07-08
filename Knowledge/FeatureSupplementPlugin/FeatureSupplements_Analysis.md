# FeatureSupplements Plugin — Code Analysis

> Source analyzed: `Plugins/FeatureSupplements/Source`
> Scope: C++ runtime source only (generated `Intermediate/` code excluded).
> ~14,500 lines of C++ across two runtime modules.

---

## 1. Overview

`FeatureSupplements` is a gameplay/effects support plugin (`Category: GamePlay`, `EnabledByDefault: true`, version 0.1) that augments Unreal Engine's built-in systems rather than replacing them. It is split into two independent runtime modules:

| Module | Prefix | Responsibility |
|--------|--------|----------------|
| **TraversalGameplay** | `Trv` | Character movement, traversal/parkour, spline flight paths, GAS integration, animation |
| **VisualEffects** | `Vfx` | Visual FX: mesh "snapshot" ghosting, material-parameter effects, Niagara beam data interface, camera shake, lens FX |

Both modules load at the `Default` phase and use `PCHUsageMode.UseExplicitOrSharedPCHs` with full IWYU support.

### Plugin dependencies (`FeatureSupplements.uplugin`)
Niagara, EngineCameras, MotionWarping, GameplayAbilities, ProceduralMeshComponent, HairStrands.

---

## 2. Module: TraversalGameplay (`Trv`)

**Build dependencies:** Core, CoreUObject, Engine, LevelSequence (public); RHI, AnimationCore, GameplayTags, GameplayTasks, GameplayAbilities, ProceduralMeshComponent, MotionWarping (private).

### 2.1 Custom Character Movement — `UTrvCharacterMovementComponent`
`Public/Character/TrvCharacterMovementComponent.h` · `Private/.../TrvCharacterMovementComponent.cpp` (926 lines)

An extension of `UCharacterMovementComponent` that adds three custom movement modes via `ETrvCustomMovementMode`:
- `RootMotionOnly`
- `Slide`
- `FollowFlightPath`

Notable capabilities:
- **Sliding system** — auto-detects slidable floors by slope angle (`AutoSlidableFloorAngleStart/End`) and/or a component tag (`AutoSlidableComponentTag`, via `HasAutoSlidableTag`). Configurable braking deceleration, friction, max speed, and acceleration. Slope Z limits are cached (`AutoSlidableFloorMinZ/MaxZ`).
- **Flight-path following** — `ExecuteFollowFlightPath()` drives the pawn along an `ATrvSplineFlightPath` with blend-in time and a bias-speed limit; state tracked in an internal `FFollowFlightPathContext`.
- **Falling speed clamp** — optional `FallingZSpeedLimit` applied in `NewFallVelocity`.
- **Root-motion precedence override** — `bIgnoreAnimRootMotionPrecedenceOverRootMotionSource` lets root-motion *sources* win over anim root motion (with an expected `PossibleRootMotionSourceDestination`).
- **Rotation** — `bOrientRotationToMovementByVelocity` changes `ComputeOrientToMovementRotation` to orient by velocity.

Overrides a large surface of the parent CMC (`PhysCustom`, `StartNewPhysics`, `ProcessLanded`, `MoveAlongFloor`, `SlideAlongSurface`, `ConstrainAnimRootMotionVelocity`, `PhysicsRotation`, `CanAttemptJump`, `CanStepUp`, etc.), indicating deep integration into the physics tick.

### 2.2 Traversal Analysis — `UTrvCustomMoveArrangeComponent`
`Public/Character/TrvCustomMoveArrangeComponent.h` · `.cpp` (1,362 lines — largest Trv file)

An `ActorComponent` that performs **asynchronous geometry probing** for parkour/traversal decisions, feeding a `UMotionWarpingComponent`:
- **Convex analysis** (ledges/obstacles to vault or climb) — `QueueAsyncConvexAnalysis` → `HasQueuedAsyncConvexAnalysis` → `ConsumeAsyncConvexAnalysis`, returning `FTrvCustomMoveAsyncConvexAnalysisResult` (depth, height, front/back ledge locations, peak height/ratio, motion-warp direction quaternion).
- **Concave analysis** (gaps to jump across) — parallel queue/has/consume API returning `FTrvCustomMoveAsyncConcaveAnalysisResult` (gap start/end, gap length, height difference).
- Uses raw `void*` async task handles plus queued-cycle counters and a `FQueuedJabValidationContext` to validate a completed task against the position/direction it was queued for (guards against stale results when the pawn has moved).
- Options structs expose extensive tuning: trace granularity, acceptable slopes, ray offsets, optional sphere tracing, and an `InvDirToHitNormalBlendRangeByHeight` blend rule for choosing warp direction by obstacle height.

### 2.3 Spline Flight Paths
- **`ATrvSplineFlightPath`** (`TrvSplineFlightPath.h/.cpp`, 272 lines) — an actor wrapping a `USplineComponent` that pre-samples the spline into an array of `FTrvSplineSegment` (forward/up/side vectors, center, left/right max offsets, distance). Provides `GetFlightPathPoint(distance, ...)` with binary search (`Algo::UpperBoundBy`) + linear interpolation between segments, `GetFlightPathLength()`, and `CaclculateNearestStartPerpendicularBias()` for entry alignment. In-editor it renders a debug ribbon mesh via `UProceduralMeshComponent` (editor-only, rebuilt in `OnConstruction`/`PostEditChangeProperty`).
- **`ATrvSplinePathIndicator`** (`TrvSplinePathIndicator.h/.cpp`, 290 lines) — runtime spline-following visual indicator; builds a UV-wrapped ribbon mesh from control points (`BuildSpline`) with configurable segment distance, half-width, and V-wrapping distance.
- **`FTrvSplineSegment`** (`TrvSplineCommon.h`) — shared POD struct with a custom `Serialize` and `TStructOpsTypeTraits` (WithSerializer). `FSplineMeshBuildHelper::Add` (in `TrvUtils.h`) does the per-segment width/curvature computation shared by both actors.

### 2.4 GAS Ability Task — Spline Jump
`Public/Abilities/Tasks/TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath.h` · `.cpp` (545 lines)

- **`FTrvRootMotionSource_JumpUsingSplineFlightPath`** — a custom `FRootMotionSource` (modeled on engine `FRootMotionSource_JumpForce`) that moves the character along an `ATrvSplineFlightPath` during a jump, with `BlendInDuration`, an optional `TimeMappingCurve`, timeout control, and full networking support (`NetSerialize`, `Clone`, `Matches`, `UpdateStateFrom`, `AddReferencedObjects`). Traits declare `WithNetSerializer` + `WithCopy`.
- **`UTrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath`** — the Blueprint-spawnable ability task wrapping that source. Replicated properties, `OnFinish`/`OnLanded` delegates, landing detection with `MinimumLandedTriggerTime`, and a replay-safe `TriggerLanded` workaround for `bClientUpdating` movement replays.

### 2.5 Animation — `UTrvAnimInstance`
`Public/Character/TrvAnimInstance.h` · `.cpp` (147 lines)

`UAnimInstance` subclass with GAS integration:
- Control-Rig enable/disable with alpha-blending (`ControlRigAlphaBlend`, `FAlphaBlend`).
- Parkour front/back **ledge transforms** exposed to the anim graph (`SetParkour*LedgeTransform`).
- `FGameplayTagBlueprintPropertyMap` to auto-mirror gameplay tags into anim variables; `InitializeWithAbilitySystem(ASC)`.
- Editor `IsDataValid` validation.

### 2.6 Gameplay Tags — `TrvGameplayTags`
Native gameplay tags (`UE_DECLARE_GAMEPLAY_TAG_EXTERN`) for parkour/hero-landing disable states and a full movement-mode tag set (Walking/Falling/Swimming/Flying/Custom + the three custom sub-modes). Module startup registers `Config/Tags` as a tag INI search path.

### 2.7 Blueprint Function Library — `UTrvBPFunctionsLibrary`
GAS/utility helpers: extract ability/ASC from effect contexts, `GiveAbilityAndActivateOnceWithSourceObject`, set anim root-motion translation scale, movement-mode queries, gameplay-cue tag lookup, collision-channel → trace-type conversion, dedicated VRAM query, and **`FindBestCurveFit`** — least-squares polynomial curve fitting (up to order 5) until RMSE is acceptable (used to compress/approximate motion curves).

### 2.8 Level Sequence — `ATrvLevelSequenceActor`
Thin `ALevelSequenceActor` subclass (stub — no behavior beyond overrides), presumably a project-specific hook point.

---

## 3. Module: VisualEffects (`Vfx`)

**Build dependencies:** Core, CoreUObject, Engine (public); AnimationCore, RHI, RenderCore, Renderer, Niagara, EngineCameras, VectorVM, Projects, HairStrandsCore (private). Module startup registers a virtual shader path `/Plugin/FeatureSupplements` → `Shaders/`.

### 3.1 Mesh Snapshot / Ghosting System (largest subsystem)
This is the plugin's most substantial feature — creating transient "snapshot" copies of meshes for dissolve/ghost/afterimage effects.

**`UVfxSnapshotManagerComponent`** (`VfxSnapshotManagerComponent.h`, 666 lines header · `.cpp` 2,234 lines — largest file in the plugin)
A `USceneComponent` that manages snapshots for one source `UMeshComponent`, dispatching to one of three internal managers by source type (`EVfxSnapshotSourceMeshType`):
- `FVfxSnapshotManagerSkeletalMesh`
- `FVfxSnapshotManagerStaticMesh`
- `FVfxSnapshotManagerGroom`

Two creation modes:
- **`StartWithSameMesh`** — reuses the source mesh, applying override materials / material-param effects.
- **`StartGenerate`** — generates a *new filtered mesh* culled to a shape (sphere/box via `FVfxSnapshotCullOption`) around a center/forward direction, with UV projection, optional back-face discard, and optional Niagara particle-sampling regions defined by UV range.

Each snapshot instance (`FVfxSnapshotManagerInstance` + type-specific subclasses) tracks materials, an optional `UVfxMaterialParamsData`, optional attached `UNiagaraSystem` (with a `NotRequired/Waiting/Done` spawn stage), loop flag and remaining duration. Instances are keyed by `int64` and are async-ready-checked (`IsInstanceReady`).

**Spatial culling via octree:** The skeletal-mesh manager builds a bone-OBB octree (`TOctree2<FVfxSnapshotSourceMeshBoneOBB, ...>`, 16 elems/leaf, depth 12) refreshed on a background thread (`FVfxSnapshotManagerOctreeRefreshTask`), guarded by a `TPimplPtr<FRWLock>`. A separate `FVfxSnapshotManagerCullingTask` performs per-instance vertex culling asynchronously. Bones cache component-space bounds, local bound boxes, and a `TBitArray` of influencing vertices.

**`FVfxSnapshotManagerOrganizer`** — actor-level registry mapping names and source mesh components to managers (find/find-or-create/remove/clear).

**Snapshot render components** — lightweight subclasses that carry an embedded `FVfxMaterialEffectManager`:
- `UVfxSnapshotSkinnedMeshComponent` (base, holds a weak `SourcePoseComponent`)
- `UVfxSnapshotSkeletalMeshComponent`
- `UVfxSnapshotStaticMeshComponent`
- `UVfxSnapshotGroomComponent` (`.cpp` 985 lines — non-trivial groom/hair handling)

**`SkeletalMeshCompositeUtils`** (`.h/.cpp`, 751 lines) — the mesh-surgery backend: `GenerateFilteredSkeletalMesh(...)` builds a new `USkeletalMesh` from a source restricted to a `TBitArray` of valid vertices, with an optional `FSnapotControllerBackfaceDiscardTool` (CCW normal test against a view direction), UV overrides, and optional particle-sampling-region construction by UV range/bones. `GeneratedMeshIsReadyForNiagaraGPUSpawnLocation` gates GPU particle spawning on the generated mesh.

### 3.2 Material Parameter Effect System — `FVfxMaterialEffectManager`
`Public/Material/VfxMaterialEffectManager.h` (402 lines) · `.cpp` (1,914 lines — 2nd largest)

A sophisticated **layered, time-animated MID (Material Instance Dynamic) parameter driver**:
- Manages a set of target `UPrimitiveComponent`s, swapping their materials to MIDs and animating scalar / color / texture parameters over time.
- **Priority layering** — `EVfxMaterialParamPriority` (Layer0–4 + System). Parameters accumulate per layer (`FScalarLayers`/`FColorLayers`/`FTextureLayers`), with the top active layer winning; supports full material override per layer (`ActiveOverrideMaterial`, with fall-through to next priority when an override ends).
- **Effect lifecycle** — `CreateEffect(...)` returns an internal id; effects have fade-in / life / fade-out / loop, driven by `FRuntimeFloatCurve`/`FRuntimeCurveLinearColor` curves (normalized or absolute time, play-once option). Kill by internal id, given id, layer, or all.
- **Pending queue** — effects requested before init (or needing deferral) are queued in `PendingEffects`.
- **Texture streaming** — async loads via `FStreamableHandle` map, with a texture reference holder to keep loaded textures alive.
- **Pooling** — static free-lists for scalar/color/texture value arrays (`sScalarArrayPool` etc.) to avoid per-frame allocation.
- **Snapshot awareness** — a flag (`bIsSnapshot` / `IdentifySnapshotFX`) adjusts behavior when driving snapshot components. Static `RemoveAllMaterialInstanceDynamic` cleanup helper.
- Editor-only `DebugInfo` string for inspection.

**`UVfxMaterialParamsData`** (`VfxMaterialParams.h`) — a `UDataAsset` wrapping `FVfxMaterialParams`: lifetime/fade/loop/priority, override material, snapshot options (overdraw, sync-anim-and-position, hit-transform params `HitLocation`/`HitNormal`/`HitUV_V`, inverse normal), original-texture copying, and arrays of scalar/color/texture param definitions (each supporting a constant value or a curve). Well-known param names centralized in the `VfxMaterialParams` namespace (`BaseColor_Tex`, `Normal_Tex`, `Mask_RMES`, `Opacity_Int`).

### 3.3 Niagara Beam Coordinator
A pair that lets Niagara systems (CPU **and** GPU) read live "beam interaction" state from gameplay code.

**`UVfxBeamCoordinatorComponent`** (`.h` 190 · `.cpp` 328) — an `ActorComponent` acting as a broker for beam/drone interactions:
- `StartInteracting(Obj, Type, Delay, Duration)` returns a unique int32 id (static generator). Interaction types: `Assemble`, `Dismantle`, `Scan`.
- Query API by id: progress, elapsed secs, duration, target location/move-direction/width per `EAxis`.
- Targets stored in a `TMap<int32, FVfxBeamTargetObject>` guarded by an `FRWLock` (with a `FReadLookScoped` RAII helper); a `QueryHistory` cache holds the last per-axis results for the render thread. Timer-driven update loop (`OnTimerCallback`) with start/end delegates.
- **`IVfxBeamTargetInterface`** — the interface interaction targets implement (`OnBeamTargetInteractionStart/End`, `OnUpdateProgress`, `GetWorkingWorldPosition/Direction/Width/Progress`). *Note: the `PURE_VIRTUAL` macros reference `IGDroneSummonableInterface` names — copy/paste leftovers, cosmetic only.*

**`UNiagaraDataInterfaceBeamCoordinatorComponent`** (`.h` 148 · `.cpp` 663) — a custom Niagara Data Interface (modeled on engine `UNiagaraDataInterfaceActorComponent`/`SkeletalMesh`) exposing the coordinator to particle graphs:
- Full CPU VM function set (`VMGetProgress`, `VMGetDuration`, `VMGetTargetLocation`, `VMGetTargetMoveDirection`, combined getters, ...).
- Full **GPU** path — `BEGIN_SHADER_PARAMETER_STRUCT` (Valid/Progress/ElapsedSecs/Duration/TargetWidth/TargetLocation/TargetMoveDirection), HLSL generation, compile-hash, per-instance data with render-thread hand-off, and tick-group prereq handling.
- Source binding modes (`Default`/`Source`/`AttachParent`), settable from Blueprint via `UVfxBPFunctionsLibrary::OverrideSystemUserVariableBeamCoordinatorComponent`.
- Shader template: `Shaders/NiagaraDataInterfaceBeamCoordinatorComponentTemplate.ush`.

### 3.4 Camera & Lens FX
- **`UVfxCameraShakePattern_ConstOffset`** — a `USimpleCameraShakePattern` applying a constant Location/Rotation/FOV offset (a "hold" shake, useful for sustained camera displacement).
- **`AVfxNiagaraLensEffect`** — Blueprintable `ANiagaraLensEffectBase` subclass (thin extension point for Niagara-based lens/camera FX).

### 3.5 Volume Decision & Utilities
- **`FVfxVolumeDecisionBaseParam`** — base params (Origin, ForwardDirection, Roll as `FVector3f`) for volume-based decisions; consumed by a Blueprint (`BP_Vfx_VolumeDecisionParamComponent`).
- **`VfxUtils.h/.cpp`** (180-line header) and **`VfxBPFunctionLibrary`** — supporting math/helpers and Blueprint exposure for the beam DI.

---

## 4. Content & Config (context)

- **Content** (`Content/`): Parkour system Blueprints — chooser tables, pose-search databases (`PSC_Trv_ParkourTouch_*`), gameplay effects to disable parkour/hero-landing, custom-movement BP, flight-path builder, and Vfx sample materials/Niagara for the skeletal-mesh decal snapshot demo. `MF_NormalExtrude` material function.
- **Config**: `Engine.ini`, `Game.ini`, and `Config/Tags/TraversalGameplay.ini` (gameplay-tag definitions matching `TrvGameplayTags`).
- **Shaders**: one `.ush` template for the beam-coordinator Niagara DI.

---

## 5. Architecture Notes & Observations

**Design strengths**
- Clean two-module separation of concerns (gameplay/movement vs. rendering/FX) with distinct dependency sets — the Vfx module pulls heavy render deps (Renderer, RenderCore, HairStrandsCore) that Core does not.
- Consistent `Trv`/`Vfx` prefixing and folder layout (Abilities/Tasks, Character, LevelSequence, Niagara, Material, Components, CameraShake).
- Heavy, correct use of engine idioms: custom `FRootMotionSource` with net-serialization, custom Niagara DI with CPU+GPU parity, octree spatial acceleration, async tasks for culling/octree/geometry probing, MID pooling, and streamable-handle-based texture loading.
- Extends engine base classes rather than forking them, so it composes with stock GAS/CMC/Niagara.

**Complexity hotspots** (by size / concurrency)
- `VfxSnapshotManagerComponent.cpp` (2,234) and `VfxMaterialEffectManager.cpp` (1,914) are the two largest and most intricate files — multithreaded (octree/culling tasks + RW locks) and stateful (layered param accumulation). Highest-risk area for lifetime/threading bugs.
- `TrvCustomMoveArrangeComponent.cpp` (1,362) uses raw `void*` async task handles with manual queued-cycle validation — functional but type-unsafe; a typed wrapper would harden it.
- `TrvCharacterMovementComponent.cpp` (926) overrides a wide CMC surface; changes there have broad physics/replication impact.

**Minor / cosmetic issues spotted**
- `IVfxBeamTargetInterface` `PURE_VIRTUAL` macros cite `IGDroneSummonableInterface::...` (wrong interface name — harmless but misleading in logs).
- `FVfxMaterialEffectManagerPendingEffect::bLoop` is initialized from a float literal (`bLoop(1.f)`); several typos in identifiers/comments (`Caclculate`, `Aceeptable`, `Snapot`, `Origial`, `RMVfx`/`RMES`, `ReadLookScoped`).
- Several actors declare `Tick` overrides that are empty stubs (`ATrvLevelSequenceActor`, `AVfxNiagaraLensEffect`, `ATrvSplineFlightPath` with `bCanEverTick=false`) — dead overrides.
- `FVfxMaterialParams` comment references texture name `Mask_RMEfs` while the code constant is `Mask_RMES` — documentation/constant drift.

**Suggested follow-ups**
1. Wrap the `void*` async-analysis handles in `TrvCustomMoveArrangeComponent` in a typed RAII/`TUniquePtr` structure.
2. Fix the `IVfxBeamTargetInterface` `PURE_VIRTUAL` interface-name references.
3. Audit snapshot-manager async task teardown paths (`Uninitialize`/`OnUnregister`/destructor) for races between the game thread and octree/culling tasks under the `FRWLock`.
4. Consider consolidating the three near-identical `FVfxSnapshotManager{Skeletal,Static,Groom}Mesh` classes (they share large `StartGenerate`/`StartWithSameMesh` signatures) behind a common template/base to reduce duplication.
5. Normalize the widespread identifier typos before the API surface hardens beyond v0.1.

---

*Generated analysis — structural/architectural review from source inspection; runtime behavior not executed.*

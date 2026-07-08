# Snapshot Mechanism — Real-Time Performance Analysis

> Scope: `VisualEffects` snapshot system.
> Files: `Components/VfxSnapshotManagerComponent.cpp` (2,235), `SkeletalMeshCompositeUtils.cpp` (751),
> `Components/VfxSnapshotSkinnedMeshComponent.cpp`, `VfxSnapshotSkeletalMeshComponent.cpp`,
> `Material/VfxMaterialEffectManager.*`.
> Method: source inspection of the actual runtime paths (not benchmarked). Line refs are to current code.

---

## 1. What the mechanism actually does

The system produces transient "snapshot" copies of a character's mesh for localized material FX
(dissolve / ghost / scan over a sphere or box region). The fully-implemented path is **skeletal**;
`StartGenerate` for static mesh and groom just log "Not yet supported" and return `-1`
(`VfxSnapshotManagerComponent.cpp:1336`, `:1648`).

Two entry points:
- **`StartWithSameMesh`** — cheap: attaches a leader-pose-following `USkinnedMeshComponent` that reuses the
  source mesh and only overrides materials / drives a `FVfxMaterialEffectManager`. No geometry work.
- **`StartGenerate`** — expensive: builds a **brand-new culled `USkeletalMesh`** at runtime.

### The `StartGenerate` pipeline (per snapshot)

| Stage | Thread | Work |
|-------|--------|------|
| `Initialize` (once/mesh) | Game | CPU-skins **every** LOD0 vertex to build per-bone OBBs + per-bone `InfluencingVertices` bitsets (`:651-677`) |
| every frame while inited | Game→Task | `RefreshObbOctree` **rebuilds the whole octree from scratch** and copies all component-space transforms (`:695-738`, `:478-528`) |
| `StartGenerate` | Game | `NewObject<USkeletalMesh>` (`:812`), alloc `ValidVertexBits` + `ProjectedUVs` at full vertex count (`:817-818`), kick culling task |
| culling task | Task | octree bounds-cull → OR influencing bits; then **re-CPU-skin every candidate vertex** (uncached path) + sphere/box test + UV projection (`:302-412`); then `GenerateFilteredSkeletalMesh` rebuilds vertex/index/weight/color buffers + optional sampling region (`SkeletalMeshCompositeUtils.cpp`) |
| ready (≤3 frames later) | Game | `Result->InitResources()` uploads GPU buffers **on the game thread** (`:1101`); create+register component, set leader pose, create material effect, spawn Niagara |
| every frame, per live snapshot | Game | component ticks @30Hz → `MaterialEffectManager.Tick`; GPU-skins the culled subset; drawn in `SDPG_Foreground` |
| destroy | Game | `MarkAsGarbage` the generated mesh (`:1039`) |

The generated snapshot **follows the live pose** via `SetLeaderPoseComponent` — it is an *overlay* of a
body subset, not a frozen afterimage.

---

## 2. Performance issues (grounded in code)

### 🔴 P0 — Octree is rebuilt from scratch every frame, unconditionally
`Tick` calls `RefreshObbOctree()` at its end (`:1183`) **whenever the manager is initialized — even with zero
live snapshots**. Each refresh `new`s a whole `FVfxSnapshotSourceMeshBoneOBBOctree`, `AddElement`s every bone,
copies all component-space transforms, then `delete`s the previous octree (`:478-528`). That is continuous
background-thread work + allocation/free churn + a per-frame full transform copy, paid **all the time the
component exists**, whether or not a snapshot is ever generated. The octree is only consumed inside the
culling task at `StartGenerate` time.

### 🔴 P0 — Runtime `USkeletalMesh` creation + `InitResources()` per snapshot
Every `StartGenerate` allocates a fresh `USkeletalMesh` (`:812`), builds all render buffers on the task, then
calls `InitResources()` on the **game thread** (`:1101`) to create/upload GPU resources, and on teardown
`MarkAsGarbage`es it (`:1039`). For frequent snapshots (trails, rapid re-triggers) this is the dominant cost:
per-instance UObject creation, full buffer allocation + GPU upload, and GC pressure. `TGenerateFilteredSkeletalMesh`
even hard-forbids reuse (`check(false)` when DestMesh already has render data, `SkeletalMeshCompositeUtils.cpp:578`),
so nothing is pooled.

### 🟠 P1 — Redundant CPU skinning + uncached matrix path in the culling task
Vertices are CPU-skinned in `Initialize` with the **cached** path `GetTypedSkinnedVertexPosition<true>` (`:660`),
then skinned **again** in the culling task with the **uncached** path `<false>` (`:339`, `:381`), which recomputes
`RefBasesInvMatrix[bone] * BoneTransform` **per influence, per vertex** (`:161-164`). The cached path exists and
takes a precomputed `RefToLocals` array; the task could build `RefToLocal[bone]` once (a few hundred matrices)
and skip a matrix multiply on every influence of every candidate vertex.

### 🟠 P1 — Octree read-lock held across the entire vertex-cull loop
The culling task takes `OctreeLock->ReadLock()` (`:256`) and holds it until after skinning **all** candidate
vertices (`:414`) — potentially tens of thousands of CPU-skin operations. Because the octree refresh runs every
frame and needs the **write** lock (`:513`), a long culling task serializes against refresh (and vice-versa).
Only the octree traversal (`:262-295`) actually needs the lock; the skin loop reads `Owner->Bones` /
`ComponentSpaceTransformCache`, which could be snapshotted under the lock and then released.

### 🟡 P2 — Forced game-thread stall fallback
If the task isn't done after 3 frames, `Tick` calls `EnsureCompletion(true)` (`:1088`), **blocking the game
thread** until the task finishes. Good latency hiding for the common case, but under load this converts a
background job into a frame hitch. Prefer extending the wait or dropping the snapshot over stalling.

### 🟡 P2 — Per-generate full-vertex-count allocations
`ValidVertexBits.Init(false, VertexCount)` and `ProjectedUVs.SetNumZeroed(VertexCount)` allocate on every
`StartGenerate` (`:817-818`). Positions already use a `TThreadSingleton` scratch (`FVfxSnapshotTaskScratch`);
the bitset and UV array could be pooled the same way.

### 🟡 P2 — Baseline tick cost when idle
Manager ticks @60Hz in `TG_PostPhysics` (`:1844`) and still runs the octree refresh with zero instances.
Each snapshot component ticks @30Hz to run `MaterialEffectManager.Tick` (`VfxSnapshotSkinnedMeshComponent.cpp:22`)
even when the bound material params are static (no animated curve). Ticks could be disabled when there is nothing
to update.

### ⚪ Notes / correctness-adjacent
- Live-count guard is off-by-one and **per source component**: `if (Instances.Num() > MaxLiveSnapshotMeshCount)`
  allows 17 with a cap of 16 (`:796`), and there is no **global** budget across managers/characters.
- `StartGenerate` for static/groom is unimplemented, so the cull/generate cost model only applies to skeletal.

---

## 3. Suggested improvements (ranked by impact / effort)

### A. Make octree refresh on-demand instead of every-frame  *(P0, low effort, low risk)*
Only refresh when a `StartGenerate` is requested (or dirty-flag it and rebuild lazily on the next generate),
or throttle to a low fixed rate and **update in place** rather than `new`/`delete`. Removes a constant
background + allocation cost that is currently paid even when no snapshots exist. Highest value when snapshots
are infrequent.

### B. Pool and reuse generated meshes + render resources  *(P0, higher effort, biggest win for frequent snapshots)*
Keep a small pool of `USkeletalMesh` (or just their `FSkeletalMeshLODRenderData` buffers) keyed by source mesh.
On generate, reuse a pooled mesh: `ReleaseResources`/re-`Init` the buffers in place rather than `NewObject` +
`InitResources` + `MarkAsGarbage` each time. Eliminates per-instance UObject creation and GC churn — the actual
"runtime mesh-generation overhead" the earlier proposal was chasing, addressed without changing the rendering
model or Niagara sampling. (Requires lifting the `check(false)` reuse guard in `SkeletalMeshCompositeUtils.cpp:578`
behind an explicit "reset for reuse" path.)

### C. Precompute `RefToLocal` per bone; use cached skin path in the task  *(P1, low effort)*
Build `RefToLocal[bone]` once in the culling task and call `GetTypedSkinnedVertexPosition<true>`, removing a
matrix multiply per influence per candidate vertex. Optionally reuse the positions already computed in
`Initialize` for vertices whose bones haven't moved.

### D. Narrow the octree lock scope  *(P1, low effort)*
Hold `OctreeLock` only for the traversal (`:262-295`); copy the needed bone indices/bounds out, unlock, then do
the (long) skinning loop lock-free. Cuts contention against the every-frame refresh.

### E. Pool per-generate scratch (bits + UVs)  *(P2, low effort)*
Move `ValidVertexBits` / `ProjectedUVs` into the existing `FVfxSnapshotTaskScratch` (or a pool) to stop
full-vertex-count allocations per call.

### F. Replace the forced stall with defer-or-drop  *(P2, low effort)*
Instead of `EnsureCompletion(true)` after 3 frames, keep waiting (bounded) or discard the pending instance so a
heavy frame never blocks the game thread.

### G. Sleep ticks when idle  *(P2, low effort)*
Disable the manager's octree work (see A) and each snapshot component's tick when there are no active animated
effects, re-enabling on demand.

### H. Consider skipping geometry generation for pure localized material FX  *(design, medium effort)*
When the effect is only a localized material change on the **same** posed mesh (no Niagara surface sampling, no
separately-frozen pose), you don't need a generated mesh at all: drive the source component's material through
the existing `FVfxMaterialEffectManager` with a bounding-shape parameter (center/extent/forward) and clip/mask in
the shader. That removes the whole cull-task + mesh-build + `InitResources` chain for those cases. This is the
efficient core of the earlier "data-texture" idea, but scoped to where it genuinely wins — and it does **not**
cover the cases that need real geometry: Niagara surface/region spawning still requires either generated geometry
with a sampling region (current approach) or a spawn-script that samples the cull data and rejects invalid points.

---

## 4. Recommended sequencing

1. **A + D + C + E + F** — small, localized, low-risk wins that cut both the idle cost and the per-generate CPU
   cost without touching the architecture. Do these first; measure.
2. **B** — the structural win for frequent snapshots; more work, needs the reuse path in the composite util.
3. **H** — a routing decision per effect type (material-only vs. real-geometry), applied where Niagara/frozen-pose
   isn't needed.

> Measure before/after with `stat game`, `stat gpu`, and the existing `VfxSnapshotCulledMeshGenerator` /
> `VfxSnapshotManagerOctreeRefreshTask` thread-pool stats. The `msk.fx.snapshot.GeneratorForceSync` CVar
> (`:37`) is useful to isolate generation cost synchronously while profiling.

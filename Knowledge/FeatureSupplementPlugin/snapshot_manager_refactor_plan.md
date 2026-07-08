# Design Proposal: Texture-Based Snapshot Component

> **Status: PROPOSED / NOT IMPLEMENTED.**
> None of the strategy below exists in the code yet. As of this writing,
> `UVfxSnapshotSkinnedMeshComponent` is a lightweight leader-pose follower with a
> `FVfxMaterialEffectManager` — it does **not** pack vertex data into textures, create
> data textures, or run CPU culling/projection. This document describes where we *want*
> to take the system and how it would slot into the current architecture.

## Goal

Explore captured mesh snapshots (ghost / trail / freeze effects) whose projected UVs and
per-vertex culling are driven by a **data texture** rather than by generating new
`USkeletalMesh` assets at runtime or hacking `FSkeletalMeshSceneProxy`. The aim is to cut
the runtime mesh-generation overhead flagged in `knowledge/CoreFeatureSupplement_Analysis.md`
while keeping the existing snapshot manager and material-effect plumbing intact.

## Current State (baseline to build on)

Grounding facts from the code, so the design hooks into reality:

- **Component (`UVfxSnapshotSkinnedMeshComponent`)** inherits `USkinnedMeshComponent`.
  Today it only:
  - follows a `SourcePoseComponent` via `SetLeaderPoseComponent(...)` in `OnRegister`/`TickComponent`,
  - ticks a `FVfxMaterialEffectManager MaterialEffectManager`.
  There is no texture, no DMI creation, and no vertex math in this class.
- **Manager (`UVfxSnapshotManagerComponent`)** creates snapshot components at runtime as
  `NewObject<...>(Owner, UniqueName, RF_Transient)` and selects the type by source:
  - plain skinned → `UVfxSnapshotSkinnedMeshComponent`,
  - clothing assets **or** a Niagara system present → `UVfxSnapshotSkeletalMeshComponent`,
  - plus `UVfxSnapshotStaticMeshComponent` / `UVfxSnapshotGroomComponent`.
- **Material seam:** every variant owns a `FVfxMaterialEffectManager`, driven by
  `Initialize(Owner/Compo, Compo)` → `CreateEffect(MaterialParams, ...)`. This is the single
  place where materials/DMIs are already set up per snapshot — the natural home for creating
  and binding the proposed data texture, rather than ad-hoc code inside the component.

## Proposed Strategy

Pass the results of the CPU culling/projection math to the material via a `UTexture2D`
instead of manipulating index buffers or vertex factories in C++.

1. **Data texture:** on snapshot generation, create a transient 128-bit
   `PF_A32B32G32R32F` texture.
2. **Packing:** one pixel per LOD0 vertex index.
   - `R, G`: projected UVs (from the CPU calculation).
   - `B`: cull mask (`1.0` = valid, `0.0` = culled).
   - `A`: reserved (see open questions).
3. **Binding:** assign the texture to the material's `SnapshotVertexData` parameter through
   the existing `FVfxMaterialEffectManager` DMI created in `CreateEffect(...)` — **not** by
   hand-rolling DMI creation in the component.

### UV blending in the material

To get perspective-correct barycentric interpolation of the projected UVs across each
triangle without touching vertex factories, use Unreal's Customized UV system:

1. **Custom node (vertex shader):**
   `return SnapshotVertexData.Load(int3(VertexIndex % Width, VertexIndex / Width, 0)).rgb;`
2. **Customized UVs:** plug that node into a Customized UV slot so it evaluates in the vertex
   shader; the rasterizer interpolates the RGB across the triangle for free.
3. **Pixel shader:** `TextureCoordinate(x).rg` → projected UV; `TextureCoordinate(x).b` →
   opacity mask to cull invisible vertices.

## Integration Points (where the work actually lands)

- **Where the CPU math lives / texture is built:** the manager's snapshot-build path (the
  `CreateSnapshot*` flow around the `NewObject<UVfxSnapshotSkinnedMeshComponent>` calls),
  since that is where source mesh, LOD0, and pose are known.
- **Where the texture is bound:** `FVfxMaterialEffectManager` (extend it to accept/own the
  `SnapshotVertexData` texture and set it on the DMI it already builds in `CreateEffect`).
- **Component role:** stays a thin leader-pose follower. If it needs to hold a reference to
  the transient texture for lifetime/GC, add a `UPROPERTY(Transient)` here — but keep logic
  in the manager/effect-manager.

## Open Questions / TODO

- [ ] Does the current build path already compute per-vertex projected UVs and cull results,
      or is that math still to be written? (The doc previously *assumed* it exists.)
- [ ] Skeletal vs. skinned path: clothing/Niagara snapshots use
      `UVfxSnapshotSkeletalMeshComponent`. Does the texture approach apply there too, or only
      to the plain skinned path? Niagara sampling regions may still need real geometry.
- [ ] Texture dimensions: how is `Width` chosen for arbitrary vertex counts, and how is it
      communicated to the material (static param, scalar param, or texture size query)?
- [ ] Lifetime/GC of the transient `PF_A32B32G32R32F` texture across the snapshot's lifespan.
- [ ] Confirm the Customized UV / `Load()` custom node path is available on the target
      feature levels/platforms.
- [ ] Perf validation vs. the existing runtime-mesh-generation approach — measure before
      claiming the win.

## Relationship to Existing Code

This design intends **zero rewrite** of the snapshot manager's high-level flow: it reuses the
existing component creation and `FVfxMaterialEffectManager` seam. The new work is (a) producing
and packing the data texture in the build path, and (b) extending the effect manager to bind it.

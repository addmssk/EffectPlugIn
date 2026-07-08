# Snapshot Scar FX — Design & Performance Decision

> Supersedes `artifacts/snapshot_manager_refactor_plan.md` (the per-vertex data-texture plan).
> Use case: localized surface FX ("blade scars") on skinned characters — a hit leaves a
> material effect on the body that tracks skin deformation.
> Grounded in: `VfxSnapshotManagerComponent.cpp`, `SkeletalMeshCompositeUtils.cpp`.

---

## 1. Requirements (fixed)

1. **No artist UV work** — the FX must not force a unique/non-overlapping UV set on characters.
2. **No sliding** — the scar sticks to deforming skin.
3. **Minimize game-thread cost.**
4. **Many simultaneous scars per mesh** (10s).
5. **Per-scar material** — each scar can be a different effect.
6. **Small scale** — not MMO; < 10 characters ever use this.

## 2. Design space, and what each requirement eliminates

| Candidate | Eliminated by | Why |
|---|---|---|
| Global **UV-space mask** (hit-UV + radius, mask in PS) | **#1** | Needs *globally unique* UVs; tiled/mirrored character UVs would mirror the scar. Forces artist UV authoring. |
| **Bone-attached decal** | **#2** | A surface point is a *blend* of bones; pinning to one bone frame swims relative to skin. |
| **Single shared overlay + per-scar param array** | **#5** | One material can't be N genuinely different effects. |
| **Per-vertex data-texture** (the prior plan) | **#1, #3** | Still runs the full O(mesh) CPU cull/projection to fill the texture; needs `VertexID` in the VS (GPU-skin-cache fragile); LOD0-only. Heavy and brittle. |
| **Topological patch** (BFS over triangle adjacency from hit) | **correctness** | A swing is a **swept spatial volume**; it hits **disconnected** islands (e.g. both forearms in a guard). Connectivity ≠ the query. |
| **Content pooling** of generated meshes | **variability** | Surviving vertex set differs every hit (depends on victim×attacker pose), so no content is reusable. (Object-*shell* reuse still helps GC — see §5.) |

**Conclusion:** the region is a **spatial swept-volume ∩ skinned-surface** query, resolved once at hit time and then anchored to the surface so it rides the skin. This is exactly what the current generator does — the selection is already correct (it builds a mesh from any surviving triangle set, disconnected or not). **The problem is cost, not correctness.**

## 3. Current cost hotspots (why it's slow, not why it's wrong)

- 🔴 **Octree rebuilt from scratch every frame**, even with zero live scars — `RefreshObbOctree` at `VfxSnapshotManagerComponent.cpp:1183`, `new`/`delete` whole octree + full transform copy (`:478-528`).
- 🔴 **Whole-mesh selection** — cull seeds from a shape vs. the *entire* mesh; skins every candidate vertex (`:329-411`), and re-skins with the **uncached** matrix path recomputing `RefBasesInvMatrix*BoneMatrix` per influence per vertex (`:161-164`).
- 🟠 **Per-scar `USkeletalMesh` + `InitResources()` on the game thread** (`:812`, `:1101`) + `MarkAsGarbage` (`:1039`).
- 🟠 **Octree read-lock held across the whole skin loop** (`:256`…`:414`), serializing against the every-frame refresh's write lock.

## 4. Chosen approach — localized swept-volume selection → generated sub-mesh

Keep the spatially-correct selection and the sub-mesh generator; make selection localized, one-shot, and cheap; synthesize UVs so artists never touch mesh UVs.

### 4.1 Capture (game thread, once per swing, cheap)

```
during swing frames: record blade transform → TArray<FBladeSegment{ P0, P1, Radius }>   // world
at resolve time:
    convert segments to victim COMPONENT space (source comp-to-world inverse)
    SweptAABB = union of segment AABBs                     // the only region we ever touch
```

The segment list is what makes disconnected islands work: membership is "distance to the
*nearest* blade segment < radius", true on both arms at once.

### 4.2 Selection — rework of `FVfxSnapshotManagerCullingTask::DoWork`

```cpp
// (1) Seed candidates from the swept AABB, NOT the whole mesh.
Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(SweptAABB), [&](const BoneOBB& e){
    ValidVertexBits |= Bones[e.BoneIndex].InfluencingVertices;   // existing OR-union (:275-282)
    CandidateBones.Add(e.BoneIndex);
});
// -- release octree read lock here; the skin loop below does not need it (fixes :256..:414) --

// (2) Precompute RefToLocal ONCE per candidate bone (kills per-influence matrix mul :161-164).
for (int32 b : CandidateBones)
    RefToLocal[b] = SrcMesh->GetRefBasesInvMatrix()[b] * ComponentSpaceTransformCache[b];

// (3) Skin only candidates; test against the swept volume; synthesize ribbon UVs.
for (TConstSetBitIterator<> It(ValidVertexBits); It; ++It) {
    const FVector3f P = SkinCached(It.GetIndex(), RefToLocal);        // cached path, was <false>
    float t; int seg; float dist = ClosestBladeSegment(P, Segments, seg, t);
    if (dist > Segments[seg].Radius) { ValidVertexBits[It.GetIndex()] = false; continue; }
    if (bProjectUV) {
        const float U = ArcLenUpTo(seg) + t * Segments[seg].Length;   // along swing
        const float V = SignedPerpDistance(P, Segments[seg]);         // across blade
        ProjectedUVs[It.GetIndex()] = { U * InvScaleU, V * InvScaleV + 0.5f };
    }
}
```

Because it is seeded by the swept AABB, this skins a few hundred candidate verts near the blade
path instead of the whole body — same correct result, a fraction of the cost.

### 4.3 Octree — make it on-demand (remove the per-frame rebuild)

```
- Tick(): delete the unconditional RefreshObbOctree() call (:1183).
- On swing-resolve: if octree older than N ms, refresh once (async), then run the cull.
- Refresh updates in place (reuse allocation) instead of new/delete (:521-527).
- Optional: low-Hz refresh only for meshes flagged "currently hittable".
```

### 4.4 Emit

Feed the surviving `ValidVertexBits` + generated ribbon `ProjectedUVs` into the existing
`GenerateFilteredSkeletalMesh(...)` (unchanged — already disconnected-island-safe). Attach
leader-pose; assign **this scar's own material**.

- **No slide** — it is the mesh's own triangles under leader pose.
- **No artist UV** — ribbon UVs are synthesized from the blade path (this is what the existing
  `bProjectUV`/`ProjectedUVs` machinery is for) and also give a directional slash look.
- Residual cost: one *small* `InitResources` per scar, amortized over combat — fine at scale #6.

## 5. Object-shell reuse (not content pooling)

Content can't be pooled (surviving set differs per hit), but the **`USkeletalMesh` UObject shell**
can be reused to remove `NewObject` + GC churn (`:812`, `:1039`). Keep a small free-list; on
generate, reset a pooled mesh's LOD render data in place and re-upload the (tiny) buffers. This
requires lifting the explicit reuse guard `check(false)` in `SkeletalMeshCompositeUtils.cpp:578`
behind an explicit "reset for reuse" path. The RHI upload (`InitResources`) still happens per
generate, but the object/GC overhead is gone.

## 6. Alternative — deferred decal per connected island (zero geometry)

If the residual `InitResources` is unacceptable and decal quality is acceptable:

- Union-find the selected triangles into islands (one swing → e.g. 2 islands for 2 arms).
- Per island, store `{triIdx, barycentric, tangentFrame}`; each frame skin that anchor (3 verts)
  and drive a **deferred decal** with the scar's material.
- ✔ no mesh gen, no `InitResources`, per-decal material, handles disconnection, no UVs.
- ✖ shear/bleed on strongly curved/deforming regions; can project onto adjacent body parts inside
  the decal box. Keep boxes small/thin/surface-oriented.

## 7. Requirement check

| # | Requirement | How met |
|---|---|---|
| 1 | No artist UV | Ribbon UVs synthesized from blade path; mesh UVs untouched |
| 2 | No slide | Selection rides mesh's own verts (§4.4) / skinned anchor (§6) — never a bone frame |
| 3 | Game thread | Cull seeded by swept AABB + on-demand octree + cached skin ⇒ O(candidates) once/swing, ~0/frame |
| 4 | Many scars | Per-scar primitive; dozens fine |
| 5 | Per-scar material | Own material per generated sub-mesh / per decal |
| 6 | Small scale | Residual per-scar `InitResources` (or decal count) is negligible at < 10 chars |

## 8. Work items (targeted, not a rewrite)

1. `RefreshObbOctree` → on-demand + update-in-place; drop the per-frame call. *(P0, low risk)*
2. `FVfxSnapshotManagerCullingTask::DoWork` → swept-AABB seed, narrow lock, cached RefToLocal,
   swept-volume membership, ribbon-UV synthesis. *(P0)*
3. Swing capture → `TArray<FBladeSegment>` + component-space conversion at resolve. *(new, small)*
4. `USkeletalMesh` shell free-list + reuse path (lift `check(false)` at composite util `:578`). *(P1)*
5. (Optional) island split + deferred-decal emit path as the zero-geometry alternative. *(P2)*

> Validate with the existing `VfxSnapshotCulledMeshGenerator` thread-pool stat and
> `msk.fx.snapshot.GeneratorForceSync` CVar to isolate generation cost while profiling.

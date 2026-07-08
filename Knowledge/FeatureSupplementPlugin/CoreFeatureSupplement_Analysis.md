### 1. TraversalGameplay (Gameplay & Movement)

This module focuses on extending character movement, parkour/traversal, and spline-based pathing.

• Custom Movement Component ( TrvCharacterMovementComponent ): Extends  UCharacterMovementComponent  to add custom movement modes, primarily for Sliding and
Following Flight Paths. It includes extensive parameters for slope angles, sliding friction, acceleration, and integration with Spline paths.
• Parkour & Environment Analysis ( TrvCustomMoveArrangeComponent ): Acts as an asynchronous environment scanner for traversal systems. It runs tasks to analyze
concave shapes (gaps, pits) and convex shapes (hurdles, walls, ledges) using line traces. It yields dimensions (depth, height, ledge location) and calculates
FQuat  target rotations to sync with  UMotionWarpingComponent  for smooth vaulting and jumping animations.
• Spline Flight Paths ( TrvSplineFlightPath ,  TrvSplinePathIndicator ): Provides actors and data structures for defining spline paths that characters can
travel along. It is coupled with the custom movement component and a custom Gameplay Ability Task ( TrvAbilityTask_ApplyRootMotionJumpUsingSplineFlightPath )
for locking root motion jumping to these splines.

### 2. VisualEffects (VFX & Rendering)

This module focuses on advanced visual effects, specifically mesh snapshotting (ghost/trail/freeze effects) and complex Niagara integrations.

• Snapshot Manager System ( VfxSnapshotManagerComponent ,  SkeletalMeshCompositeUtils ): A highly optimized system to capture "snapshots" of meshes (Skeletal,
Static, and Groom) at runtime. This is likely used for after-image trails, time-freeze effects, or petrification.
  • Notable Architecture/Optimization: The snapshotting process avoids deep copying full skeletal meshes blindly. It builds customized Static/Skeletal meshes
  on the fly using multithreaded Octree culling ( FVfxSnapshotSourceMeshBoneOBBOctree ), backface discarding, and UV overrides to strip out unseen geometry.
  • Niagara Integration: It dynamically generates specific particle sampling regions ( FSkeletalMeshSamplingRegion ) on the newly built snapshot meshes,
  allowing Niagara systems to accurately spawn particles (like embers or dissolving dust) from the exact pose of the ghost mesh.
• Niagara Beam Coordination ( VfxBeamCoordinatorComponent ): Interacts directly with Niagara via a custom data interface (
UNiagaraDataInterfaceBeamCoordinatorComponent ), likely allowing complex multi-point beam coordination (e.g., chain lightning or tether effects).

### Potential Issues / Observations

• Complex Async State Management:  TrvCustomMoveArrangeComponent  uses raw  void*  pointers for its async tasks ( AsyncConcaveAnalysisTask ,
AsyncConvexAnalysisTask ) and manual cycle tracking. Care must be taken to ensure these tasks are properly cancelled and cleaned up during level transitions or
actor destruction to prevent memory leaks or dangling pointer crashes.
• Runtime Mesh Generation Overhead: Although the  VfxSnapshotManagerComponent  tries to optimize mesh generation via asynchronous culling, generating skeletal
meshes at runtime is inherently heavy. If  MaxLiveSnapshotMeshCount  is set too high or if the mesh polycount is massive, it could cause frame hitches.
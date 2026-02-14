# VividVision Vulkan FBX Prototype Plan & Status

## Scope
- Primary platform: macOS + Apple Silicon + MoltenVK.
- Rendering backend: Vulkan only.
- Milestone goal: FBX closed loop with animation, materials, lights, shadows, and interactive demo camera.

## Architecture
- `engine/platform`: platform abstraction (`IWindow`, `IFileSystem`, `IClock`, `IThreading`) and macOS implementation.
- `engine/rhi/vulkan`: instance/device/swapchain/command/render loop ownership.
- `engine/asset/import`: FBX import via Assimp into internal scene model.
- `engine/render`: scene model, animation, lighting, material, render pass system.
- `engine/app`: demo runtime loop, input handling, logging.

## Math Convention
- Unit: meter.
- Handedness: right-handed.
- Up: `+Y`.
- Forward: `-Z`.
- Matrix convention: column-major + column vectors.
- Transform chain: `world = parent * local`.
- Skinning chain: `skinMatrix = animatedGlobal * inverseBind`.

## Implemented Status
- [x] CMake/CPM project scaffold and dependency wiring.
- [x] Platform-isolated Vulkan surface creation on macOS via GLFW.
- [x] Vulkan swapchain + depth + main render pass loop.
- [x] FBX import for nodes, meshes, skeletons, skin weights, clips, materials, lights.
- [x] CPU animation sampling and palette generation.
- [x] GPU skinning pipeline and draw submission.
- [x] PBR material path with legacy spec-gloss compatibility.
- [x] Texture decode/upload path with fallback textures.
- [x] Mipmap generation and anisotropic sampling support.
- [x] Directional shadow map (single cascade) integrated into main shading.
- [x] Shadow stability/quality upgrade (texel snapping + weighted PCF 5x5).
- [x] Demo procedural grid ground mesh for shadow reception.
- [x] Demo orbit camera controls (`RMB drag` + wheel zoom).
- [x] Runtime debug toggles for normal map and specular IBL.
- [x] Regression/unit tests for animation, weights, and HipHop import.

## Current Risks / Open Technical Debt
- IBL is approximate and can still show unstable shading artifacts on some assets.
- Shadow system is directional-only and single-cascade.
- Bone palette capacity is fixed to 1024 matrices/frame.
- Texture streaming/reload is not implemented.

## Next Milestones
1. Shadow system upgrade
- Cascaded shadow maps (2-4 cascades).
- Better cascade fitting and stabilization.
- Runtime shadow quality presets.

2. IBL completion
- HDR environment input pipeline.
- Offline/online irradiance and prefilter generation.
- BRDF LUT integration.

3. Material diagnostics
- Debug views for albedo/normal/roughness/metallic/AO/shadow.
- Per-material overrides for rapid triage.

4. Resource/runtime robustness
- Texture streaming/reload.
- Larger or streaming bone palettes.
- Render path profiling hooks.

## Validation Checklist
- Build and run demo successfully.
- Load target FBX and verify scene stats logs.
- Verify animation controls and camera controls.
- Verify directional shadow on model and floor.
- Run all tests with no failures.

# VividVision Prototype

Vulkan-based cross-platform rendering engine prototype focused on an FBX render loop (`import -> animate -> light -> PBR -> present`).

## Current State
- Platform architecture is isolated (`engine/platform/*`), with macOS path implemented via GLFW + MoltenVK-compatible Vulkan setup.
- Vulkan renderer runs swapchain + depth + main pass and an additional directional shadow pass.
- Assimp FBX import supports scene nodes, meshes, skeleton/weights, clips, materials, and lights.
- CPU animation sampling + GPU skinning is active (up to 4 influences/vertex).
- PBR path supports baseColor, normal, occlusion, emissive, metallic/roughness (packed or separate), alpha mask, and legacy spec-gloss fallback.
- Directional shadow map is implemented (single cascade) with stabilization and weighted PCF filtering.
- Texture upload supports mipmap generation (capability-based fallback) and anisotropic filtering when supported by device.
- Demo automatically appends a static procedural grid floor for scale/grounding and shadow reception.
- Demo camera supports orbit and zoom (`RMB drag` + `mouse wheel`).

## Build Prerequisites (macOS)
- CMake 3.25+
- Vulkan SDK with MoltenVK
- Xcode command line tools
- Optional: `glslangValidator` for shader compilation

## Build
```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

## Run Demo
```bash
./build/engine/vividvision_demo /absolute/path/to/model.fbx
```

## Controls
- `Space`: pause/resume animation
- `N` / `P`: next/previous clip
- `+` / `-`: animation speed up/down
- `Right Mouse Drag`: orbit around model center
- `Mouse Wheel`: zoom in/out
- `1`: toggle normal map (debug)
- `2`: toggle specular IBL (debug)

## Tests
```bash
ctest --test-dir build --output-on-failure
```

Current tests:
- `vv_unit_animator`
- `vv_unit_weights`
- `vv_unit_import_hiphop`

## Validation Focus
- Rendering correctness: swapchain present, depth correctness, resize behavior.
- Shadow correctness: shadow appears on model and floor, reduced shimmering with camera movement.
- Animation correctness: clip loop/switch/pause, skin deformation stability.
- Material correctness: FBX texture mapping, spec-gloss fallback behavior, normal convention handling.
- Performance sanity: runtime FPS and ms/frame logs.

## Known Gaps
- Bone palette budget is fixed at 1024 matrices/frame.
- Shadow supports directional light only and only single-cascade.
- IBL is still procedural + approximate; full HDR asset pipeline (`irradiance + prefilter + BRDF LUT`) is not implemented.
- On some assets, IBL may still show unstable shading artifacts; this path is currently deprioritized and can be diagnosed with runtime debug toggles.
- Runtime texture streaming/reload is pending.

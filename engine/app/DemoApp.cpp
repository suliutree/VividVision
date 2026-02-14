#include "app/DemoApp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "asset/import/AssimpFbxImporter.hpp"
#include "core/log/Log.hpp"
#include "platform/common/InputCodes.hpp"
#include "platform/macos/MacWindowGLFW.hpp"
#include "render/animation/Animator.hpp"
#include "render/scene/RenderScene.hpp"
#include "render/scene/SceneTypes.hpp"
#include "rhi/vulkan/VulkanRenderer.hpp"

namespace vv {
namespace {

AABB ComputeWorldBounds(const Scene& scene) {
  Vec3 bmin(FLT_MAX);
  Vec3 bmax(-FLT_MAX);
  bool hasBounds = false;

  for (const Node& node : scene.nodes) {
    if (!node.mesh.has_value()) {
      continue;
    }
    const MeshId meshId = *node.mesh;
    if (meshId >= scene.meshes.size()) {
      continue;
    }
    const Mesh& mesh = scene.meshes[meshId];
    const Vec3 lmin = mesh.localBounds.min;
    const Vec3 lmax = mesh.localBounds.max;
    const std::array<Vec3, 8> corners = {
        Vec3(lmin.x, lmin.y, lmin.z), Vec3(lmax.x, lmin.y, lmin.z), Vec3(lmin.x, lmax.y, lmin.z), Vec3(lmax.x, lmax.y, lmin.z),
        Vec3(lmin.x, lmin.y, lmax.z), Vec3(lmax.x, lmin.y, lmax.z), Vec3(lmin.x, lmax.y, lmax.z), Vec3(lmax.x, lmax.y, lmax.z)};
    for (const Vec3& c : corners) {
      const Vec3 world = Vec3(node.worldCurrent * Vec4(c, 1.0F));
      bmin = glm::min(bmin, world);
      bmax = glm::max(bmax, world);
      hasBounds = true;
    }
  }

  if (!hasBounds) {
    bmin = Vec3(-1.0F, -1.0F, -1.0F);
    bmax = Vec3(1.0F, 1.0F, 1.0F);
  }

  return {bmin, bmax};
}

void ComputeOrbitDefaults(const AABB& bounds, Vec3& target, float& distance, float& yaw, float& pitch) {
  target = 0.5F * (bounds.min + bounds.max);
  const Vec3 ext = glm::max(bounds.max - bounds.min, Vec3(0.1F));
  const float radius = std::max({ext.x, ext.y, ext.z}) * 0.5F;
  distance = std::max(2.5F, radius * 2.5F);
  yaw = glm::pi<float>();
  pitch = 0.22F;
}

void AppendDemoGridGround(Scene& scene) {
  const AABB bounds = ComputeWorldBounds(scene);
  const Vec3 center = 0.5F * (bounds.min + bounds.max);
  const Vec3 size = glm::max(bounds.max - bounds.min, Vec3(0.1F));

  const float halfX = std::max(4.0F, size.x * 1.5F);
  const float halfZ = std::max(4.0F, size.z * 1.5F);
  const float y = bounds.min.y - 0.02F;

  Material floorMat;
  floorMat.name = "DemoGridGround";
  floorMat.baseColorFactor = Vec4(0.66F, 0.68F, 0.72F, 1.0F);
  floorMat.metallicFactor = 0.0F;
  floorMat.roughnessFactor = 0.95F;
  floorMat.baseColorTex = 0;
  floorMat.metallicRoughnessTex = 4;
  floorMat.metallicTex = 1;
  floorMat.roughnessTex = 4;
  floorMat.normalTex = 2;
  floorMat.occlusionTex = 4;
  floorMat.emissiveTex = 1;
  floorMat.specularTex = 3;
  floorMat.gridOverlay = true;
  const MaterialId materialId = static_cast<MaterialId>(scene.materials.size());
  scene.materials.push_back(std::move(floorMat));

  Mesh floorMesh;
  floorMesh.name = "DemoGridGroundMesh";
  floorMesh.vertices.resize(4);
  floorMesh.indices = {0, 1, 2, 0, 2, 3};

  const std::array<Vec3, 4> positions = {
      Vec3(center.x - halfX, y, center.z - halfZ),
      Vec3(center.x + halfX, y, center.z - halfZ),
      Vec3(center.x + halfX, y, center.z + halfZ),
      Vec3(center.x - halfX, y, center.z + halfZ),
  };
  const std::array<Vec2, 4> uvs = {Vec2(0.0F, 0.0F), Vec2(1.0F, 0.0F), Vec2(1.0F, 1.0F), Vec2(0.0F, 1.0F)};

  for (size_t i = 0; i < floorMesh.vertices.size(); ++i) {
    VertexSkinned v;
    v.position = positions[i];
    v.normal = Vec3(0.0F, 1.0F, 0.0F);
    v.tangent = Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    v.uv0 = uvs[i];
    v.joints = {0, 0, 0, 0};
    v.weights = {1.0F, 0.0F, 0.0F, 0.0F};
    floorMesh.vertices[i] = v;
  }

  floorMesh.localBounds.min = Vec3(center.x - halfX, y, center.z - halfZ);
  floorMesh.localBounds.max = Vec3(center.x + halfX, y, center.z + halfZ);

  Submesh floorSubmesh;
  floorSubmesh.firstIndex = 0;
  floorSubmesh.indexCount = static_cast<uint32_t>(floorMesh.indices.size());
  floorSubmesh.material = materialId;
  floorMesh.submeshes.push_back(floorSubmesh);

  const MeshId meshId = static_cast<MeshId>(scene.meshes.size());
  scene.meshes.push_back(std::move(floorMesh));

  Node floorNode;
  floorNode.name = "DemoGridGroundNode";
  floorNode.parent = kInvalidNodeId;
  floorNode.localBind = Transform{};
  floorNode.localCurrent = floorNode.localBind;
  floorNode.worldCurrent = Mat4(1.0F);
  floorNode.mesh = meshId;
  floorNode.skin = std::nullopt;
  floorNode.light = std::nullopt;

  const NodeId floorNodeId = static_cast<NodeId>(scene.nodes.size());
  scene.nodes.push_back(std::move(floorNode));
  scene.roots.push_back(floorNodeId);
}

}  // namespace

int DemoApp::Run(const std::string& fbxPath) {
  log::Initialize();
  auto logger = log::Get();

  logger->info("VividVision Demo starting");
  logger->info("Input mapping: Space=pause/resume, N=next clip, P=previous clip, +=speed up, -=speed down");
  logger->info("Mouse mapping: Right-drag=orbit, Wheel=zoom");
  logger->info("Debug mapping: 1=toggle normal map, 2=toggle specular IBL");

  MacWindowGLFW window(1280, 720, "VividVision Vulkan FBX Demo");
  VulkanRenderer renderer;
#if defined(VV_ENABLE_VALIDATION)
  constexpr bool kEnableValidation = true;
#else
  constexpr bool kEnableValidation = false;
#endif
  renderer.Initialize(window, kEnableValidation);

  Scene scene;
  std::vector<Animator> animators;
  std::vector<uint32_t> skeletonPaletteOffsets;
  std::vector<Mat4> combinedPalette;
  ClipId activeClip = 0;
  constexpr uint32_t kBonePaletteCapacity = 1024;
  Vec3 orbitTarget(0.0F, 1.0F, 0.0F);
  float orbitDistance = 3.5F;
  float orbitYaw = glm::pi<float>();
  float orbitPitch = 0.22F;

  if (!fbxPath.empty()) {
    const auto t0 = std::chrono::steady_clock::now();
    AssimpFbxImporter importer;
    ImportOptions options;
    const auto loaded = importer.Import(fbxPath, options);
    const auto t1 = std::chrono::steady_clock::now();

    if (!loaded.Ok()) {
      logger->error("FBX import failed: {}", loaded.error);
      return 1;
    }

    scene = std::move(*loaded.value);
    const SceneStats stats = ComputeSceneStats(scene);
    const double loadMs = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    logger->info("FBX loaded: {} ms", loadMs);
    logger->info("Meshes: {}, Materials: {}, Textures: {}", stats.meshCount, stats.materialCount, stats.textureCount);
    logger->info("Triangles: {}, Skeletons: {}, Bones: {}, Clips: {}, Lights: {}",
                 stats.triangleCount,
                 stats.skeletonCount,
                 stats.boneCount,
                 stats.clipCount,
                 stats.lightCount);
    for (size_t i = 0; i < scene.materials.size(); ++i) {
      const auto& mat = scene.materials[i];
      logger->info("Material[{}]: specGloss={}, separateMR={}, flipNormalY={}, roughness={:.3f}, metallic={:.3f}, ao={:.2f}, normalScale={:.2f}, baseTex={}, mrTex={}, mTex={}, rTex={}, aoTex={}, normalTex={}, specTex={}",
                   i,
                   mat.useSpecularGlossiness,
                   mat.useSeparateMetalRoughness,
                   mat.normalGreenInverted,
                   mat.roughnessFactor,
                   mat.metallicFactor,
                   mat.occlusionStrength,
                   mat.normalScale,
                   mat.baseColorTex,
                   mat.metallicRoughnessTex,
                   mat.metallicTex,
                   mat.roughnessTex,
                   mat.occlusionTex,
                   mat.normalTex,
                   mat.specularTex);
    }

    const AABB modelBounds = ComputeWorldBounds(scene);
    ComputeOrbitDefaults(modelBounds, orbitTarget, orbitDistance, orbitYaw, orbitPitch);
    AppendDemoGridGround(scene);
    logger->info("Demo ground: enabled (grid floor mesh appended)");

    if (!scene.skeletons.empty()) {
      animators.resize(scene.skeletons.size());
      skeletonPaletteOffsets.resize(scene.skeletons.size(), 0);
      for (SkeletonId sid = 0; sid < animators.size(); ++sid) {
        animators[sid].Bind(&scene, sid);
      }
    }
    if (!scene.clips.empty() && !animators.empty()) {
      activeClip = 0;
      for (auto& animator : animators) {
        animator.SetClip(activeClip, true);
      }
      logger->info("Default clip: {} (duration {:.3f}s)", scene.clips[activeClip].name, scene.clips[activeClip].durationSec);
    }
  } else {
    logger->warn("No FBX path provided. Running renderer with empty scene.");
  }

  bool prevPause = false;
  bool prevNext = false;
  bool prevPrev = false;
  bool prevToggleNormal = false;
  bool prevToggleSpecIbl = false;
  bool enableNormalMap = true;
  bool enableSpecularIbl = true;
  bool prevOrbitButton = false;
  double lastCursorX = 0.0;
  double lastCursorY = 0.0;
  float perfAccumSec = 0.0F;
  uint32_t perfFrameCount = 0;

  auto lastTick = std::chrono::steady_clock::now();
  while (window.PollEvents()) {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    perfAccumSec += dt;
    perfFrameCount += 1;
    if (perfAccumSec >= 1.0F) {
      const float fps = perfFrameCount / perfAccumSec;
      const float frameMs = 1000.0F / std::max(fps, 1.0F);
      logger->info("Perf: {:.1f} FPS, {:.2f} ms/frame", fps, frameMs);
      perfAccumSec = 0.0F;
      perfFrameCount = 0;
    }

    const bool pause = window.IsKeyPressed(DemoInputMap::kPause);
    const bool nextClip = window.IsKeyPressed(DemoInputMap::kNextClip);
    const bool prevClip = window.IsKeyPressed(DemoInputMap::kPrevClip);
    const bool toggleNormal = window.IsKeyPressed(DemoInputMap::kToggleNormalMap);
    const bool toggleSpecIbl = window.IsKeyPressed(DemoInputMap::kToggleSpecularIbl);
    const bool orbitButton = window.IsMouseButtonPressed(DemoInputMap::kOrbitButton);
    double cursorX = 0.0;
    double cursorY = 0.0;
    window.GetCursorPosition(cursorX, cursorY);
    const float scrollDelta = window.ConsumeScrollDeltaY();

    if (pause && !prevPause) {
      if (!animators.empty()) {
        const bool nextPaused = !animators[0].State().paused;
        for (auto& animator : animators) {
          animator.SetPaused(nextPaused);
        }
        logger->info("Animator paused={}", nextPaused);
      }
    }
    if (nextClip && !prevNext && !scene.clips.empty() && !animators.empty()) {
      activeClip = (activeClip + 1) % static_cast<ClipId>(scene.clips.size());
      for (auto& animator : animators) {
        animator.SetClip(activeClip, true);
      }
      logger->info("Switched clip to [{}] {}", activeClip, scene.clips[activeClip].name);
    }
    if (prevClip && !prevPrev && !scene.clips.empty() && !animators.empty()) {
      const ClipId count = static_cast<ClipId>(scene.clips.size());
      activeClip = (activeClip + count - 1) % count;
      for (auto& animator : animators) {
        animator.SetClip(activeClip, true);
      }
      logger->info("Switched clip to [{}] {}", activeClip, scene.clips[activeClip].name);
    }
    if (toggleNormal && !prevToggleNormal) {
      enableNormalMap = !enableNormalMap;
      logger->info("Debug: normal map {}", enableNormalMap ? "ON" : "OFF");
    }
    if (toggleSpecIbl && !prevToggleSpecIbl) {
      enableSpecularIbl = !enableSpecularIbl;
      logger->info("Debug: specular IBL {}", enableSpecularIbl ? "ON" : "OFF");
    }

    if (orbitButton) {
      if (prevOrbitButton) {
        const float dx = static_cast<float>(cursorX - lastCursorX);
        const float dy = static_cast<float>(cursorY - lastCursorY);
        orbitYaw -= dx * 0.0055F;
        orbitPitch -= dy * 0.0042F;
        orbitPitch = glm::clamp(orbitPitch, -1.35F, 1.35F);
      }
      lastCursorX = cursorX;
      lastCursorY = cursorY;
    }
    if (scrollDelta != 0.0F) {
      orbitDistance *= std::exp(-scrollDelta * 0.12F);
      orbitDistance = glm::clamp(orbitDistance, 0.8F, 60.0F);
    }

    prevPause = pause;
    prevNext = nextClip;
    prevPrev = prevClip;
    prevToggleNormal = toggleNormal;
    prevToggleSpecIbl = toggleSpecIbl;
    prevOrbitButton = orbitButton;

    if (!scene.clips.empty() && !animators.empty()) {
      if (window.IsKeyPressed(DemoInputMap::kSpeedUp)) {
        const float nextSpeed = animators[0].State().speed + 0.5F * dt;
        for (auto& animator : animators) {
          animator.SetSpeed(nextSpeed);
        }
      }
      if (window.IsKeyPressed(DemoInputMap::kSpeedDown)) {
        const float nextSpeed = animators[0].State().speed - 0.5F * dt;
        for (auto& animator : animators) {
          animator.SetSpeed(nextSpeed);
        }
      }
      for (auto& animator : animators) {
        animator.Update(dt);
      }
    }

    combinedPalette.clear();
    if (!animators.empty()) {
      if (skeletonPaletteOffsets.size() != scene.skeletons.size()) {
        skeletonPaletteOffsets.resize(scene.skeletons.size(), 0);
      }
      for (SkeletonId sid = 0; sid < animators.size(); ++sid) {
        const auto& palette = animators[sid].Palette();
        if (combinedPalette.size() + palette.size() > kBonePaletteCapacity) {
          skeletonPaletteOffsets[sid] = 0;
          continue;
        }
        skeletonPaletteOffsets[sid] = static_cast<uint32_t>(combinedPalette.size());
        combinedPalette.insert(combinedPalette.end(), palette.begin(), palette.end());
      }
    }

    RenderScene renderScene;
    renderScene.scene = &scene;
    renderScene.skinPalette = &combinedPalette;
    renderScene.skeletonPaletteOffsets = &skeletonPaletteOffsets;

    FrameContext frame;
    frame.deltaSec = dt;
    frame.enableNormalMap = enableNormalMap ? 1.0F : 0.0F;
    frame.enableSpecularIbl = enableSpecularIbl ? 1.0F : 0.0F;
    const Vec3 orbitDir(std::cos(orbitPitch) * std::sin(orbitYaw),
                        std::sin(orbitPitch),
                        std::cos(orbitPitch) * std::cos(orbitYaw));
    frame.cameraPos = orbitTarget + orbitDir * orbitDistance;
    frame.view = glm::lookAt(frame.cameraPos, orbitTarget, Vec3(0.0F, 1.0F, 0.0F));

    const Extent2D extent = window.GetFramebufferSize();
    const float aspect = extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0F;
    frame.proj = glm::perspective(glm::radians(60.0F), aspect, 0.1F, 200.0F);
    frame.proj[1][1] *= -1.0F;

    renderer.RenderFrame(renderScene, frame);
  }

  renderer.Shutdown();
  return 0;
}

}  // namespace vv

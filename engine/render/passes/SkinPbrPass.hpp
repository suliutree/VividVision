#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "render/scene/RenderScene.hpp"

namespace vv {

class SkinPbrPass {
 public:
  static constexpr uint32_t kFramesInFlight = 2;

  void Initialize(VkPhysicalDevice physicalDevice,
                  VkDevice device,
                  VkQueue graphicsQueue,
                  uint32_t queueFamilyIndex,
                  VkRenderPass renderPass,
                  VkExtent2D extent,
                  VkFormat outputFormat,
                  const std::string& shaderDir);
  void Shutdown();

  void RecreateForRenderPass(VkRenderPass renderPass, VkExtent2D extent, VkFormat outputFormat);

  void PrepareFrame(uint32_t frameIndex, const RenderScene& scene, const FrameContext& frameContext);
  void RenderShadow(VkCommandBuffer cmd, uint32_t frameIndex, const RenderScene& scene);
  void Render(VkCommandBuffer cmd,
              uint32_t frameIndex,
              const RenderScene& scene,
              const FrameContext& frameContext);

 private:
  struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
  };

  struct MeshGpu {
    Buffer vertex;
    Buffer index;
    uint32_t indexCount = 0;
  };

  struct TextureGpu {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t mipLevels = 1;
  };

  struct FrameUbo {
    Mat4 view{1.0F};
    Mat4 proj{1.0F};
    Mat4 lightViewProj{1.0F};
    Vec4 cameraPos{0.0F, 0.0F, 3.0F, 1.0F};
    Vec4 lightMeta{1.0F, 0.12F, 1.18F, 1.22F};  // x=lightCount, y=ambientStrength, z=exposure, w=iblStrength
    Vec4 debugFlags{1.0F, 1.0F, 0.0F, 255.0F};  // x=enableNormalMap, y=enableSpecularIbl, z=timeSec, w=outputColorLevels
    Vec4 shadowMeta{1.0F, 0.0008F, 0.92F, 1.5F};  // x=enabled, y=bias, z=strength, w=pcfRadiusTexel
  };

  struct LightGpu {
    Vec4 positionRange{0.0F, 2.0F, 0.0F, 50.0F};       // xyz=position, w=range
    Vec4 directionType{0.0F, -1.0F, 0.0F, 0.0F};       // xyz=direction, w=type(0/1/2)
    Vec4 colorIntensity{1.0F, 1.0F, 1.0F, 1.0F};       // rgb=color, w=intensity
    Vec4 coneCos{0.95F, 0.85F, 0.0F, 0.0F};            // x=innerCos, y=outerCos
  };

  struct DrawPush {
    Mat4 model{1.0F};
    Vec4 baseColor{1.0F};
    Vec4 emissive{0.0F, 0.0F, 0.0F, 1.0F};  // xyz=emissiveFactor, w=emissiveStrength
    Vec4 flags{0.0F};  // x=useSpecGloss, y=signedNormalScale, z=occlusionStrength, w=useSeparateMetalRoughness
    Vec4 mrAlpha{0.0F, 1.0F, 0.5F, 0.0F};  // x=metallic, y=roughness, z=alphaCutoff, w=boneOffset
  };

  struct ShadowPush {
    Mat4 model{1.0F};
    Vec4 misc{0.0F};  // x=boneOffset
  };

  uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
  Buffer CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      bool persistentMap);
  void DestroyBuffer(Buffer& buffer);

  void CreateDescriptorLayouts();
  void DestroyDescriptorLayouts();
  void CreateFrameDescriptorPool();
  void DestroyFrameDescriptorPool();
  void CreateMaterialDescriptorPool();
  void DestroyMaterialDescriptorPool();
  void CreatePerFrameBuffers();
  void DestroyPerFrameBuffers();
  void AllocateAndWriteFrameDescriptorSets();
  void RebuildMaterialDescriptorSets(const Scene& scene);

  void CreatePipeline(VkRenderPass renderPass);
  void DestroyPipeline();
  void CreateShadowResources();
  void DestroyShadowResources();
  void CreateShadowPipeline();
  void DestroyShadowPipeline();

  std::vector<uint32_t> ReadSpv(const std::string& path) const;
  VkShaderModule CreateShaderModule(const std::vector<uint32_t>& words) const;

  void EnsureSceneUploaded(const Scene* scene);
  void UploadScene(const Scene& scene);
  void DestroySceneBuffers();
  void UploadTextures(const Scene& scene);
  void DestroyTextures();
  void CreateIblEnvironmentTexture();
  void DestroyIblEnvironmentTexture();

  void UpdateFrameUbo(uint32_t frameIndex,
                      const RenderScene& scene,
                      const FrameContext& frameContext,
                      uint32_t lightCount);
  Mat4 ComputeDirectionalShadowMatrix(const RenderScene& scene) const;
  void UpdateBoneBuffer(uint32_t frameIndex, const RenderScene& scene);
  uint32_t UpdateLightBuffer(uint32_t frameIndex, const RenderScene& scene);

  VkCommandPool CreateTransientCommandPool() const;
  void DestroyTransientCommandPool();
  VkCommandBuffer BeginOneShot() const;
  void EndOneShot(VkCommandBuffer cmd) const;
  void TransitionImageLayout(VkCommandBuffer cmd,
                             VkImage image,
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout) const;

  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex_ = 0;
  VkExtent2D extent_{};
  VkCommandPool transientCommandPool_ = VK_NULL_HANDLE;

  VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout boneSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
  VkFramebuffer shadowFramebuffer_ = VK_NULL_HANDLE;
  VkImage shadowDepthImage_ = VK_NULL_HANDLE;
  VkDeviceMemory shadowDepthMemory_ = VK_NULL_HANDLE;
  VkImageView shadowDepthView_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkSampler iblSampler_ = VK_NULL_HANDLE;
  VkSampler shadowSampler_ = VK_NULL_HANDLE;

  VkDescriptorPool frameDescriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorPool materialDescriptorPool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, kFramesInFlight> frameSets_{};
  std::array<VkDescriptorSet, kFramesInFlight> boneSets_{};
  std::vector<VkDescriptorSet> materialSets_;

  std::array<Buffer, kFramesInFlight> frameUboBuffers_{};
  std::array<Buffer, kFramesInFlight> boneSsboBuffers_{};
  std::array<Buffer, kFramesInFlight> lightSsboBuffers_{};

  std::vector<MeshGpu> meshBuffers_;
  std::vector<TextureGpu> textureGpus_;
  TextureGpu iblEnvironment_{};
  const Scene* uploadedScene_ = nullptr;
  bool boneOverflowWarned_ = false;

  std::string vertSpvPath_;
  std::string fragSpvPath_;
  std::string shadowVertSpvPath_;

  float outputColorLevels_ = 255.0F;
  float elapsedSec_ = 0.0F;

  bool initialized_ = false;
};

}  // namespace vv

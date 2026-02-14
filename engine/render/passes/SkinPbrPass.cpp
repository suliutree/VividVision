#include "render/passes/SkinPbrPass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "rhi/vulkan/VulkanCheck.hpp"

namespace vv {
namespace {

constexpr VkDeviceSize kMaxBoneMatrices = 1024;
constexpr uint32_t kMaxLights = 64;
constexpr uint32_t kMaterialDescriptorCapacity = 1024;
constexpr uint32_t kIblWidth = 512;
constexpr uint32_t kIblHeight = 256;
constexpr uint32_t kShadowMapSize = 2048;
constexpr float kPi = 3.14159265359F;

bool SupportsSampledTransferDst(VkPhysicalDevice physicalDevice, VkFormat format) {
  VkFormatProperties props{};
  vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
  const VkFormatFeatureFlags features = props.optimalTilingFeatures;
  return (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
         (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
}

bool SupportsLinearBlit(VkPhysicalDevice physicalDevice, VkFormat format) {
  VkFormatProperties props{};
  vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
  const VkFormatFeatureFlags features = props.optimalTilingFeatures;
  return (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0 &&
         (features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0 &&
         (features & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
}

std::vector<float> BuildProceduralIblPixels(uint32_t width, uint32_t height) {
  std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U, 1.0F);

  const Vec3 sunDir = glm::normalize(Vec3(0.35F, 0.78F, 0.24F));
  const Vec3 horizon(0.95F, 0.88F, 0.78F);
  const Vec3 skyZenith(0.30F, 0.55F, 0.95F);
  const Vec3 ground(0.08F, 0.08F, 0.09F);

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
      const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);

      const float phi = (u - 0.5F) * (2.0F * kPi);
      const float theta = v * kPi;
      const float sinTheta = std::sin(theta);
      Vec3 dir(std::cos(phi) * sinTheta, std::cos(theta), std::sin(phi) * sinTheta);
      dir = glm::normalize(dir);

      const float upFactor = std::clamp(dir.y * 0.5F + 0.5F, 0.0F, 1.0F);
      const Vec3 sky = glm::mix(horizon, skyZenith, std::pow(upFactor, 0.35F));
      const Vec3 dome = (dir.y >= 0.0F) ? sky : glm::mix(ground, horizon * 0.35F, upFactor);

      const float sunNoL = std::max(glm::dot(dir, sunDir), 0.0F);
      const float sunTerm = std::pow(sunNoL, 220.0F);
      const Vec3 sunColor = Vec3(7.0F, 6.0F, 4.8F) * sunTerm;

      const Vec3 finalColor = dome + sunColor;
      const size_t base = (static_cast<size_t>(y) * static_cast<size_t>(width) + x) * 4U;
      pixels[base + 0] = finalColor.r;
      pixels[base + 1] = finalColor.g;
      pixels[base + 2] = finalColor.b;
      pixels[base + 3] = 1.0F;
    }
  }

  return pixels;
}

VkVertexInputBindingDescription MakeVertexBinding() {
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(VertexSkinned);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return binding;
}

std::array<VkVertexInputAttributeDescription, 6> MakeVertexAttributes() {
  std::array<VkVertexInputAttributeDescription, 6> attrs{};

  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = static_cast<uint32_t>(offsetof(VertexSkinned, position));

  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = static_cast<uint32_t>(offsetof(VertexSkinned, normal));

  attrs[2].binding = 0;
  attrs[2].location = 2;
  attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attrs[2].offset = static_cast<uint32_t>(offsetof(VertexSkinned, tangent));

  attrs[3].binding = 0;
  attrs[3].location = 3;
  attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
  attrs[3].offset = static_cast<uint32_t>(offsetof(VertexSkinned, uv0));

  attrs[4].binding = 0;
  attrs[4].location = 4;
  attrs[4].format = VK_FORMAT_R16G16B16A16_UINT;
  attrs[4].offset = static_cast<uint32_t>(offsetof(VertexSkinned, joints));

  attrs[5].binding = 0;
  attrs[5].location = 5;
  attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attrs[5].offset = static_cast<uint32_t>(offsetof(VertexSkinned, weights));

  return attrs;
}

std::array<VkVertexInputAttributeDescription, 3> MakeShadowVertexAttributes() {
  std::array<VkVertexInputAttributeDescription, 3> attrs{};

  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = static_cast<uint32_t>(offsetof(VertexSkinned, position));

  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R16G16B16A16_UINT;
  attrs[1].offset = static_cast<uint32_t>(offsetof(VertexSkinned, joints));

  attrs[2].binding = 0;
  attrs[2].location = 2;
  attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attrs[2].offset = static_cast<uint32_t>(offsetof(VertexSkinned, weights));

  return attrs;
}

}  // namespace

uint32_t SkinPbrPass::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if (((typeFilter & (1U << i)) != 0U) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("SkinPbrPass: suitable memory type not found");
}

SkinPbrPass::Buffer SkinPbrPass::CreateBuffer(VkDeviceSize size,
                                              VkBufferUsageFlags usage,
                                              VkMemoryPropertyFlags properties,
                                              bool persistentMap) {
  Buffer out;
  out.size = size;

  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &out.handle), "SkinPbrPass: vkCreateBuffer failed");

  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device_, out.handle, &req);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = req.size;
  allocInfo.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, properties);
  VkCheck(vkAllocateMemory(device_, &allocInfo, nullptr, &out.memory), "SkinPbrPass: vkAllocateMemory failed");
  VkCheck(vkBindBufferMemory(device_, out.handle, out.memory, 0), "SkinPbrPass: vkBindBufferMemory failed");

  if (persistentMap) {
    VkCheck(vkMapMemory(device_, out.memory, 0, size, 0, &out.mapped), "SkinPbrPass: vkMapMemory failed");
  }

  return out;
}

void SkinPbrPass::DestroyBuffer(Buffer& buffer) {
  if (buffer.mapped != nullptr) {
    vkUnmapMemory(device_, buffer.memory);
    buffer.mapped = nullptr;
  }
  if (buffer.handle != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, buffer.handle, nullptr);
    buffer.handle = VK_NULL_HANDLE;
  }
  if (buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
  }
  buffer.size = 0;
}

std::vector<uint32_t> SkinPbrPass::ReadSpv(const std::string& path) const {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("SkinPbrPass: failed to open shader: " + path);
  }

  const std::streamsize bytes = file.tellg();
  if (bytes <= 0 || (bytes % 4) != 0) {
    throw std::runtime_error("SkinPbrPass: invalid shader bytecode size: " + path);
  }
  file.seekg(0, std::ios::beg);

  std::vector<uint32_t> words(static_cast<size_t>(bytes) / 4);
  file.read(reinterpret_cast<char*>(words.data()), bytes);
  if (!file) {
    throw std::runtime_error("SkinPbrPass: failed to read shader: " + path);
  }
  return words;
}

VkShaderModule SkinPbrPass::CreateShaderModule(const std::vector<uint32_t>& words) const {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = words.size() * sizeof(uint32_t);
  info.pCode = words.data();

  VkShaderModule module = VK_NULL_HANDLE;
  VkCheck(vkCreateShaderModule(device_, &info, nullptr, &module), "SkinPbrPass: vkCreateShaderModule failed");
  return module;
}

VkCommandPool SkinPbrPass::CreateTransientCommandPool() const {
  VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  info.queueFamilyIndex = queueFamilyIndex_;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCheck(vkCreateCommandPool(device_, &info, nullptr, &pool), "SkinPbrPass: vkCreateCommandPool(transient) failed");
  return pool;
}

void SkinPbrPass::DestroyTransientCommandPool() {
  if (transientCommandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, transientCommandPool_, nullptr);
    transientCommandPool_ = VK_NULL_HANDLE;
  }
}

VkCommandBuffer SkinPbrPass::BeginOneShot() const {
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = transientCommandPool_;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCheck(vkAllocateCommandBuffers(device_, &alloc, &cmd), "SkinPbrPass: vkAllocateCommandBuffers(one-shot) failed");

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VkCheck(vkBeginCommandBuffer(cmd, &begin), "SkinPbrPass: vkBeginCommandBuffer(one-shot) failed");
  return cmd;
}

void SkinPbrPass::EndOneShot(VkCommandBuffer cmd) const {
  VkCheck(vkEndCommandBuffer(cmd), "SkinPbrPass: vkEndCommandBuffer(one-shot) failed");

  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  VkCheck(vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE), "SkinPbrPass: vkQueueSubmit(one-shot) failed");
  VkCheck(vkQueueWaitIdle(graphicsQueue_), "SkinPbrPass: vkQueueWaitIdle(one-shot) failed");

  vkFreeCommandBuffers(device_, transientCommandPool_, 1, &cmd);
}

void SkinPbrPass::TransitionImageLayout(VkCommandBuffer cmd,
                                        VkImage image,
                                        VkImageLayout oldLayout,
                                        VkImageLayout newLayout) const {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::runtime_error("SkinPbrPass: unsupported image layout transition");
  }

  vkCmdPipelineBarrier(cmd,
                       srcStage,
                       dstStage,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &barrier);
}

void SkinPbrPass::CreateDescriptorLayouts() {
  std::array<VkDescriptorSetLayoutBinding, 4> frameBindings{};
  frameBindings[0].binding = 0;
  frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  frameBindings[0].descriptorCount = 1;
  frameBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  frameBindings[1].binding = 1;
  frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  frameBindings[1].descriptorCount = 1;
  frameBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  frameBindings[2].binding = 2;
  frameBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  frameBindings[2].descriptorCount = 1;
  frameBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  frameBindings[3].binding = 3;
  frameBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  frameBindings[3].descriptorCount = 1;
  frameBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo frameInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  frameInfo.bindingCount = static_cast<uint32_t>(frameBindings.size());
  frameInfo.pBindings = frameBindings.data();
  VkCheck(vkCreateDescriptorSetLayout(device_, &frameInfo, nullptr, &frameSetLayout_),
          "SkinPbrPass: vkCreateDescriptorSetLayout(frame) failed");

  VkDescriptorSetLayoutBinding boneBinding{};
  boneBinding.binding = 0;
  boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  boneBinding.descriptorCount = 1;
  boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo boneInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  boneInfo.bindingCount = 1;
  boneInfo.pBindings = &boneBinding;
  VkCheck(vkCreateDescriptorSetLayout(device_, &boneInfo, nullptr, &boneSetLayout_),
          "SkinPbrPass: vkCreateDescriptorSetLayout(bone) failed");

  std::array<VkDescriptorSetLayoutBinding, 8> materialBindings{};
  for (uint32_t i = 0; i < materialBindings.size(); ++i) {
    materialBindings[i].binding = i;
    materialBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBindings[i].descriptorCount = 1;
    materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo materialInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  materialInfo.bindingCount = static_cast<uint32_t>(materialBindings.size());
  materialInfo.pBindings = materialBindings.data();
  VkCheck(vkCreateDescriptorSetLayout(device_, &materialInfo, nullptr, &materialSetLayout_),
          "SkinPbrPass: vkCreateDescriptorSetLayout(material) failed");
}

void SkinPbrPass::DestroyDescriptorLayouts() {
  if (frameSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, frameSetLayout_, nullptr);
    frameSetLayout_ = VK_NULL_HANDLE;
  }
  if (boneSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, boneSetLayout_, nullptr);
    boneSetLayout_ = VK_NULL_HANDLE;
  }
  if (materialSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, materialSetLayout_, nullptr);
    materialSetLayout_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::CreateFrameDescriptorPool() {
  std::array<VkDescriptorPoolSize, 3> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = kFramesInFlight;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[1].descriptorCount = kFramesInFlight * 2;
  poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[2].descriptorCount = kFramesInFlight * 2;

  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.maxSets = kFramesInFlight * 2;
  info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  info.pPoolSizes = poolSizes.data();
  VkCheck(vkCreateDescriptorPool(device_, &info, nullptr, &frameDescriptorPool_),
          "SkinPbrPass: vkCreateDescriptorPool(frame) failed");
}

void SkinPbrPass::DestroyFrameDescriptorPool() {
  if (frameDescriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, frameDescriptorPool_, nullptr);
    frameDescriptorPool_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::CreateMaterialDescriptorPool() {
  VkDescriptorPoolSize size{};
  size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  size.descriptorCount = kMaterialDescriptorCapacity * 8;

  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  info.maxSets = kMaterialDescriptorCapacity;
  info.poolSizeCount = 1;
  info.pPoolSizes = &size;

  VkCheck(vkCreateDescriptorPool(device_, &info, nullptr, &materialDescriptorPool_),
          "SkinPbrPass: vkCreateDescriptorPool(material) failed");
}

void SkinPbrPass::DestroyMaterialDescriptorPool() {
  if (materialDescriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
    materialDescriptorPool_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::CreatePerFrameBuffers() {
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    frameUboBuffers_[i] = CreateBuffer(sizeof(FrameUbo),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       true);

    boneSsboBuffers_[i] = CreateBuffer(sizeof(Mat4) * kMaxBoneMatrices,
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       true);
    lightSsboBuffers_[i] = CreateBuffer(sizeof(LightGpu) * kMaxLights,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        true);

    const Mat4 identity(1.0F);
    std::memcpy(boneSsboBuffers_[i].mapped, &identity, sizeof(Mat4));
    std::memset(lightSsboBuffers_[i].mapped, 0, sizeof(LightGpu) * kMaxLights);
  }
}

void SkinPbrPass::DestroyPerFrameBuffers() {
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    DestroyBuffer(frameUboBuffers_[i]);
    DestroyBuffer(boneSsboBuffers_[i]);
    DestroyBuffer(lightSsboBuffers_[i]);
  }
}

void SkinPbrPass::AllocateAndWriteFrameDescriptorSets() {
  std::array<VkDescriptorSetLayout, kFramesInFlight> frameLayouts{};
  frameLayouts.fill(frameSetLayout_);

  VkDescriptorSetAllocateInfo frameAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  frameAlloc.descriptorPool = frameDescriptorPool_;
  frameAlloc.descriptorSetCount = kFramesInFlight;
  frameAlloc.pSetLayouts = frameLayouts.data();
  VkCheck(vkAllocateDescriptorSets(device_, &frameAlloc, frameSets_.data()),
          "SkinPbrPass: vkAllocateDescriptorSets(frame) failed");

  std::array<VkDescriptorSetLayout, kFramesInFlight> boneLayouts{};
  boneLayouts.fill(boneSetLayout_);

  VkDescriptorSetAllocateInfo boneAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  boneAlloc.descriptorPool = frameDescriptorPool_;
  boneAlloc.descriptorSetCount = kFramesInFlight;
  boneAlloc.pSetLayouts = boneLayouts.data();
  VkCheck(vkAllocateDescriptorSets(device_, &boneAlloc, boneSets_.data()),
          "SkinPbrPass: vkAllocateDescriptorSets(bone) failed");

  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    VkDescriptorBufferInfo frameInfo{};
    frameInfo.buffer = frameUboBuffers_[i].handle;
    frameInfo.offset = 0;
    frameInfo.range = sizeof(FrameUbo);

    VkDescriptorBufferInfo frameLightInfo{};
    frameLightInfo.buffer = lightSsboBuffers_[i].handle;
    frameLightInfo.offset = 0;
    frameLightInfo.range = sizeof(LightGpu) * kMaxLights;

    VkDescriptorBufferInfo boneInfo{};
    boneInfo.buffer = boneSsboBuffers_[i].handle;
    boneInfo.offset = 0;
    boneInfo.range = sizeof(Mat4) * kMaxBoneMatrices;

    VkDescriptorImageInfo envInfo{};
    envInfo.sampler = iblSampler_ != VK_NULL_HANDLE ? iblSampler_ : sampler_;
    envInfo.imageView = iblEnvironment_.view;
    envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.sampler = shadowSampler_ != VK_NULL_HANDLE ? shadowSampler_ : sampler_;
    shadowInfo.imageView = shadowDepthView_;
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet frameUboWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    frameUboWrite.dstSet = frameSets_[i];
    frameUboWrite.dstBinding = 0;
    frameUboWrite.descriptorCount = 1;
    frameUboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameUboWrite.pBufferInfo = &frameInfo;

    VkWriteDescriptorSet frameLightWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    frameLightWrite.dstSet = frameSets_[i];
    frameLightWrite.dstBinding = 1;
    frameLightWrite.descriptorCount = 1;
    frameLightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    frameLightWrite.pBufferInfo = &frameLightInfo;

    VkWriteDescriptorSet frameEnvWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    frameEnvWrite.dstSet = frameSets_[i];
    frameEnvWrite.dstBinding = 2;
    frameEnvWrite.descriptorCount = 1;
    frameEnvWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frameEnvWrite.pImageInfo = &envInfo;

    VkWriteDescriptorSet frameShadowWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    frameShadowWrite.dstSet = frameSets_[i];
    frameShadowWrite.dstBinding = 3;
    frameShadowWrite.descriptorCount = 1;
    frameShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frameShadowWrite.pImageInfo = &shadowInfo;

    VkWriteDescriptorSet boneWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    boneWrite.dstSet = boneSets_[i];
    boneWrite.dstBinding = 0;
    boneWrite.descriptorCount = 1;
    boneWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneWrite.pBufferInfo = &boneInfo;

    std::array<VkWriteDescriptorSet, 5> writes{frameUboWrite, frameLightWrite, frameEnvWrite, frameShadowWrite, boneWrite};
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

void SkinPbrPass::CreatePipeline(VkRenderPass renderPass) {
  const auto vertWords = ReadSpv(vertSpvPath_);
  const auto fragWords = ReadSpv(fragSpvPath_);
  VkShaderModule vertModule = CreateShaderModule(vertWords);
  VkShaderModule fragModule = CreateShaderModule(fragWords);

  VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragModule;
  fragStage.pName = "main";

  std::array<VkPipelineShaderStageCreateInfo, 2> stages{vertStage, fragStage};

  const auto binding = MakeVertexBinding();
  const auto attrs = MakeVertexAttributes();

  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vertexInput.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.lineWidth = 1.0F;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo msaa{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;

  std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  std::array<VkDescriptorSetLayout, 3> setLayouts = {frameSetLayout_, boneSetLayout_, materialSetLayout_};

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(DrawPush);

  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  VkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
          "SkinPbrPass: vkCreatePipelineLayout failed");

  VkGraphicsPipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipeInfo.pStages = stages.data();
  pipeInfo.pVertexInputState = &vertexInput;
  pipeInfo.pInputAssemblyState = &inputAssembly;
  pipeInfo.pViewportState = &viewportState;
  pipeInfo.pRasterizationState = &raster;
  pipeInfo.pMultisampleState = &msaa;
  pipeInfo.pDepthStencilState = &depth;
  pipeInfo.pColorBlendState = &blend;
  pipeInfo.pDynamicState = &dynamic;
  pipeInfo.layout = pipelineLayout_;
  pipeInfo.renderPass = renderPass;
  pipeInfo.subpass = 0;

  VkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline_),
          "SkinPbrPass: vkCreateGraphicsPipelines failed");

  vkDestroyShaderModule(device_, vertModule, nullptr);
  vkDestroyShaderModule(device_, fragModule, nullptr);
}

void SkinPbrPass::CreateShadowResources() {
  if (shadowDepthImage_ != VK_NULL_HANDLE) {
    return;
  }

  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = kShadowMapSize;
  imageInfo.extent.height = kShadowMapSize;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_D32_SFLOAT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkCheck(vkCreateImage(device_, &imageInfo, nullptr, &shadowDepthImage_), "SkinPbrPass: vkCreateImage(shadow depth) failed");

  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device_, shadowDepthImage_, &req);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = req.size;
  allocInfo.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkCheck(vkAllocateMemory(device_, &allocInfo, nullptr, &shadowDepthMemory_), "SkinPbrPass: vkAllocateMemory(shadow depth) failed");
  VkCheck(vkBindImageMemory(device_, shadowDepthImage_, shadowDepthMemory_, 0), "SkinPbrPass: vkBindImageMemory(shadow depth) failed");

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = shadowDepthImage_;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_D32_SFLOAT;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  VkCheck(vkCreateImageView(device_, &viewInfo, nullptr, &shadowDepthView_), "SkinPbrPass: vkCreateImageView(shadow depth) failed");

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = VK_FORMAT_D32_SFLOAT;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

  VkAttachmentReference depthRef{};
  depthRef.attachment = 0;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.pDepthStencilAttachment = &depthRef;

  std::array<VkSubpassDependency, 2> deps{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &depthAttachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
  rpInfo.pDependencies = deps.data();
  VkCheck(vkCreateRenderPass(device_, &rpInfo, nullptr, &shadowRenderPass_), "SkinPbrPass: vkCreateRenderPass(shadow) failed");

  VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbInfo.renderPass = shadowRenderPass_;
  fbInfo.attachmentCount = 1;
  fbInfo.pAttachments = &shadowDepthView_;
  fbInfo.width = kShadowMapSize;
  fbInfo.height = kShadowMapSize;
  fbInfo.layers = 1;
  VkCheck(vkCreateFramebuffer(device_, &fbInfo, nullptr, &shadowFramebuffer_), "SkinPbrPass: vkCreateFramebuffer(shadow) failed");
}

void SkinPbrPass::DestroyShadowResources() {
  if (shadowFramebuffer_ != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device_, shadowFramebuffer_, nullptr);
    shadowFramebuffer_ = VK_NULL_HANDLE;
  }
  if (shadowRenderPass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
    shadowRenderPass_ = VK_NULL_HANDLE;
  }
  if (shadowDepthView_ != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, shadowDepthView_, nullptr);
    shadowDepthView_ = VK_NULL_HANDLE;
  }
  if (shadowDepthImage_ != VK_NULL_HANDLE) {
    vkDestroyImage(device_, shadowDepthImage_, nullptr);
    shadowDepthImage_ = VK_NULL_HANDLE;
  }
  if (shadowDepthMemory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, shadowDepthMemory_, nullptr);
    shadowDepthMemory_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::CreateShadowPipeline() {
  const auto vertWords = ReadSpv(shadowVertSpvPath_);
  VkShaderModule vertModule = CreateShaderModule(vertWords);

  VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = "main";

  const auto binding = MakeVertexBinding();
  const auto attrs = MakeShadowVertexAttributes();

  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vertexInput.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.lineWidth = 1.0F;
  raster.cullMode = VK_CULL_MODE_BACK_BIT;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.depthBiasEnable = VK_TRUE;

  VkPipelineMultisampleStateCreateInfo msaa{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 0;

  std::array<VkDynamicState, 3> dynamicStates = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  std::array<VkDescriptorSetLayout, 2> setLayouts = {frameSetLayout_, boneSetLayout_};
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(ShadowPush);

  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  VkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &shadowPipelineLayout_),
          "SkinPbrPass: vkCreatePipelineLayout(shadow) failed");

  VkGraphicsPipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeInfo.stageCount = 1;
  pipeInfo.pStages = &vertStage;
  pipeInfo.pVertexInputState = &vertexInput;
  pipeInfo.pInputAssemblyState = &inputAssembly;
  pipeInfo.pViewportState = &viewportState;
  pipeInfo.pRasterizationState = &raster;
  pipeInfo.pMultisampleState = &msaa;
  pipeInfo.pDepthStencilState = &depth;
  pipeInfo.pColorBlendState = &blend;
  pipeInfo.pDynamicState = &dynamic;
  pipeInfo.layout = shadowPipelineLayout_;
  pipeInfo.renderPass = shadowRenderPass_;
  pipeInfo.subpass = 0;

  VkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &shadowPipeline_),
          "SkinPbrPass: vkCreateGraphicsPipelines(shadow) failed");

  vkDestroyShaderModule(device_, vertModule, nullptr);
}

void SkinPbrPass::DestroyShadowPipeline() {
  if (shadowPipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, shadowPipeline_, nullptr);
    shadowPipeline_ = VK_NULL_HANDLE;
  }
  if (shadowPipelineLayout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
    shadowPipelineLayout_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::DestroyPipeline() {
  if (pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
  if (pipelineLayout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    pipelineLayout_ = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::DestroySceneBuffers() {
  for (auto& mesh : meshBuffers_) {
    DestroyBuffer(mesh.vertex);
    DestroyBuffer(mesh.index);
  }
  meshBuffers_.clear();
  uploadedScene_ = nullptr;
  boneOverflowWarned_ = false;
}

void SkinPbrPass::DestroyTextures() {
  for (auto& tex : textureGpus_) {
    if (tex.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, tex.view, nullptr);
      tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
      vkDestroyImage(device_, tex.image, nullptr);
      tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, tex.memory, nullptr);
      tex.memory = VK_NULL_HANDLE;
    }
  }
  textureGpus_.clear();
  materialSets_.clear();
}

void SkinPbrPass::CreateIblEnvironmentTexture() {
  DestroyIblEnvironmentTexture();

  const VkFormat preferred = VK_FORMAT_R16G16B16A16_SFLOAT;
  const VkFormat fallback = VK_FORMAT_R32G32B32A32_SFLOAT;
  VkFormat format = preferred;
  if (!SupportsSampledTransferDst(physicalDevice_, preferred) && SupportsSampledTransferDst(physicalDevice_, fallback)) {
    format = fallback;
  }
  if (!SupportsSampledTransferDst(physicalDevice_, format)) {
    throw std::runtime_error("SkinPbrPass: no supported floating-point format for IBL environment");
  }

  const auto pixels = BuildProceduralIblPixels(kIblWidth, kIblHeight);
  std::vector<uint8_t> uploadBytes;
  if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
    std::vector<uint16_t> packed(pixels.size(), 0);
    for (size_t i = 0; i < pixels.size(); ++i) {
      const float value = std::clamp(pixels[i], -65504.0F, 65504.0F);
      packed[i] = glm::packHalf1x16(value);
    }
    uploadBytes.resize(packed.size() * sizeof(uint16_t));
    std::memcpy(uploadBytes.data(), packed.data(), uploadBytes.size());
  } else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
    uploadBytes.resize(pixels.size() * sizeof(float));
    std::memcpy(uploadBytes.data(), pixels.data(), uploadBytes.size());
  } else {
    throw std::runtime_error("SkinPbrPass: unsupported IBL upload format");
  }

  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = kIblWidth;
  imageInfo.extent.height = kIblHeight;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkCheck(vkCreateImage(device_, &imageInfo, nullptr, &iblEnvironment_.image),
          "SkinPbrPass: vkCreateImage(ibl) failed");

  VkMemoryRequirements imageReq{};
  vkGetImageMemoryRequirements(device_, iblEnvironment_.image, &imageReq);

  VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  imageAlloc.allocationSize = imageReq.size;
  imageAlloc.memoryTypeIndex = FindMemoryType(imageReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkCheck(vkAllocateMemory(device_, &imageAlloc, nullptr, &iblEnvironment_.memory),
          "SkinPbrPass: vkAllocateMemory(ibl) failed");
  VkCheck(vkBindImageMemory(device_, iblEnvironment_.image, iblEnvironment_.memory, 0),
          "SkinPbrPass: vkBindImageMemory(ibl) failed");

  const VkDeviceSize pixelBytes = static_cast<VkDeviceSize>(uploadBytes.size());
  Buffer staging = CreateBuffer(pixelBytes,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                true);
  std::memcpy(staging.mapped, uploadBytes.data(), static_cast<size_t>(pixelBytes));

  VkCommandBuffer cmd = BeginOneShot();
  TransitionImageLayout(cmd,
                        iblEnvironment_.image,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy copy{};
  copy.bufferOffset = 0;
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.mipLevel = 0;
  copy.imageSubresource.baseArrayLayer = 0;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {kIblWidth, kIblHeight, 1};
  vkCmdCopyBufferToImage(cmd,
                         staging.handle,
                         iblEnvironment_.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &copy);

  TransitionImageLayout(cmd,
                        iblEnvironment_.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  EndOneShot(cmd);
  DestroyBuffer(staging);

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = iblEnvironment_.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  VkCheck(vkCreateImageView(device_, &viewInfo, nullptr, &iblEnvironment_.view),
          "SkinPbrPass: vkCreateImageView(ibl) failed");
  iblEnvironment_.format = format;
}

void SkinPbrPass::DestroyIblEnvironmentTexture() {
  if (iblEnvironment_.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, iblEnvironment_.view, nullptr);
    iblEnvironment_.view = VK_NULL_HANDLE;
  }
  if (iblEnvironment_.image != VK_NULL_HANDLE) {
    vkDestroyImage(device_, iblEnvironment_.image, nullptr);
    iblEnvironment_.image = VK_NULL_HANDLE;
  }
  if (iblEnvironment_.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, iblEnvironment_.memory, nullptr);
    iblEnvironment_.memory = VK_NULL_HANDLE;
  }
}

void SkinPbrPass::UploadTextures(const Scene& scene) {
  DestroyTextures();

  textureGpus_.resize(std::max<size_t>(scene.textures.size(), 1));

  for (size_t i = 0; i < textureGpus_.size(); ++i) {
    std::vector<uint8_t> pixels;
    uint32_t width = 1;
    uint32_t height = 1;
    bool srgb = true;

    if (i < scene.textures.size()) {
      const Texture& src = scene.textures[i];
      if (!src.pixels.empty() && src.width > 0 && src.height > 0) {
        pixels = src.pixels;
        width = src.width;
        height = src.height;
        srgb = src.srgb;
      }
    }

    if (pixels.empty()) {
      pixels = {255, 255, 255, 255};
      width = 1;
      height = 1;
      srgb = true;
    }

    TextureGpu gpu{};
    gpu.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const uint32_t maxDim = std::max(width, height);
    gpu.mipLevels = (maxDim > 0) ? (static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1U) : 1U;
    const bool canLinearBlit = SupportsLinearBlit(physicalDevice_, gpu.format);
    if (!canLinearBlit) {
      gpu.mipLevels = 1;
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = gpu.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = gpu.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkCheck(vkCreateImage(device_, &imageInfo, nullptr, &gpu.image), "SkinPbrPass: vkCreateImage(texture) failed");

    VkMemoryRequirements imageReq{};
    vkGetImageMemoryRequirements(device_, gpu.image, &imageReq);

    VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imageAlloc.allocationSize = imageReq.size;
    imageAlloc.memoryTypeIndex = FindMemoryType(imageReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkCheck(vkAllocateMemory(device_, &imageAlloc, nullptr, &gpu.memory), "SkinPbrPass: vkAllocateMemory(texture) failed");
    VkCheck(vkBindImageMemory(device_, gpu.image, gpu.memory, 0), "SkinPbrPass: vkBindImageMemory(texture) failed");

    Buffer staging = CreateBuffer(pixels.size(),
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  true);
    std::memcpy(staging.mapped, pixels.data(), pixels.size());

    VkCommandBuffer cmd = BeginOneShot();

    TransitionImageLayout(cmd, gpu.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd,
                           staging.handle,
                           gpu.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy);

    if (gpu.mipLevels == 1) {
      TransitionImageLayout(cmd,
                            gpu.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
      int32_t mipWidth = static_cast<int32_t>(width);
      int32_t mipHeight = static_cast<int32_t>(height);

      auto transitionLevel = [&](uint32_t level,
                                 VkImageLayout oldLayout,
                                 VkImageLayout newLayout,
                                 VkAccessFlags srcAccess,
                                 VkAccessFlags dstAccess,
                                 VkPipelineStageFlags srcStage,
                                 VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gpu.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = level;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
      };

      transitionLevel(0,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);

      for (uint32_t level = 1; level < gpu.mipLevels; ++level) {
        transitionLevel(level,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

        const int32_t dstWidth = std::max(1, mipWidth / 2);
        const int32_t dstHeight = std::max(1, mipHeight / 2);
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {dstWidth, dstHeight, 1};

        vkCmdBlitImage(cmd,
                       gpu.image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       gpu.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,
                       &blit,
                       VK_FILTER_LINEAR);

        transitionLevel(level - 1,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        transitionLevel(level,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);

        mipWidth = dstWidth;
        mipHeight = dstHeight;
      }

      transitionLevel(gpu.mipLevels - 1,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    EndOneShot(cmd);
    DestroyBuffer(staging);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = gpu.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = gpu.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = gpu.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VkCheck(vkCreateImageView(device_, &viewInfo, nullptr, &gpu.view), "SkinPbrPass: vkCreateImageView(texture) failed");

    textureGpus_[i] = gpu;
  }
}

void SkinPbrPass::RebuildMaterialDescriptorSets(const Scene& scene) {
  if (materialDescriptorPool_ == VK_NULL_HANDLE) {
    return;
  }

  VkCheck(vkResetDescriptorPool(device_, materialDescriptorPool_, 0), "SkinPbrPass: vkResetDescriptorPool(material) failed");

  const size_t setCount = std::max<size_t>(scene.materials.size(), 1);
  materialSets_.resize(setCount);

  std::vector<VkDescriptorSetLayout> layouts(setCount, materialSetLayout_);
  VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = materialDescriptorPool_;
  alloc.descriptorSetCount = static_cast<uint32_t>(setCount);
  alloc.pSetLayouts = layouts.data();
  VkCheck(vkAllocateDescriptorSets(device_, &alloc, materialSets_.data()),
          "SkinPbrPass: vkAllocateDescriptorSets(material) failed");

  auto pickView = [&](TextureId id, TextureId fallback) -> VkImageView {
    if (id < textureGpus_.size() && textureGpus_[id].view != VK_NULL_HANDLE) {
      return textureGpus_[id].view;
    }
    if (fallback < textureGpus_.size() && textureGpus_[fallback].view != VK_NULL_HANDLE) {
      return textureGpus_[fallback].view;
    }
    return textureGpus_[0].view;
  };

  for (size_t i = 0; i < setCount; ++i) {
    Material material;
    material.baseColorTex = 0;
    material.metallicRoughnessTex = 4;
    material.metallicTex = 1;
    material.roughnessTex = 4;
    material.normalTex = 2;
    material.occlusionTex = 4;
    material.emissiveTex = 1;
    material.specularTex = 3;
    if (i < scene.materials.size()) {
      material = scene.materials[i];
    }

    std::array<VkDescriptorImageInfo, 8> imageInfos{};
    imageInfos[0].sampler = sampler_;
    imageInfos[0].imageView = pickView(material.baseColorTex, 0);
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler = sampler_;
    imageInfos[1].imageView = pickView(material.metallicRoughnessTex, 0);
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[2].sampler = sampler_;
    imageInfos[2].imageView = pickView(material.normalTex, 2);
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[3].sampler = sampler_;
    imageInfos[3].imageView = pickView(material.emissiveTex, 1);
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[4].sampler = sampler_;
    imageInfos[4].imageView = pickView(material.specularTex, 3);
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[5].sampler = sampler_;
    imageInfos[5].imageView = pickView(material.metallicTex, 1);
    imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[6].sampler = sampler_;
    imageInfos[6].imageView = pickView(material.roughnessTex, 4);
    imageInfos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[7].sampler = sampler_;
    imageInfos[7].imageView = pickView(material.occlusionTex, 4);
    imageInfos[7].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 8> writes{};
    for (uint32_t b = 0; b < writes.size(); ++b) {
      writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[b].dstSet = materialSets_[i];
      writes[b].dstBinding = b;
      writes[b].descriptorCount = 1;
      writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[b].pImageInfo = &imageInfos[b];
    }

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

void SkinPbrPass::UploadScene(const Scene& scene) {
  DestroySceneBuffers();

  UploadTextures(scene);
  RebuildMaterialDescriptorSets(scene);

  meshBuffers_.resize(scene.meshes.size());
  for (size_t i = 0; i < scene.meshes.size(); ++i) {
    const Mesh& src = scene.meshes[i];
    MeshGpu& dst = meshBuffers_[i];

    const VkDeviceSize vertexBytes = sizeof(VertexSkinned) * src.vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * src.indices.size();
    const VkDeviceSize safeVertexBytes = std::max<VkDeviceSize>(vertexBytes, sizeof(uint32_t));
    const VkDeviceSize safeIndexBytes = std::max<VkDeviceSize>(indexBytes, sizeof(uint32_t));

    dst.vertex = CreateBuffer(safeVertexBytes,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
    dst.index = CreateBuffer(safeIndexBytes,
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             true);

    if (vertexBytes > 0) {
      std::memcpy(dst.vertex.mapped, src.vertices.data(), static_cast<size_t>(vertexBytes));
    }
    if (indexBytes > 0) {
      std::memcpy(dst.index.mapped, src.indices.data(), static_cast<size_t>(indexBytes));
    }

    dst.indexCount = static_cast<uint32_t>(src.indices.size());
  }

  uploadedScene_ = &scene;
}

void SkinPbrPass::EnsureSceneUploaded(const Scene* scene) {
  if (scene == nullptr) {
    return;
  }
  if (uploadedScene_ != scene) {
    UploadScene(*scene);
  }
}

Mat4 SkinPbrPass::ComputeDirectionalShadowMatrix(const RenderScene& scene) const {
  if (scene.scene == nullptr || scene.scene->nodes.empty()) {
    return Mat4(1.0F);
  }

  const Scene& src = *scene.scene;
  Vec3 lightDir = glm::normalize(Vec3(0.3F, -1.0F, 0.4F));

  std::vector<NodeId> lightNodes(src.lights.size(), kInvalidNodeId);
  for (NodeId i = 0; i < src.nodes.size(); ++i) {
    if (src.nodes[i].light.has_value()) {
      const LightId lightId = *src.nodes[i].light;
      if (lightId < lightNodes.size()) {
        lightNodes[lightId] = i;
      }
    }
  }

  for (LightId lid = 0; lid < src.lights.size(); ++lid) {
    const Light& light = src.lights[lid];
    if (light.type != LightType::kDirectional) {
      continue;
    }
    Vec3 dir = light.direction;
    if (glm::dot(dir, dir) <= 1e-8F) {
      continue;
    }
    if (lid < lightNodes.size() && lightNodes[lid] != kInvalidNodeId) {
      const Node& n = src.nodes[lightNodes[lid]];
      Vec3 transformed = Vec3(glm::mat3(n.worldCurrent) * dir);
      if (glm::dot(transformed, transformed) > 1e-8F) {
        dir = transformed;
      }
    }
    lightDir = glm::normalize(dir);
    break;
  }

  Vec3 bmin(FLT_MAX);
  Vec3 bmax(-FLT_MAX);
  bool hasBounds = false;
  for (const Node& node : src.nodes) {
    if (!node.mesh.has_value()) {
      continue;
    }
    const MeshId meshId = *node.mesh;
    if (meshId >= src.meshes.size()) {
      continue;
    }
    const Mesh& mesh = src.meshes[meshId];
    const Vec3 lmin = mesh.localBounds.min;
    const Vec3 lmax = mesh.localBounds.max;
    const std::array<Vec3, 8> corners = {
        Vec3(lmin.x, lmin.y, lmin.z), Vec3(lmax.x, lmin.y, lmin.z), Vec3(lmin.x, lmax.y, lmin.z), Vec3(lmax.x, lmax.y, lmin.z),
        Vec3(lmin.x, lmin.y, lmax.z), Vec3(lmax.x, lmin.y, lmax.z), Vec3(lmin.x, lmax.y, lmax.z), Vec3(lmax.x, lmax.y, lmax.z)};
    for (const Vec3& c : corners) {
      Vec3 w = Vec3(node.worldCurrent * Vec4(c, 1.0F));
      bmin = glm::min(bmin, w);
      bmax = glm::max(bmax, w);
      hasBounds = true;
    }
  }

  if (!hasBounds) {
    bmin = Vec3(-2.0F, -2.0F, -2.0F);
    bmax = Vec3(2.0F, 2.0F, 2.0F);
  }

  const Vec3 center = 0.5F * (bmin + bmax);
  const Vec3 extent = glm::max(bmax - bmin, Vec3(0.1F));
  const float radius = std::max({extent.x, extent.y, extent.z}) * 0.7F;
  const float safeRadius = std::max(radius, 2.0F);

  const Vec3 eye = center - lightDir * (safeRadius * 2.8F);
  Vec3 up(0.0F, 1.0F, 0.0F);
  if (std::abs(glm::dot(up, lightDir)) > 0.95F) {
    up = Vec3(0.0F, 0.0F, 1.0F);
  }

  const Mat4 view = glm::lookAt(eye, center, up);
  Mat4 stabilizedView = view;
  const float rawOrthoExtent = safeRadius * 1.35F;
  const float orthoQuant = 0.25F;
  const float orthoExtent = std::ceil(rawOrthoExtent / orthoQuant) * orthoQuant;
  const float worldUnitsPerTexel = (2.0F * orthoExtent) / static_cast<float>(kShadowMapSize);
  const Vec4 centerLs4 = stabilizedView * Vec4(center, 1.0F);
  const Vec2 centerLs = Vec2(centerLs4.x, centerLs4.y);
  const Vec2 snappedLs = glm::round(centerLs / worldUnitsPerTexel) * worldUnitsPerTexel;
  const Vec2 offsetLs = snappedLs - centerLs;
  stabilizedView = glm::translate(Mat4(1.0F), Vec3(offsetLs, 0.0F)) * stabilizedView;

  const Mat4 proj = glm::ortho(-orthoExtent, orthoExtent, -orthoExtent, orthoExtent, 0.1F, safeRadius * 7.0F + 20.0F);
  return proj * stabilizedView;
}

void SkinPbrPass::UpdateFrameUbo(uint32_t frameIndex,
                                 const RenderScene& scene,
                                 const FrameContext& frameContext,
                                 uint32_t lightCount) {
  FrameUbo ubo;
  ubo.view = frameContext.view;
  ubo.proj = frameContext.proj;
  ubo.lightViewProj = ComputeDirectionalShadowMatrix(scene);
  ubo.cameraPos = Vec4(frameContext.cameraPos, 1.0F);
  ubo.lightMeta = Vec4(static_cast<float>(lightCount), 0.08F, 1.25F, 1.00F);
  ubo.debugFlags = Vec4(frameContext.enableNormalMap, frameContext.enableSpecularIbl, 0.0F, 0.0F);
  ubo.shadowMeta = Vec4((scene.scene != nullptr) ? 1.0F : 0.0F, 0.0008F, 0.92F, 1.5F);

  std::memcpy(frameUboBuffers_[frameIndex].mapped, &ubo, sizeof(FrameUbo));
}

uint32_t SkinPbrPass::UpdateLightBuffer(uint32_t frameIndex, const RenderScene& scene) {
  std::array<LightGpu, kMaxLights> packed{};
  uint32_t lightCount = 0;

  if (scene.scene != nullptr) {
    const Scene& src = *scene.scene;
    std::vector<NodeId> lightNodes(src.lights.size(), kInvalidNodeId);

    for (NodeId i = 0; i < src.nodes.size(); ++i) {
      if (src.nodes[i].light.has_value()) {
        const LightId lightId = *src.nodes[i].light;
        if (lightId < lightNodes.size()) {
          lightNodes[lightId] = i;
        }
      }
    }

    for (LightId i = 0; i < src.lights.size() && lightCount < kMaxLights; ++i) {
      const Light& light = src.lights[i];

      Vec3 position(0.0F, 2.0F, 0.0F);
      Vec3 direction = light.direction;
      if (glm::dot(direction, direction) < 1e-8F) {
        direction = Vec3(0.0F, -1.0F, 0.0F);
      }

      if (i < lightNodes.size() && lightNodes[i] != kInvalidNodeId) {
        const Node& node = src.nodes[lightNodes[i]];
        position = Vec3(node.worldCurrent[3]);
        Vec3 transformed = Vec3(glm::mat3(node.worldCurrent) * direction);
        if (glm::dot(transformed, transformed) > 1e-8F) {
          direction = transformed;
        }
      }
      direction = glm::normalize(direction);

      float type = 0.0F;
      if (light.type == LightType::kPoint) {
        type = 1.0F;
      } else if (light.type == LightType::kSpot) {
        type = 2.0F;
      }

      const float range = std::max(light.range, 0.001F);
      const float intensity = std::max(light.intensity, 0.0F);
      const float innerAngle = std::min(light.innerCone, light.outerCone);
      const float outerAngle = std::max(light.innerCone, light.outerCone);

      LightGpu gpu{};
      gpu.positionRange = Vec4(position, range);
      gpu.directionType = Vec4(direction, type);
      gpu.colorIntensity = Vec4(light.color, intensity);
      gpu.coneCos = Vec4(std::cos(innerAngle), std::cos(outerAngle), 0.0F, 0.0F);
      packed[lightCount++] = gpu;
    }
  }

  if (lightCount == 0) {
    LightGpu fallback{};
    fallback.positionRange = Vec4(0.0F, 2.0F, 0.0F, 100.0F);
    fallback.directionType = Vec4(0.3F, -1.0F, 0.4F, 0.0F);
    fallback.colorIntensity = Vec4(1.0F, 1.0F, 1.0F, 3.0F);
    fallback.coneCos = Vec4(0.95F, 0.85F, 0.0F, 0.0F);
    packed[0] = fallback;
    lightCount = 1;
  }

  std::memcpy(lightSsboBuffers_[frameIndex].mapped, packed.data(), sizeof(LightGpu) * kMaxLights);
  return lightCount;
}

void SkinPbrPass::UpdateBoneBuffer(uint32_t frameIndex, const RenderScene& scene) {
  const std::vector<Mat4>* palette = scene.skinPalette;
  const Mat4 identity(1.0F);
  auto* dstMats = static_cast<Mat4*>(boneSsboBuffers_[frameIndex].mapped);

  for (size_t i = 0; i < kMaxBoneMatrices; ++i) {
    dstMats[i] = identity;
  }

  if (palette == nullptr || palette->empty()) {
    return;
  }

  const size_t srcCount = palette->size();
  const size_t count = std::min<size_t>(srcCount, kMaxBoneMatrices);
  std::memcpy(dstMats, palette->data(), sizeof(Mat4) * count);

  if (srcCount > kMaxBoneMatrices && !boneOverflowWarned_) {
    boneOverflowWarned_ = true;
  }
}

void SkinPbrPass::Initialize(VkPhysicalDevice physicalDevice,
                             VkDevice device,
                             VkQueue graphicsQueue,
                             uint32_t queueFamilyIndex,
                             VkRenderPass renderPass,
                             VkExtent2D extent,
                             const std::string& shaderDir) {
  if (initialized_) {
    return;
  }

  physicalDevice_ = physicalDevice;
  device_ = device;
  graphicsQueue_ = graphicsQueue;
  queueFamilyIndex_ = queueFamilyIndex;
  extent_ = extent;

  vertSpvPath_ = shaderDir + "/skin_pbr.vert.spv";
  fragSpvPath_ = shaderDir + "/skin_pbr.frag.spv";
  shadowVertSpvPath_ = shaderDir + "/skin_shadow.vert.spv";

  transientCommandPool_ = CreateTransientCommandPool();

  VkPhysicalDeviceFeatures supportedFeatures{};
  vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);
  VkPhysicalDeviceProperties deviceProps{};
  vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProps);

  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.minLod = 0.0F;
  samplerInfo.maxLod = 16.0F;
  samplerInfo.mipLodBias = 0.0F;
  if (supportedFeatures.samplerAnisotropy == VK_TRUE) {
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = std::min(8.0F, deviceProps.limits.maxSamplerAnisotropy);
  }
  VkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_), "SkinPbrPass: vkCreateSampler failed");

  VkSamplerCreateInfo iblSamplerInfo = samplerInfo;
  iblSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  iblSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  iblSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkCheck(vkCreateSampler(device_, &iblSamplerInfo, nullptr, &iblSampler_),
          "SkinPbrPass: vkCreateSampler(ibl) failed");

  VkSamplerCreateInfo shadowSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  shadowSamplerInfo.magFilter = VK_FILTER_LINEAR;
  shadowSamplerInfo.minFilter = VK_FILTER_LINEAR;
  shadowSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  shadowSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  shadowSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  shadowSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  shadowSamplerInfo.compareEnable = VK_FALSE;
  shadowSamplerInfo.minLod = 0.0F;
  shadowSamplerInfo.maxLod = 0.0F;
  VkCheck(vkCreateSampler(device_, &shadowSamplerInfo, nullptr, &shadowSampler_),
          "SkinPbrPass: vkCreateSampler(shadow) failed");

  CreateIblEnvironmentTexture();
  CreateShadowResources();
  CreateDescriptorLayouts();
  CreateFrameDescriptorPool();
  CreateMaterialDescriptorPool();
  CreatePerFrameBuffers();
  AllocateAndWriteFrameDescriptorSets();
  CreatePipeline(renderPass);
  CreateShadowPipeline();

  initialized_ = true;
}

void SkinPbrPass::Shutdown() {
  if (!initialized_) {
    return;
  }

  DestroySceneBuffers();
  DestroyTextures();
  DestroyIblEnvironmentTexture();
  DestroyShadowPipeline();
  DestroyPipeline();
  DestroyShadowResources();
  DestroyPerFrameBuffers();
  DestroyMaterialDescriptorPool();
  DestroyFrameDescriptorPool();
  DestroyDescriptorLayouts();

  if (sampler_ != VK_NULL_HANDLE) {
    vkDestroySampler(device_, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
  }
  if (iblSampler_ != VK_NULL_HANDLE) {
    vkDestroySampler(device_, iblSampler_, nullptr);
    iblSampler_ = VK_NULL_HANDLE;
  }
  if (shadowSampler_ != VK_NULL_HANDLE) {
    vkDestroySampler(device_, shadowSampler_, nullptr);
    shadowSampler_ = VK_NULL_HANDLE;
  }

  DestroyTransientCommandPool();

  initialized_ = false;
  device_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  graphicsQueue_ = VK_NULL_HANDLE;
}

void SkinPbrPass::RecreateForRenderPass(VkRenderPass renderPass, VkExtent2D extent) {
  extent_ = extent;
  if (!initialized_) {
    return;
  }

  DestroyPipeline();
  CreatePipeline(renderPass);
}

void SkinPbrPass::PrepareFrame(uint32_t frameIndex, const RenderScene& scene, const FrameContext& frameContext) {
  if (!initialized_ || scene.scene == nullptr || scene.scene->meshes.empty()) {
    return;
  }
  EnsureSceneUploaded(scene.scene);
  const uint32_t lightCount = UpdateLightBuffer(frameIndex, scene);
  UpdateFrameUbo(frameIndex, scene, frameContext, lightCount);
  UpdateBoneBuffer(frameIndex, scene);
}

void SkinPbrPass::RenderShadow(VkCommandBuffer cmd, uint32_t frameIndex, const RenderScene& scene) {
  if (!initialized_ || scene.scene == nullptr || scene.scene->meshes.empty()) {
    return;
  }
  if (shadowRenderPass_ == VK_NULL_HANDLE || shadowFramebuffer_ == VK_NULL_HANDLE || shadowPipeline_ == VK_NULL_HANDLE) {
    return;
  }

  VkViewport viewport{};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(kShadowMapSize);
  viewport.height = static_cast<float>(kShadowMapSize);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {kShadowMapSize, kShadowMapSize};

  VkClearValue depthClear{};
  depthClear.depthStencil = {1.0F, 0};

  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass = shadowRenderPass_;
  begin.framebuffer = shadowFramebuffer_;
  begin.renderArea.offset = {0, 0};
  begin.renderArea.extent = {kShadowMapSize, kShadowMapSize};
  begin.clearValueCount = 1;
  begin.pClearValues = &depthClear;
  vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  vkCmdSetDepthBias(cmd, 1.75F, 0.0F, 3.5F);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);

  std::array<VkDescriptorSet, 2> globalSets = {frameSets_[frameIndex], boneSets_[frameIndex]};
  vkCmdBindDescriptorSets(cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowPipelineLayout_,
                          0,
                          static_cast<uint32_t>(globalSets.size()),
                          globalSets.data(),
                          0,
                          nullptr);

  for (const Node& node : scene.scene->nodes) {
    if (!node.mesh.has_value()) {
      continue;
    }

    const MeshId meshId = *node.mesh;
    if (meshId >= meshBuffers_.size() || meshId >= scene.scene->meshes.size()) {
      continue;
    }

    const MeshGpu& gpuMesh = meshBuffers_[meshId];
    const Mesh& mesh = scene.scene->meshes[meshId];
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gpuMesh.vertex.handle, &offset);
    vkCmdBindIndexBuffer(cmd, gpuMesh.index.handle, 0, VK_INDEX_TYPE_UINT32);

    float boneOffset = static_cast<float>(kMaxBoneMatrices - 1);
    if (node.skin.has_value() && *node.skin < scene.scene->skins.size()) {
      const Skin& skin = scene.scene->skins[*node.skin];
      if (scene.skeletonPaletteOffsets != nullptr && skin.skeleton < scene.skeletonPaletteOffsets->size()) {
        boneOffset = static_cast<float>((*scene.skeletonPaletteOffsets)[skin.skeleton]);
      }
    }

    ShadowPush push{};
    push.model = node.worldCurrent;
    push.misc = Vec4(boneOffset, 0.0F, 0.0F, 0.0F);

    vkCmdPushConstants(cmd,
                       shadowPipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(ShadowPush),
                       &push);

    for (const Submesh& submesh : mesh.submeshes) {
      vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    }
  }

  vkCmdEndRenderPass(cmd);
}

void SkinPbrPass::Render(VkCommandBuffer cmd,
                         uint32_t frameIndex,
                         const RenderScene& scene,
                         const FrameContext& frameContext) {
  if (!initialized_ || scene.scene == nullptr || scene.scene->meshes.empty()) {
    return;
  }
  (void)frameContext;
  EnsureSceneUploaded(scene.scene);

  VkViewport viewport{};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(extent_.width);
  viewport.height = static_cast<float>(extent_.height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = extent_;

  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

  std::array<VkDescriptorSet, 2> globalSets = {frameSets_[frameIndex], boneSets_[frameIndex]};
  vkCmdBindDescriptorSets(cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout_,
                          0,
                          static_cast<uint32_t>(globalSets.size()),
                          globalSets.data(),
                          0,
                          nullptr);

  for (const Node& node : scene.scene->nodes) {
    if (!node.mesh.has_value()) {
      continue;
    }

    const MeshId meshId = *node.mesh;
    if (meshId >= meshBuffers_.size() || meshId >= scene.scene->meshes.size()) {
      continue;
    }

    const MeshGpu& gpuMesh = meshBuffers_[meshId];
    const Mesh& mesh = scene.scene->meshes[meshId];

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gpuMesh.vertex.handle, &offset);
    vkCmdBindIndexBuffer(cmd, gpuMesh.index.handle, 0, VK_INDEX_TYPE_UINT32);

    for (const Submesh& submesh : mesh.submeshes) {
      DrawPush push;
      push.model = node.worldCurrent;
      push.mrAlpha.w = static_cast<float>(kMaxBoneMatrices - 1);
      push.flags = Vec4(0.0F);

      if (node.skin.has_value() && *node.skin < scene.scene->skins.size()) {
        const Skin& skin = scene.scene->skins[*node.skin];
        if (scene.skeletonPaletteOffsets != nullptr && skin.skeleton < scene.skeletonPaletteOffsets->size()) {
          push.mrAlpha.w = static_cast<float>((*scene.skeletonPaletteOffsets)[skin.skeleton]);
        }
      }

      VkDescriptorSet materialSet = materialSets_.empty() ? VK_NULL_HANDLE : materialSets_[0];

      if (submesh.material < scene.scene->materials.size()) {
        const Material& material = scene.scene->materials[submesh.material];
        push.baseColor = material.baseColorFactor;
        push.emissive = Vec4(material.emissiveFactor, material.emissiveStrength);
        const float signedNormalScale = std::max(0.0F, material.normalScale) * (material.normalGreenInverted ? -1.0F : 1.0F);
        const float materialMode = material.gridOverlay ? 2.0F : (material.useSpecularGlossiness ? 1.0F : 0.0F);
        push.flags = Vec4(materialMode,
                          signedNormalScale,
                          std::max(0.0F, material.occlusionStrength),
                          material.useSeparateMetalRoughness ? 1.0F : 0.0F);
        push.mrAlpha = Vec4(material.metallicFactor,
                            material.roughnessFactor,
                            material.alphaMask ? material.alphaCutoff : 0.0F,
                            push.mrAlpha.w);

        if (submesh.material < materialSets_.size()) {
          materialSet = materialSets_[submesh.material];
        }
      }

      if (materialSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_,
                                2,
                                1,
                                &materialSet,
                                0,
                                nullptr);
      }

      vkCmdPushConstants(cmd,
                         pipelineLayout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0,
                         sizeof(DrawPush),
                         &push);

      vkCmdDrawIndexed(cmd,
                       submesh.indexCount,
                       1,
                       submesh.firstIndex,
                       0,
                       0);
    }
  }
}

}  // namespace vv

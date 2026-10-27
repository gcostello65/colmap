#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <VkBootstrap.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "colmap/sensor/bitmap.h"

#include <vk_mem_alloc.h>

namespace colmap {

// With VMA, all resources have the allocation bookkeeping that needs to live
// alongside the resource
struct AllocatedBuffer {
  VkBuffer buffer;
  VmaAllocation allocation;
  void* mapped = nullptr;
};

struct AllocatedImage {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

struct SampledImage {
  AllocatedImage image;
  VkSampler sampler;
};

struct ComputePipeline {
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
};

struct CommandResources {
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
};

class VulkanContext {
 public:
  VulkanContext();
  ~VulkanContext();

  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;

  bool IsInitialized() const;

  std::string DeviceName() const;

  AllocatedBuffer createBuffer(VkDeviceSize size,
                               VkBufferUsageFlags usageFlags,
                               VkSharingMode sharingMode,
                               VmaAllocationCreateFlags allocFlags);

  VkDescriptorSetLayout createDescriptorSetLayout(
      const std::vector<VkDescriptorSetLayoutBinding>& bindings);

  static VkDescriptorPoolSize createDescriptorPoolSize(
      VkDescriptorType descriptorType, uint32_t descriptorCount);

  VkDescriptorPool createDescriptorPool(
      uint32_t maxDescriptorSets,
      const std::vector<VkDescriptorPoolSize>& poolSizes);

  VkDescriptorSet createDescriptorSet(
      const VkDescriptorPool& descriptorPool,
      const VkDescriptorSetLayout& descriptorSetLayout);

  // bufferInfos must remain valid until updateDescriptorSets() returns.
  VkWriteDescriptorSet createBufferDescriptorWrite(
      const VkDescriptorSet& descriptorSet,
      uint32_t binding,
      uint32_t destinationArrayElement,
      const std::vector<VkDescriptorBufferInfo>& bufferInfos,
      VkDescriptorType descriptorType);

  ComputePipeline createComputePipeline(
      const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
      VkShaderModule shaderModule);

  CommandResources createCommandResources();

  // Call after GPU work using this image has finished.
  void destroyImage(AllocatedImage &image);

  void copyImageDataToVkImage(VkImage &image, const Bitmap &imageData);

  AllocatedImage createGrayscaleImage(uint32_t width,
                                      uint32_t height,
                                      VkFormat format,
                                      VkImageUsageFlags usage,
                                      const Bitmap &imageData);

  CommandResources beginCommand();
  void endCommand(CommandResources &commandResources);
  void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout ,VkImageLayout newLayout);

 private:
  struct Context;
  std::unique_ptr<Context> context_;
  VkShaderModule createShaderModule(const std::vector<uint32_t>& shader_code);
};

}  // namespace colmap
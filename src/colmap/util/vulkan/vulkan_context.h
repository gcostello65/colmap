#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <VkBootstrap.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace colmap {

// With VMA, all resources have the allocation bookkeeping that needs to live alongside the resource
struct AllocatedBuffer {
  VkBuffer buffer;
  VmaAllocation allocation;
  void* mapped = nullptr;
};


class VulkanContext {
 public:
  VulkanContext();
  ~VulkanContext();

  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;

  bool IsInitialized() const;

  std::string DeviceName() const;

  // Temporary smoke-test operation.
  // Executes the addition on the GPU.
  uint32_t Add(uint32_t a, uint32_t b);
  AllocatedBuffer createBuffer(VkDeviceSize size,
                                      VkBufferUsageFlags usageFlags,
                                      VkSharingMode sharingMode,
                                      VmaAllocationCreateFlags allocFlags);

 VkDescriptorSetLayout createDescriptorSetLayout(
      const std::vector<VkDescriptorSetLayoutBinding>& bindings);

 static VkDescriptorPoolSize createDescriptorPoolSize(VkDescriptorType descriptorType,
                                               uint32_t descriptorCount);

 VkDescriptorPool createDescriptorPool(
     uint32_t maxDescriptorSets,
     const std::vector<VkDescriptorPoolSize>& poolSizes);

VkDescriptorSet createDescriptorSet(const VkDescriptorPool &descriptorPool, const VkDescriptorSetLayout &descriptorSetLayout);

VkWriteDescriptorSet createBufferDescriptorWrite(
    const VkDescriptorSet& descriptorSet,
    uint32_t binding,
    uint32_t destinationArrayElement,
    const std::vector<VkDescriptorBufferInfo>& bufferInfos,
    VkDescriptorType descriptorType);

     private:
  struct Context;
  std::unique_ptr<Context> context_;
};

}  // namespace colmap
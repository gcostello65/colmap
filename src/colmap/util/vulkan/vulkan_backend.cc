#include "colmap/util/vulkan/vulkan_backend.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <VkBootstrap.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace colmap {

namespace {

std::vector<char> ReadFile(const std::string& filename) {
  // reading the shader file spirv
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + filename);
  }

  const std::size_t file_size = static_cast<std::size_t>(file.tellg());

  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(file_size));

  return buffer;
}

uint32_t GetMemoryIndex(const vkb::Device& device, uint32_t type_bits) {
  const VkPhysicalDeviceMemoryProperties& properties =
      device.physical_device.memory_properties;

  for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    const bool type_supported = (type_bits & (1u << i)) != 0;

    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const bool properties_supported =
        (properties.memoryTypes[i].propertyFlags & required) == required;

    if (type_supported && properties_supported) {
      return i;
    }
  }

  throw std::runtime_error(
      "Could not find suitable host-visible Vulkan memory");
}

}  // namespace

// Using PImpl pattern here to decouple internal vulkan objects from the header
struct VulkanBackend::Impl {
  vkb::Instance instance;
  vkb::InstanceDispatchTable instance_dispatch;

  vkb::PhysicalDevice physical_device;

  vkb::Device device;
  vkb::DispatchTable dispatch;

  VmaAllocator allocator;

  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;

  bool initialized = false;
};

// RAII: the constructor initializes the standard vulkan objects needed to perform computations
VulkanBackend::VulkanBackend() : impl_(std::make_unique<Impl>()) {
  //
  // Instance
  //
  vkb::InstanceBuilder instance_builder;

  auto instance_result = instance_builder.use_default_debug_messenger()
                             .request_validation_layers()
                             .set_headless()
                             .build();

  if (!instance_result) {
    throw std::runtime_error("Failed to create Vulkan instance: " +
                             instance_result.error().message());
  }

  impl_->instance = instance_result.value();
  impl_->instance_dispatch = impl_->instance.make_table();

  //
  // Physical device
  //
  vkb::PhysicalDeviceSelector selector{impl_->instance};

  auto physical_device_result = selector.select();

  if (!physical_device_result) {
    std::string error = "Failed to select Vulkan physical device: " +
                        physical_device_result.error().message();

    const auto& reasons = physical_device_result.detailed_failure_reasons();

    for (const std::string& reason : reasons) {
      error += "\n  " + reason;
    }

    throw std::runtime_error(error);
  }

  impl_->physical_device = physical_device_result.value();

  //
  // Logical device
  //
  vkb::DeviceBuilder device_builder{impl_->physical_device};

  auto device_result = device_builder.build();

  if (!device_result) {
    throw std::runtime_error("Failed to create Vulkan device: " +
                             device_result.error().message());
  }

  impl_->device = device_result.value();
  impl_->dispatch = impl_->device.make_table();

  //
  // Compute can run on a graphics queue.
  //
  auto queue_result = impl_->device.get_queue(vkb::QueueType::compute);

  if (!queue_result) {
    throw std::runtime_error("Failed to get Vulkan queue: " +
                             queue_result.error().message());
  }

  impl_->queue = queue_result.value();

  auto queue_index_result =
      impl_->device.get_queue_index(vkb::QueueType::compute);

  if (!queue_index_result) {
    throw std::runtime_error("Failed to get Vulkan queue family index");
  }

  impl_->queue_family_index = queue_index_result.value();

  impl_->initialized = true;

  // Init VMA allocator
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorCreateInfo = {};
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
  allocatorCreateInfo.physicalDevice = impl_->physical_device;
  allocatorCreateInfo.device = impl_->device;
  allocatorCreateInfo.instance = impl_->instance;
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VmaAllocator allocator;
  vmaCreateAllocator(&allocatorCreateInfo, &allocator);

  if (allocator == VK_NULL_HANDLE) {
    throw std::runtime_error("Could not create VMA allocator");
  }
  impl_->allocator = allocator;
}

VulkanBackend::~VulkanBackend() {
  if (!impl_) {
    return;
  }

  if (impl_->device.device != VK_NULL_HANDLE) {
    impl_->dispatch.deviceWaitIdle();

    vkb::destroy_device(impl_->device);
  }

  if (impl_->instance.instance != VK_NULL_HANDLE) {
    vkb::destroy_instance(impl_->instance);
  }

  if (impl_->instance.instance != VK_NULL_HANDLE) {
    vmaDestroyAllocator(impl_->allocator);
  }
}

bool VulkanBackend::IsInitialized() const {
  return impl_ && impl_->initialized;
}

std::string VulkanBackend::DeviceName() const {
  if (!impl_ || !impl_->initialized) {
    return {};
  }

  return impl_->physical_device.properties.deviceName;
}

//void patch_match();
//void VulkanBackend::patch_match() {
//
//}


uint32_t VulkanBackend::Add(uint32_t a, uint32_t b) {
  if (!IsInitialized()) {
    throw std::runtime_error("Vulkan backend is not initialized");
  }

  //
  // Descriptor set layout
  //
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binding.descriptorCount = 2;
  binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;

  if (impl_->dispatch.createDescriptorSetLayout(
          &layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor set layout");
  }

  //
  // Descriptor pool
  //
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_size.descriptorCount = 2;

  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;

  if (impl_->dispatch.createDescriptorPool(
          &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool");
  }

  //
  // Descriptor set
  //
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

  VkDescriptorSetAllocateInfo descriptor_alloc_info{};
  descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_alloc_info.descriptorPool = descriptor_pool;
  descriptor_alloc_info.descriptorSetCount = 1;
  descriptor_alloc_info.pSetLayouts = &descriptor_set_layout;

  if (impl_->dispatch.allocateDescriptorSets(&descriptor_alloc_info,
                                             &descriptor_set) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate descriptor set");
  }

  //
  // Create the two storage buffers.
  //
  VkBuffer buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

  VkDeviceMemory memories[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

  uint32_t* mapped[2] = {nullptr, nullptr};

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(uint32_t);
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  for (int i = 0; i < 2; ++i) {
    if (impl_->dispatch.createBuffer(&buffer_info, nullptr, &buffers[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create Vulkan buffer");
    }

    VkMemoryRequirements requirements{};

    impl_->dispatch.getBufferMemoryRequirements(buffers[i], &requirements);

    VkMemoryAllocateInfo allocation_info{};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = requirements.size;
    allocation_info.memoryTypeIndex =
        GetMemoryIndex(impl_->device, requirements.memoryTypeBits);

    if (impl_->dispatch.allocateMemory(
            &allocation_info, nullptr, &memories[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate Vulkan memory");
    }

    if (impl_->dispatch.bindBufferMemory(buffers[i], memories[i], 0) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to bind Vulkan buffer memory");
    }

    if (impl_->dispatch.mapMemory(memories[i],
                                  0,
                                  VK_WHOLE_SIZE,
                                  0,
                                  reinterpret_cast<void**>(&mapped[i])) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to map Vulkan memory");
    }
  }

  mapped[0][0] = a;
  mapped[1][0] = b;

  //
  // Point the descriptor set at our buffers.
  //
  VkDescriptorBufferInfo buffer_infos[2]{};

  buffer_infos[0].buffer = buffers[0];
  buffer_infos[0].offset = 0;
  buffer_infos[0].range = sizeof(uint32_t);

  buffer_infos[1].buffer = buffers[1];
  buffer_infos[1].offset = 0;
  buffer_infos[1].range = sizeof(uint32_t);

  VkWriteDescriptorSet descriptor_write{};
  descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_write.dstSet = descriptor_set;
  descriptor_write.dstBinding = 0;
  descriptor_write.dstArrayElement = 0;
  descriptor_write.descriptorCount = 2;
  descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_write.pBufferInfo = buffer_infos;

  impl_->dispatch.updateDescriptorSets(1, &descriptor_write, 0, nullptr);

  //
  // Load SPIR-V.
  //
  const std::string shader_path = std::string(COLMAP_SOURCE_DIR) +
                                  "/src/colmap/util/vulkan/shaders/"
                                  "simple_compute.comp.spv";

  const std::vector<char> shader_code = ReadFile(shader_path);

  VkShaderModuleCreateInfo shader_info{};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.codeSize = shader_code.size();
  shader_info.pCode = reinterpret_cast<const uint32_t*>(shader_code.data());

  VkShaderModule shader_module = VK_NULL_HANDLE;

  if (impl_->dispatch.createShaderModule(
          &shader_info, nullptr, &shader_module) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute shader module");
  }

  //
  // Pipeline layout
  //
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout;

  if (impl_->dispatch.createPipelineLayout(
          &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout");
  }

  //
  // Compute pipeline
  //
  VkPipelineShaderStageCreateInfo shader_stage{};
  shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shader_stage.module = shader_module;
  shader_stage.pName = "main";

  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage = shader_stage;
  pipeline_info.layout = pipeline_layout;

  VkPipeline pipeline = VK_NULL_HANDLE;

  if (impl_->dispatch.createComputePipelines(
          VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute pipeline");
  }

  //
  // Shader module is no longer needed after pipeline creation.
  //
  impl_->dispatch.destroyShaderModule(shader_module, nullptr);

  //
  // Command pool
  //
  VkCommandPool command_pool = VK_NULL_HANDLE;

  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.queueFamilyIndex = impl_->queue_family_index;

  if (impl_->dispatch.createCommandPool(
          &command_pool_info, nullptr, &command_pool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool");
  }

  //
  // Command buffer
  //
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  VkCommandBufferAllocateInfo command_alloc_info{};
  command_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_alloc_info.commandPool = command_pool;
  command_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_alloc_info.commandBufferCount = 1;

  if (impl_->dispatch.allocateCommandBuffers(&command_alloc_info,
                                             &command_buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffer");
  }

  //
  // Record compute commands.
  //
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (impl_->dispatch.beginCommandBuffer(command_buffer, &begin_info) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  impl_->dispatch.cmdBindPipeline(
      command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

  impl_->dispatch.cmdBindDescriptorSets(command_buffer,
                                        VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipeline_layout,
                                        0,
                                        1,
                                        &descriptor_set,
                                        0,
                                        nullptr);

  impl_->dispatch.cmdDispatch(command_buffer, 1, 1, 1);

  if (impl_->dispatch.endCommandBuffer(command_buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to end command buffer");
  }

  //
  // Submit.
  //
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  if (impl_->dispatch.queueSubmit(
          impl_->queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit Vulkan compute work");
  }

  impl_->dispatch.deviceWaitIdle();

  //
  // Shader writes the result into buffer A.
  //
  const uint32_t result = mapped[0][0];

  //
  // Cleanup.
  //
  impl_->dispatch.destroyCommandPool(command_pool, nullptr);

  impl_->dispatch.destroyPipeline(pipeline, nullptr);

  impl_->dispatch.destroyPipelineLayout(pipeline_layout, nullptr);

  impl_->dispatch.destroyDescriptorPool(descriptor_pool, nullptr);

  impl_->dispatch.destroyDescriptorSetLayout(descriptor_set_layout, nullptr);

  for (int i = 0; i < 2; ++i) {
    impl_->dispatch.unmapMemory(memories[i]);

    impl_->dispatch.destroyBuffer(buffers[i], nullptr);

    impl_->dispatch.freeMemory(memories[i], nullptr);
  }

  return result;
}

}  // namespace colmap
#include "colmap/util/vulkan/vulkan_context.h"

namespace colmap {

namespace {

std::vector<uint32_t> ReadFile(const std::string& filename) {
  // reading the shader file spirv
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + filename);
  }

  const std::size_t file_size = static_cast<std::size_t>(file.tellg());

  if (file_size == 0 || file_size % sizeof(uint32_t) != 0) {
    throw std::runtime_error("Invalid SPIR-V file size: " + filename);
  }

  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(buffer.data()),
                 static_cast<std::streamsize>(file_size))) {
    throw std::runtime_error("Failed to read shader file: " + filename);
  }

  return buffer;
}

VkDescriptorSetLayoutBinding createDescriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlagBits shaderStage,
                                                              uint32_t bindingIndex, uint32_t descriptorCount) {
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = bindingIndex;
  binding.descriptorType = type;
  binding.descriptorCount = descriptorCount;
  binding.stageFlags = shaderStage;

  return binding;
}
}  // namespace

// Using PImpl pattern here to decouple internal vulkan objects from the header
struct VulkanContext::Context {
  vkb::Instance instance;
  vkb::InstanceDispatchTable instance_dispatch;

  vkb::PhysicalDevice physical_device;

  vkb::Device device;
  vkb::DispatchTable dispatch;

  VmaAllocator allocator = VK_NULL_HANDLE;

  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;

  bool initialized = false;

  ~Context() {
    if (device.device != VK_NULL_HANDLE) {
      dispatch.deviceWaitIdle();
    }
    if (allocator != VK_NULL_HANDLE) {
      vmaDestroyAllocator(allocator);
    }
    if (device.device != VK_NULL_HANDLE) {
      vkb::destroy_device(device);
    }
    if (instance.instance != VK_NULL_HANDLE) {
      vkb::destroy_instance(instance);
    }
  }
};

// RAII: the constructor initializes the standard vulkan objects needed to perform computations
VulkanContext::VulkanContext() : context_(std::make_unique<Context>()) {
  //
  // Instance
  //
  vkb::InstanceBuilder instance_builder;

  auto instance_result = instance_builder.use_default_debug_messenger()
                             .request_validation_layers()
                             .set_headless()
                             .require_api_version(1, 1, 0)
                             .build();

  if (!instance_result) {
    throw std::runtime_error("Failed to create Vulkan instance: " +
                             instance_result.error().message());
  }

  context_->instance = instance_result.value();
  context_->instance_dispatch = context_->instance.make_table();

  //
  // Physical device
  //
  vkb::PhysicalDeviceSelector selector{context_->instance};

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

  context_->physical_device = physical_device_result.value();

  //
  // Logical device
  //
  vkb::DeviceBuilder device_builder{context_->physical_device};

  auto device_result = device_builder.build();

  if (!device_result) {
    throw std::runtime_error("Failed to create Vulkan device: " +
                             device_result.error().message());
  }

  context_->device = device_result.value();
  context_->dispatch = context_->device.make_table();

  //
  // Compute can run on a graphics queue.
  //
  auto queue_result = context_->device.get_queue(vkb::QueueType::compute);

  if (!queue_result) {
    throw std::runtime_error("Failed to get Vulkan queue: " +
                             queue_result.error().message());
  }

  context_->queue = queue_result.value();

  auto queue_index_result =
      context_->device.get_queue_index(vkb::QueueType::compute);

  if (!queue_index_result) {
    throw std::runtime_error("Failed to get Vulkan queue family index");
  }

  context_->queue_family_index = queue_index_result.value();

  // Init VMA allocator
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorCreateInfo = {};
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_1;
  allocatorCreateInfo.physicalDevice = context_->physical_device;
  allocatorCreateInfo.device = context_->device;
  allocatorCreateInfo.instance = context_->instance;
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  if (vmaCreateAllocator(&allocatorCreateInfo, &context_->allocator) != VK_SUCCESS) {
    throw std::runtime_error("Could not create VMA allocator");
  }
  context_->initialized = true;
}

VulkanContext::~VulkanContext() = default;

bool VulkanContext::IsInitialized() const {
  return context_ && context_->initialized;
}

std::string VulkanContext::DeviceName() const {
  if (!context_ || !context_->initialized) {
    return {};
  }

  return context_->physical_device.properties.deviceName;
}

VkDescriptorSetLayout VulkanContext::createDescriptorSetLayout(
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
  VkDescriptorSetLayout descriptorSetLayout{};

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = bindings.size();
  layout_info.pBindings = bindings.data();

  if (context_->dispatch.createDescriptorSetLayout(&layout_info,
                                                   nullptr, &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor set layout");
  }

  return descriptorSetLayout;
}

VkDescriptorPoolSize VulkanContext::createDescriptorPoolSize(VkDescriptorType descriptorType, uint32_t descriptorCount) {
  VkDescriptorPoolSize pool_size{};
  pool_size.type = descriptorType;
  pool_size.descriptorCount = descriptorCount;
  return pool_size;
}

VkDescriptorPool VulkanContext::createDescriptorPool(
    uint32_t maxDescriptorSets,
    const std::vector<VkDescriptorPoolSize>& poolSizes) {
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = maxDescriptorSets;
  pool_info.poolSizeCount = poolSizes.size();
  pool_info.pPoolSizes = poolSizes.data();

  VkDescriptorPool descriptorPool{};

  if (context_->dispatch.createDescriptorPool(&pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool");
  }

  return descriptorPool;
}

VkDescriptorSet VulkanContext::createDescriptorSet(const VkDescriptorPool &descriptorPool, const VkDescriptorSetLayout &descriptorSetLayout) {
  VkDescriptorSetAllocateInfo descriptor_alloc_info{};
  descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_alloc_info.descriptorPool = descriptorPool;
  descriptor_alloc_info.descriptorSetCount = 1;
  descriptor_alloc_info.pSetLayouts = &descriptorSetLayout;

  VkDescriptorSet descriptorSet{};

  if (context_->dispatch.allocateDescriptorSets(&descriptor_alloc_info,
                                                &descriptorSet) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate descriptor set");
  }

  return descriptorSet;
}

//TODO: createImageDescriptorWrite(...) as an analog to this below
VkWriteDescriptorSet VulkanContext::createBufferDescriptorWrite(const VkDescriptorSet &descriptorSet, uint32_t binding, uint32_t destinationArrayElement,
                                                                const std::vector<VkDescriptorBufferInfo> &bufferInfos, VkDescriptorType descriptorType) {
  VkWriteDescriptorSet descriptor_write{};
  descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_write.dstSet = descriptorSet;
  descriptor_write.dstBinding = binding;
  descriptor_write.dstArrayElement = destinationArrayElement;
  descriptor_write.descriptorCount = bufferInfos.size();
  descriptor_write.descriptorType = descriptorType;
  descriptor_write.pBufferInfo = bufferInfos.data();

  return descriptor_write;
}

AllocatedBuffer VulkanContext::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usageFlags,
    VkSharingMode sharingMode,
    VmaAllocationCreateFlags allocFlags) {
  AllocatedBuffer allocBuffer{};

  VkBufferCreateInfo bInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bInfo.size = size;
  bInfo.usage = usageFlags;
  bInfo.sharingMode = sharingMode;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocInfo.flags = allocFlags;

  VmaAllocationInfo allocationInfo{};
  VkResult allocResult = vmaCreateBuffer(context_->allocator, &bInfo, &allocInfo,
                                         &allocBuffer.buffer, &allocBuffer.allocation, &allocationInfo);
  allocBuffer.mapped = allocationInfo.pMappedData;

  // TODO: Determine, Greg, if we should silently fail here and retry?
  if (allocResult != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan buffer");
  }

  return allocBuffer;
}

VkShaderModule VulkanContext::createShaderModule(const std::vector<uint32_t>& shader_code) {
  VkShaderModuleCreateInfo shader_info{};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.codeSize = shader_code.size() * sizeof(uint32_t);
  shader_info.pCode = shader_code.data();

  VkShaderModule shaderModule{};
  if (context_->dispatch.createShaderModule(
          &shader_info, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute shader module");
  }

  return shaderModule;
}

ComputePipeline VulkanContext::createComputePipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts,
                                                     VkShaderModule shaderModule) {
  //
  // Pipeline layout
  //
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = descriptorSetLayouts.size();
  pipeline_layout_info.pSetLayouts = descriptorSetLayouts.data();

  if (context_->dispatch.createPipelineLayout(
          &pipeline_layout_info, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout");
  }

  //
  // Compute pipeline
  //
  VkPipelineShaderStageCreateInfo shader_stage{};
  shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shader_stage.module = shaderModule;
  shader_stage.pName = "main";

  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage = shader_stage;
  pipeline_info.layout = pipelineLayout;

  VkPipeline pipeline = VK_NULL_HANDLE;

  if (context_->dispatch.createComputePipelines(
          VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) !=
      VK_SUCCESS) {
    context_->dispatch.destroyPipeline(pipeline, nullptr);
    context_->dispatch.destroyPipelineLayout(pipelineLayout, nullptr);
    throw std::runtime_error("Failed to create compute pipeline");
  }

  return {pipeline, pipelineLayout};
}

CommandResources VulkanContext::createCommandResources() {
  VkCommandPool command_pool = VK_NULL_HANDLE;

  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.queueFamilyIndex = context_->queue_family_index;

  if (context_->dispatch.createCommandPool(
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

  if (context_->dispatch.allocateCommandBuffers(&command_alloc_info,
                                                &command_buffer) != VK_SUCCESS) {
    context_->dispatch.destroyCommandPool(command_pool, nullptr);
    throw std::runtime_error("Failed to allocate command buffer");
  }

  return {command_pool, command_buffer};
}

uint32_t VulkanContext::Add(uint32_t a, uint32_t b) {
  if (!IsInitialized()) {
    throw std::runtime_error("Vulkan backend is not initialized");
  }

  //
  // Descriptor set layout
  //
  std::vector<VkDescriptorSetLayoutBinding> bindings{};

  VkDescriptorSetLayoutBinding binding = createDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                                          VK_SHADER_STAGE_COMPUTE_BIT, 0, 2);

  bindings.push_back(binding);

  VkDescriptorSetLayout descriptor_set_layout = createDescriptorSetLayout(bindings);

  //
  // Descriptor pool
  //
  std::vector<VkDescriptorPoolSize> poolSizes{};

  VkDescriptorPoolSize poolSize = createDescriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2);

  poolSizes.push_back(poolSize);

  VkDescriptorPool descriptorPool = createDescriptorPool(1, poolSizes);

  //
  // Descriptor set
  //
  VkDescriptorSet descriptor_set = createDescriptorSet(descriptorPool, descriptor_set_layout);

  //
  // Create the two storage buffers.
  //
  AllocatedBuffer buffers[2] = {{}, {}};

  for (int i = 0; i < 2; ++i) {
    buffers[i] = createBuffer(
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);
  }

  *static_cast<uint32_t*>(buffers[0].mapped) = a;

  *static_cast<uint32_t*>(buffers[1].mapped) = b;

  //
  // Point the descriptor set at our buffers.
  //
  std::vector<VkDescriptorBufferInfo> buffer_infos{};
  buffer_infos.resize(2);

  buffer_infos[0].buffer = buffers[0].buffer;
  buffer_infos[0].offset = 0;
  buffer_infos[0].range = sizeof(uint32_t);

  buffer_infos[1].buffer = buffers[1].buffer;
  buffer_infos[1].offset = 0;
  buffer_infos[1].range = sizeof(uint32_t);

  std::vector<VkWriteDescriptorSet> writeDescriptorSets{};

  writeDescriptorSets.push_back(
      createBufferDescriptorWrite(descriptor_set,
                                  binding.binding,
                                  0,
                                  buffer_infos,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

  context_->dispatch.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
  //
  // Load SPIR-V.
  //
  const std::string shaderPath = std::string(COLMAP_VULKAN_SHADER_DIR) +
                                 "/"
                                 "simple_compute.comp.spv";

  const std::vector<uint32_t> shaderCode = ReadFile(shaderPath);


  VkShaderModule shaderModule = createShaderModule(shaderCode);


  std::vector<VkDescriptorSetLayout> descLayouts{};

  descLayouts.push_back(descriptor_set_layout);
  ComputePipeline computePipeline = createComputePipeline(descLayouts, shaderModule);

  //
  // Shader module is no longer needed after pipeline creation.
  //
  context_->dispatch.destroyShaderModule(shaderModule, nullptr);

  //
  // Command pool
  //
  CommandResources commandResources = createCommandResources();

  //
  // Record compute commands.
  //
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (context_->dispatch.beginCommandBuffer(commandResources.commandBuffer, &begin_info) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  context_->dispatch.cmdBindPipeline(
      commandResources.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);

  context_->dispatch.cmdBindDescriptorSets(commandResources.commandBuffer,
                                           VK_PIPELINE_BIND_POINT_COMPUTE,
                                           computePipeline.pipelineLayout,
                                           0,
                                           1,
                                           &descriptor_set,
                                           0,
                                           nullptr);

  context_->dispatch.cmdDispatch(commandResources.commandBuffer, 1, 1, 1);

  if (context_->dispatch.endCommandBuffer(commandResources.commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to end command buffer");
  }

  //
  // Submit.
  //
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &commandResources.commandBuffer;

  if (context_->dispatch.queueSubmit(
          context_->queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit Vulkan compute work");
  }

  context_->dispatch.deviceWaitIdle();

  //
  // Shader writes the result into buffer A.
  //
  const uint32_t result =  *static_cast<uint32_t*>(buffers[0].mapped);


  //TODO - Move this into the destructor and make sure it is owned by the correct code, not the throw away Add method
  //
  // Cleanup.
  //
  context_->dispatch.destroyCommandPool(commandResources.commandPool, nullptr);

  context_->dispatch.destroyPipeline(computePipeline.pipeline, nullptr);

  context_->dispatch.destroyPipelineLayout(computePipeline.pipelineLayout, nullptr);

  context_->dispatch.destroyDescriptorPool(descriptorPool, nullptr);

  context_->dispatch.destroyDescriptorSetLayout(descriptor_set_layout, nullptr);

  for (int i = 0; i < 2; ++i) {
    vmaDestroyBuffer(
        context_->allocator, buffers[i].buffer, buffers[i].allocation);
  }

  return result;
}

CommandResources VulkanContext::beginCommand() {
  CommandResources commandResources = createCommandResources();
  VkCommandBufferBeginInfo beginInfo{};

  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandResources.commandBuffer, &beginInfo);

  return commandResources;
}

void VulkanContext::endCommand(CommandResources commandResources) {
  vkEndCommandBuffer(commandResources.commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandResources.commandBuffer;

  vkQueueSubmit(context_->queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(context_->queue);

  vkFreeCommandBuffers(context_->device, commandResources.commandPool, 1, & commandResources.commandBuffer);
}

void VulkanContext::copyImageDataToVkImage(VkImage &image, const Bitmap &imageData) {
  AllocatedBuffer stagingBuffer = createBuffer(
      imageData.NumBytes(),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

  if (vmaCopyMemoryToAllocation(context_->allocator, imageData.RowMajorData().data(),
                                stagingBuffer.allocation, 0, imageData.NumBytes()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to copy image data to the staging buffer");
  }

  transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  CommandResources commandResources = beginCommand();
  VkBufferImageCopy region{};

  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {
      static_cast<uint32_t>(imageData.Width()),
      static_cast<uint32_t>(imageData.Height()),
      1
  };

  vkCmdCopyBufferToImage(
      commandResources.commandBuffer,
      stagingBuffer.buffer,
      image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region
  );

  endCommand(commandResources);

  transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vmaDestroyBuffer(context_->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

void VulkanContext::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout ,VkImageLayout newLayout) {
  CommandResources commandResources = beginCommand();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(
      commandResources.commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier
  );

  endCommand(commandResources);
}

AllocatedImage VulkanContext::createGrayscaleImage(uint32_t width,
                           uint32_t height,
                           VkFormat format,
                           VkImageUsageFlags usage, const Bitmap &imageData) {
  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1; // Maybe make this configurable, but for now just one
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = usage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCreateInfo{};
  allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

  VkImage image;
  VmaAllocation allocation;

  if (vmaCreateImage(context_->allocator, &imageInfo, &allocationCreateInfo,
                     &image, &allocation, nullptr) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image");
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView{};

  if (vkCreateImageView(context_->device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image view");
  }

  copyImageDataToVkImage(image, imageData);

  return {image, allocation, imageView};
}

}  // namespace colmap
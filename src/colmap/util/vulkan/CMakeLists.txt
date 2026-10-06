set(FOLDER_NAME "vulkan")

COLMAP_ADD_LIBRARY(
        NAME colmap_vulkan
        SRCS
        vulkan_backend.h
        vulkan_backend.cc
        vma.cc
        PRIVATE_LINK_LIBS
        Vulkan::Vulkan
)

target_link_libraries(colmap_vulkan
        PRIVATE
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
)

target_compile_definitions(
        colmap_vulkan
        PRIVATE COLMAP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(colmap_vulkan SYSTEM PRIVATE
        ${vma_SOURCE_DIR}/include
)
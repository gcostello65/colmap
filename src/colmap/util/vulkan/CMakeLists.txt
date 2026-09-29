set(FOLDER_NAME "vulkan")

COLMAP_ADD_LIBRARY(
        NAME colmap_vulkan
        SRCS
        vulkan_backend.h
        vulkan_backend.cc
        PRIVATE_LINK_LIBS
        Vulkan::Vulkan
)
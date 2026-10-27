#include "colmap/util/vulkan/vulkan_context.h"

#include <iostream>

#include <gtest/gtest.h>

namespace colmap {
namespace {

TEST(VulkanBackendTest, Initializes) {
  VulkanContext backend;

  EXPECT_TRUE(backend.IsInitialized());
  EXPECT_FALSE(backend.DeviceName().empty());

  std::cout << "Vulkan device: "
            << backend.DeviceName()
            << '\n';
}

}  // namespace
}  // namespace colmap

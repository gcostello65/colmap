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

TEST(VulkanBackendTest, HeadlessCompute) {
  VulkanContext backend;

  EXPECT_EQ(backend.Add(42, 58), 100);
}

TEST(VulkanBackendTest, HeadlessComputeDifferentValues) {
  VulkanContext backend;

  EXPECT_EQ(backend.Add(123, 456), 579);
}

}  // namespace
}  // namespace colmap
#include "colmap/util/vulkan/vulkan_backend.h"

#include <gtest/gtest.h>

#include <iostream>

namespace colmap {
namespace {

TEST(VulkanBackendTest, Initializes) {
  VulkanBackend backend;

  EXPECT_TRUE(backend.IsInitialized());
  EXPECT_FALSE(backend.DeviceName().empty());

  std::cout << "Vulkan device: "
            << backend.DeviceName()
            << '\n';
}

TEST(VulkanBackendTest, HeadlessCompute) {
  VulkanBackend backend;

  EXPECT_EQ(backend.Add(42, 58), 100);
}

TEST(VulkanBackendTest, HeadlessComputeDifferentValues) {
  VulkanBackend backend;

  EXPECT_EQ(backend.Add(123, 456), 579);
}

}  // namespace
}  // namespace colmap
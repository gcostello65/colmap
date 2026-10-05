#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace colmap {

class VulkanBackend {
 public:
  VulkanBackend();
  ~VulkanBackend();

  VulkanBackend(const VulkanBackend&) = delete;
  VulkanBackend& operator=(const VulkanBackend&) = delete;

  bool IsInitialized() const;

  std::string DeviceName() const;

  // Temporary smoke-test operation.
  // Executes the addition on the GPU.
  uint32_t Add(uint32_t a, uint32_t b);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace colmap
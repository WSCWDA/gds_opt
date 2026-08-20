#pragma once

#include <cstddef>

namespace ghc::detail {

class HostPool {
 public:
  HostPool(std::size_t capacity, std::size_t line_size);
  ~HostPool();
  HostPool(const HostPool&) = delete;
  HostPool& operator=(const HostPool&) = delete;

  void* base() const noexcept { return base_; }
  void* slot(std::size_t id) const noexcept;
  std::size_t capacity() const noexcept { return capacity_; }
  std::size_t line_size() const noexcept { return line_size_; }
  std::size_t slots() const noexcept { return capacity_ / line_size_; }

 private:
  void* base_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t line_size_ = 0;
  bool cuda_registered_ = false;
};

}  // namespace ghc::detail

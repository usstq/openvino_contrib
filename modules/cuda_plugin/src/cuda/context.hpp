// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "dnn.hpp"

namespace CUDA {

class ThreadContext {
  CUDA::Device device_;
  CUDA::Stream stream_;
  CUDA::DnnHandle dnnHandle_{stream_};

 public:
  explicit ThreadContext(CUDA::Device d) : device_{d.setCurrent()} {}
  CUDA::Device device() const { return device_; }
  const CUDA::Stream& stream() const noexcept { return stream_; }
  const CUDA::DnnHandle& dnnHandle() const noexcept { return dnnHandle_; }
};

}  // namespace CUDA

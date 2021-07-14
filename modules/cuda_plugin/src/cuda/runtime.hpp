// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cuda_runtime.h>
#include <string>

#include "props.hpp"

#include <gpu/device_pointers.hpp>
#if __has_include(<experimental/source_location>)
#include <experimental/source_location>
#else
namespace std::experimental {
struct source_location {
  constexpr std::uint_least32_t line() const noexcept { return 0; }
  constexpr std::uint_least32_t column() const noexcept { return 0; }
  constexpr const char* file_name() const noexcept { return "unknown"; }
  constexpr const char* function_name() const noexcept { return "unknown"; }
  static constexpr source_location current() noexcept { return {}; }
};
}  // namespace std::experimental
#endif
namespace CUDA {
[[gnu::cold, noreturn]] void throwIEException(
    const std::string& msg, const std::experimental::source_location& location =
                                std::experimental::source_location::current());
[[gnu::cold]] void logError(const std::string& msg,
                            const std::experimental::source_location& location =
                                std::experimental::source_location::current());
}  // namespace CUDA

inline void throwIfError(cudaError_t err,
                         const std::experimental::source_location& location =
                             std::experimental::source_location::current()) {
  if (err != cudaSuccess)
    CUDA::throwIEException(cudaGetErrorString(err), location);
}

inline void logIfError(cudaError_t err,
                       const std::experimental::source_location& location =
                           std::experimental::source_location::current()) {
  if (err != cudaSuccess) CUDA::logError(cudaGetErrorString(err), location);
}

namespace CUDA {

template <typename T, typename R, typename... Args>
T create(R (*creator)(T*, Args... args), Args... args) {
  T t;
  throwIfError(creator(&t, args...));
  return t;
}

class Device {
  int id;

 public:
  Device() : Device{currentId()} {}
  explicit Device(int id) noexcept : id{id} {}
  static int currentId() { return create(cudaGetDevice); }
  static int count() { return create(cudaGetDeviceCount); }
  cudaDeviceProp props() const { return create(cudaGetDeviceProperties, id); }
  const Device& setCurrent() const {
    throwIfError(cudaSetDevice(id));
    return *this;
  }
};

constexpr auto defaultResidentGrids = 16;

inline int residentGrids6x(int minor) {
  switch (minor) {
    case 0:
      return 128;
    case 1:
      return 32;
    case 2:
      return 16;
  }
  return defaultResidentGrids;
}

inline int residentGrids7x(int minor) {
  switch (minor) {
    case 0:
      return 128;
    case 2:
      return 16;
    case 5:
      return 128;
  }
  return defaultResidentGrids;
}

inline int residentGrids8x(int minor) { return 128; }

inline int residentGrids(const cudaDeviceProp& p) {
  switch (p.major) {
    case 6:
      return residentGrids6x(p.minor);
    case 7:
      return residentGrids7x(p.minor);
    case 8:
      return residentGrids8x(p.minor);
  }
  return defaultResidentGrids;
}

inline int maxConcurrentStreams(CUDA::Device d) {
  auto p = d.props();
  int r = p.asyncEngineCount;
  if (!p.concurrentKernels) return r + 1;
  return r + residentGrids(p);
}

inline bool isHalfSupported(CUDA::Device d) {
  const auto computeCompatabilityVersion = std::to_string(d.props().major) + "." + std::to_string(d.props().minor);
  return fp16SupportedArchitecture.count(computeCompatabilityVersion) > 0;
}

inline bool isInt8Supported(CUDA::Device d) {
  const auto computeCompatabilityVersion = std::to_string(d.props().major) + "." + std::to_string(d.props().minor);
  return int8SupportedArchitecture.count(computeCompatabilityVersion) > 0;
}

template <typename R, typename T, typename... Args>
T firstArgHelper(R (*f)(T, Args...));
template <auto F>
using FirstArg = decltype(firstArgHelper(F));

template <auto Construct, auto Destruct, typename N = FirstArg<Destruct>>
class UniqueBase {
  static_assert(std::is_same_v<N, FirstArg<Destruct>>,
                "you can pass third argument explicitly, but it must be the "
                "same as default");  // third arg isn't needed, but
                                     // eclipse parser can't derive it
  using Native = N;
  Native native{create(Construct)};
public:
  ~UniqueBase() {
      logIfError(Destruct(native));
  }
  const Native& get() const noexcept { return native; }
};

template <auto Construct, auto Destruct, typename N>
class UniqueBase<Construct, Destruct, N*> {
  static_assert(std::is_same_v<N*, FirstArg<Destruct>>,
                "you can pass third argument explicitly, but it must be the "
                "same as default");  // third arg isn't needed, but
                                     // eclipse parser can't derive it
  using Native = N*;
  struct Deleter {
    void operator()(Native p) const noexcept { logIfError(Destruct(p)); }
  };
  std::unique_ptr<std::remove_pointer_t<Native>, Deleter> native{
      create(Construct)};

 public:
  Native get() const noexcept { return native.get(); }
};

class Allocation {
  class Deleter {
    cudaStream_t stream;  // no raii, fixme?
    // maybe deallocation stream could be different, i.e. maybe we could have
    // setStream method?
   public:
    Deleter(cudaStream_t stream) noexcept : stream{stream} {}
    void operator()(void* p) const noexcept {
      logIfError(cudaFreeAsync(p, stream));
    }
  };
  std::unique_ptr<void, Deleter> p;

 public:
  Allocation(void* p, cudaStream_t stream) noexcept : p{p, Deleter{stream}} {}
  void* get() const noexcept { return p.get(); }
};

class DefaultAllocation {
  struct Deleter {
      void operator()(void* p) const noexcept {
        logIfError(cudaFree(p));
      }
  };
  std::unique_ptr<void, Deleter> p;

 public:
  explicit DefaultAllocation(void* p) noexcept : p{p} {}
  void* get() const noexcept { return p.get(); }
};

class Stream
    : public UniqueBase<cudaStreamCreate, cudaStreamDestroy, cudaStream_t> {
  void uploadImpl(void* dst, const void* src, std::size_t count) const {
    throwIfError(
        cudaMemcpyAsync(dst, src, count, cudaMemcpyHostToDevice, get()));
  }
  void downloadImpl(void* dst, const void* src, std::size_t count) const {
    throwIfError(
        cudaMemcpyAsync(dst, src, count, cudaMemcpyDeviceToHost, get()));
  }

 public:
  Allocation malloc(std::size_t size) const {
    return {create<void*, cudaError_t>(cudaMallocAsync, size, get()), get()};
  }
  void upload(InferenceEngine::gpu::DevicePointer<void*> dst, const void* src,
              std::size_t count) const {
    uploadImpl(dst.get(), src, count);
  }
  void transfer(InferenceEngine::gpu::DevicePointer<void*> dst,
                InferenceEngine::gpu::DevicePointer<const void*> src,
                std::size_t count) const {
    throwIfError(
        cudaMemcpyAsync(dst.get(), src.get(), count, cudaMemcpyDeviceToDevice, get()));
  }
  void upload(const Allocation& dst, const void* src, std::size_t count) const {
    uploadImpl(dst.get(), src, count);
  }
  void download(void* dst, const Allocation& src, std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
  void download(void* dst, InferenceEngine::gpu::DevicePointer<const void*> src,
                std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
  void download(void* dst, InferenceEngine::gpu::DevicePointer<void*> src,
                std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
  void synchronize() const { throwIfError(cudaStreamSynchronize(get())); }
#ifdef __CUDACC__
  template <typename... Args>
  void run(dim3 gridDim, dim3 blockDim, void (*kernel)(Args...),
           Args... args) const {
    kernel
#ifndef __CDT_PARSER__
        <<<gridDim, blockDim, 0, get()>>>
#endif
        (args...);
  }
#endif
};

class DefaultStream {
  void uploadImpl(void* dst, const void* src, std::size_t count) const {
    throwIfError(cudaMemcpy(dst, src, count, cudaMemcpyHostToDevice));
  }
  void downloadImpl(void* dst, const void* src, std::size_t count) const {
    throwIfError(cudaMemcpy(dst, src, count, cudaMemcpyDeviceToHost));
  }
  DefaultStream() = default;

 public:
  static DefaultStream& stream() {
    static DefaultStream stream{};
    return stream;
  }

  auto malloc(std::size_t size) const {
    return DefaultAllocation{create<void*, cudaError_t>(cudaMalloc, size)};
  }
  void upload(InferenceEngine::gpu::DevicePointer<void*> dst, const void* src,
              std::size_t count) const {
    uploadImpl(dst.get(), src, count);
  }
  void upload(const Allocation& dst, const void* src, std::size_t count) const {
    uploadImpl(dst.get(), src, count);
  }
  void download(void* dst, const Allocation& src, std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
  void download(void* dst, InferenceEngine::gpu::DevicePointer<const void*> src,
                std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
  void download(void* dst, InferenceEngine::gpu::DevicePointer<void*> src,
                std::size_t count) const {
    downloadImpl(dst, src.get(), count);
  }
};

}  // namespace CUDA

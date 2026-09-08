// Copyright 2026 The HRX Authors
// SPDX-License-Identifier: Apache-2.0

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "binding/hip/api.h"
#include "iree/testing/gtest.h"
#include "libhrx/cts/core/amdgpu_executable_test_data.hpp"
#include "libhrx/cts/core/amdgpu_hip_cooperative_test_data.hpp"
#include "libhrx/cts/core/amdgpu_hip_printf_test_data.hpp"

namespace {

const char* CandidateLibPath() {
  if (const char* environment_path = std::getenv("HRX_TEST_LIBAMDHIP64");
      environment_path && *environment_path != '\0') {
    return environment_path;
  }
  return "libamdhip64.so";
}

template <typename T>
T ResolveHipSymbol(void* library, const char* name) {
  return reinterpret_cast<T>(dlsym(library, name));
}

using HipInitFn = hipError_t (*)(unsigned int flags);
using HipGetDeviceFn = hipError_t (*)(int* device);
using HipGetDevicePropertiesFn = hipError_t (*)(hipDeviceProp_t* properties,
                                                int device);
using HipModuleLoadDataFn = hipError_t (*)(hipModule_t* module,
                                           const void* image);
using HipModuleUnloadFn = hipError_t (*)(hipModule_t module);
using HipModuleGetFunctionFn = hipError_t (*)(hipFunction_t* function,
                                              hipModule_t module,
                                              const char* name);
using HipFuncGetAttributeFn = hipError_t (*)(int* value,
                                             hipFuncAttribute_t attribute,
                                             hipFunction_t function);
using HipModuleLaunchKernelFn = hipError_t (*)(
    hipFunction_t function, unsigned int grid_dim_x, unsigned int grid_dim_y,
    unsigned int grid_dim_z, unsigned int block_dim_x, unsigned int block_dim_y,
    unsigned int block_dim_z, unsigned int shared_memory_bytes,
    hipStream_t stream, void** arguments, void** extra);
using HipModuleLaunchCooperativeKernelFn = hipError_t (*)(
    hipFunction_t function, unsigned int grid_dim_x, unsigned int grid_dim_y,
    unsigned int grid_dim_z, unsigned int block_dim_x, unsigned int block_dim_y,
    unsigned int block_dim_z, unsigned int shared_memory_bytes,
    hipStream_t stream, void** arguments);
using HipDeviceSynchronizeFn = hipError_t (*)(void);
using HipMallocFn = hipError_t (*)(hipDeviceptr_t* pointer, size_t size);
using HipFreeFn = hipError_t (*)(hipDeviceptr_t pointer);
using HipMemcpyFn = hipError_t (*)(void* target, const void* source,
                                   size_t size, hipMemcpyKind kind);
using HipMemcpyAsyncFn = hipError_t (*)(void* target, const void* source,
                                        size_t size, hipMemcpyKind kind,
                                        hipStream_t stream);
using HipMemsetAsyncFn = hipError_t (*)(void* target, int value, size_t size,
                                        hipStream_t stream);
using HipStreamCreateFn = hipError_t (*)(hipStream_t* stream);
using HipStreamDestroyFn = hipError_t (*)(hipStream_t stream);
using HipStreamSynchronizeFn = hipError_t (*)(hipStream_t stream);
using HipDeviceGetDevResourceFn = hipError_t (*)(hipDevice_t device,
                                                 hipDevResource* resource,
                                                 hipDevResourceType type);
using HipDeviceGetExecutionCtxFn = hipError_t (*)(hipExecutionCtx_t* context,
                                                  int device);
using HipDevResourceGenerateDescFn =
    hipError_t (*)(hipDevResourceDesc_t* descriptor, hipDevResource* resources,
                   unsigned int resource_count);
using HipGreenCtxCreateFn = hipError_t (*)(hipExecutionCtx_t* context,
                                           hipDevResourceDesc_t descriptor,
                                           int device, unsigned int flags);
using HipExecutionCtxDestroyFn = hipError_t (*)(hipExecutionCtx_t context);
using HipExecutionCtxStreamCreateFn = hipError_t (*)(hipStream_t* stream,
                                                     hipExecutionCtx_t context,
                                                     unsigned int flags,
                                                     int priority);
using HipStreamBeginCaptureFn = hipError_t (*)(hipStream_t stream,
                                               hipStreamCaptureMode mode);
using HipStreamEndCaptureFn = hipError_t (*)(hipStream_t stream,
                                             hipGraph_t* graph);
using HipGraphCreateFn = hipError_t (*)(hipGraph_t* graph, unsigned int flags);
using HipGraphDestroyFn = hipError_t (*)(hipGraph_t graph);
using HipGraphAddKernelNodeFn = hipError_t (*)(
    hipGraphNode_t* node, hipGraph_t graph, const hipGraphNode_t* dependencies,
    size_t dependency_count, const void* params);
using HipGraphInstantiateFn = hipError_t (*)(hipGraphExec_t* graph_executable,
                                             hipGraph_t graph,
                                             hipGraphNode_t* error_node,
                                             char* log_buffer,
                                             size_t buffer_size);
using HipGraphLaunchFn = hipError_t (*)(hipGraphExec_t graph_executable,
                                        hipStream_t stream);
using HipGraphExecDestroyFn = hipError_t (*)(hipGraphExec_t graph_executable);

struct ExecutionContextDeleter {
  // Runtime entry point used to destroy a live execution context.
  HipExecutionCtxDestroyFn destroy = nullptr;

  void operator()(ihipExecutionCtx_t* context) const {
    if (context) {
      const hipError_t result = destroy(context);
      EXPECT_EQ(hipSuccess, result);
    }
  }
};

using ScopedExecutionContext =
    std::unique_ptr<ihipExecutionCtx_t, ExecutionContextDeleter>;

struct StreamDeleter {
  // Runtime entry point used to destroy a live stream.
  HipStreamDestroyFn destroy = nullptr;

  void operator()(hipStream_st* stream) const {
    if (stream) {
      const hipError_t result = destroy(stream);
      EXPECT_EQ(hipSuccess, result);
    }
  }
};

using ScopedStream = std::unique_ptr<hipStream_st, StreamDeleter>;
struct PointerArguments {
  // Device input values read by the kernel.
  uint32_t* input;
  // Device output values written by the kernel.
  uint32_t* output;
};

hipError_t AddKernelGraphNode(HipGraphAddKernelNodeFn add_kernel_node,
                              hipGraph_t graph, hipFunction_t function,
                              hipDeviceptr_t input, hipDeviceptr_t output,
                              hipGraphNode_t* out_node) {
  PointerArguments pointer_arguments = {
      static_cast<uint32_t*>(input),
      static_cast<uint32_t*>(output),
  };
  uint32_t scale = 5;
  uint32_t offset = 2;
  void* arguments[] = {&pointer_arguments, &scale, &offset};
  const hipKernelNodeParams params = {
      /*.blockDim=*/{1, 1, 1},
      /*.extra=*/nullptr,
      /*.func=*/function,
      /*.gridDim=*/{1, 1, 1},
      /*.kernelParams=*/arguments,
      /*.sharedMemBytes=*/0,
  };
  const hipError_t result =
      add_kernel_node(out_node, graph, /*dependencies=*/nullptr,
                      /*dependency_count=*/0, &params);

  // Poison every caller-owned argument container before returning. The graph
  // must already own the copied native argument image.
  pointer_arguments = {};
  scale = UINT32_MAX;
  offset = UINT32_MAX;
  std::fill(std::begin(arguments), std::end(arguments), nullptr);
  return result;
}

hipError_t AddPrintfGraphNode(HipGraphAddKernelNodeFn add_kernel_node,
                              hipGraph_t graph, hipFunction_t function,
                              hipDeviceptr_t output, hipGraphNode_t* out_node) {
  uint32_t value = 42;
  hipDeviceptr_t result = output;
  void* arguments[] = {&value, &result};
  const hipKernelNodeParams params = {
      /*.blockDim=*/{1, 1, 1},
      /*.extra=*/nullptr,
      /*.func=*/function,
      /*.gridDim=*/{1, 1, 1},
      /*.kernelParams=*/arguments,
      /*.sharedMemBytes=*/0,
  };
  const hipError_t add_result =
      add_kernel_node(out_node, graph, /*dependencies=*/nullptr,
                      /*dependency_count=*/0, &params);

  // The graph must own the copied native argument image.
  value = UINT32_MAX;
  result = nullptr;
  std::fill(std::begin(arguments), std::end(arguments), nullptr);
  return add_result;
}

TEST(HipModuleExecutionTest, OwnsLoadAndGraphInputsAcrossReloads) {
  void* library = dlopen(CandidateLibPath(), RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    GTEST_SKIP() << "cannot dlopen " << CandidateLibPath() << ": " << dlerror();
  }

  const auto init = ResolveHipSymbol<HipInitFn>(library, "hipInit");
  const auto get_device =
      ResolveHipSymbol<HipGetDeviceFn>(library, "hipGetDevice");
  const auto get_device_properties = ResolveHipSymbol<HipGetDevicePropertiesFn>(
      library, "hipGetDeviceProperties");
  const auto module_load_data =
      ResolveHipSymbol<HipModuleLoadDataFn>(library, "hipModuleLoadData");
  const auto module_unload =
      ResolveHipSymbol<HipModuleUnloadFn>(library, "hipModuleUnload");
  const auto module_get_function =
      ResolveHipSymbol<HipModuleGetFunctionFn>(library, "hipModuleGetFunction");
  const auto function_get_attribute =
      ResolveHipSymbol<HipFuncGetAttributeFn>(library, "hipFuncGetAttribute");
  const auto module_launch_kernel = ResolveHipSymbol<HipModuleLaunchKernelFn>(
      library, "hipModuleLaunchKernel");
  const auto device_synchronize =
      ResolveHipSymbol<HipDeviceSynchronizeFn>(library, "hipDeviceSynchronize");
  const auto hip_malloc = ResolveHipSymbol<HipMallocFn>(library, "hipMalloc");
  const auto hip_free = ResolveHipSymbol<HipFreeFn>(library, "hipFree");
  const auto hip_memcpy = ResolveHipSymbol<HipMemcpyFn>(library, "hipMemcpy");
  const auto graph_create =
      ResolveHipSymbol<HipGraphCreateFn>(library, "hipGraphCreate");
  const auto graph_destroy =
      ResolveHipSymbol<HipGraphDestroyFn>(library, "hipGraphDestroy");
  const auto graph_add_kernel_node = ResolveHipSymbol<HipGraphAddKernelNodeFn>(
      library, "hipGraphAddKernelNode");
  const auto graph_instantiate =
      ResolveHipSymbol<HipGraphInstantiateFn>(library, "hipGraphInstantiate");
  const auto graph_launch =
      ResolveHipSymbol<HipGraphLaunchFn>(library, "hipGraphLaunch");
  const auto graph_exec_destroy =
      ResolveHipSymbol<HipGraphExecDestroyFn>(library, "hipGraphExecDestroy");

  ASSERT_NE(nullptr, init);
  ASSERT_NE(nullptr, get_device);
  ASSERT_NE(nullptr, get_device_properties);
  ASSERT_NE(nullptr, module_load_data);
  ASSERT_NE(nullptr, module_unload);
  ASSERT_NE(nullptr, module_get_function);
  ASSERT_NE(nullptr, function_get_attribute);
  ASSERT_NE(nullptr, module_launch_kernel);
  ASSERT_NE(nullptr, device_synchronize);
  ASSERT_NE(nullptr, hip_malloc);
  ASSERT_NE(nullptr, hip_free);
  ASSERT_NE(nullptr, hip_memcpy);
  ASSERT_NE(nullptr, graph_create);
  ASSERT_NE(nullptr, graph_destroy);
  ASSERT_NE(nullptr, graph_add_kernel_node);
  ASSERT_NE(nullptr, graph_instantiate);
  ASSERT_NE(nullptr, graph_launch);
  ASSERT_NE(nullptr, graph_exec_destroy);

  const hipError_t init_result = init(/*flags=*/0);
  if (init_result != hipSuccess) {
    GTEST_SKIP() << "hipInit failed: " << init_result;
  }
  int device = 0;
  ASSERT_EQ(hipSuccess, get_device(&device));
  hipDeviceProp_t properties = {};
  ASSERT_EQ(hipSuccess, get_device_properties(&properties, device));

  const hrx_cts::AmdgpuExecutableTestImage test_image =
      hrx_cts::FindAmdgpuExecutableTestImage(properties.gcnArchName);
  ASSERT_NE(nullptr, test_image.file)
      << "no embedded HSACO for " << properties.gcnArchName;

  auto load_transient_image = [&](hipModule_t* out_module) {
    std::vector<uint8_t> image(test_image.file->data,
                               test_image.file->data + test_image.file->size);
    const hipError_t result = module_load_data(out_module, image.data());
    std::fill(image.begin(), image.end(), uint8_t{0xA5});
    std::vector<uint8_t>().swap(image);
    return result;
  };

  hipDeviceptr_t input = nullptr;
  hipDeviceptr_t output = nullptr;
  ASSERT_EQ(hipSuccess, hip_malloc(&input, 4 * sizeof(uint32_t)));
  ASSERT_EQ(hipSuccess, hip_malloc(&output, 4 * sizeof(uint32_t)));
  const std::array<uint32_t, 4> input_values = {1, 2, 3, 4};
  ASSERT_EQ(hipSuccess,
            hip_memcpy(input, input_values.data(), sizeof(input_values),
                       hipMemcpyHostToDevice));

  hipModule_t module = nullptr;
  ASSERT_EQ(hipSuccess, load_transient_image(&module));
  hipFunction_t function = nullptr;
  ASSERT_EQ(hipSuccess, module_get_function(&function, module,
                                            "hrx_transform_nested_pointers"));

  int maximum_threads_per_block = 0;
  int fixed_shared_memory_size = 0;
  int local_memory_size = 0;
  int register_count = 0;
  int maximum_dynamic_shared_memory_size = 0;
  ASSERT_EQ(hipSuccess, function_get_attribute(
                            &maximum_threads_per_block,
                            hipFuncAttributeMaxThreadsPerBlock, function));
  ASSERT_EQ(hipSuccess,
            function_get_attribute(&fixed_shared_memory_size,
                                   hipFuncAttributeSharedSizeBytes, function));
  ASSERT_EQ(hipSuccess,
            function_get_attribute(&local_memory_size,
                                   hipFuncAttributeLocalSizeBytes, function));
  ASSERT_EQ(hipSuccess,
            function_get_attribute(&register_count, hipFuncAttributeNumRegs,
                                   function));
  ASSERT_EQ(hipSuccess,
            function_get_attribute(&maximum_dynamic_shared_memory_size,
                                   hipFuncAttributeMaxDynamicSharedSizeBytes,
                                   function));
  EXPECT_GT(maximum_threads_per_block, 0);
  EXPECT_LE(maximum_threads_per_block, properties.maxThreadsPerBlock);
  ASSERT_GE(fixed_shared_memory_size, 0);
  EXPECT_GE(local_memory_size, 0);
  EXPECT_GT(register_count, 0);
  ASSERT_GE(maximum_dynamic_shared_memory_size, 0);
  EXPECT_EQ(properties.sharedMemPerBlock,
            static_cast<size_t>(fixed_shared_memory_size) +
                maximum_dynamic_shared_memory_size);

  PointerArguments pointer_arguments = {
      static_cast<uint32_t*>(input),
      static_cast<uint32_t*>(output),
  };
  uint32_t scale = 4;
  uint32_t offset = 1;
  void* arguments[] = {&pointer_arguments, &scale, &offset};
  ASSERT_EQ(hipSuccess, module_launch_kernel(function, 1, 1, 1, 1, 1, 1,
                                             /*shared_memory_bytes=*/0,
                                             /*stream=*/nullptr, arguments,
                                             /*extra=*/nullptr));
  ASSERT_EQ(hipSuccess, device_synchronize());
  std::array<uint32_t, 4> actual = {};
  ASSERT_EQ(hipSuccess, hip_memcpy(actual.data(), output, sizeof(actual),
                                   hipMemcpyDeviceToHost));
  EXPECT_EQ((std::array<uint32_t, 4>{5, 9, 13, 17}), actual);

  ASSERT_EQ(hipSuccess, module_unload(module));
  module = nullptr;
  function = nullptr;

  // Reload the same code object after both the first module and its caller-
  // owned image have been destroyed.
  ASSERT_EQ(hipSuccess, load_transient_image(&module));
  ASSERT_EQ(hipSuccess, module_get_function(&function, module,
                                            "hrx_transform_nested_pointers"));

  const std::array<uint32_t, 4> zero_values = {};
  ASSERT_EQ(hipSuccess, hip_memcpy(output, zero_values.data(),
                                   sizeof(zero_values), hipMemcpyHostToDevice));
  struct NativeArguments {
    // Device pointers packed as one by-value argument.
    PointerArguments pointers;
    // Multiplication applied to each input value.
    uint32_t scale;
    // Offset added to each scaled input value.
    uint32_t offset;
    // Caller-provided trailing ABI padding.
    uint8_t trailing_padding[16];
  } native_arguments = {
      /*.pointers=*/pointer_arguments,
      /*.scale=*/3,
      /*.offset=*/10,
      /*.trailing_padding=*/{},
  };
  static_assert(offsetof(NativeArguments, scale) == 2 * sizeof(void*));
  size_t native_arguments_size = sizeof(native_arguments);
  void* extra[] = {
      HIP_LAUNCH_PARAM_BUFFER_POINTER,
      &native_arguments,
      HIP_LAUNCH_PARAM_BUFFER_SIZE,
      &native_arguments_size,
      HIP_LAUNCH_PARAM_END,
  };
  ASSERT_EQ(hipSuccess, module_launch_kernel(function, 1, 1, 1, 1, 1, 1,
                                             /*shared_memory_bytes=*/0,
                                             /*stream=*/nullptr,
                                             /*arguments=*/nullptr, extra));
  ASSERT_EQ(hipSuccess, device_synchronize());
  actual = {};
  ASSERT_EQ(hipSuccess, hip_memcpy(actual.data(), output, sizeof(actual),
                                   hipMemcpyDeviceToHost));
  EXPECT_EQ((std::array<uint32_t, 4>{13, 16, 19, 22}), actual);

  hipGraph_t graph = nullptr;
  ASSERT_EQ(hipSuccess, graph_create(&graph, /*flags=*/0));
  hipGraphNode_t graph_node = nullptr;
  ASSERT_EQ(hipSuccess,
            AddKernelGraphNode(graph_add_kernel_node, graph, function, input,
                               output, &graph_node));
  hipGraphExec_t graph_executable = nullptr;
  ASSERT_EQ(hipSuccess,
            graph_instantiate(&graph_executable, graph,
                              /*error_node=*/nullptr, /*log_buffer=*/nullptr,
                              /*buffer_size=*/0));

  const std::array<uint32_t, 4> graph_expected = {7, 12, 17, 22};
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(hipSuccess,
              hip_memcpy(output, zero_values.data(), sizeof(zero_values),
                         hipMemcpyHostToDevice));
    ASSERT_EQ(hipSuccess, graph_launch(graph_executable, /*stream=*/nullptr));
    ASSERT_EQ(hipSuccess, device_synchronize());
    actual = {};
    ASSERT_EQ(hipSuccess, hip_memcpy(actual.data(), output, sizeof(actual),
                                     hipMemcpyDeviceToHost));
    EXPECT_EQ(graph_expected, actual);
  }

  EXPECT_EQ(hipSuccess, graph_exec_destroy(graph_executable));
  EXPECT_EQ(hipSuccess, graph_destroy(graph));
  EXPECT_EQ(hipSuccess, module_unload(module));
  EXPECT_EQ(hipSuccess, hip_free(output));
  EXPECT_EQ(hipSuccess, hip_free(input));
}

TEST(HipModuleExecutionTest, BlockingPrintfDirectAndGraphReplay) {
  if (hrx_cts_amdgpu_hip_printf_test_kernels_size() == 0) {
    GTEST_SKIP() << "ROCm device libraries are unavailable";
  }

  void* library = dlopen(CandidateLibPath(), RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    GTEST_SKIP() << "cannot dlopen " << CandidateLibPath() << ": " << dlerror();
  }

  const auto init = ResolveHipSymbol<HipInitFn>(library, "hipInit");
  const auto get_device =
      ResolveHipSymbol<HipGetDeviceFn>(library, "hipGetDevice");
  const auto get_device_properties = ResolveHipSymbol<HipGetDevicePropertiesFn>(
      library, "hipGetDeviceProperties");
  const auto module_load_data =
      ResolveHipSymbol<HipModuleLoadDataFn>(library, "hipModuleLoadData");
  const auto module_unload =
      ResolveHipSymbol<HipModuleUnloadFn>(library, "hipModuleUnload");
  const auto module_get_function =
      ResolveHipSymbol<HipModuleGetFunctionFn>(library, "hipModuleGetFunction");
  const auto module_launch_kernel = ResolveHipSymbol<HipModuleLaunchKernelFn>(
      library, "hipModuleLaunchKernel");
  const auto device_synchronize =
      ResolveHipSymbol<HipDeviceSynchronizeFn>(library, "hipDeviceSynchronize");
  const auto hip_malloc = ResolveHipSymbol<HipMallocFn>(library, "hipMalloc");
  const auto hip_free = ResolveHipSymbol<HipFreeFn>(library, "hipFree");
  const auto hip_memcpy = ResolveHipSymbol<HipMemcpyFn>(library, "hipMemcpy");
  const auto graph_create =
      ResolveHipSymbol<HipGraphCreateFn>(library, "hipGraphCreate");
  const auto graph_destroy =
      ResolveHipSymbol<HipGraphDestroyFn>(library, "hipGraphDestroy");
  const auto graph_add_kernel_node = ResolveHipSymbol<HipGraphAddKernelNodeFn>(
      library, "hipGraphAddKernelNode");
  const auto graph_instantiate =
      ResolveHipSymbol<HipGraphInstantiateFn>(library, "hipGraphInstantiate");
  const auto graph_launch =
      ResolveHipSymbol<HipGraphLaunchFn>(library, "hipGraphLaunch");
  const auto graph_exec_destroy =
      ResolveHipSymbol<HipGraphExecDestroyFn>(library, "hipGraphExecDestroy");

  ASSERT_NE(nullptr, init);
  ASSERT_NE(nullptr, get_device);
  ASSERT_NE(nullptr, get_device_properties);
  ASSERT_NE(nullptr, module_load_data);
  ASSERT_NE(nullptr, module_unload);
  ASSERT_NE(nullptr, module_get_function);
  ASSERT_NE(nullptr, module_launch_kernel);
  ASSERT_NE(nullptr, device_synchronize);
  ASSERT_NE(nullptr, hip_malloc);
  ASSERT_NE(nullptr, hip_free);
  ASSERT_NE(nullptr, hip_memcpy);
  ASSERT_NE(nullptr, graph_create);
  ASSERT_NE(nullptr, graph_destroy);
  ASSERT_NE(nullptr, graph_add_kernel_node);
  ASSERT_NE(nullptr, graph_instantiate);
  ASSERT_NE(nullptr, graph_launch);
  ASSERT_NE(nullptr, graph_exec_destroy);

  const hipError_t init_result = init(/*flags=*/0);
  if (init_result != hipSuccess) {
    GTEST_SKIP() << "hipInit failed: " << init_result;
  }
  int device = 0;
  ASSERT_EQ(hipSuccess, get_device(&device));
  hipDeviceProp_t properties = {};
  ASSERT_EQ(hipSuccess, get_device_properties(&properties, device));

  const hrx_cts::AmdgpuHipPrintfTestImage test_image =
      hrx_cts::FindAmdgpuHipPrintfTestImage(properties.gcnArchName);
  ASSERT_NE(nullptr, test_image.file)
      << "no embedded HIP printf HSACO for " << properties.gcnArchName;

  std::vector<uint8_t> image(test_image.file->data,
                             test_image.file->data + test_image.file->size);
  hipModule_t module = nullptr;
  ASSERT_EQ(hipSuccess, module_load_data(&module, image.data()));
  std::fill(image.begin(), image.end(), uint8_t{0xA5});
  std::vector<uint8_t>().swap(image);

  hipFunction_t function = nullptr;
  ASSERT_EQ(hipSuccess,
            module_get_function(&function, module, "hrx_blocking_printf"));

  hipDeviceptr_t output = nullptr;
  ASSERT_EQ(hipSuccess, hip_malloc(&output, 2 * sizeof(int)));
  const std::array<int, 2> initial_result = {-1, -1};
  ASSERT_EQ(hipSuccess,
            hip_memcpy(output, initial_result.data(), sizeof(initial_result),
                       hipMemcpyHostToDevice));

  uint32_t value = 42;
  hipDeviceptr_t result = output;
  void* arguments[] = {&value, &result};
  ::testing::internal::CaptureStdout();
  const hipError_t launch_result =
      module_launch_kernel(function, 1, 1, 1, 1, 1, 1,
                           /*shared_memory_bytes=*/0, /*stream=*/nullptr,
                           arguments, /*extra=*/nullptr);
  const hipError_t synchronize_result =
      launch_result == hipSuccess ? device_synchronize() : launch_result;
  std::fflush(stdout);
  const std::string direct_output = ::testing::internal::GetCapturedStdout();
  ASSERT_EQ(hipSuccess, launch_result);
  ASSERT_EQ(hipSuccess, synchronize_result);
  EXPECT_EQ("hrx cts printf value=42\n", direct_output);

  constexpr int kExpectedPrintfLength = sizeof("hrx cts printf value=42\n") - 1;
  constexpr int kPostPrintfMarker = 0xC0FFEE;
  std::array<int, 2> actual_result = {};
  ASSERT_EQ(hipSuccess,
            hip_memcpy(actual_result.data(), output, sizeof(actual_result),
                       hipMemcpyDeviceToHost));
  EXPECT_EQ(kExpectedPrintfLength, actual_result[0]);
  EXPECT_EQ(kPostPrintfMarker, actual_result[1]);

  hipGraph_t graph = nullptr;
  ASSERT_EQ(hipSuccess, graph_create(&graph, /*flags=*/0));
  hipGraphNode_t graph_node = nullptr;
  ASSERT_EQ(hipSuccess, AddPrintfGraphNode(graph_add_kernel_node, graph,
                                           function, output, &graph_node));
  hipGraphExec_t graph_executable = nullptr;
  ASSERT_EQ(hipSuccess,
            graph_instantiate(&graph_executable, graph,
                              /*error_node=*/nullptr, /*log_buffer=*/nullptr,
                              /*buffer_size=*/0));

  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(hipSuccess,
              hip_memcpy(output, initial_result.data(), sizeof(initial_result),
                         hipMemcpyHostToDevice));
    ::testing::internal::CaptureStdout();
    const hipError_t graph_launch_result =
        graph_launch(graph_executable, /*stream=*/nullptr);
    const hipError_t graph_synchronize_result =
        graph_launch_result == hipSuccess ? device_synchronize()
                                          : graph_launch_result;
    std::fflush(stdout);
    const std::string graph_output = ::testing::internal::GetCapturedStdout();
    ASSERT_EQ(hipSuccess, graph_launch_result);
    ASSERT_EQ(hipSuccess, graph_synchronize_result);
    EXPECT_EQ("hrx cts printf value=42\n", graph_output);

    actual_result = {};
    ASSERT_EQ(hipSuccess,
              hip_memcpy(actual_result.data(), output, sizeof(actual_result),
                         hipMemcpyDeviceToHost));
    EXPECT_EQ(kExpectedPrintfLength, actual_result[0]);
    EXPECT_EQ(kPostPrintfMarker, actual_result[1]);
  }

  EXPECT_EQ(hipSuccess, graph_exec_destroy(graph_executable));
  EXPECT_EQ(hipSuccess, graph_destroy(graph));

  hipFunction_t many_workitems_function = nullptr;
  ASSERT_EQ(hipSuccess,
            module_get_function(&many_workitems_function, module,
                                "hrx_blocking_printf_many_workitems"));
  EXPECT_EQ(hipSuccess, hip_free(output));
  // One workgroup exercises concurrent fragmented calls from multiple
  // resident waves while keeping routine CTS execution bounded.
  constexpr uint32_t kBlockSize = 256;
  constexpr uint32_t kWorkitemCount = kBlockSize;
  static_assert(kWorkitemCount % kBlockSize == 0);
  ASSERT_EQ(hipSuccess, hip_malloc(&output, kWorkitemCount * sizeof(int)));
  uint32_t workgroup_size = kBlockSize;
  uint32_t workitem_count = kWorkitemCount;
  result = output;
  void* many_workitems_arguments[] = {&workgroup_size, &workitem_count,
                                      &result};
  ::testing::internal::CaptureStdout();
  const hipError_t many_workitems_launch_result = module_launch_kernel(
      many_workitems_function, kWorkitemCount / kBlockSize, 1, 1, kBlockSize, 1,
      1, /*shared_memory_bytes=*/0, /*stream=*/nullptr,
      many_workitems_arguments, /*extra=*/nullptr);
  const hipError_t many_workitems_synchronize_result =
      many_workitems_launch_result == hipSuccess ? device_synchronize()
                                                 : many_workitems_launch_result;
  std::fflush(stdout);
  const std::string many_workitems_output =
      ::testing::internal::GetCapturedStdout();
  ASSERT_EQ(hipSuccess, many_workitems_launch_result);
  ASSERT_EQ(hipSuccess, many_workitems_synchronize_result);
  EXPECT_EQ(kWorkitemCount, many_workitems_output.size());
  EXPECT_TRUE(std::all_of(many_workitems_output.begin(),
                          many_workitems_output.end(),
                          [](char value) { return value == '7'; }));

  std::vector<int> many_workitems_results(kWorkitemCount);
  ASSERT_EQ(hipSuccess, hip_memcpy(many_workitems_results.data(), output,
                                   many_workitems_results.size() * sizeof(int),
                                   hipMemcpyDeviceToHost));
  EXPECT_TRUE(std::all_of(many_workitems_results.begin(),
                          many_workitems_results.end(),
                          [](int value) { return value == 1; }));

  EXPECT_EQ(hipSuccess, hip_free(output));
  EXPECT_EQ(hipSuccess, module_unload(module));
}

TEST(HipModuleExecutionTest, CooperativeLaunchPreservesStreamAndGraphOrdering) {
  if (hrx_cts_amdgpu_hip_cooperative_test_kernels_size() == 0) {
    GTEST_SKIP() << "ROCm device libraries are unavailable";
  }

  void* library = dlopen(CandidateLibPath(), RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    GTEST_SKIP() << "cannot dlopen " << CandidateLibPath() << ": " << dlerror();
  }

  const auto init = ResolveHipSymbol<HipInitFn>(library, "hipInit");
  const auto get_device =
      ResolveHipSymbol<HipGetDeviceFn>(library, "hipGetDevice");
  const auto get_device_properties = ResolveHipSymbol<HipGetDevicePropertiesFn>(
      library, "hipGetDeviceProperties");
  const auto module_load_data =
      ResolveHipSymbol<HipModuleLoadDataFn>(library, "hipModuleLoadData");
  const auto module_unload =
      ResolveHipSymbol<HipModuleUnloadFn>(library, "hipModuleUnload");
  const auto module_get_function =
      ResolveHipSymbol<HipModuleGetFunctionFn>(library, "hipModuleGetFunction");
  const auto module_launch_cooperative_kernel =
      ResolveHipSymbol<HipModuleLaunchCooperativeKernelFn>(
          library, "hipModuleLaunchCooperativeKernel");
  const auto hip_malloc = ResolveHipSymbol<HipMallocFn>(library, "hipMalloc");
  const auto hip_free = ResolveHipSymbol<HipFreeFn>(library, "hipFree");
  const auto hip_memcpy_async =
      ResolveHipSymbol<HipMemcpyAsyncFn>(library, "hipMemcpyAsync");
  const auto hip_memset_async =
      ResolveHipSymbol<HipMemsetAsyncFn>(library, "hipMemsetAsync");
  const auto stream_create =
      ResolveHipSymbol<HipStreamCreateFn>(library, "hipStreamCreate");
  const auto stream_destroy =
      ResolveHipSymbol<HipStreamDestroyFn>(library, "hipStreamDestroy");
  const auto stream_synchronize =
      ResolveHipSymbol<HipStreamSynchronizeFn>(library, "hipStreamSynchronize");
  const auto device_get_resource = ResolveHipSymbol<HipDeviceGetDevResourceFn>(
      library, "hipDeviceGetDevResource");
  const auto device_get_execution_context =
      ResolveHipSymbol<HipDeviceGetExecutionCtxFn>(library,
                                                   "hipDeviceGetExecutionCtx");
  const auto generate_resource_descriptor =
      ResolveHipSymbol<HipDevResourceGenerateDescFn>(
          library, "hipDevResourceGenerateDesc");
  const auto green_context_create =
      ResolveHipSymbol<HipGreenCtxCreateFn>(library, "hipGreenCtxCreate");
  const auto execution_context_destroy =
      ResolveHipSymbol<HipExecutionCtxDestroyFn>(library,
                                                 "hipExecutionCtxDestroy");
  const auto execution_context_stream_create =
      ResolveHipSymbol<HipExecutionCtxStreamCreateFn>(
          library, "hipExecutionCtxStreamCreate");
  const auto stream_begin_capture = ResolveHipSymbol<HipStreamBeginCaptureFn>(
      library, "hipStreamBeginCapture");
  const auto stream_end_capture =
      ResolveHipSymbol<HipStreamEndCaptureFn>(library, "hipStreamEndCapture");
  const auto graph_destroy =
      ResolveHipSymbol<HipGraphDestroyFn>(library, "hipGraphDestroy");
  const auto graph_instantiate =
      ResolveHipSymbol<HipGraphInstantiateFn>(library, "hipGraphInstantiate");
  const auto graph_launch =
      ResolveHipSymbol<HipGraphLaunchFn>(library, "hipGraphLaunch");
  const auto graph_exec_destroy =
      ResolveHipSymbol<HipGraphExecDestroyFn>(library, "hipGraphExecDestroy");

  ASSERT_NE(nullptr, init);
  ASSERT_NE(nullptr, get_device);
  ASSERT_NE(nullptr, get_device_properties);
  ASSERT_NE(nullptr, module_load_data);
  ASSERT_NE(nullptr, module_unload);
  ASSERT_NE(nullptr, module_get_function);
  ASSERT_NE(nullptr, module_launch_cooperative_kernel);
  ASSERT_NE(nullptr, hip_malloc);
  ASSERT_NE(nullptr, hip_free);
  ASSERT_NE(nullptr, hip_memcpy_async);
  ASSERT_NE(nullptr, hip_memset_async);
  ASSERT_NE(nullptr, stream_create);
  ASSERT_NE(nullptr, stream_destroy);
  ASSERT_NE(nullptr, stream_synchronize);
  ASSERT_NE(nullptr, device_get_resource);
  ASSERT_NE(nullptr, device_get_execution_context);
  ASSERT_NE(nullptr, generate_resource_descriptor);
  ASSERT_NE(nullptr, green_context_create);
  ASSERT_NE(nullptr, execution_context_destroy);
  ASSERT_NE(nullptr, execution_context_stream_create);
  ASSERT_NE(nullptr, stream_begin_capture);
  ASSERT_NE(nullptr, stream_end_capture);
  ASSERT_NE(nullptr, graph_destroy);
  ASSERT_NE(nullptr, graph_instantiate);
  ASSERT_NE(nullptr, graph_launch);
  ASSERT_NE(nullptr, graph_exec_destroy);

  const hipError_t init_result = init(/*flags=*/0);
  if (init_result != hipSuccess) {
    GTEST_SKIP() << "hipInit failed: " << init_result;
  }
  int device = 0;
  ASSERT_EQ(hipSuccess, get_device(&device));
  hipDeviceProp_t properties = {};
  ASSERT_EQ(hipSuccess, get_device_properties(&properties, device));
  if (!properties.cooperativeLaunch) {
    GTEST_SKIP() << "device does not support cooperative launch";
  }

  const hrx_cts::AmdgpuHipCooperativeTestImage test_image =
      hrx_cts::FindAmdgpuHipCooperativeTestImage(properties.gcnArchName);
  ASSERT_NE(nullptr, test_image.file)
      << "no embedded cooperative HSACO for " << properties.gcnArchName;

  std::vector<uint8_t> image(test_image.file->data,
                             test_image.file->data + test_image.file->size);
  hipModule_t module = nullptr;
  ASSERT_EQ(hipSuccess, module_load_data(&module, image.data()));
  std::fill(image.begin(), image.end(), uint8_t{0xA5});
  std::vector<uint8_t>().swap(image);
  hipFunction_t function = nullptr;
  ASSERT_EQ(hipSuccess, module_get_function(&function, module,
                                            "hrx_cooperative_grid_sync"));

  constexpr uint32_t kWorkgroupCount = 4;
  constexpr uint32_t kWorkgroupSize = 64;
  hipDeviceptr_t scratch = nullptr;
  hipDeviceptr_t output = nullptr;
  ASSERT_EQ(hipSuccess,
            hip_malloc(&scratch, kWorkgroupCount * sizeof(uint32_t)));
  ASSERT_EQ(hipSuccess,
            hip_malloc(&output, kWorkgroupCount * sizeof(uint32_t)));
  hipStream_t direct_stream = nullptr;
  ASSERT_EQ(hipSuccess, stream_create(&direct_stream));
  ScopedStream direct_stream_guard(direct_stream,
                                   StreamDeleter{stream_destroy});
  hipStream_t graph_stream = nullptr;
  ASSERT_EQ(hipSuccess, stream_create(&graph_stream));
  ScopedStream graph_stream_guard(graph_stream, StreamDeleter{stream_destroy});

  hipExecutionCtx_t primary_context = nullptr;
  ASSERT_EQ(hipSuccess, device_get_execution_context(&primary_context, device));
  hipStream_t primary_context_stream = nullptr;
  ASSERT_EQ(hipSuccess, execution_context_stream_create(
                            &primary_context_stream, primary_context,
                            hipStreamDefault, /*priority=*/0));
  ScopedStream primary_context_stream_guard(primary_context_stream,
                                            StreamDeleter{stream_destroy});

  hipDevResource full_resource;
  ASSERT_EQ(hipSuccess,
            device_get_resource(device, &full_resource, hipDevResourceTypeSm));
  hipDevResourceDesc_t full_resource_descriptor = nullptr;
  ASSERT_EQ(hipSuccess,
            generate_resource_descriptor(&full_resource_descriptor,
                                         &full_resource, /*resource_count=*/1));
  hipExecutionCtx_t full_resource_context = nullptr;
  ASSERT_EQ(hipSuccess, green_context_create(&full_resource_context,
                                             full_resource_descriptor, device,
                                             /*flags=*/0));
  ScopedExecutionContext full_resource_context_guard(
      full_resource_context,
      ExecutionContextDeleter{execution_context_destroy});
  hipStream_t full_resource_stream = nullptr;
  ASSERT_EQ(hipSuccess, execution_context_stream_create(
                            &full_resource_stream, full_resource_context,
                            hipStreamDefault, /*priority=*/0));
  ScopedStream full_resource_stream_guard(full_resource_stream,
                                          StreamDeleter{stream_destroy});

  auto launch = [&](hipStream_t stream, uint32_t incarnation) {
    hipDeviceptr_t scratch_argument = scratch;
    hipDeviceptr_t output_argument = output;
    uint32_t workgroup_count = kWorkgroupCount;
    void* arguments[] = {&scratch_argument, &output_argument, &incarnation,
                         &workgroup_count};
    return module_launch_cooperative_kernel(
        function, kWorkgroupCount, 1, 1, kWorkgroupSize, 1, 1,
        /*shared_memory_bytes=*/0, stream, arguments);
  };
  auto expected_sum = [](uint32_t incarnation) {
    return kWorkgroupCount * incarnation +
           (kWorkgroupCount * (kWorkgroupCount - 1)) / 2;
  };

  constexpr uint32_t kDirectIncarnation = 100;
  std::array<uint32_t, kWorkgroupCount> direct_expected = {};
  direct_expected.fill(expected_sum(kDirectIncarnation));
  // These three operations execute on ordinary, cooperative, and ordinary
  // queue realizations. Their shared stream timeline must order both queue
  // transitions without relying on either hardware queue being FIFO.
  ASSERT_EQ(hipSuccess,
            hip_memset_async(output, 0, kWorkgroupCount * sizeof(uint32_t),
                             direct_stream));
  ASSERT_EQ(hipSuccess, launch(direct_stream, kDirectIncarnation));
  std::array<uint32_t, kWorkgroupCount> actual = {};
  ASSERT_EQ(hipSuccess, hip_memcpy_async(actual.data(), output, sizeof(actual),
                                         hipMemcpyDeviceToHost, direct_stream));
  ASSERT_EQ(hipSuccess, stream_synchronize(direct_stream));
  EXPECT_EQ(direct_expected, actual);

  constexpr uint32_t kPrimaryContextIncarnation = 125;
  std::array<uint32_t, kWorkgroupCount> primary_context_expected = {};
  primary_context_expected.fill(expected_sum(kPrimaryContextIncarnation));
  ASSERT_EQ(hipSuccess,
            hip_memset_async(output, 0, kWorkgroupCount * sizeof(uint32_t),
                             primary_context_stream));
  ASSERT_EQ(hipSuccess,
            launch(primary_context_stream, kPrimaryContextIncarnation));
  actual.fill(UINT32_MAX);
  ASSERT_EQ(hipSuccess,
            hip_memcpy_async(actual.data(), output, sizeof(actual),
                             hipMemcpyDeviceToHost, primary_context_stream));
  ASSERT_EQ(hipSuccess, stream_synchronize(primary_context_stream));
  EXPECT_EQ(primary_context_expected, actual);

  constexpr uint32_t kFullResourceIncarnation = 150;
  std::array<uint32_t, kWorkgroupCount> full_resource_expected = {};
  full_resource_expected.fill(expected_sum(kFullResourceIncarnation));
  ASSERT_EQ(hipSuccess,
            hip_memset_async(output, 0, kWorkgroupCount * sizeof(uint32_t),
                             full_resource_stream));
  ASSERT_EQ(hipSuccess, launch(full_resource_stream, kFullResourceIncarnation));
  actual.fill(UINT32_MAX);
  ASSERT_EQ(hipSuccess,
            hip_memcpy_async(actual.data(), output, sizeof(actual),
                             hipMemcpyDeviceToHost, full_resource_stream));
  ASSERT_EQ(hipSuccess, stream_synchronize(full_resource_stream));
  EXPECT_EQ(full_resource_expected, actual);

  constexpr uint32_t kGraphIncarnation = 200;
  std::array<uint32_t, kWorkgroupCount> graph_expected = {};
  graph_expected.fill(expected_sum(kGraphIncarnation));
  // Capture creates a recordable memset partition followed by a direct
  // cooperative dispatch partition. Replaying twice proves each launch gets
  // fresh grid-synchronization state and rejoins the launching stream tail.
  ASSERT_EQ(hipSuccess,
            stream_begin_capture(graph_stream, hipStreamCaptureModeGlobal));
  ASSERT_EQ(hipSuccess,
            hip_memset_async(output, 0, kWorkgroupCount * sizeof(uint32_t),
                             graph_stream));
  ASSERT_EQ(hipSuccess, launch(graph_stream, kGraphIncarnation));
  hipGraph_t graph = nullptr;
  ASSERT_EQ(hipSuccess, stream_end_capture(graph_stream, &graph));
  ASSERT_NE(nullptr, graph);
  hipGraphExec_t graph_executable = nullptr;
  ASSERT_EQ(hipSuccess,
            graph_instantiate(&graph_executable, graph,
                              /*error_node=*/nullptr, /*log_buffer=*/nullptr,
                              /*buffer_size=*/0));

  for (int replay = 0; replay < 2; ++replay) {
    actual.fill(UINT32_MAX);
    ASSERT_EQ(hipSuccess, graph_launch(graph_executable, graph_stream));
    ASSERT_EQ(hipSuccess,
              hip_memcpy_async(actual.data(), output, sizeof(actual),
                               hipMemcpyDeviceToHost, graph_stream));
    ASSERT_EQ(hipSuccess, stream_synchronize(graph_stream));
    EXPECT_EQ(graph_expected, actual);
  }

  EXPECT_EQ(hipSuccess, graph_exec_destroy(graph_executable));
  EXPECT_EQ(hipSuccess, graph_destroy(graph));
  EXPECT_EQ(hipSuccess, hip_free(output));
  EXPECT_EQ(hipSuccess, hip_free(scratch));
  EXPECT_EQ(hipSuccess, module_unload(module));
}

}  // namespace

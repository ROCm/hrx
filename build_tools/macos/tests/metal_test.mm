// Copyright 2026 The IREE Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#import "build_tools/macos/tests/shader_library.h"

#include <cstdint>
#include <vector>

#include "iree/testing/gtest.h"

TEST(MacOSToolchainTest, EmbeddedShaderDispatch) {
  @autoreleasepool {
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    ASSERT_GT(devices.count, 0u) << "The test requires a Metal device";
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    ASSERT_TRUE(device) << "Default-device selection requires CoreGraphics";
    id<MTLCommandQueue> queue = [device newCommandQueue];
    ASSERT_TRUE(queue);

    NSError* error = nil;
    id<MTLLibrary> library = iree_macos_test_create_library(device, &error);
    ASSERT_TRUE(library) << error.localizedDescription.UTF8String;
    id<MTLFunction> function = [library newFunctionWithName:@"transform"];
    ASSERT_TRUE(function);
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function
                                                                                 error:&error];
    ASSERT_TRUE(pipeline) << error.localizedDescription.UTF8String;

    std::vector<uint32_t> input(128);
    for (size_t i = 0; i < input.size(); ++i) input[i] = i;
    const size_t byte_length = input.size() * sizeof(input[0]);
    id<MTLBuffer> input_buffer = [device newBufferWithBytes:input.data()
                                                     length:byte_length
                                                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> output_buffer = [device newBufferWithLength:byte_length
                                                      options:MTLResourceStorageModeShared];
    ASSERT_TRUE(input_buffer);
    ASSERT_TRUE(output_buffer);

    id<MTLCommandBuffer> command = [queue commandBuffer];
    ASSERT_TRUE(command);
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    ASSERT_TRUE(encoder);
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:input_buffer offset:0 atIndex:0];
    [encoder setBuffer:output_buffer offset:0 atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(input.size(), 1, 1)
        threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    ASSERT_EQ(command.status, MTLCommandBufferStatusCompleted)
        << command.error.localizedDescription.UTF8String;

    const auto* output = static_cast<const uint32_t*>(output_buffer.contents);
    for (size_t i = 0; i < input.size(); ++i) {
      EXPECT_EQ(output[i], input[i] * 3 + 7) << "element " << i;
    }
  }
}

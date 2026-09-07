// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "experimental/xdna/executable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "experimental/xdna/fake_provider.h"
#include "iree/hal/drivers/amd/xdna/image/aie2p/strix_halo.h"
#include "iree/hal/drivers/amd/xdna/image/testdata/mul_i32.h"
#include "iree/hal/drivers/amd/xdna/image/testing/aie2p_image_fixture.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

using iree::Status;
using iree::StatusCode;
using iree::hal::amd::xdna::testing::ByteSequencePtr;
using iree::hal::amd::xdna::testing::FakeProgram;
using iree::hal::amd::xdna::testing::FakeProvider;
using iree::hal::amd::xdna::testing::MakeOwnedByteSequence;
using testing::HasSubstr;

struct ExecutableDeleter {
  void operator()(iree_hal_executable_t* executable) const {
    iree_hal_executable_release(executable);
  }
};

using ExecutablePtr = std::unique_ptr<iree_hal_executable_t, ExecutableDeleter>;

static ByteSequencePtr LoadMulI32Image() {
  EXPECT_EQ(iree_hal_amd_xdna_test_mul_i32_size(), 1u);
  if (iree_hal_amd_xdna_test_mul_i32_size() != 1u) return {};
  const iree_file_toc_t* file = iree_hal_amd_xdna_test_mul_i32_create();
  const auto* begin = reinterpret_cast<const uint8_t*>(file->data);
  return MakeOwnedByteSequence(std::vector<uint8_t>(begin, begin + file->size));
}

class XdnaExecutableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_hal_queue_family_initialize(/*ordinal=*/7, &queue_family_);
  }

  void TearDown() override { EXPECT_EQ(provider_.live_program_count, 0u); }

  ExecutablePtr LoadCanonical(
      amdf_xdna_program_flags_t required_program_flags = 0) {
    ByteSequencePtr sequence = LoadMulI32Image();
    iree_hal_amd_xdna_aie2p_target_t target;
    IREE_CHECK_OK(iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
        /*context_column_count=*/1, &target));
    iree_hal_executable_t* executable = nullptr;
    IREE_CHECK_OK(iree_hal_amd_xdna_executable_create(
        provider_.xdna_api(), provider_.device(), &queue_family_,
        sequence.get(), &target, required_program_flags,
        iree_allocator_system(), &executable));
    return ExecutablePtr(executable);
  }

  FakeProvider provider_;
  iree_hal_queue_family_t queue_family_ = {};
};

TEST_F(XdnaExecutableTest, LoadsCanonicalImageIntoNativeProgram) {
  ExecutablePtr executable = LoadCanonical(AMDF_XDNA_PROGRAM_FLAG_RESIDENT |
                                           AMDF_XDNA_PROGRAM_FLAG_PREEMPTIBLE);

  ASSERT_EQ(provider_.program_create_count, 1u);
  ASSERT_EQ(provider_.live_program_count, 1u);
  ASSERT_NE(provider_.last_program, nullptr);
  const FakeProgram& program = *provider_.last_program;
  EXPECT_EQ(program.create_info_type,
            AMDF_STRUCTURE_TYPE_XDNA_PROGRAM_CREATE_INFO);
  EXPECT_EQ(program.create_info_structure_size,
            sizeof(amdf_xdna_program_create_info_t));
  EXPECT_EQ(program.create_info_next, nullptr);
  EXPECT_EQ(program.required_flags, AMDF_XDNA_PROGRAM_FLAG_RESIDENT |
                                        AMDF_XDNA_PROGRAM_FLAG_PREEMPTIBLE);
  EXPECT_EQ(program.footprint.coordinate_mode,
            AMDF_XDNA_COORDINATE_MODE_CONTEXT_RELATIVE);
  EXPECT_EQ(program.footprint.column_origin, 0u);
  EXPECT_EQ(program.footprint.column_count, 1u);
  EXPECT_EQ(program.footprint.row_count, 6u);
  ASSERT_EQ(program.components.size(), 1u);
  EXPECT_EQ(program.components[0].kind,
            AMDF_XDNA_PROGRAM_COMPONENT_ARRAY_CONFIGURATION);
  EXPECT_EQ(program.components[0].reserved, 0u);
  ASSERT_GE(program.components[0].bytes.size(), 16u);
  EXPECT_EQ(program.components[0].bytes[2], 4u);
  EXPECT_EQ(program.components[0].bytes[3], 6u);
  EXPECT_EQ(iree_unaligned_load_le_u32(program.components[0].bytes.data() + 8),
            50u);
  EXPECT_EQ(iree_unaligned_load_le_u32(program.components[0].bytes.data() + 12),
            program.components[0].bytes.size());

  EXPECT_EQ(iree_hal_executable_queue_family(executable.get()), &queue_family_);
  EXPECT_TRUE(iree_hal_amd_xdna_executable_isa(executable.get()));
  EXPECT_EQ(iree_hal_executable_function_count(executable.get()), 1u);

  iree_hal_executable_function_t function =
      iree_hal_executable_function_invalid();
  IREE_ASSERT_OK(iree_hal_executable_lookup_function_by_name(
      executable.get(), IREE_SV("mul_i32"), &function));
  EXPECT_EQ(function.value, 0u);

  iree_hal_executable_function_info_t function_info;
  IREE_ASSERT_OK(iree_hal_executable_function_info(executable.get(), function,
                                                   &function_info));
  EXPECT_TRUE(iree_string_view_equal(function_info.name, IREE_SV("mul_i32")));
  EXPECT_EQ(function_info.binding_count, 3u);
  EXPECT_EQ(function_info.parameter_count, 3u);
  EXPECT_EQ(function_info.maximum_workgroup_invocations, 1u);
  EXPECT_EQ(function_info.workgroup_size[0], 1u);
  EXPECT_EQ(function_info.workgroup_size[1], 1u);
  EXPECT_EQ(function_info.workgroup_size[2], 1u);

  std::array<iree_hal_executable_function_parameter_t, 3> parameters;
  IREE_ASSERT_OK(iree_hal_executable_function_parameters(
      executable.get(), function, parameters.size(), parameters.data()));
  for (iree_host_size_t i = 0; i < parameters.size(); ++i) {
    EXPECT_EQ(parameters[i].type,
              IREE_HAL_EXECUTABLE_FUNCTION_PARAMETER_TYPE_BINDING);
    EXPECT_EQ(parameters[i].offset, i);
  }

  iree_hal_amd_xdna_executable_entry_t entry;
  IREE_ASSERT_OK(iree_hal_amd_xdna_executable_query_entry(executable.get(),
                                                          function, &entry));
  EXPECT_EQ(entry.program,
            reinterpret_cast<amdf_xdna_program_t*>(provider_.last_program));
  EXPECT_EQ(entry.binding_count, 3u);
  ASSERT_GE(entry.control.data_length, 16u);
  EXPECT_EQ(iree_unaligned_load_le_u32(entry.control.data + 8), 11u);
  EXPECT_EQ(iree_unaligned_load_le_u32(entry.control.data + 12),
            entry.control.data_length);

  constexpr uint64_t kExpectedBindingByteLengths[] = {64, 64, 64};
  for (iree_host_size_t i = 0; i < entry.binding_count; ++i) {
    iree_hal_amd_xdna_elf_binding_record_t binding;
    IREE_ASSERT_OK(iree_hal_amd_xdna_executable_query_binding(
        executable.get(), function, i, &binding));
    EXPECT_EQ(binding.binding_ordinal, i);
    EXPECT_EQ(binding.entry_ordinal, 0u);
    EXPECT_EQ(binding.kind, IREE_HAL_AMD_XDNA_ELF_BINDING_KIND_BUFFER);
    EXPECT_EQ(binding.address_space,
              IREE_HAL_AMD_XDNA_ELF_BINDING_ADDRESS_SPACE_GLOBAL);
    EXPECT_EQ(binding.access, i == 2
                                  ? IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_WRITE
                                  : IREE_HAL_AMD_XDNA_ELF_BINDING_ACCESS_READ);
    EXPECT_EQ(binding.usage,
              IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_DEVICE_VISIBLE |
                  IREE_HAL_AMD_XDNA_ELF_BINDING_USAGE_COHERENT);
    EXPECT_EQ(binding.minimum_byte_length, kExpectedBindingByteLengths[i]);
    EXPECT_EQ(binding.minimum_alignment, 4u);
  }

  executable.reset();
  EXPECT_EQ(provider_.program_destroy_count, 1u);
  EXPECT_EQ(provider_.live_program_count, 0u);
}

TEST_F(XdnaExecutableTest, RejectsIncompleteProviderApi) {
  provider_.api.structure_size = offsetof(amdf_xdna_api_t, command_destroy);
  ByteSequencePtr sequence = LoadMulI32Image();
  iree_hal_amd_xdna_aie2p_target_t target;
  IREE_ASSERT_OK(iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
      /*context_column_count=*/1, &target));
  iree_hal_executable_t* executable =
      reinterpret_cast<iree_hal_executable_t*>(uintptr_t{1});
  IREE_EXPECT_STATUS_IS(
      StatusCode::kFailedPrecondition,
      iree_hal_amd_xdna_executable_create(
          provider_.xdna_api(), provider_.device(), &queue_family_,
          sequence.get(), &target, /*required_program_flags=*/0,
          iree_allocator_system(), &executable));
  EXPECT_EQ(executable, nullptr);
  EXPECT_EQ(provider_.program_create_count, 0u);
}

TEST_F(XdnaExecutableTest, PropagatesProviderCreationFailure) {
  provider_.program_create_status =
      amdf_make_api_status(AMDF_STATUS_CODE_RESOURCE_EXHAUSTED);
  ByteSequencePtr sequence = LoadMulI32Image();
  iree_hal_amd_xdna_aie2p_target_t target;
  IREE_ASSERT_OK(iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
      /*context_column_count=*/1, &target));
  iree_hal_executable_t* executable = nullptr;
  Status status(iree_hal_amd_xdna_executable_create(
      provider_.xdna_api(), provider_.device(), &queue_family_, sequence.get(),
      &target, /*required_program_flags=*/0, iree_allocator_system(),
      &executable));
  EXPECT_EQ(status.code(), StatusCode::kResourceExhausted);
  EXPECT_THAT(status.ToString(), HasSubstr("xdna.program_create"));
  EXPECT_EQ(executable, nullptr);
  EXPECT_EQ(provider_.program_create_count, 1u);
  EXPECT_EQ(provider_.program_destroy_count, 0u);
}

TEST_F(XdnaExecutableTest, RejectsProviderSuccessWithoutProgram) {
  provider_.return_null_program = true;
  ByteSequencePtr sequence = LoadMulI32Image();
  iree_hal_amd_xdna_aie2p_target_t target;
  IREE_ASSERT_OK(iree_hal_amd_xdna_aie2p_strix_halo_target_initialize(
      /*context_column_count=*/1, &target));
  iree_hal_executable_t* executable = nullptr;
  IREE_EXPECT_STATUS_IS(
      StatusCode::kFailedPrecondition,
      iree_hal_amd_xdna_executable_create(
          provider_.xdna_api(), provider_.device(), &queue_family_,
          sequence.get(), &target, /*required_program_flags=*/0,
          iree_allocator_system(), &executable));
  EXPECT_EQ(executable, nullptr);
  EXPECT_EQ(provider_.program_create_count, 1u);
  EXPECT_EQ(provider_.program_destroy_count, 0u);
}

}  // namespace

// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/hal/execution_backend.h"

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/ops/op_registry.h"
#include "loom/ops/test/ops.h"
#include "loom/ops/test/registry.h"
#include "loom/target/low_descriptor_registry_core_test.h"
#include "loom/target/provider.h"
#include "loom/target/test/target_records.h"
#include "loom/tooling/compile/options.h"
#include "loom/tooling/execution/hal/runtime.h"
#include "loom/tooling/execution/session.h"

namespace loom {
namespace {

typedef struct SelectionObservation {
  // Monotonic event ordinal assigned by each observed stage.
  iree_host_size_t next_event_ordinal;
  // Number of explicit device-profile selections.
  iree_host_size_t selection_count;
  // Event ordinal at which the device target was selected.
  iree_host_size_t selection_event_ordinal;
  // Event ordinal at which the selected profile was projected for compilation.
  iree_host_size_t projection_event_ordinal;
  // Event ordinal at which the artifact provider received the target.
  iree_host_size_t emission_event_ordinal;
  // Static profile received by the device provider.
  const loom_target_profile_t* selected_profile;
  // Static profile projected by the compiler pipeline.
  const loom_target_profile_t* projected_profile;
  // Static profile received by artifact emission.
  const loom_target_profile_t* emitted_profile;
  // Device-spec row selected for executable loading.
  const iree_hal_executable_target_t* executable_target;
  // Number of function versions visible to artifact emission.
  iree_host_size_t emitted_function_version_count;
  // True when artifact emission retained the selected executable target key.
  bool emission_target_key_matches_selection;
} SelectionObservation;

static SelectionObservation g_observation;

typedef struct FakeTargetProfile {
  // Generic target profile base.
  loom_target_profile_t base;

  // Test target selector projected into compiler facts.
  loom_test_target_kind_t kind;
} FakeTargetProfile;

static iree_status_t ProjectFakeTargetFacts(
    const loom_target_profile_t* profile, iree_arena_allocator_t* arena,
    loom_target_facts_t* out_facts) {
  (void)arena;
  const auto* fake_profile =
      reinterpret_cast<const FakeTargetProfile*>(profile);
  out_facts->selector = fake_profile->kind;
  g_observation.projected_profile = profile;
  g_observation.projection_event_ordinal = ++g_observation.next_event_ordinal;
  return iree_ok_status();
}

static const loom_target_profile_type_t kFakeTargetProfileType = {
    /*.name=*/IREE_SVL("fake"),
    /*.fact_type=*/&loom_test_target_fact_type,
    /*.project_facts=*/ProjectFakeTargetFacts,
};
static const FakeTargetProfile kFakeTargetProfile = {
    /*.base=*/
    {
        /*.type=*/&kFakeTargetProfileType,
        /*.target_bundle=*/
        loom_test_target_bundles.values[LOOM_TEST_TARGET_KIND_LOW_CORE],
    },
    /*.kind=*/LOOM_TEST_TARGET_KIND_LOW_CORE,
};

static iree_status_t SelectFakeTargetProfile(
    iree_string_view_t selector, const loom_target_profile_t** out_profile) {
  *out_profile = nullptr;
  if (!iree_string_view_equal(selector, IREE_SV("forced"))) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "unknown fake target selector");
  }
  *out_profile = &kFakeTargetProfile.base;
  return iree_ok_status();
}

static loom_target_provider_t MakeFakeTargetProvider() {
  loom_target_provider_t provider = {};
  provider.profile_type = &kFakeTargetProfileType;
  provider.select_profile = SelectFakeTargetProfile;
  return provider;
}

static const loom_target_provider_t kFakeTargetProvider =
    MakeFakeTargetProvider();

static iree_status_t SelectFakeDeviceProfileTarget(
    const loom_device_provider_t* provider,
    const loom_run_hal_runtime_t* runtime,
    const loom_target_profile_t* target_profile,
    loom_device_target_t* out_target) {
  (void)provider;
  ++g_observation.selection_count;
  g_observation.selection_event_ordinal = ++g_observation.next_event_ordinal;
  g_observation.selected_profile = target_profile;
  if (target_profile != &kFakeTargetProfile.base) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "unexpected fake target profile");
  }

  const iree_hal_device_spec_t* device_spec =
      iree_hal_device_spec(runtime->device);
  const iree_hal_device_queue_spec_t* queue_spec =
      iree_hal_device_spec_queues(device_spec);
  const iree_hal_queue_family_ordinal_t queue_family_ordinal =
      iree_hal_queue_family_ordinal(
          iree_hal_queue_family(runtime->dispatch_queue));
  const iree_hal_executable_target_selection_t selection = {
      /*.family=*/{},
      /*.target_key=*/{},
      /*.kind_flags=*/0,
      /*.physical_device_affinity=*/
      queue_spec->families[queue_family_ordinal].physical_device_affinity,
  };
  const iree_hal_executable_target_selection_result_t result =
      iree_hal_device_spec_select_executable_target(device_spec, &selection);
  if (result.outcome != IREE_HAL_EXECUTABLE_TARGET_SELECTION_OUTCOME_SELECTED) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "task device has no unique executable target");
  }

  g_observation.executable_target = result.target;
  *out_target = (loom_device_target_t){
      /*.executable_target=*/result.target,
      /*.artifact_target=*/
      {
          /*.target_profile=*/target_profile,
          /*.target_key=*/result.target->target_key,
      },
  };
  return iree_ok_status();
}

static iree_status_t EmitFakeArtifact(const loom_artifact_provider_t* provider,
                                      loom_module_t* module,
                                      const loom_artifact_target_t* target,
                                      const loom_compile_options_t* options,
                                      iree_allocator_t allocator,
                                      bool* out_emitted,
                                      loom_artifact_t* out_artifact) {
  (void)provider;
  (void)module;
  (void)allocator;
  g_observation.emission_event_ordinal = ++g_observation.next_event_ordinal;
  g_observation.emitted_profile = target->target_profile;
  g_observation.emitted_function_version_count =
      options->function_versions != nullptr ? options->function_versions->count
                                            : 0;
  g_observation.emission_target_key_matches_selection =
      g_observation.executable_target != nullptr &&
      iree_string_view_equal(target->target_key,
                             g_observation.executable_target->target_key);
  *out_emitted = false;
  *out_artifact = (loom_artifact_t){};
  return iree_ok_status();
}

static iree_status_t RegisterContext(void* user_data, loom_context_t* context) {
  (void)user_data;
  IREE_RETURN_IF_ERROR(loom_op_registry_register_all_dialects(context));
  return loom_test_dialect_register(context);
}

static iree_status_t InitializeLowDescriptorRegistry(
    void* user_data, loom_target_low_descriptor_registry_t* out_registry) {
  (void)user_data;
  loom_target_core_test_low_descriptor_registry_initialize(out_registry);
  return iree_ok_status();
}

class HalExecutionBackendTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loom_run_session_options_t session_options = {};
    loom_run_session_options_initialize(&session_options);
    session_options.register_context = (loom_run_register_context_callback_t){
        /*.fn=*/RegisterContext,
    };
    session_options.initialize_low_descriptor_registry =
        (loom_run_initialize_low_descriptor_registry_callback_t){
            /*.fn=*/InitializeLowDescriptorRegistry,
        };
    IREE_ASSERT_OK(loom_run_session_initialize(&session_options, &session_));

    target_providers_[0] = &kFakeTargetProvider;
    target_provider_set_ = loom_target_provider_set_make(target_providers_, 1);
    IREE_ASSERT_OK(loom_target_environment_initialize(&target_provider_set_,
                                                      &target_environment_));
  }

  void TearDown() override {
    loom_target_environment_deinitialize(&target_environment_);
    loom_run_session_deinitialize(&session_);
  }

  iree_status_t Parse(iree_string_view_t source,
                      loom_run_module_t* out_module) {
    loom_run_module_parse_options_t options = {};
    loom_run_module_parse_options_initialize(&options);
    options.filename = IREE_SV("execution_backend_test.loom");
    options.source = source;
    return loom_run_module_parse(&session_, &options, out_module);
  }

  // Session owning compiler contexts and transient storage.
  loom_run_session_t session_ = {};
  // Static target providers used to compose |target_environment_|.
  const loom_target_provider_t* target_providers_[1] = {};
  // Provider set backing |target_environment_|.
  loom_target_provider_set_t target_provider_set_ = {};
  // Target environment resolving the explicit fake profile.
  loom_target_environment_t target_environment_ = {};
};

TEST_F(HalExecutionBackendTest, SelectsTargetBeforeSingleSpecialization) {
  static constexpr char kSource[] = R"(
test.target<low_core> @target {abi = hal_kernel}

kernel.def target(@target) @entry() {
  %unit = index.constant 1 : index
  kernel.launch.config workgroups(%unit, %unit, %unit) workgroup_size(%unit, %unit, %unit) : index
} launch() {
  kernel.return
}
)";
  loom_run_module_t run_module = {};
  IREE_ASSERT_OK(Parse(IREE_SV(kSource), &run_module));

  loom_artifact_provider_t artifact_provider = {};
  artifact_provider.name = IREE_SV("fake-hal");
  artifact_provider.public_artifact_format = IREE_SV("FakeExecutableFormat123");
  artifact_provider.flags = LOOM_ARTIFACT_PROVIDER_FLAG_CANONICAL;
  artifact_provider.target_profile_type = &kFakeTargetProfileType;
  artifact_provider.artifact_kind =
      LOOM_TARGET_COMPILE_ARTIFACT_KIND_HAL_EXECUTABLE;
  artifact_provider.emit_artifact = EmitFakeArtifact;

  loom_device_provider_t device_provider = {};
  device_provider.artifact_provider = &artifact_provider;
  device_provider.driver_name = IREE_SV("task");
  device_provider.select_profile_target = SelectFakeDeviceProfileTarget;

  loom_run_hal_execution_backend_t backend = {};
  backend.base.name = IREE_SV("fake-task-hal");
  backend.base.device_driver_name = IREE_SV("task");
  backend.device_provider = &device_provider;

  loom_compile_options_t compile_options = {};
  loom_compile_options_initialize(&compile_options);
  compile_options.source_resolver =
      loom_run_module_source_resolver(&run_module);
  loom_run_one_shot_options_t run_options = {};
  loom_run_one_shot_options_initialize(&run_options);
  run_options.hal_function_name = IREE_SV("entry");
  run_options.hal_emit_only = true;
  loom_run_one_shot_result_t result = {};
  loom_run_one_shot_result_initialize(iree_allocator_system(), &result);
  const loom_run_one_shot_request_t request = {
      /*.session=*/&session_,
      /*.target_environment=*/&target_environment_,
      /*.pipeline=*/IREE_SV("none"),
      /*.target=*/IREE_SV("fake:forced"),
      /*.run_module=*/&run_module,
      /*.compile_options=*/&compile_options,
      /*.options=*/&run_options,
      /*.compile_report_capture=*/nullptr,
      /*.host_allocator=*/iree_allocator_system(),
      /*.result=*/&result,
  };

  g_observation = {};
  IREE_ASSERT_OK(
      loom_run_hal_execution_backend_run_one_shot(&backend.base, &request));

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_EQ(g_observation.selection_count, 1u);
  EXPECT_EQ(g_observation.selection_event_ordinal, 1u);
  EXPECT_EQ(g_observation.projection_event_ordinal, 2u);
  EXPECT_EQ(g_observation.emission_event_ordinal, 3u);
  EXPECT_EQ(g_observation.selected_profile, &kFakeTargetProfile.base);
  EXPECT_EQ(g_observation.projected_profile, &kFakeTargetProfile.base);
  EXPECT_EQ(g_observation.emitted_profile, &kFakeTargetProfile.base);
  EXPECT_EQ(g_observation.emitted_function_version_count, 1u);
  EXPECT_TRUE(g_observation.emission_target_key_matches_selection);

  loom_run_one_shot_result_deinitialize(&result);
  loom_run_module_deinitialize(&run_module);
}

}  // namespace
}  // namespace loom

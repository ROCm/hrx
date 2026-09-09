// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_AMDGPU_UTIL_EPOCH_SIGNAL_TABLE_H_
#define IREE_HAL_DRIVERS_AMDGPU_UTIL_EPOCH_SIGNAL_TABLE_H_

#include <string.h>

#include "iree/async/frontier.h"
#include "iree/base/api.h"
#include "iree/base/internal/atomics.h"
#include "iree/hal/drivers/amdgpu/util/libhsa.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

//===----------------------------------------------------------------------===//
// iree_hal_amdgpu_epoch_signal_table_t
//===----------------------------------------------------------------------===//

// Flat lookup table mapping one logical HAL device's queue indices to their
// hsa_signal_t epoch signals. Ordinary queue signals are immutable after device
// publication and use plain hot-path loads. Reserved private slots use scalable
// release/acquire publication bits as they are materialized.
//
// The epoch signal is the single hsa_signal_t that the CP decrements on each
// AQL packet completion. It is the mechanism by which tier 2 (device-side)
// cross-queue waits work: a queue waiting on a peer emits an AQL barrier-value
// packet referencing the peer's epoch signal with a condition that fires when
// the peer's epoch reaches the required value.
//
// For producer-frontier-exact cross-queue waits, the submission path reads the
// semaphore's last_signal cache to identify the producer axis/epoch directly,
// then does one lookup here to map that producer axis to an hsa_signal_t for a
// single barrier-value packet. For multi-dependency cases, TP collective joins
// can still require N lookups for N undominated peer axes discovered from the
// semaphore frontier.
//
// The table is allocated once at logical-device topology assignment. Each queue
// registers its epoch signal using its flattened logical queue index and
// deregisters during deinit. Lookup verifies the axis's session, machine, and
// topology-assigned logical device index. Axes from another identity fail the
// lookup and use the tier 3 host-deferral path.
// Queue axes use an 8-bit flattened queue index, so four words cover the full
// representable range.
#define IREE_HAL_AMDGPU_EPOCH_SIGNAL_TABLE_REGISTRATION_WORD_COUNT \
  (((iree_host_size_t)UINT8_MAX + 1 + 63) / 64)

typedef struct iree_hal_amdgpu_epoch_signal_table_t {
  // Session epoch from the axis encoding. Used to verify that a lookup axis
  // belongs to the same session as this table. Prevents cross-session aliasing
  // if axes from different sessions happen to share device/queue indices.
  uint8_t session_epoch;
  // Machine index from the axis encoding. Used to verify that a lookup axis
  // belongs to the same machine. Cross-machine waits use tier 3 (host deferral)
  // since there is no shared HSA signal.
  uint8_t machine_index;
  // Topology-assigned HAL logical device index encoded in every queue axis.
  uint8_t device_index;
  // Reserved for alignment and future identity dimensions.
  uint8_t reserved0;
  // Number of flattened logical queues addressable by |signals|.
  uint16_t queue_count;
  // Number of queue slots reserved for each physical device. Flattened queue
  // indices are grouped by physical device using this fixed stride.
  uint16_t queue_count_per_physical_device;
  // Number of leading ordinary queue slots in each physical-device group.
  // Remaining slots are private queues published dynamically.
  uint16_t ordinary_queue_count_per_physical_device;
  // Reserved for future table flags.
  uint16_t reserved1;
  // Dynamic queue bits whose signals were published with release semantics.
  // Private queue lookups acquire the corresponding word before reading
  // |signals|. Ordinary queue lookups skip this array entirely because those
  // signals predate device publication.
  iree_atomic_uint64_t dynamically_registered_queue_mask
      [IREE_HAL_AMDGPU_EPOCH_SIGNAL_TABLE_REGISTRATION_WORD_COUNT];
  // Epoch signals indexed by flattened logical queue index. Unregistered slots
  // have handle == 0.
  hsa_signal_t signals[];
} iree_hal_amdgpu_epoch_signal_table_t;

// Returns the total allocation size in bytes for |queue_count| signals.
static inline iree_host_size_t iree_hal_amdgpu_epoch_signal_table_size(
    uint16_t queue_count) {
  return sizeof(iree_hal_amdgpu_epoch_signal_table_t) +
         (iree_host_size_t)queue_count * sizeof(hsa_signal_t);
}

// Initializes an epoch signal table in caller-provided memory. The caller
// must have allocated at least iree_hal_amdgpu_epoch_signal_table_size()
// bytes. All signal slots are zeroed (unregistered).
static inline void iree_hal_amdgpu_epoch_signal_table_initialize(
    iree_hal_amdgpu_epoch_signal_table_t* table, uint8_t session_epoch,
    uint8_t machine_index, uint8_t device_index, uint16_t queue_count,
    uint16_t queue_count_per_physical_device,
    uint16_t ordinary_queue_count_per_physical_device) {
  IREE_ASSERT(
      queue_count <=
          IREE_HAL_AMDGPU_EPOCH_SIGNAL_TABLE_REGISTRATION_WORD_COUNT * 64,
      "queue_count out of range");
  IREE_ASSERT(queue_count_per_physical_device != 0,
              "queue_count_per_physical_device must be non-zero");
  IREE_ASSERT(queue_count % queue_count_per_physical_device == 0,
              "queue_count must contain whole physical-device groups");
  IREE_ASSERT(ordinary_queue_count_per_physical_device <=
                  queue_count_per_physical_device,
              "ordinary queue count exceeds queues per physical device");
  table->session_epoch = session_epoch;
  table->machine_index = machine_index;
  table->device_index = device_index;
  table->reserved0 = 0;
  table->queue_count = queue_count;
  table->queue_count_per_physical_device = queue_count_per_physical_device;
  table->ordinary_queue_count_per_physical_device =
      ordinary_queue_count_per_physical_device;
  table->reserved1 = 0;
  for (iree_host_size_t i = 0;
       i < IREE_HAL_AMDGPU_EPOCH_SIGNAL_TABLE_REGISTRATION_WORD_COUNT; ++i) {
    iree_atomic_store(&table->dynamically_registered_queue_mask[i], 0,
                      iree_memory_order_relaxed);
  }
  memset(table->signals, 0,
         (iree_host_size_t)queue_count * sizeof(hsa_signal_t));
}

// Returns true if |queue_index| identifies a dynamically published private
// queue instead of an ordinary queue created before device publication.
static inline bool iree_hal_amdgpu_epoch_signal_table_queue_is_dynamic(
    const iree_hal_amdgpu_epoch_signal_table_t* table, uint8_t queue_index) {
  return queue_index % table->queue_count_per_physical_device >=
         table->ordinary_queue_count_per_physical_device;
}

// Registers a queue's epoch signal in the table. Called during queue init
// after the notification ring (which owns the epoch signal) is created.
//
// The slot must not already be registered (programming error if it is).
static inline void iree_hal_amdgpu_epoch_signal_table_register(
    iree_hal_amdgpu_epoch_signal_table_t* table, uint8_t queue_index,
    hsa_signal_t epoch_signal) {
  IREE_ASSERT(queue_index < table->queue_count, "queue_index out of range");
  IREE_ASSERT(table->signals[queue_index].handle == 0,
              "epoch signal slot already registered");
  IREE_ASSERT(epoch_signal.handle != 0, "cannot register null epoch signal");
  table->signals[queue_index] = epoch_signal;
  if (iree_hal_amdgpu_epoch_signal_table_queue_is_dynamic(table, queue_index)) {
    const iree_host_size_t word_index = queue_index / 64;
    const uint64_t queue_bit = UINT64_C(1) << (queue_index % 64);
    iree_atomic_fetch_or(&table->dynamically_registered_queue_mask[word_index],
                         queue_bit, iree_memory_order_release);
  }
}

// Deregisters a queue's epoch signal from the table. Called during queue
// deinit before the notification ring (which owns the epoch signal) is
// destroyed. The slot must currently be registered.
static inline void iree_hal_amdgpu_epoch_signal_table_deregister(
    iree_hal_amdgpu_epoch_signal_table_t* table, uint8_t queue_index) {
  IREE_ASSERT(queue_index < table->queue_count, "queue_index out of range");
  if (iree_hal_amdgpu_epoch_signal_table_queue_is_dynamic(table, queue_index)) {
    const iree_host_size_t word_index = queue_index / 64;
    const uint64_t queue_bit = UINT64_C(1) << (queue_index % 64);
    IREE_ASSERT(((uint64_t)iree_atomic_load(
                     &table->dynamically_registered_queue_mask[word_index],
                     iree_memory_order_relaxed) &
                 queue_bit) != 0,
                "epoch signal slot not registered");
    iree_atomic_fetch_and(&table->dynamically_registered_queue_mask[word_index],
                          ~queue_bit, iree_memory_order_release);
    // Private slots retain their stale value after withdrawal so a reader that
    // acquired the prior publication bit cannot race a plain handle write. The
    // cleared bit excludes all later private lookups.
  } else {
    IREE_ASSERT(table->signals[queue_index].handle != 0,
                "epoch signal slot not registered");
    // Ordinary slots are only withdrawn after the device has excluded lookup
    // readers and can be cleared directly.
    table->signals[queue_index].handle = 0;
  }
}

// Looks up the epoch signal for the queue identified by |axis|. Returns true
// and writes the signal to |out_signal| if the axis matches this table's
// session/machine/device identity, is a QUEUE-domain axis, is within bounds,
// and the slot is registered. Returns false otherwise (caller should fall back
// to tier 3).
//
// This is the hot-path lookup for tier 2 barrier emission. Ordinary queues use
// identity and bounds checks, one per-device classification, and a plain array
// load. Private queues additionally acquire their dynamic publication bit.
static inline bool iree_hal_amdgpu_epoch_signal_table_lookup(
    const iree_hal_amdgpu_epoch_signal_table_t* table, iree_async_axis_t axis,
    hsa_signal_t* out_signal) {
  // Domain-specific fields are only defined for queue axes.
  if (iree_async_axis_domain(axis) != IREE_ASYNC_CAUSAL_DOMAIN_QUEUE) {
    return false;
  }
  // Verify this axis is from our session, machine, and logical HAL device.
  if (iree_async_axis_session(axis) != table->session_epoch ||
      iree_async_axis_machine(axis) != table->machine_index ||
      iree_async_axis_device_index(axis) != table->device_index) {
    return false;
  }
  uint8_t queue_index = iree_async_axis_queue_index(axis);
  if (queue_index >= table->queue_count) return false;
  if (iree_hal_amdgpu_epoch_signal_table_queue_is_dynamic(table, queue_index)) {
    const iree_host_size_t word_index = queue_index / 64;
    const uint64_t queue_bit = UINT64_C(1) << (queue_index % 64);
    if (((uint64_t)iree_atomic_load(
             &table->dynamically_registered_queue_mask[word_index],
             iree_memory_order_acquire) &
         queue_bit) == 0) {
      return false;
    }
  }
  const hsa_signal_t signal = table->signals[queue_index];
  if (signal.handle == 0) return false;  // Slot not registered.
  *out_signal = signal;
  return true;
}

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_AMDGPU_UTIL_EPOCH_SIGNAL_TABLE_H_

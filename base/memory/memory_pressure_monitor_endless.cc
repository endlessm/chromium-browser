// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_pressure_monitor_endless.h"

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/process/process_metrics.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/sys_info.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"

namespace base {

namespace endless {

// The time between memory pressure checks. While under critical pressure, this
// is also the timer to repeat cleanup attempts.
const int kMemoryPressureIntervalMs = 1000;

// The time which should pass between two moderate memory pressure calls.
const int kModerateMemoryPressureCooldownMs = 10000;

// Number of event polls before the next moderate pressure event can be sent.
const int kModerateMemoryPressureCooldown = kModerateMemoryPressureCooldownMs / kMemoryPressureIntervalMs;

// Default threshold (as in % of used memory) for emission of memory pressure events.
const int kDefaultMemoryPressureModerateThreshold = 45;
const int kDefaultMemoryPressureCriticalThreshold = 80;

MemoryPressureMonitor::MemoryPressureMonitor()
    : current_memory_pressure_level_(MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE)
    , moderate_pressure_repeat_count_(0)
    , moderate_threshold_(kDefaultMemoryPressureModerateThreshold)
    , critical_threshold_(kDefaultMemoryPressureCriticalThreshold)
    , dispatch_callback_(
          base::Bind(&MemoryPressureListener::NotifyMemoryPressure))
    , weak_ptr_factory_(this) {

	ParseCommandLineParameters();
	StartObserving();
}

MemoryPressureMonitor::~MemoryPressureMonitor() {
  StopObserving();
}

MemoryPressureListener::MemoryPressureLevel MemoryPressureMonitor::GetCurrentPressureLevel() const {
  return current_memory_pressure_level_;
}

// static
MemoryPressureMonitor* MemoryPressureMonitor::Get() {
  return static_cast<MemoryPressureMonitor*>(
      base::MemoryPressureMonitor::Get());
}

void MemoryPressureMonitor::ParseCommandLineParameters() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string switch_str;
  int value;

  // Parse the switch for the MODERATE threshold (should be a positive integer between 0 - 100).
  if (command_line->HasSwitch(switches::kMemoryPressureModerateThreshold)) {
    switch_str = command_line->GetSwitchValueASCII(switches::kMemoryPressureModerateThreshold);
    if (base::StringToInt(switch_str, &value) && value >= 0 && value < 100)
      moderate_threshold_ = value;
    else
      VLOG(1) << "Invalid switch for memory-pressure-moderate-threshold: " << switch_str;
  }

  // Parse the switch for the CRITICAL threshold (should be a positive integer between MODERATE - 100).
  if (command_line->HasSwitch(switches::kMemoryPressureCriticalThreshold)) {
    switch_str = command_line->GetSwitchValueASCII(switches::kMemoryPressureCriticalThreshold);
    if (base::StringToInt(switch_str, &value) && value > moderate_threshold_ && value < 100)
      critical_threshold_ = value;
    else
      VLOG(1) << "Invalid switch for memory-pressure-critical-threshold: " << switch_str;
  }
}

void MemoryPressureMonitor::StartObserving() {
  timer_.Start(FROM_HERE,
               TimeDelta::FromMilliseconds(kMemoryPressureIntervalMs),
               Bind(&MemoryPressureMonitor::CheckMemoryPressure,
                    weak_ptr_factory_.GetWeakPtr()));
}

void MemoryPressureMonitor::StopObserving() {
  // If StartObserving failed, StopObserving will still get called.
  timer_.Stop();
}

void MemoryPressureMonitor::CheckMemoryPressure() {
  MemoryPressureListener::MemoryPressureLevel old_pressure = current_memory_pressure_level_;

  current_memory_pressure_level_ = GetMemoryPressureLevelFromPercent(GetUsedMemoryInPercent());

  // In case there is no memory pressure we do not notify.
  if (current_memory_pressure_level_ == MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE) {
    return;
  }

  if (old_pressure == current_memory_pressure_level_) {
    // If the memory pressure is still at the same level, we notify again for a
    // critical level. In case of a moderate level repeat however, we only send
    // a notification after a certain time has passed.
    if (current_memory_pressure_level_ == MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE
        && ++moderate_pressure_repeat_count_ < kModerateMemoryPressureCooldown) {
      return;
    }
  } else if (current_memory_pressure_level_ == MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE &&
             old_pressure == MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL) {
    // When we reducing the pressure level from critical to moderate, we
    // restart the timeout and do not send another notification.
    moderate_pressure_repeat_count_ = 0;
    return;
  }
  moderate_pressure_repeat_count_ = 0;
  dispatch_callback_.Run(current_memory_pressure_level_);
}

// Converts free percent of memory into a memory pressure value.
MemoryPressureListener::MemoryPressureLevel MemoryPressureMonitor::GetMemoryPressureLevelFromPercent(int percent) {
  if (percent < moderate_threshold_)
    return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE;

  if (percent < critical_threshold_)
    return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE;

  return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL;
}

// Gets the used memory in percent.
int MemoryPressureMonitor::GetUsedMemoryInPercent() {
  base::SystemMemoryInfoKB info;
  if (!base::GetSystemMemoryInfo(&info)) {
    VLOG(1) << "Cannot determine the free memory of the system.";
    return 0;
  }

  // The available memory consists of "real" and virtual (z)ram memory.
  // Since swappable memory uses a non pre-deterministic compression and
  // the compression creates its own "dynamic" in the system, it gets
  // de-emphasized by the |kSwapWeight| factor.
  const int kSwapWeight = 4;

  // The total memory we have is the "real memory" plus the virtual (z)ram.
  int total_memory = info.total + info.swap_total / kSwapWeight;

  // The kernel internally uses 50MB.
  const int kMinFileMemory = 50 * 1024;

  // Most file memory can be easily reclaimed.
  int file_memory = info.active_file + info.inactive_file;
  // unless it is dirty or it's a minimal portion which is required.
  file_memory -= info.dirty + kMinFileMemory;

  // Available memory is the sum of free, swap and easy reclaimable memory.
  int available_memory =
      info.free + info.swap_free / kSwapWeight + file_memory;

  DCHECK(available_memory < total_memory);
  int percentage = ((total_memory - available_memory) * 100) / total_memory;
  return percentage;
}

void MemoryPressureMonitor::SetDispatchCallback(
    const DispatchCallback& callback) {
  dispatch_callback_ = callback;
}

} // namespace endless

}  // namespace base

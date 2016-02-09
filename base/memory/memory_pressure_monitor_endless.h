// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_MEMORY_PRESSURE_MONITOR_ENDLESS_H_
#define BASE_MEMORY_MEMORY_PRESSURE_MONITOR_ENDLESS_H_

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

namespace base {

namespace endless {

class BASE_EXPORT MemoryPressureMonitor : public base::MemoryPressureMonitor {
 public:
  explicit MemoryPressureMonitor();
  ~MemoryPressureMonitor() override;

  // Get the current memory pressure level.
  MemoryPressureListener::MemoryPressureLevel GetCurrentPressureLevel() const override;

  // Returns a type-casted version of the current memory pressure monitor. A
  // simple wrapper to base::MemoryPressureMonitor::Get.
  static MemoryPressureMonitor* Get();

 private:
  // Starts observing the memory fill level.
  // Calls to StartObserving should always be matched with calls to
  // StopObserving.
  void StartObserving();

  // Stop observing the memory fill level.
  // May be safely called if StartObserving has not been called.
  void StopObserving();

  // The function which gets periodically called to check any changes in the
  // memory pressure. It will report pressure changes as well as continuous
  // critical pressure levels.
  void CheckMemoryPressure();

  // The function periodically checks the memory pressure changes and records
  // the UMA histogram statistics for the current memory pressure level.
  void CheckMemoryPressureAndRecordStatistics();

  // Returns the correct MemoryPressureLevel according to the moderate and critical tresholds.
  MemoryPressureListener::MemoryPressureLevel GetMemoryPressureLevelFromPercent(int percent);

  // Get the memory pressure in percent.
  int GetUsedMemoryInPercent();

  // The current memory pressure.
  base::MemoryPressureListener::MemoryPressureLevel current_memory_pressure_level_;

  // A periodic timer to check for resource pressure changes. This will get
  // replaced by a kernel triggered event system (see crbug.com/381196).
  base::RepeatingTimer timer_;

  // To slow down the amount of moderate pressure event calls, this counter
  // gets used to count the number of events since the last event occured.
  int moderate_pressure_repeat_count_;

  base::WeakPtrFactory<MemoryPressureMonitor> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(MemoryPressureMonitor);
};

} // namespace endless

}  // namespace base

#endif  // BASE_MEMORY_MEMORY_PRESSURE_MONITOR_ENDLESS_H_

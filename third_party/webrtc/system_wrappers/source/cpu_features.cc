/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Parts of this file derived from Chromium's base/cpu.cc.

#include "webrtc/system_wrappers/include/cpu_features_wrapper.h"

#if defined(WEBRTC_ARCH_X86_FAMILY) && defined(_MSC_VER)
#include <intrin.h>
#endif

#include "webrtc/typedefs.h"

#include <elf.h>
#ifdef __arm__
#include <fcntl.h>
#include <unistd.h>
#include <linux/auxvec.h>
#include <asm/hwcap.h>
#endif

#ifdef __arm__
uint64_t WebRtc_GetCPUFeaturesARM() {
	static bool detected = false;
	static uint64_t have_neon = 0;

	if (!detected) {
		int fd;
		Elf32_auxv_t auxv;

		fd = open("/proc/self/auxv", O_RDONLY);
		if (fd >= 0) {
			while (read(fd, &auxv, sizeof(Elf32_auxv_t)) == sizeof(Elf32_auxv_t)) {
				if (auxv.a_type == AT_HWCAP) {
					have_neon = (auxv.a_un.a_val & HWCAP_NEON) ? kCPUFeatureNEON : 0;
					break;
				}
			}
			close (fd);
		} else {
			have_neon = 0;
		}
		detected = true;
	}

	return 0 | have_neon; // others here as we need them
}
#endif

// No CPU feature is available => straight C path.
int GetCPUInfoNoASM(CPUFeature feature) {
  (void)feature;
  return 0;
}

#if defined(WEBRTC_ARCH_X86_FAMILY)
#ifndef _MSC_VER
// Intrinsic for "cpuid".
#if defined(__pic__) && defined(__i386__)
static inline void __cpuid(int cpu_info[4], int info_type) {
  __asm__ volatile(
    "mov %%ebx, %%edi\n"
    "cpuid\n"
    "xchg %%edi, %%ebx\n"
    : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]), "=d"(cpu_info[3])
    : "a"(info_type));
}
#else
static inline void __cpuid(int cpu_info[4], int info_type) {
  __asm__ volatile(
    "cpuid\n"
    : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]), "=d"(cpu_info[3])
    : "a"(info_type));
}
#endif
#endif  // _MSC_VER
#endif  // WEBRTC_ARCH_X86_FAMILY

#if defined(WEBRTC_ARCH_X86_FAMILY)
// Actual feature detection for x86.
static int GetCPUInfo(CPUFeature feature) {
  int cpu_info[4];
  __cpuid(cpu_info, 1);
  if (feature == kSSE2) {
    return 0 != (cpu_info[3] & 0x04000000);
  }
  if (feature == kSSE3) {
    return 0 != (cpu_info[2] & 0x00000001);
  }
  return 0;
}
#else
// Default to straight C for other platforms.
static int GetCPUInfo(CPUFeature feature) {
  (void)feature;
  return 0;
}
#endif

WebRtc_CPUInfo WebRtc_GetCPUInfo = GetCPUInfo;
WebRtc_CPUInfo WebRtc_GetCPUInfoNoASM = GetCPUInfoNoASM;

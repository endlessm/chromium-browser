/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/rtc_base/refcount.h"
#include "webrtc/sdk/android/src/jni/jni_helpers.h"

namespace webrtc_jni {

JNI_FUNCTION_DECLARATION(void,
                         JniCommon_nativeAddRef,
                         JNIEnv* jni,
                         jclass,
                         jlong j_native_ref_counted_pointer) {
  reinterpret_cast<rtc::RefCountInterface*>(j_native_ref_counted_pointer)
      ->AddRef();
}

JNI_FUNCTION_DECLARATION(void,
                         JniCommon_nativeReleaseRef,
                         JNIEnv* jni,
                         jclass,
                         jlong j_native_ref_counted_pointer) {
  reinterpret_cast<rtc::RefCountInterface*>(j_native_ref_counted_pointer)
      ->Release();
}

}  // namespace webrtc_jni

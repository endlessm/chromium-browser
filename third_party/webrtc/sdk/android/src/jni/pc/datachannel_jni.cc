/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/sdk/android/src/jni/jni_helpers.h"
#include "webrtc/sdk/android/src/jni/pc/datachannelobserver_jni.h"

namespace webrtc {
namespace jni {

static DataChannelInterface* ExtractNativeDC(JNIEnv* jni, jobject j_dc) {
  jfieldID native_dc_id =
      GetFieldID(jni, GetObjectClass(jni, j_dc), "nativeDataChannel", "J");
  jlong j_d = GetLongField(jni, j_dc, native_dc_id);
  return reinterpret_cast<DataChannelInterface*>(j_d);
}

JNI_FUNCTION_DECLARATION(jlong,
                         DataChannel_registerObserverNative,
                         JNIEnv* jni,
                         jobject j_dc,
                         jobject j_observer) {
  std::unique_ptr<DataChannelObserverJni> observer(
      new DataChannelObserverJni(jni, j_observer));
  ExtractNativeDC(jni, j_dc)->RegisterObserver(observer.get());
  return jlongFromPointer(observer.release());
}

JNI_FUNCTION_DECLARATION(void,
                         DataChannel_unregisterObserverNative,
                         JNIEnv* jni,
                         jobject j_dc,
                         jlong native_observer) {
  ExtractNativeDC(jni, j_dc)->UnregisterObserver();
  delete reinterpret_cast<DataChannelObserverJni*>(native_observer);
}

JNI_FUNCTION_DECLARATION(jstring,
                         DataChannel_label,
                         JNIEnv* jni,
                         jobject j_dc) {
  return JavaStringFromStdString(jni, ExtractNativeDC(jni, j_dc)->label());
}

JNI_FUNCTION_DECLARATION(jint, DataChannel_id, JNIEnv* jni, jobject j_dc) {
  int id = ExtractNativeDC(jni, j_dc)->id();
  RTC_CHECK_LE(id, std::numeric_limits<int32_t>::max())
      << "id overflowed jint!";
  return static_cast<jint>(id);
}

JNI_FUNCTION_DECLARATION(jobject,
                         DataChannel_state,
                         JNIEnv* jni,
                         jobject j_dc) {
  return JavaEnumFromIndexAndClassName(jni, "DataChannel$State",
                                       ExtractNativeDC(jni, j_dc)->state());
}

JNI_FUNCTION_DECLARATION(jlong,
                         DataChannel_bufferedAmount,
                         JNIEnv* jni,
                         jobject j_dc) {
  uint64_t buffered_amount = ExtractNativeDC(jni, j_dc)->buffered_amount();
  RTC_CHECK_LE(buffered_amount, std::numeric_limits<int64_t>::max())
      << "buffered_amount overflowed jlong!";
  return static_cast<jlong>(buffered_amount);
}

JNI_FUNCTION_DECLARATION(void, DataChannel_close, JNIEnv* jni, jobject j_dc) {
  ExtractNativeDC(jni, j_dc)->Close();
}

JNI_FUNCTION_DECLARATION(jboolean,
                         DataChannel_sendNative,
                         JNIEnv* jni,
                         jobject j_dc,
                         jbyteArray data,
                         jboolean binary) {
  jbyte* bytes = jni->GetByteArrayElements(data, NULL);
  bool ret = ExtractNativeDC(jni, j_dc)->Send(DataBuffer(
      rtc::CopyOnWriteBuffer(bytes, jni->GetArrayLength(data)), binary));
  jni->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
  return ret;
}

JNI_FUNCTION_DECLARATION(void, DataChannel_dispose, JNIEnv* jni, jobject j_dc) {
  CHECK_RELEASE(ExtractNativeDC(jni, j_dc));
}

}  // namespace jni
}  // namespace webrtc

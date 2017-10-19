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
#include <utility>

#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/media/base/mediaengine.h"
#include "webrtc/modules/utility/include/jvm_android.h"
#include "webrtc/rtc_base/event_tracer.h"
#include "webrtc/rtc_base/stringutils.h"
#include "webrtc/rtc_base/thread.h"
#include "webrtc/sdk/android/src/jni/jni_helpers.h"
#include "webrtc/sdk/android/src/jni/pc/androidnetworkmonitor_jni.h"
#include "webrtc/sdk/android/src/jni/pc/audio_jni.h"
#include "webrtc/sdk/android/src/jni/pc/java_native_conversion.h"
#include "webrtc/sdk/android/src/jni/pc/media_jni.h"
#include "webrtc/sdk/android/src/jni/pc/ownedfactoryandthreads.h"
#include "webrtc/sdk/android/src/jni/pc/peerconnectionobserver_jni.h"
#include "webrtc/sdk/android/src/jni/pc/video_jni.h"
#include "webrtc/system_wrappers/include/field_trial.h"
// Adding 'nogncheck' to disable the gn include headers check.
// We don't want to depend on 'system_wrappers:field_trial_default' because
// clients should be able to provide their own implementation.
#include "webrtc/system_wrappers/include/field_trial_default.h"  // nogncheck
#include "webrtc/system_wrappers/include/trace.h"

namespace webrtc {
namespace jni {

// Note: Some of the video-specific PeerConnectionFactory methods are
// implemented in "video_jni.cc". This is done so that if an application
// doesn't need video support, it can just link with "null_video_jni.cc"
// instead of "video_jni.cc", which doesn't bring in the video-specific
// dependencies.

// Field trials initialization string
static char* field_trials_init_string = nullptr;

// Set in PeerConnectionFactory_initializeAndroidGlobals().
static bool factory_static_initialized = false;
static bool video_hw_acceleration_enabled = true;

JNI_FUNCTION_DECLARATION(jlong,
                         PeerConnectionFactory_nativeCreateObserver,
                         JNIEnv* jni,
                         jclass,
                         jobject j_observer) {
  return (jlong) new PeerConnectionObserverJni(jni, j_observer);
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_nativeInitializeAndroidGlobals,
                         JNIEnv* jni,
                         jclass,
                         jobject context,
                         jboolean video_hw_acceleration) {
  video_hw_acceleration_enabled = video_hw_acceleration;
  if (!factory_static_initialized) {
    JVM::Initialize(GetJVM());
    factory_static_initialized = true;
  }
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_initializeFieldTrials,
                         JNIEnv* jni,
                         jclass,
                         jstring j_trials_init_string) {
  field_trials_init_string = NULL;
  if (j_trials_init_string != NULL) {
    const char* init_string =
        jni->GetStringUTFChars(j_trials_init_string, NULL);
    int init_string_length = jni->GetStringUTFLength(j_trials_init_string);
    field_trials_init_string = new char[init_string_length + 1];
    rtc::strcpyn(field_trials_init_string, init_string_length + 1, init_string);
    jni->ReleaseStringUTFChars(j_trials_init_string, init_string);
    LOG(LS_INFO) << "initializeFieldTrials: " << field_trials_init_string;
  }
  field_trial::InitFieldTrialsFromString(field_trials_init_string);
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_initializeInternalTracer,
                         JNIEnv* jni,
                         jclass) {
  rtc::tracing::SetupInternalTracer();
}

JNI_FUNCTION_DECLARATION(jstring,
                         PeerConnectionFactory_nativeFieldTrialsFindFullName,
                         JNIEnv* jni,
                         jclass,
                         jstring j_name) {
  return JavaStringFromStdString(
      jni, field_trial::FindFullName(JavaToStdString(jni, j_name)));
}

JNI_FUNCTION_DECLARATION(jboolean,
                         PeerConnectionFactory_startInternalTracingCapture,
                         JNIEnv* jni,
                         jclass,
                         jstring j_event_tracing_filename) {
  if (!j_event_tracing_filename)
    return false;

  const char* init_string =
      jni->GetStringUTFChars(j_event_tracing_filename, NULL);
  LOG(LS_INFO) << "Starting internal tracing to: " << init_string;
  bool ret = rtc::tracing::StartInternalCapture(init_string);
  jni->ReleaseStringUTFChars(j_event_tracing_filename, init_string);
  return ret;
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_stopInternalTracingCapture,
                         JNIEnv* jni,
                         jclass) {
  rtc::tracing::StopInternalCapture();
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_shutdownInternalTracer,
                         JNIEnv* jni,
                         jclass) {
  rtc::tracing::ShutdownInternalTracer();
}

JNI_FUNCTION_DECLARATION(
    jlong,
    PeerConnectionFactory_nativeCreatePeerConnectionFactory,
    JNIEnv* jni,
    jclass,
    jobject joptions,
    jobject jencoder_factory,
    jobject jdecoder_factory) {
  // talk/ assumes pretty widely that the current Thread is ThreadManager'd, but
  // ThreadManager only WrapCurrentThread()s the thread where it is first
  // created.  Since the semantics around when auto-wrapping happens in
  // webrtc/rtc_base/ are convoluted, we simply wrap here to avoid having to
  // think about ramifications of auto-wrapping there.
  rtc::ThreadManager::Instance()->WrapCurrentThread();
  Trace::CreateTrace();

  std::unique_ptr<rtc::Thread> network_thread =
      rtc::Thread::CreateWithSocketServer();
  network_thread->SetName("network_thread", nullptr);
  RTC_CHECK(network_thread->Start()) << "Failed to start thread";

  std::unique_ptr<rtc::Thread> worker_thread = rtc::Thread::Create();
  worker_thread->SetName("worker_thread", nullptr);
  RTC_CHECK(worker_thread->Start()) << "Failed to start thread";

  std::unique_ptr<rtc::Thread> signaling_thread = rtc::Thread::Create();
  signaling_thread->SetName("signaling_thread", NULL);
  RTC_CHECK(signaling_thread->Start()) << "Failed to start thread";

  cricket::WebRtcVideoEncoderFactory* video_encoder_factory = nullptr;
  cricket::WebRtcVideoDecoderFactory* video_decoder_factory = nullptr;
  rtc::NetworkMonitorFactory* network_monitor_factory = nullptr;
  auto audio_encoder_factory = CreateAudioEncoderFactory();
  auto audio_decoder_factory = CreateAudioDecoderFactory();

  PeerConnectionFactoryInterface::Options options;
  bool has_options = joptions != NULL;
  if (has_options) {
    options = JavaToNativePeerConnectionFactoryOptions(jni, joptions);
  }

  if (video_hw_acceleration_enabled) {
    video_encoder_factory = CreateVideoEncoderFactory(jni, jencoder_factory);
    video_decoder_factory = CreateVideoDecoderFactory(jni, jdecoder_factory);
  }
  // Do not create network_monitor_factory only if the options are
  // provided and disable_network_monitor therein is set to true.
  if (!(has_options && options.disable_network_monitor)) {
    network_monitor_factory = new AndroidNetworkMonitorFactory();
    rtc::NetworkMonitorFactory::SetFactory(network_monitor_factory);
  }

  AudioDeviceModule* adm = nullptr;
  rtc::scoped_refptr<AudioMixer> audio_mixer = nullptr;
  std::unique_ptr<CallFactoryInterface> call_factory(CreateCallFactory());
  std::unique_ptr<RtcEventLogFactoryInterface> rtc_event_log_factory(
      CreateRtcEventLogFactory());
  std::unique_ptr<cricket::MediaEngineInterface> media_engine(CreateMediaEngine(
      adm, audio_encoder_factory, audio_decoder_factory, video_encoder_factory,
      video_decoder_factory, audio_mixer));

  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      CreateModularPeerConnectionFactory(
          network_thread.get(), worker_thread.get(), signaling_thread.get(),
          adm, audio_encoder_factory, audio_decoder_factory,
          video_encoder_factory, video_decoder_factory, audio_mixer,
          std::move(media_engine), std::move(call_factory),
          std::move(rtc_event_log_factory)));
  RTC_CHECK(factory) << "Failed to create the peer connection factory; "
                     << "WebRTC/libjingle init likely failed on this device";
  // TODO(honghaiz): Maybe put the options as the argument of
  // CreatePeerConnectionFactory.
  if (has_options) {
    factory->SetOptions(options);
  }
  OwnedFactoryAndThreads* owned_factory = new OwnedFactoryAndThreads(
      std::move(network_thread), std::move(worker_thread),
      std::move(signaling_thread), video_encoder_factory, video_decoder_factory,
      network_monitor_factory, factory.release());
  owned_factory->InvokeJavaCallbacksOnFactoryThreads();
  return jlongFromPointer(owned_factory);
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_nativeFreeFactory,
                         JNIEnv*,
                         jclass,
                         jlong j_p) {
  delete reinterpret_cast<OwnedFactoryAndThreads*>(j_p);
  if (field_trials_init_string) {
    field_trial::InitFieldTrialsFromString(NULL);
    delete field_trials_init_string;
    field_trials_init_string = NULL;
  }
  Trace::ReturnTrace();
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_nativeThreadsCallbacks,
                         JNIEnv*,
                         jclass,
                         jlong j_p) {
  OwnedFactoryAndThreads* factory =
      reinterpret_cast<OwnedFactoryAndThreads*>(j_p);
  factory->InvokeJavaCallbacksOnFactoryThreads();
}

JNI_FUNCTION_DECLARATION(jlong,
                         PeerConnectionFactory_nativeCreateLocalMediaStream,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory,
                         jstring label) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  rtc::scoped_refptr<MediaStreamInterface> stream(
      factory->CreateLocalMediaStream(JavaToStdString(jni, label)));
  return (jlong)stream.release();
}

JNI_FUNCTION_DECLARATION(jlong,
                         PeerConnectionFactory_nativeCreateAudioSource,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory,
                         jobject j_constraints) {
  std::unique_ptr<MediaConstraintsJni> constraints(
      new MediaConstraintsJni(jni, j_constraints));
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  cricket::AudioOptions options;
  CopyConstraintsIntoAudioOptions(constraints.get(), &options);
  rtc::scoped_refptr<AudioSourceInterface> source(
      factory->CreateAudioSource(options));
  return (jlong)source.release();
}

JNI_FUNCTION_DECLARATION(jlong,
                         PeerConnectionFactory_nativeCreateAudioTrack,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory,
                         jstring id,
                         jlong native_source) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  rtc::scoped_refptr<AudioTrackInterface> track(factory->CreateAudioTrack(
      JavaToStdString(jni, id),
      reinterpret_cast<AudioSourceInterface*>(native_source)));
  return (jlong)track.release();
}

JNI_FUNCTION_DECLARATION(jboolean,
                         PeerConnectionFactory_nativeStartAecDump,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory,
                         jint file,
                         jint filesize_limit_bytes) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  return factory->StartAecDump(file, filesize_limit_bytes);
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_nativeStopAecDump,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  factory->StopAecDump();
}

JNI_FUNCTION_DECLARATION(void,
                         PeerConnectionFactory_nativeSetOptions,
                         JNIEnv* jni,
                         jclass,
                         jlong native_factory,
                         jobject options) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> factory(
      factoryFromJava(native_factory));
  PeerConnectionFactoryInterface::Options options_to_set =
      JavaToNativePeerConnectionFactoryOptions(jni, options);
  factory->SetOptions(options_to_set);

  if (options_to_set.disable_network_monitor) {
    OwnedFactoryAndThreads* owner =
        reinterpret_cast<OwnedFactoryAndThreads*>(native_factory);
    if (owner->network_monitor_factory()) {
      rtc::NetworkMonitorFactory::ReleaseFactory(
          owner->network_monitor_factory());
      owner->clear_network_monitor_factory();
    }
  }
}

JNI_FUNCTION_DECLARATION(jlong,
                         PeerConnectionFactory_nativeCreatePeerConnection,
                         JNIEnv* jni,
                         jclass,
                         jlong factory,
                         jobject j_rtc_config,
                         jobject j_constraints,
                         jlong observer_p) {
  rtc::scoped_refptr<PeerConnectionFactoryInterface> f(
      reinterpret_cast<PeerConnectionFactoryInterface*>(
          factoryFromJava(factory)));

  PeerConnectionInterface::RTCConfiguration rtc_config(
      PeerConnectionInterface::RTCConfigurationType::kAggressive);
  JavaToNativeRTCConfiguration(jni, j_rtc_config, &rtc_config);

  jclass j_rtc_config_class = GetObjectClass(jni, j_rtc_config);
  jfieldID j_key_type_id = GetFieldID(jni, j_rtc_config_class, "keyType",
                                      "Lorg/webrtc/PeerConnection$KeyType;");
  jobject j_key_type = GetObjectField(jni, j_rtc_config, j_key_type_id);

  // Generate non-default certificate.
  rtc::KeyType key_type = JavaToNativeKeyType(jni, j_key_type);
  if (key_type != rtc::KT_DEFAULT) {
    rtc::scoped_refptr<rtc::RTCCertificate> certificate =
        rtc::RTCCertificateGenerator::GenerateCertificate(
            rtc::KeyParams(key_type), rtc::Optional<uint64_t>());
    if (!certificate) {
      LOG(LS_ERROR) << "Failed to generate certificate. KeyType: " << key_type;
      return 0;
    }
    rtc_config.certificates.push_back(certificate);
  }

  PeerConnectionObserverJni* observer =
      reinterpret_cast<PeerConnectionObserverJni*>(observer_p);
  observer->SetConstraints(new MediaConstraintsJni(jni, j_constraints));
  CopyConstraintsIntoRtcConfiguration(observer->constraints(), &rtc_config);
  rtc::scoped_refptr<PeerConnectionInterface> pc(
      f->CreatePeerConnection(rtc_config, nullptr, nullptr, observer));
  return (jlong)pc.release();
}

}  // namespace jni
}  // namespace webrtc

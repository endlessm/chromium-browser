/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_SDK_ANDROID_SRC_JNI_PC_MEDIA_JNI_H_
#define WEBRTC_SDK_ANDROID_SRC_JNI_PC_MEDIA_JNI_H_

#include "webrtc/rtc_base/scoped_ref_ptr.h"

namespace webrtc {
class AudioDeviceModule;
class CallFactoryInterface;
class AudioEncoderFactory;
class AudioDecoderFactory;
class RtcEventLogFactoryInterface;
class AudioMixer;
}  // namespace webrtc

namespace cricket {
class MediaEngineInterface;
class WebRtcVideoEncoderFactory;
class WebRtcVideoDecoderFactory;
}  // namespace cricket

namespace webrtc {
namespace jni {

CallFactoryInterface* CreateCallFactory();
RtcEventLogFactoryInterface* CreateRtcEventLogFactory();

cricket::MediaEngineInterface* CreateMediaEngine(
    AudioDeviceModule* adm,
    const rtc::scoped_refptr<AudioEncoderFactory>& audio_encoder_factory,
    const rtc::scoped_refptr<AudioDecoderFactory>& audio_decoder_factory,
    cricket::WebRtcVideoEncoderFactory* video_encoder_factory,
    cricket::WebRtcVideoDecoderFactory* video_decoder_factory,
    rtc::scoped_refptr<AudioMixer> audio_mixer);

}  // namespace jni
}  // namespace webrtc

#endif  // WEBRTC_SDK_ANDROID_SRC_JNI_PC_MEDIA_JNI_H_

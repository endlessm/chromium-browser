/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/api/audio_codecs/builtin_audio_decoder_factory.h"

#include <memory>
#include <vector>

#include "webrtc/api/audio_codecs/L16/audio_decoder_L16.h"
#include "webrtc/api/audio_codecs/audio_decoder_factory_template.h"
#include "webrtc/api/audio_codecs/g711/audio_decoder_g711.h"
#if WEBRTC_USE_BUILTIN_G722
#include "webrtc/api/audio_codecs/g722/audio_decoder_g722.h"  // nogncheck
#endif
#if WEBRTC_USE_BUILTIN_ILBC
#include "webrtc/api/audio_codecs/ilbc/audio_decoder_ilbc.h"  // nogncheck
#endif
#if WEBRTC_USE_BUILTIN_ISAC_FIX
#include "webrtc/api/audio_codecs/isac/audio_decoder_isac_fix.h"  // nogncheck
#elif WEBRTC_USE_BUILTIN_ISAC_FLOAT
#include "webrtc/api/audio_codecs/isac/audio_decoder_isac_float.h"  // nogncheck
#endif
#if WEBRTC_USE_BUILTIN_OPUS
#include "webrtc/api/audio_codecs/opus/audio_decoder_opus.h"  // nogncheck
#endif

namespace webrtc {

namespace {

// Modify an audio decoder to not advertise support for anything.
template <typename T>
struct NotAdvertised {
  using Config = typename T::Config;
  static rtc::Optional<Config> SdpToConfig(const SdpAudioFormat& audio_format) {
    return T::SdpToConfig(audio_format);
  }
  static void AppendSupportedDecoders(std::vector<AudioCodecSpec>* specs) {
    // Don't advertise support for anything.
  }
  static std::unique_ptr<AudioDecoder> MakeAudioDecoder(const Config& config) {
    return T::MakeAudioDecoder(config);
  }
};

}  // namespace

rtc::scoped_refptr<AudioDecoderFactory> CreateBuiltinAudioDecoderFactory() {
  return CreateAudioDecoderFactory<

#if WEBRTC_USE_BUILTIN_OPUS
      AudioDecoderOpus,
#endif

#if WEBRTC_USE_BUILTIN_ISAC_FIX
      AudioDecoderIsacFix,
#elif WEBRTC_USE_BUILTIN_ISAC_FLOAT
      AudioDecoderIsacFloat,
#endif

#if WEBRTC_USE_BUILTIN_G722
      AudioDecoderG722,
#endif

#if WEBRTC_USE_BUILTIN_ILBC
      AudioDecoderIlbc,
#endif

      AudioDecoderG711, NotAdvertised<AudioDecoderL16>>();
}

}  // namespace webrtc

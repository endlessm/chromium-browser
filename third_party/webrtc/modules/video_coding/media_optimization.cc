/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/video_coding/media_optimization.h"

#include <limits>

#include "webrtc/modules/video_coding/utility/frame_dropper.h"
#include "webrtc/rtc_base/logging.h"
#include "webrtc/system_wrappers/include/clock.h"

namespace webrtc {
namespace media_optimization {
const int kMsPerSec = 1000;
const int kBitsPerByte = 8;

struct MediaOptimization::EncodedFrameSample {
  EncodedFrameSample(size_t size_bytes,
                     uint32_t timestamp,
                     int64_t time_complete_ms)
      : size_bytes(size_bytes),
        timestamp(timestamp),
        time_complete_ms(time_complete_ms) {}

  size_t size_bytes;
  uint32_t timestamp;
  int64_t time_complete_ms;
};

MediaOptimization::MediaOptimization(Clock* clock)
    : clock_(clock),
      max_bit_rate_(0),
      user_frame_rate_(0),
      frame_dropper_(new FrameDropper),
      video_target_bitrate_(0),
      incoming_frame_rate_(0),
      encoded_frame_samples_(),
      avg_sent_framerate_(0) {
  memset(incoming_frame_times_, -1, sizeof(incoming_frame_times_));
}

MediaOptimization::~MediaOptimization(void) {
}

void MediaOptimization::Reset() {
  rtc::CritScope lock(&crit_sect_);
  SetEncodingDataInternal(0, 0, 0);
  memset(incoming_frame_times_, -1, sizeof(incoming_frame_times_));
  incoming_frame_rate_ = 0.0;
  frame_dropper_->Reset();
  frame_dropper_->SetRates(0, 0);
  video_target_bitrate_ = 0;
  user_frame_rate_ = 0;
  encoded_frame_samples_.clear();
}

void MediaOptimization::SetEncodingData(int32_t max_bit_rate,
                                        uint32_t target_bitrate,
                                        uint32_t frame_rate) {
  rtc::CritScope lock(&crit_sect_);
  SetEncodingDataInternal(max_bit_rate, frame_rate, target_bitrate);
}

void MediaOptimization::SetEncodingDataInternal(int32_t max_bit_rate,
                                                uint32_t frame_rate,
                                                uint32_t target_bitrate) {
  // Everything codec specific should be reset here since this means the codec
  // has changed.
  max_bit_rate_ = max_bit_rate;
  video_target_bitrate_ = target_bitrate;
  float target_bitrate_kbps = static_cast<float>(target_bitrate) / 1000.0f;
  frame_dropper_->Reset();
  frame_dropper_->SetRates(target_bitrate_kbps, static_cast<float>(frame_rate));
  user_frame_rate_ = static_cast<float>(frame_rate);
}

uint32_t MediaOptimization::SetTargetRates(uint32_t target_bitrate) {
  rtc::CritScope lock(&crit_sect_);

  video_target_bitrate_ = target_bitrate;

  // Cap target video bitrate to codec maximum.
  if (max_bit_rate_ > 0 && video_target_bitrate_ > max_bit_rate_) {
    video_target_bitrate_ = max_bit_rate_;
  }

  // Update encoding rates following protection settings.
  float target_video_bitrate_kbps =
      static_cast<float>(video_target_bitrate_) / 1000.0f;
  float framerate = incoming_frame_rate_;
  if (framerate == 0.0) {
    // No framerate estimate available, use configured max framerate instead.
    framerate = user_frame_rate_;
  }

  frame_dropper_->SetRates(target_video_bitrate_kbps, framerate);

  return video_target_bitrate_;
}

uint32_t MediaOptimization::InputFrameRate() {
  rtc::CritScope lock(&crit_sect_);
  return InputFrameRateInternal();
}

uint32_t MediaOptimization::InputFrameRateInternal() {
  ProcessIncomingFrameRate(clock_->TimeInMilliseconds());
  uint32_t framerate = static_cast<uint32_t>(std::min<float>(
      std::numeric_limits<uint32_t>::max(), incoming_frame_rate_ + 0.5f));
  return framerate;
}

uint32_t MediaOptimization::SentFrameRate() {
  rtc::CritScope lock(&crit_sect_);
  return SentFrameRateInternal();
}

uint32_t MediaOptimization::SentFrameRateInternal() {
  PurgeOldFrameSamples(clock_->TimeInMilliseconds() - kBitrateAverageWinMs);
  UpdateSentFramerate();
  return avg_sent_framerate_;
}

uint32_t MediaOptimization::SentBitRate() {
  rtc::CritScope lock(&crit_sect_);
  PurgeOldFrameSamples(clock_->TimeInMilliseconds() - kBitrateAverageWinMs);
  size_t sent_bytes = 0;
  for (auto& frame_sample : encoded_frame_samples_) {
    sent_bytes += frame_sample.size_bytes;
  }
  return sent_bytes * kBitsPerByte * kMsPerSec / kBitrateAverageWinMs;
}

int32_t MediaOptimization::UpdateWithEncodedData(
    const EncodedImage& encoded_image) {
  size_t encoded_length = encoded_image._length;
  uint32_t timestamp = encoded_image._timeStamp;
  rtc::CritScope lock(&crit_sect_);
  const int64_t now_ms = clock_->TimeInMilliseconds();
  PurgeOldFrameSamples(now_ms - kBitrateAverageWinMs);
  if (encoded_frame_samples_.size() > 0 &&
      encoded_frame_samples_.back().timestamp == timestamp) {
    // Frames having the same timestamp are generated from the same input
    // frame. We don't want to double count them, but only increment the
    // size_bytes.
    encoded_frame_samples_.back().size_bytes += encoded_length;
    encoded_frame_samples_.back().time_complete_ms = now_ms;
  } else {
    encoded_frame_samples_.push_back(
        EncodedFrameSample(encoded_length, timestamp, now_ms));
  }
  UpdateSentFramerate();
  if (encoded_length > 0) {
    const bool delta_frame = encoded_image._frameType != kVideoFrameKey;
    frame_dropper_->Fill(encoded_length, delta_frame);
  }

  return VCM_OK;
}

void MediaOptimization::EnableFrameDropper(bool enable) {
  rtc::CritScope lock(&crit_sect_);
  frame_dropper_->Enable(enable);
}

bool MediaOptimization::DropFrame() {
  rtc::CritScope lock(&crit_sect_);
  UpdateIncomingFrameRate();
  // Leak appropriate number of bytes.
  frame_dropper_->Leak((uint32_t)(InputFrameRateInternal() + 0.5f));
  return frame_dropper_->DropFrame();
}

void MediaOptimization::UpdateIncomingFrameRate() {
  int64_t now = clock_->TimeInMilliseconds();
  if (incoming_frame_times_[0] == 0) {
    // No shifting if this is the first time.
  } else {
    // Shift all times one step.
    for (int32_t i = (kFrameCountHistorySize - 2); i >= 0; i--) {
      incoming_frame_times_[i + 1] = incoming_frame_times_[i];
    }
  }
  incoming_frame_times_[0] = now;
  ProcessIncomingFrameRate(now);
}

void MediaOptimization::PurgeOldFrameSamples(int64_t threshold_ms) {
  while (!encoded_frame_samples_.empty()) {
    if (encoded_frame_samples_.front().time_complete_ms < threshold_ms) {
      encoded_frame_samples_.pop_front();
    } else {
      break;
    }
  }
}

void MediaOptimization::UpdateSentFramerate() {
  if (encoded_frame_samples_.size() <= 1) {
    avg_sent_framerate_ = encoded_frame_samples_.size();
    return;
  }
  int denom = encoded_frame_samples_.back().timestamp -
              encoded_frame_samples_.front().timestamp;
  if (denom > 0) {
    avg_sent_framerate_ =
        (90000 * (encoded_frame_samples_.size() - 1) + denom / 2) / denom;
  } else {
    avg_sent_framerate_ = encoded_frame_samples_.size();
  }
}

// Allowing VCM to keep track of incoming frame rate.
void MediaOptimization::ProcessIncomingFrameRate(int64_t now) {
  int32_t num = 0;
  int32_t nr_of_frames = 0;
  for (num = 1; num < (kFrameCountHistorySize - 1); ++num) {
    if (incoming_frame_times_[num] <= 0 ||
        // Don't use data older than 2 s.
        now - incoming_frame_times_[num] > kFrameHistoryWinMs) {
      break;
    } else {
      nr_of_frames++;
    }
  }
  if (num > 1) {
    const int64_t diff =
        incoming_frame_times_[0] - incoming_frame_times_[num - 1];
    incoming_frame_rate_ = 0.0;  // No frame rate estimate available.
    if (diff > 0) {
      incoming_frame_rate_ = nr_of_frames * 1000.0f / static_cast<float>(diff);
    }
  }
}
}  // namespace media_optimization
}  // namespace webrtc

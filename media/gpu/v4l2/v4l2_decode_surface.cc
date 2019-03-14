// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/v4l2/v4l2_decode_surface.h"

#include <linux/media.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/stringprintf.h"
#include "media/gpu/macros.h"

namespace media {

V4L2DecodeSurface::V4L2DecodeSurface(V4L2WritableBufferRef input_buffer,
                                     V4L2WritableBufferRef output_buffer,
                                     scoped_refptr<VideoFrame> frame)
    : input_record_(input_buffer.BufferId()),
      output_record_(output_buffer.BufferId()),
      decoded_(false) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  input_buffer_ = std::move(input_buffer);
  output_buffer_ = std::move(output_buffer);
  video_frame_ = std::move(frame);
}

V4L2DecodeSurface::~V4L2DecodeSurface() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DVLOGF(5) << "Releasing output record id=" << output_record_;
  if (release_cb_)
    std::move(release_cb_).Run();
}

void V4L2DecodeSurface::SetDecoded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DCHECK(!decoded_);
  decoded_ = true;

  // We can now drop references to all reference surfaces for this surface
  // as we are done with decoding.
  reference_surfaces_.clear();

  // And finally execute and drop the decode done callback, if set.
  if (done_cb_)
    std::move(done_cb_).Run();
}

void V4L2DecodeSurface::SetVisibleRect(const gfx::Rect& visible_rect) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  visible_rect_ = visible_rect;
}

void V4L2DecodeSurface::SetReferenceSurfaces(
    std::vector<scoped_refptr<V4L2DecodeSurface>> ref_surfaces) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DCHECK(reference_surfaces_.empty());
#if DCHECK_IS_ON()
  for (const auto& ref : reference_surfaces_)
    DCHECK_NE(ref->output_record(), output_record_);
#endif

  reference_surfaces_ = std::move(ref_surfaces);
}

void V4L2DecodeSurface::SetDecodeDoneCallback(base::OnceClosure done_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!done_cb_);

  done_cb_ = std::move(done_cb);
}

void V4L2DecodeSurface::SetReleaseCallback(base::OnceClosure release_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  release_cb_ = std::move(release_cb);
}

std::string V4L2DecodeSurface::ToString() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string out;
  base::StringAppendF(&out, "Buffer %d -> %d. ", input_record_, output_record_);
  base::StringAppendF(&out, "Reference surfaces:");
  for (const auto& ref : reference_surfaces_) {
    DCHECK_NE(ref->output_record(), output_record_);
    base::StringAppendF(&out, " %d", ref->output_record());
  }
  return out;
}

}  // namespace media

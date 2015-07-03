// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media.h"

#include <string>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/strings/stringize_macros.h"
#include "chrome/common/chrome_version_info_values.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "third_party/ffmpeg/ffmpeg_stubs.h"

using third_party_ffmpeg::kNumStubModules;
using third_party_ffmpeg::kModuleFfmpegsumo;
using third_party_ffmpeg::InitializeStubs;
using third_party_ffmpeg::StubPathMap;

namespace media {
namespace internal {

// Handy to prevent shooting ourselves in the foot with macro wizardry.
#if !defined(LIBAVCODEC_VERSION_MAJOR) || \
    !defined(LIBAVFORMAT_VERSION_MAJOR) || \
    !defined(LIBAVUTIL_VERSION_MAJOR)
#error FFmpeg headers not included!
#endif

#define AVCODEC_VERSION STRINGIZE(LIBAVCODEC_VERSION_MAJOR)
#define AVFORMAT_VERSION STRINGIZE(LIBAVFORMAT_VERSION_MAJOR)
#define AVUTIL_VERSION STRINGIZE(LIBAVUTIL_VERSION_MAJOR)

#if defined(OS_MACOSX)
// TODO(evan): should be using .so like ffmepgsumo here.
#define DSO_NAME(MODULE, VERSION) ("lib" MODULE "." VERSION ".dylib")
static const base::FilePath::CharType kSumoLib[] =
    FILE_PATH_LITERAL("ffmpegsumo.so");
#elif defined(OS_POSIX)
#define DSO_NAME(MODULE, VERSION) ("lib" MODULE ".so." VERSION)
static const base::FilePath::CharType kSumoLib[] =
    FILE_PATH_LITERAL("libffmpegsumo.so");
#else
#error "Do not know how to construct DSO name for this OS."
#endif

bool InitializeMediaLibraryInternal(const base::FilePath& module_dir) {
  StubPathMap paths;

  // First try to initialize with Chrome's sumo library.
  DCHECK_EQ(kNumStubModules, 1);

#if defined(OS_POSIX)
  // For Endless OS, we check the location for extra codecs first of all,
  // which will follow a naming scheme including Chrome's current version,
  // in order to facilitate upgrades of the browsers and these extra codecs.
  base::FilePath extra_codecs_dir(FILE_PATH_LITERAL("/var/lib/codecs/chromium-browser"));
  base::FilePath extra_codecs_path = extra_codecs_dir.Append(
      FILE_PATH_LITERAL(DSO_NAME("ffmpegsumo", PRODUCT_VERSION)));

  // The expected filename will be something like 'libffmpegsumo.so.40.0.2214.111'.
  if (base::PathExists(extra_codecs_path))
      paths[kModuleFfmpegsumo].push_back(extra_codecs_path.value());
  else
      paths[kModuleFfmpegsumo].push_back(module_dir.Append(kSumoLib).value());
#else
  paths[kModuleFfmpegsumo].push_back(module_dir.Append(kSumoLib).value());
#endif

  // If that fails, see if any system libraries are available.
  paths[kModuleFfmpegsumo].push_back(module_dir.Append(
      FILE_PATH_LITERAL(DSO_NAME("avutil", AVUTIL_VERSION))).value());
  paths[kModuleFfmpegsumo].push_back(module_dir.Append(
      FILE_PATH_LITERAL(DSO_NAME("avcodec", AVCODEC_VERSION))).value());
  paths[kModuleFfmpegsumo].push_back(module_dir.Append(
      FILE_PATH_LITERAL(DSO_NAME("avformat", AVFORMAT_VERSION))).value());

  return InitializeStubs(paths);
}

}  // namespace internal
}  // namespace media

// Copyright 2016 The PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TESTING_LIBFUZZER_XFA_CODEC_FUZZER_H_
#define TESTING_LIBFUZZER_XFA_CODEC_FUZZER_H_

#include <memory>

#include "core/fxcodec/codec/ccodec_bmpmodule.h"
#include "core/fxcodec/codec/ccodec_gifmodule.h"
#include "core/fxcodec/codec/ccodec_pngmodule.h"
#include "core/fxcodec/codec/ccodec_progressivedecoder.h"
#include "core/fxcodec/codec/ccodec_tiffmodule.h"
#include "core/fxcodec/fx_codec.h"
#include "core/fxcrt/fx_stream.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "third_party/base/ptr_util.h"

class XFACodecFuzzer {
 public:
  static int Fuzz(const uint8_t* data, size_t size, FXCODEC_IMAGE_TYPE type) {
    auto mgr = pdfium::MakeUnique<CCodec_ModuleMgr>();
    mgr->SetBmpModule(pdfium::MakeUnique<CCodec_BmpModule>());
    mgr->SetGifModule(pdfium::MakeUnique<CCodec_GifModule>());
    mgr->SetPngModule(pdfium::MakeUnique<CCodec_PngModule>());
    mgr->SetTiffModule(pdfium::MakeUnique<CCodec_TiffModule>());

    std::unique_ptr<CCodec_ProgressiveDecoder> decoder =
        mgr->CreateProgressiveDecoder();
    auto source = pdfium::MakeRetain<Reader>(data, size);
    FXCODEC_STATUS status = decoder->LoadImageInfo(source, type, nullptr, true);
    if (status != FXCODEC_STATUS_FRAME_READY)
      return 0;

    auto bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
    bitmap->Create(decoder->GetWidth(), decoder->GetHeight(), FXDIB_Argb);

    int32_t frames;
    if (decoder->GetFrames(frames) != FXCODEC_STATUS_DECODE_READY ||
        frames == 0) {
      return 0;
    }

    status = decoder->StartDecode(bitmap, 0, 0, bitmap->GetWidth(),
                                  bitmap->GetHeight());
    while (status == FXCODEC_STATUS_DECODE_TOBECONTINUE)
      status = decoder->ContinueDecode();

    return 0;
  }

 private:
  class Reader : public IFX_SeekableReadStream {
   public:
    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    ~Reader() {}

    bool ReadBlock(void* buffer, FX_FILESIZE offset, size_t size) override {
      if (offset < 0 || static_cast<size_t>(offset) >= m_size)
        return false;
      if (offset + size > m_size)
        size = m_size - offset;
      if (size == 0)
        return false;

      memcpy(buffer, m_data + offset, size);
      return true;
    }

    FX_FILESIZE GetSize() override { return static_cast<FX_FILESIZE>(m_size); }

   private:
    const uint8_t* const m_data;
    const size_t m_size;
  };
};

#endif  // TESTING_LIBFUZZER_XFA_CODEC_FUZZER_H_

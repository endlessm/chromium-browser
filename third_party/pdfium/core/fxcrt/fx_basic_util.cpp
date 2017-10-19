// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>

#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_stream.h"
#include "third_party/base/ptr_util.h"

bool FX_atonum(const CFX_ByteStringC& strc, void* pData) {
  if (strc.Contains('.')) {
    float* pFloat = static_cast<float*>(pData);
    *pFloat = FX_atof(strc);
    return false;
  }

  // Note, numbers in PDF are typically of the form 123, -123, etc. But,
  // for things like the Permissions on the encryption hash the number is
  // actually an unsigned value. We use a uint32_t so we can deal with the
  // unsigned and then check for overflow if the user actually signed the value.
  // The Permissions flag is listed in Table 3.20 PDF 1.7 spec.
  pdfium::base::CheckedNumeric<uint32_t> integer = 0;
  bool bNegative = false;
  bool bSigned = false;
  FX_STRSIZE cc = 0;
  if (strc[0] == '+') {
    cc++;
    bSigned = true;
  } else if (strc[0] == '-') {
    bNegative = true;
    bSigned = true;
    cc++;
  }

  while (cc < strc.GetLength() && std::isdigit(strc[cc])) {
    integer = integer * 10 + FXSYS_DecimalCharToInt(strc.CharAt(cc));
    if (!integer.IsValid())
      break;
    cc++;
  }

  // We have a sign, and the value was greater then a regular integer
  // we've overflowed, reset to the default value.
  if (bSigned) {
    if (bNegative) {
      if (integer.ValueOrDefault(0) >
          static_cast<uint32_t>(std::numeric_limits<int>::max()) + 1) {
        integer = 0;
      }
    } else if (integer.ValueOrDefault(0) >
               static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      integer = 0;
    }
  }

  // Switch back to the int space so we can flip to a negative if we need.
  uint32_t uValue = integer.ValueOrDefault(0);
  int32_t value = static_cast<int>(uValue);
  if (bNegative)
    value = -value;

  int* pInt = static_cast<int*>(pData);
  *pInt = value;
  return true;
}

static const float fraction_scales[] = {
    0.1f,         0.01f,         0.001f,        0.0001f,
    0.00001f,     0.000001f,     0.0000001f,    0.00000001f,
    0.000000001f, 0.0000000001f, 0.00000000001f};

int FXSYS_FractionalScaleCount() {
  return FX_ArraySize(fraction_scales);
}

float FXSYS_FractionalScale(size_t scale_factor, int value) {
  return fraction_scales[scale_factor] * value;
}

float FX_atof(const CFX_ByteStringC& strc) {
  if (strc.IsEmpty())
    return 0.0;

  int cc = 0;
  bool bNegative = false;
  int len = strc.GetLength();
  if (strc[0] == '+') {
    cc++;
  } else if (strc[0] == '-') {
    bNegative = true;
    cc++;
  }
  while (cc < len) {
    if (strc[cc] != '+' && strc[cc] != '-')
      break;
    cc++;
  }
  float value = 0;
  while (cc < len) {
    if (strc[cc] == '.')
      break;
    value = value * 10 + FXSYS_DecimalCharToInt(strc.CharAt(cc));
    cc++;
  }
  int scale = 0;
  if (cc < len && strc[cc] == '.') {
    cc++;
    while (cc < len) {
      value +=
          FXSYS_FractionalScale(scale, FXSYS_DecimalCharToInt(strc.CharAt(cc)));
      scale++;
      if (scale == FXSYS_FractionalScaleCount())
        break;
      cc++;
    }
  }
  return bNegative ? -value : value;
}

float FX_atof(const CFX_WideStringC& wsStr) {
  return FX_atof(FX_UTF8Encode(wsStr).c_str());
}

FX_FileHandle* FX_OpenFolder(const char* path) {
#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
  auto pData = pdfium::MakeUnique<CFindFileDataA>();
  pData->m_Handle = FindFirstFileExA((CFX_ByteString(path) + "/*.*").c_str(),
                                     FindExInfoStandard, &pData->m_FindData,
                                     FindExSearchNameMatch, nullptr, 0);
  if (pData->m_Handle == INVALID_HANDLE_VALUE)
    return nullptr;

  pData->m_bEnd = false;
  return pData.release();
#else
  return opendir(path);
#endif
}

bool FX_GetNextFile(FX_FileHandle* handle,
                    CFX_ByteString* filename,
                    bool* bFolder) {
  if (!handle)
    return false;

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
  if (handle->m_bEnd)
    return false;

  *filename = handle->m_FindData.cFileName;
  *bFolder =
      (handle->m_FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if (!FindNextFileA(handle->m_Handle, &handle->m_FindData))
    handle->m_bEnd = true;
  return true;
#else
  struct dirent* de = readdir(handle);
  if (!de)
    return false;
  *filename = de->d_name;
  *bFolder = de->d_type == DT_DIR;
  return true;
#endif
}

void FX_CloseFolder(FX_FileHandle* handle) {
  if (!handle)
    return;

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
  FindClose(handle->m_Handle);
  delete handle;
#else
  closedir(handle);
#endif
}

wchar_t FX_GetFolderSeparator() {
#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
  return '\\';
#else
  return '/';
#endif
}

uint32_t GetBits32(const uint8_t* pData, int bitpos, int nbits) {
  ASSERT(0 < nbits && nbits <= 32);
  const uint8_t* dataPtr = &pData[bitpos / 8];
  int bitShift;
  int bitMask;
  int dstShift;
  int bitCount = bitpos & 0x07;
  if (nbits < 8 && nbits + bitCount <= 8) {
    bitShift = 8 - nbits - bitCount;
    bitMask = (1 << nbits) - 1;
    dstShift = 0;
  } else {
    bitShift = 0;
    int bitOffset = 8 - bitCount;
    bitMask = (1 << std::min(bitOffset, nbits)) - 1;
    dstShift = nbits - bitOffset;
  }
  uint32_t result =
      static_cast<uint32_t>((*dataPtr++ >> bitShift & bitMask) << dstShift);
  while (dstShift >= 8) {
    dstShift -= 8;
    result |= *dataPtr++ << dstShift;
  }
  if (dstShift > 0) {
    bitShift = 8 - dstShift;
    bitMask = (1 << dstShift) - 1;
    result |= *dataPtr++ >> bitShift & bitMask;
  }
  return result;
}

// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com
// Original code is licensed as follows:
/*
 * Copyright 2010 ZXing authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fxbarcode/oned/BC_OnedCode39Writer.h"

#include <memory>

#include "fxbarcode/BC_Writer.h"
#include "fxbarcode/common/BC_CommonBitMatrix.h"
#include "fxbarcode/oned/BC_OneDimWriter.h"

namespace {

const char ALPHABET_STRING[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. *$/+%";

const char CHECKSUM_STRING[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";

const int32_t CHARACTER_ENCODINGS[44] = {
    0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124,
    0x064, 0x109, 0x049, 0x148, 0x019, 0x118, 0x058, 0x00D, 0x10C,
    0x04C, 0x01C, 0x103, 0x043, 0x142, 0x013, 0x112, 0x052, 0x007,
    0x106, 0x046, 0x016, 0x181, 0x0C1, 0x1C0, 0x091, 0x190, 0x0D0,
    0x085, 0x184, 0x0C4, 0x094, 0x0A8, 0x0A2, 0x08A, 0x02A};

}  // namespace

CBC_OnedCode39Writer::CBC_OnedCode39Writer() : m_iWideNarrRatio(3) {}

CBC_OnedCode39Writer::~CBC_OnedCode39Writer() {}

bool CBC_OnedCode39Writer::CheckContentValidity(
    const CFX_WideStringC& contents) {
  for (FX_STRSIZE i = 0; i < contents.GetLength(); i++) {
    wchar_t ch = contents[i];
    if ((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
        ch == L'-' || ch == L'.' || ch == L' ' || ch == L'*' || ch == L'$' ||
        ch == L'/' || ch == L'+' || ch == L'%') {
      continue;
    }
    return false;
  }
  return true;
}

CFX_WideString CBC_OnedCode39Writer::FilterContents(
    const CFX_WideStringC& contents) {
  CFX_WideString filtercontents;
  for (FX_STRSIZE i = 0; i < contents.GetLength(); i++) {
    wchar_t ch = contents[i];
    if (ch == L'*' && (i == 0 || i == contents.GetLength() - 1)) {
      continue;
    }
    if (ch > 175) {
      i++;
      continue;
    } else {
      ch = Upper(ch);
    }
    if ((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
        ch == L'-' || ch == L'.' || ch == L' ' || ch == L'*' || ch == L'$' ||
        ch == L'/' || ch == L'+' || ch == L'%') {
      filtercontents += ch;
    }
  }
  return filtercontents;
}

CFX_WideString CBC_OnedCode39Writer::RenderTextContents(
    const CFX_WideStringC& contents) {
  CFX_WideString renderContents;
  for (FX_STRSIZE i = 0; i < contents.GetLength(); i++) {
    wchar_t ch = contents[i];
    if (ch == L'*' && (i == 0 || i == contents.GetLength() - 1)) {
      continue;
    }
    if (ch > 175) {
      i++;
      continue;
    }
    if ((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'a' && ch <= L'z') || ch == L'-' || ch == L'.' || ch == L' ' ||
        ch == L'*' || ch == L'$' || ch == L'/' || ch == L'+' || ch == L'%') {
      renderContents += ch;
    }
  }
  return renderContents;
}

bool CBC_OnedCode39Writer::SetTextLocation(BC_TEXT_LOC location) {
  if (location < BC_TEXT_LOC_NONE || location > BC_TEXT_LOC_BELOWEMBED) {
    return false;
  }
  m_locTextLoc = location;
  return true;
}
bool CBC_OnedCode39Writer::SetWideNarrowRatio(int8_t ratio) {
  if (ratio < 2 || ratio > 3)
    return false;

  m_iWideNarrRatio = ratio;
  return true;
}

uint8_t* CBC_OnedCode39Writer::EncodeWithHint(const CFX_ByteString& contents,
                                              BCFORMAT format,
                                              int32_t& outWidth,
                                              int32_t& outHeight,
                                              int32_t hints) {
  if (format != BCFORMAT_CODE_39)
    return nullptr;
  return CBC_OneDimWriter::EncodeWithHint(contents, format, outWidth, outHeight,
                                          hints);
}

void CBC_OnedCode39Writer::ToIntArray(int32_t a, int8_t* toReturn) {
  for (int32_t i = 0; i < 9; i++) {
    toReturn[i] = (a & (1 << i)) == 0 ? 1 : m_iWideNarrRatio;
  }
}

char CBC_OnedCode39Writer::CalcCheckSum(const CFX_ByteString& contents) {
  FX_STRSIZE length = contents.GetLength();
  if (length > 80)
    return '*';

  int32_t checksum = 0;
  size_t len = strlen(ALPHABET_STRING);
  for (const auto& c : contents) {
    size_t j = 0;
    for (; j < len; j++) {
      if (ALPHABET_STRING[j] == c) {
        if (c != '*')
          checksum += j;
        break;
      }
    }
    if (j >= len)
      return '*';
  }
  checksum = checksum % 43;
  return CHECKSUM_STRING[checksum];
}

uint8_t* CBC_OnedCode39Writer::EncodeImpl(const CFX_ByteString& contents,
                                          int32_t& outlength) {
  char checksum = CalcCheckSum(contents);
  if (checksum == '*')
    return nullptr;

  int8_t widths[9] = {0};
  int32_t wideStrideNum = 3;
  int32_t narrStrideNum = 9 - wideStrideNum;
  CFX_ByteString encodedContents = contents;
  if (m_bCalcChecksum)
    encodedContents += checksum;
  m_iContentLen = encodedContents.GetLength();
  int32_t codeWidth = (wideStrideNum * m_iWideNarrRatio + narrStrideNum) * 2 +
                      1 + m_iContentLen;
  size_t len = strlen(ALPHABET_STRING);
  for (FX_STRSIZE j = 0; j < m_iContentLen; j++) {
    for (size_t i = 0; i < len; i++) {
      if (ALPHABET_STRING[i] != encodedContents[j])
        continue;

      ToIntArray(CHARACTER_ENCODINGS[i], widths);
      for (size_t k = 0; k < 9; k++)
        codeWidth += widths[k];
    }
  }
  outlength = codeWidth;
  std::unique_ptr<uint8_t, FxFreeDeleter> result(FX_Alloc(uint8_t, codeWidth));
  ToIntArray(CHARACTER_ENCODINGS[39], widths);
  int32_t e = BCExceptionNO;
  int32_t pos = AppendPattern(result.get(), 0, widths, 9, 1, e);
  if (e != BCExceptionNO)
    return nullptr;

  int8_t narrowWhite[] = {1};
  pos += AppendPattern(result.get(), pos, narrowWhite, 1, 0, e);
  if (e != BCExceptionNO)
    return nullptr;

  for (int32_t l = m_iContentLen - 1; l >= 0; l--) {
    for (size_t i = 0; i < len; i++) {
      if (ALPHABET_STRING[i] != encodedContents[l])
        continue;

      ToIntArray(CHARACTER_ENCODINGS[i], widths);
      pos += AppendPattern(result.get(), pos, widths, 9, 1, e);
      if (e != BCExceptionNO)
        return nullptr;
    }
    pos += AppendPattern(result.get(), pos, narrowWhite, 1, 0, e);
    if (e != BCExceptionNO)
      return nullptr;
  }
  ToIntArray(CHARACTER_ENCODINGS[39], widths);
  pos += AppendPattern(result.get(), pos, widths, 9, 1, e);
  if (e != BCExceptionNO)
    return nullptr;

  auto* result_ptr = result.get();
  for (int32_t i = 0; i < codeWidth / 2; i++) {
    result_ptr[i] ^= result_ptr[codeWidth - 1 - i];
    result_ptr[codeWidth - 1 - i] ^= result_ptr[i];
    result_ptr[i] ^= result_ptr[codeWidth - 1 - i];
  }
  return result.release();
}

bool CBC_OnedCode39Writer::encodedContents(const CFX_WideStringC& contents,
                                           CFX_WideString* result) {
  *result = CFX_WideString(contents);
  if (m_bCalcChecksum && m_bPrintChecksum) {
    CFX_WideString checksumContent = FilterContents(contents);
    CFX_ByteString str = checksumContent.UTF8Encode();
    char checksum;
    checksum = CalcCheckSum(str);
    if (checksum == '*')
      return false;
    str += checksum;
    *result += checksum;
  }
  return true;
}

bool CBC_OnedCode39Writer::RenderResult(const CFX_WideStringC& contents,
                                        uint8_t* code,
                                        int32_t codeLength,
                                        bool isDevice) {
  CFX_WideString encodedCon;
  if (!encodedContents(contents, &encodedCon))
    return false;
  return CBC_OneDimWriter::RenderResult(encodedCon.AsStringC(), code,
                                        codeLength, isDevice);
}

// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/font/cpdf_cmap.h"

#include <memory>
#include <utility>
#include <vector>

#include "core/fpdfapi/cmaps/cmap_int.h"
#include "core/fpdfapi/font/cpdf_cmapmanager.h"
#include "core/fpdfapi/font/cpdf_cmapparser.h"
#include "core/fpdfapi/parser/cpdf_simple_parser.h"

namespace {

struct ByteRange {
  uint8_t m_First;
  uint8_t m_Last;  // Inclusive.
};

struct PredefinedCMap {
  const char* m_pName;
  CIDSet m_Charset;
  CIDCoding m_Coding;
  CPDF_CMap::CodingScheme m_CodingScheme;
  uint8_t m_LeadingSegCount;
  ByteRange m_LeadingSegs[2];
};

const PredefinedCMap g_PredefinedCMaps[] = {
    {"GB-EUC",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"GBpc-EUC",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfc}}},
    {"GBK-EUC",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBKp-EUC",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBK2K-EUC",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"GBK2K",
     CIDSET_GB1,
     CIDCODING_GB,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"UniGB-UCS2", CIDSET_GB1, CIDCODING_UCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniGB-UTF16", CIDSET_GB1, CIDCODING_UTF16, CPDF_CMap::TwoBytes, 0, {}},
    {"B5pc",
     CIDSET_CNS1,
     CIDCODING_BIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfc}}},
    {"HKscs-B5",
     CIDSET_CNS1,
     CIDCODING_BIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x88, 0xfe}}},
    {"ETen-B5",
     CIDSET_CNS1,
     CIDCODING_BIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"ETenms-B5",
     CIDSET_CNS1,
     CIDCODING_BIG5,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"UniCNS-UCS2", CIDSET_CNS1, CIDCODING_UCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniCNS-UTF16", CIDSET_CNS1, CIDCODING_UTF16, CPDF_CMap::TwoBytes, 0, {}},
    {"83pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90ms-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90msp-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"90pv-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"Add-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"EUC",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x8e, 0x8e}, {0xa1, 0xfe}}},
    {"H", CIDSET_JAPAN1, CIDCODING_JIS, CPDF_CMap::TwoBytes, 1, {{0x21, 0x7e}}},
    {"V", CIDSET_JAPAN1, CIDCODING_JIS, CPDF_CMap::TwoBytes, 1, {{0x21, 0x7e}}},
    {"Ext-RKSJ",
     CIDSET_JAPAN1,
     CIDCODING_JIS,
     CPDF_CMap::MixedTwoBytes,
     2,
     {{0x81, 0x9f}, {0xe0, 0xfc}}},
    {"UniJIS-UCS2", CIDSET_JAPAN1, CIDCODING_UCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniJIS-UCS2-HW",
     CIDSET_JAPAN1,
     CIDCODING_UCS2,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"UniJIS-UTF16",
     CIDSET_JAPAN1,
     CIDCODING_UTF16,
     CPDF_CMap::TwoBytes,
     0,
     {}},
    {"KSC-EUC",
     CIDSET_KOREA1,
     CIDCODING_KOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfe}}},
    {"KSCms-UHC",
     CIDSET_KOREA1,
     CIDCODING_KOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"KSCms-UHC-HW",
     CIDSET_KOREA1,
     CIDCODING_KOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0x81, 0xfe}}},
    {"KSCpc-EUC",
     CIDSET_KOREA1,
     CIDCODING_KOREA,
     CPDF_CMap::MixedTwoBytes,
     1,
     {{0xa1, 0xfd}}},
    {"UniKS-UCS2", CIDSET_KOREA1, CIDCODING_UCS2, CPDF_CMap::TwoBytes, 0, {}},
    {"UniKS-UTF16", CIDSET_KOREA1, CIDCODING_UTF16, CPDF_CMap::TwoBytes, 0, {}},
};

int CheckFourByteCodeRange(uint8_t* codes,
                           FX_STRSIZE size,
                           const std::vector<CPDF_CMap::CodeRange>& ranges) {
  int iSeg = pdfium::CollectionSize<int>(ranges) - 1;
  while (iSeg >= 0) {
    if (ranges[iSeg].m_CharSize < size) {
      --iSeg;
      continue;
    }
    int iChar = 0;
    while (iChar < size) {
      if (codes[iChar] < ranges[iSeg].m_Lower[iChar] ||
          codes[iChar] > ranges[iSeg].m_Upper[iChar]) {
        break;
      }
      ++iChar;
    }
    if (iChar == ranges[iSeg].m_CharSize)
      return 2;
    if (iChar)
      return (size == ranges[iSeg].m_CharSize) ? 2 : 1;
    iSeg--;
  }
  return 0;
}

int GetFourByteCharSizeImpl(uint32_t charcode,
                            const std::vector<CPDF_CMap::CodeRange>& ranges) {
  if (ranges.empty())
    return 1;

  uint8_t codes[4];
  codes[0] = codes[1] = 0x00;
  codes[2] = (uint8_t)(charcode >> 8 & 0xFF);
  codes[3] = (uint8_t)charcode;
  FX_STRSIZE offset = 0;
  int size = 4;
  for (int i = 0; i < 4; ++i) {
    int iSeg = pdfium::CollectionSize<int>(ranges) - 1;
    while (iSeg >= 0) {
      if (ranges[iSeg].m_CharSize < size) {
        --iSeg;
        continue;
      }
      int iChar = 0;
      while (iChar < size) {
        if (codes[offset + iChar] < ranges[iSeg].m_Lower[iChar] ||
            codes[offset + iChar] > ranges[iSeg].m_Upper[iChar]) {
          break;
        }
        ++iChar;
      }
      if (iChar == ranges[iSeg].m_CharSize)
        return size;
      --iSeg;
    }
    --size;
    ++offset;
  }
  return 1;
}

}  // namespace

CPDF_CMap::CPDF_CMap()
    : m_bLoaded(false),
      m_bVertical(false),
      m_Charset(CIDSET_UNKNOWN),
      m_CodingScheme(TwoBytes),
      m_Coding(CIDCODING_UNKNOWN),
      m_pEmbedMap(nullptr) {}

CPDF_CMap::~CPDF_CMap() {}

void CPDF_CMap::LoadPredefined(CPDF_CMapManager* pMgr,
                               const CFX_ByteString& bsName,
                               bool bPromptCJK) {
  m_PredefinedCMap = bsName;
  if (m_PredefinedCMap == "Identity-H" || m_PredefinedCMap == "Identity-V") {
    m_Coding = CIDCODING_CID;
    m_bVertical = bsName[9] == 'V';
    m_bLoaded = true;
    return;
  }
  CFX_ByteString cmapid = m_PredefinedCMap;
  m_bVertical = cmapid.Right(1) == "V";
  if (cmapid.GetLength() > 2) {
    cmapid = cmapid.Left(cmapid.GetLength() - 2);
  }
  const PredefinedCMap* map = nullptr;
  for (size_t i = 0; i < FX_ArraySize(g_PredefinedCMaps); ++i) {
    if (cmapid == CFX_ByteStringC(g_PredefinedCMaps[i].m_pName)) {
      map = &g_PredefinedCMaps[i];
      break;
    }
  }
  if (!map)
    return;

  m_Charset = map->m_Charset;
  m_Coding = map->m_Coding;
  m_CodingScheme = map->m_CodingScheme;
  if (m_CodingScheme == MixedTwoBytes) {
    m_MixedTwoByteLeadingBytes = std::vector<bool>(256);
    for (uint32_t i = 0; i < map->m_LeadingSegCount; ++i) {
      const ByteRange& seg = map->m_LeadingSegs[i];
      for (int b = seg.m_First; b <= seg.m_Last; ++b)
        m_MixedTwoByteLeadingBytes[b] = true;
    }
  }
  FPDFAPI_FindEmbeddedCMap(bsName, m_Charset, m_Coding, m_pEmbedMap);
  if (!m_pEmbedMap)
    return;

  m_bLoaded = true;
}

void CPDF_CMap::LoadEmbedded(const uint8_t* pData, uint32_t size) {
  m_DirectCharcodeToCIDTable = std::vector<uint16_t>(65536);
  CPDF_CMapParser parser(this);
  CPDF_SimpleParser syntax(pData, size);
  while (1) {
    CFX_ByteStringC word = syntax.GetWord();
    if (word.IsEmpty()) {
      break;
    }
    parser.ParseWord(word);
  }
  if (m_CodingScheme == MixedFourBytes && parser.HasAdditionalMappings()) {
    m_AdditionalCharcodeToCIDMappings = parser.TakeAdditionalMappings();
    std::sort(
        m_AdditionalCharcodeToCIDMappings.begin(),
        m_AdditionalCharcodeToCIDMappings.end(),
        [](const CPDF_CMap::CIDRange& arg1, const CPDF_CMap::CIDRange& arg2) {
          return arg1.m_EndCode < arg2.m_EndCode;
        });
  }
}

uint16_t CPDF_CMap::CIDFromCharCode(uint32_t charcode) const {
  if (m_Coding == CIDCODING_CID)
    return static_cast<uint16_t>(charcode);

  if (m_pEmbedMap)
    return FPDFAPI_CIDFromCharCode(m_pEmbedMap, charcode);

  if (m_DirectCharcodeToCIDTable.empty())
    return static_cast<uint16_t>(charcode);

  if (charcode < 0x10000)
    return m_DirectCharcodeToCIDTable[charcode];

  auto it = std::lower_bound(m_AdditionalCharcodeToCIDMappings.begin(),
                             m_AdditionalCharcodeToCIDMappings.end(), charcode,
                             [](const CPDF_CMap::CIDRange& arg, uint32_t val) {
                               return arg.m_EndCode < val;
                             });
  if (it == m_AdditionalCharcodeToCIDMappings.end() ||
      it->m_StartCode > charcode) {
    return 0;
  }
  return it->m_StartCID + charcode - it->m_StartCode;
}

uint32_t CPDF_CMap::GetNextChar(const char* pString,
                                int nStrLen,
                                int& offset) const {
  auto* pBytes = reinterpret_cast<const uint8_t*>(pString);
  switch (m_CodingScheme) {
    case OneByte: {
      return pBytes[offset++];
    }
    case TwoBytes: {
      uint8_t byte1 = pBytes[offset++];
      return 256 * byte1 + pBytes[offset++];
    }
    case MixedTwoBytes: {
      uint8_t byte1 = pBytes[offset++];
      if (!m_MixedTwoByteLeadingBytes[byte1])
        return byte1;
      return 256 * byte1 + pBytes[offset++];
    }
    case MixedFourBytes: {
      uint8_t codes[4];
      int char_size = 1;
      codes[0] = pBytes[offset++];
      while (1) {
        int ret = CheckFourByteCodeRange(codes, char_size,
                                         m_MixedFourByteLeadingRanges);
        if (ret == 0)
          return 0;
        if (ret == 2) {
          uint32_t charcode = 0;
          for (int i = 0; i < char_size; i++)
            charcode = (charcode << 8) + codes[i];
          return charcode;
        }
        if (char_size == 4 || offset == nStrLen)
          return 0;
        codes[char_size++] = pBytes[offset++];
      }
      break;
    }
  }
  return 0;
}

int CPDF_CMap::GetCharSize(uint32_t charcode) const {
  switch (m_CodingScheme) {
    case OneByte:
      return 1;
    case TwoBytes:
      return 2;
    case MixedTwoBytes:
      if (charcode < 0x100)
        return 1;
      return 2;
    case MixedFourBytes:
      if (charcode < 0x100)
        return 1;
      if (charcode < 0x10000)
        return 2;
      if (charcode < 0x1000000)
        return 3;
      return 4;
  }
  return 1;
}

int CPDF_CMap::CountChar(const char* pString, int size) const {
  switch (m_CodingScheme) {
    case OneByte:
      return size;
    case TwoBytes:
      return (size + 1) / 2;
    case MixedTwoBytes: {
      int count = 0;
      for (int i = 0; i < size; i++) {
        count++;
        if (m_MixedTwoByteLeadingBytes[reinterpret_cast<const uint8_t*>(
                pString)[i]]) {
          i++;
        }
      }
      return count;
    }
    case MixedFourBytes: {
      int count = 0, offset = 0;
      while (offset < size) {
        GetNextChar(pString, size, offset);
        count++;
      }
      return count;
    }
  }
  return size;
}

int CPDF_CMap::AppendChar(char* str, uint32_t charcode) const {
  switch (m_CodingScheme) {
    case OneByte:
      str[0] = (uint8_t)charcode;
      return 1;
    case TwoBytes:
      str[0] = (uint8_t)(charcode / 256);
      str[1] = (uint8_t)(charcode % 256);
      return 2;
    case MixedTwoBytes:
      if (charcode < 0x100 && !m_MixedTwoByteLeadingBytes[(uint8_t)charcode]) {
        str[0] = (uint8_t)charcode;
        return 1;
      }
      str[0] = (uint8_t)(charcode >> 8);
      str[1] = (uint8_t)charcode;
      return 2;
    case MixedFourBytes:
      if (charcode < 0x100) {
        int iSize =
            GetFourByteCharSizeImpl(charcode, m_MixedFourByteLeadingRanges);
        if (iSize == 0)
          iSize = 1;
        str[iSize - 1] = (uint8_t)charcode;
        if (iSize > 1)
          memset(str, 0, iSize - 1);
        return iSize;
      }
      if (charcode < 0x10000) {
        str[0] = (uint8_t)(charcode >> 8);
        str[1] = (uint8_t)charcode;
        return 2;
      }
      if (charcode < 0x1000000) {
        str[0] = (uint8_t)(charcode >> 16);
        str[1] = (uint8_t)(charcode >> 8);
        str[2] = (uint8_t)charcode;
        return 3;
      }
      str[0] = (uint8_t)(charcode >> 24);
      str[1] = (uint8_t)(charcode >> 16);
      str[2] = (uint8_t)(charcode >> 8);
      str[3] = (uint8_t)charcode;
      return 4;
  }
  return 0;
}

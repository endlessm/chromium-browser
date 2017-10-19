// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxcrt/css/cfx_cssdeclaration.h"

#include "core/fxcrt/css/cfx_csscolorvalue.h"
#include "core/fxcrt/css/cfx_csscustomproperty.h"
#include "core/fxcrt/css/cfx_cssenumvalue.h"
#include "core/fxcrt/css/cfx_cssnumbervalue.h"
#include "core/fxcrt/css/cfx_csspropertyholder.h"
#include "core/fxcrt/css/cfx_cssstringvalue.h"
#include "core/fxcrt/css/cfx_cssvaluelist.h"
#include "core/fxcrt/css/cfx_cssvaluelistparser.h"
#include "core/fxcrt/fx_extension.h"
#include "third_party/base/logging.h"
#include "third_party/base/ptr_util.h"

namespace {

uint8_t Hex2Dec(uint8_t hexHigh, uint8_t hexLow) {
  return (FXSYS_HexCharToInt(hexHigh) << 4) + FXSYS_HexCharToInt(hexLow);
}

struct CFX_CSSPropertyValueTable {
  CFX_CSSPropertyValue eName;
  const wchar_t* pszName;
  uint32_t dwHash;
};
const CFX_CSSPropertyValueTable g_CFX_CSSPropertyValues[] = {
    {CFX_CSSPropertyValue::Bolder, L"bolder", 0x009F1058},
    {CFX_CSSPropertyValue::None, L"none", 0x048B6670},
    {CFX_CSSPropertyValue::Dot, L"dot", 0x0A48CB27},
    {CFX_CSSPropertyValue::Sub, L"sub", 0x0BD37FAA},
    {CFX_CSSPropertyValue::Top, L"top", 0x0BEDAF33},
    {CFX_CSSPropertyValue::Right, L"right", 0x193ADE3E},
    {CFX_CSSPropertyValue::Normal, L"normal", 0x247CF3E9},
    {CFX_CSSPropertyValue::Auto, L"auto", 0x2B35B6D9},
    {CFX_CSSPropertyValue::Text, L"text", 0x2D08AF85},
    {CFX_CSSPropertyValue::XSmall, L"x-small", 0x2D2FCAFE},
    {CFX_CSSPropertyValue::Thin, L"thin", 0x2D574D53},
    {CFX_CSSPropertyValue::Small, L"small", 0x316A3739},
    {CFX_CSSPropertyValue::Bottom, L"bottom", 0x399F02B5},
    {CFX_CSSPropertyValue::Underline, L"underline", 0x3A0273A6},
    {CFX_CSSPropertyValue::Double, L"double", 0x3D98515B},
    {CFX_CSSPropertyValue::Lighter, L"lighter", 0x45BEB7AF},
    {CFX_CSSPropertyValue::Oblique, L"oblique", 0x53EBDDB1},
    {CFX_CSSPropertyValue::Super, L"super", 0x6A4F842F},
    {CFX_CSSPropertyValue::Center, L"center", 0x6C51AFC1},
    {CFX_CSSPropertyValue::XxLarge, L"xx-large", 0x70BB1508},
    {CFX_CSSPropertyValue::Smaller, L"smaller", 0x849769F0},
    {CFX_CSSPropertyValue::Baseline, L"baseline", 0x87436BA3},
    {CFX_CSSPropertyValue::Thick, L"thick", 0x8CC35EB3},
    {CFX_CSSPropertyValue::Justify, L"justify", 0x8D269CAE},
    {CFX_CSSPropertyValue::Middle, L"middle", 0x947FA00F},
    {CFX_CSSPropertyValue::Medium, L"medium", 0xA084A381},
    {CFX_CSSPropertyValue::ListItem, L"list-item", 0xA32382B8},
    {CFX_CSSPropertyValue::XxSmall, L"xx-small", 0xADE1FC76},
    {CFX_CSSPropertyValue::Bold, L"bold", 0xB18313A1},
    {CFX_CSSPropertyValue::SmallCaps, L"small-caps", 0xB299428D},
    {CFX_CSSPropertyValue::Inline, L"inline", 0xC02D649F},
    {CFX_CSSPropertyValue::Overline, L"overline", 0xC0EC9FA4},
    {CFX_CSSPropertyValue::TextBottom, L"text-bottom", 0xC7D08D87},
    {CFX_CSSPropertyValue::Larger, L"larger", 0xCD3C409D},
    {CFX_CSSPropertyValue::InlineTable, L"inline-table", 0xD131F494},
    {CFX_CSSPropertyValue::InlineBlock, L"inline-block", 0xD26A8BD7},
    {CFX_CSSPropertyValue::Blink, L"blink", 0xDC36E390},
    {CFX_CSSPropertyValue::Block, L"block", 0xDCD480AB},
    {CFX_CSSPropertyValue::Italic, L"italic", 0xE31D5396},
    {CFX_CSSPropertyValue::LineThrough, L"line-through", 0xE4C5A276},
    {CFX_CSSPropertyValue::XLarge, L"x-large", 0xF008E390},
    {CFX_CSSPropertyValue::Large, L"large", 0xF4434FCB},
    {CFX_CSSPropertyValue::Left, L"left", 0xF5AD782B},
    {CFX_CSSPropertyValue::TextTop, L"text-top", 0xFCB58D45},
};
const int32_t g_iCSSPropertyValueCount =
    sizeof(g_CFX_CSSPropertyValues) / sizeof(CFX_CSSPropertyValueTable);
static_assert(g_iCSSPropertyValueCount ==
                  static_cast<int32_t>(CFX_CSSPropertyValue::LAST_MARKER),
              "Property value table differs in size from property value enum");

struct CFX_CSSLengthUnitTable {
  uint16_t wHash;
  CFX_CSSNumberType wValue;
};
const CFX_CSSLengthUnitTable g_CFX_CSSLengthUnits[] = {
    {0x0672, CFX_CSSNumberType::EMS},
    {0x067D, CFX_CSSNumberType::EXS},
    {0x1AF7, CFX_CSSNumberType::Inches},
    {0x2F7A, CFX_CSSNumberType::MilliMeters},
    {0x3ED3, CFX_CSSNumberType::Picas},
    {0x3EE4, CFX_CSSNumberType::Points},
    {0x3EE8, CFX_CSSNumberType::Pixels},
    {0xFC30, CFX_CSSNumberType::CentiMeters},
};

struct CFX_CSSColorTable {
  uint32_t dwHash;
  FX_ARGB dwValue;
};
const CFX_CSSColorTable g_CFX_CSSColors[] = {
    {0x031B47FE, 0xff000080}, {0x0BB8DF5B, 0xffff0000},
    {0x0D82A78C, 0xff800000}, {0x2ACC82E8, 0xff00ffff},
    {0x2D083986, 0xff008080}, {0x4A6A6195, 0xffc0c0c0},
    {0x546A8EF3, 0xff808080}, {0x65C9169C, 0xffffa500},
    {0x8422BB61, 0xffffffff}, {0x9271A558, 0xff800080},
    {0xA65A3EE3, 0xffff00ff}, {0xB1345708, 0xff0000ff},
    {0xB6D2CF1F, 0xff808000}, {0xD19B5E1C, 0xffffff00},
    {0xDB64391D, 0xff000000}, {0xF616D507, 0xff00ff00},
    {0xF6EFFF31, 0xff008000},
};

const CFX_CSSPropertyValueTable* GetCSSPropertyValueByName(
    const CFX_WideStringC& wsName) {
  ASSERT(!wsName.IsEmpty());
  uint32_t dwHash = FX_HashCode_GetW(wsName, true);
  int32_t iEnd = g_iCSSPropertyValueCount;
  int32_t iMid, iStart = 0;
  uint32_t dwMid;
  do {
    iMid = (iStart + iEnd) / 2;
    dwMid = g_CFX_CSSPropertyValues[iMid].dwHash;
    if (dwHash == dwMid) {
      return g_CFX_CSSPropertyValues + iMid;
    } else if (dwHash > dwMid) {
      iStart = iMid + 1;
    } else {
      iEnd = iMid - 1;
    }
  } while (iStart <= iEnd);
  return nullptr;
}

const CFX_CSSLengthUnitTable* GetCSSLengthUnitByName(
    const CFX_WideStringC& wsName) {
  ASSERT(!wsName.IsEmpty());
  uint16_t wHash = FX_HashCode_GetW(wsName, true);
  int32_t iEnd =
      sizeof(g_CFX_CSSLengthUnits) / sizeof(CFX_CSSLengthUnitTable) - 1;
  int32_t iMid, iStart = 0;
  uint16_t wMid;
  do {
    iMid = (iStart + iEnd) / 2;
    wMid = g_CFX_CSSLengthUnits[iMid].wHash;
    if (wHash == wMid) {
      return g_CFX_CSSLengthUnits + iMid;
    } else if (wHash > wMid) {
      iStart = iMid + 1;
    } else {
      iEnd = iMid - 1;
    }
  } while (iStart <= iEnd);
  return nullptr;
}

const CFX_CSSColorTable* GetCSSColorByName(const CFX_WideStringC& wsName) {
  ASSERT(!wsName.IsEmpty());
  uint32_t dwHash = FX_HashCode_GetW(wsName, true);
  int32_t iEnd = sizeof(g_CFX_CSSColors) / sizeof(CFX_CSSColorTable) - 1;
  int32_t iMid, iStart = 0;
  uint32_t dwMid;
  do {
    iMid = (iStart + iEnd) / 2;
    dwMid = g_CFX_CSSColors[iMid].dwHash;
    if (dwHash == dwMid) {
      return g_CFX_CSSColors + iMid;
    } else if (dwHash > dwMid) {
      iStart = iMid + 1;
    } else {
      iEnd = iMid - 1;
    }
  } while (iStart <= iEnd);
  return nullptr;
}

bool ParseCSSNumber(const wchar_t* pszValue,
                    int32_t iValueLen,
                    float& fValue,
                    CFX_CSSNumberType& eUnit) {
  ASSERT(pszValue && iValueLen > 0);
  int32_t iUsedLen = 0;
  fValue = FXSYS_wcstof(pszValue, iValueLen, &iUsedLen);
  if (iUsedLen <= 0)
    return false;

  iValueLen -= iUsedLen;
  pszValue += iUsedLen;
  eUnit = CFX_CSSNumberType::Number;
  if (iValueLen >= 1 && *pszValue == '%') {
    eUnit = CFX_CSSNumberType::Percent;
  } else if (iValueLen == 2) {
    const CFX_CSSLengthUnitTable* pUnit =
        GetCSSLengthUnitByName(CFX_WideStringC(pszValue, 2));
    if (pUnit)
      eUnit = pUnit->wValue;
  }
  return true;
}

}  // namespace

// static
bool CFX_CSSDeclaration::ParseCSSString(const wchar_t* pszValue,
                                        int32_t iValueLen,
                                        int32_t* iOffset,
                                        int32_t* iLength) {
  ASSERT(pszValue && iValueLen > 0);
  *iOffset = 0;
  *iLength = iValueLen;
  if (iValueLen >= 2) {
    wchar_t first = pszValue[0], last = pszValue[iValueLen - 1];
    if ((first == '\"' && last == '\"') || (first == '\'' && last == '\'')) {
      *iOffset = 1;
      *iLength -= 2;
    }
  }
  return iValueLen > 0;
}

// static.
bool CFX_CSSDeclaration::ParseCSSColor(const wchar_t* pszValue,
                                       int32_t iValueLen,
                                       FX_ARGB* dwColor) {
  ASSERT(pszValue && iValueLen > 0);
  ASSERT(dwColor);

  if (*pszValue == '#') {
    switch (iValueLen) {
      case 4: {
        uint8_t red = Hex2Dec((uint8_t)pszValue[1], (uint8_t)pszValue[1]);
        uint8_t green = Hex2Dec((uint8_t)pszValue[2], (uint8_t)pszValue[2]);
        uint8_t blue = Hex2Dec((uint8_t)pszValue[3], (uint8_t)pszValue[3]);
        *dwColor = ArgbEncode(255, red, green, blue);
        return true;
      }
      case 7: {
        uint8_t red = Hex2Dec((uint8_t)pszValue[1], (uint8_t)pszValue[2]);
        uint8_t green = Hex2Dec((uint8_t)pszValue[3], (uint8_t)pszValue[4]);
        uint8_t blue = Hex2Dec((uint8_t)pszValue[5], (uint8_t)pszValue[6]);
        *dwColor = ArgbEncode(255, red, green, blue);
        return true;
      }
      default:
        return false;
    }
  }

  if (iValueLen >= 10) {
    if (pszValue[iValueLen - 1] != ')' || FXSYS_wcsnicmp(L"rgb(", pszValue, 4))
      return false;

    uint8_t rgb[3] = {0};
    float fValue;
    CFX_CSSPrimitiveType eType;
    CFX_CSSValueListParser list(pszValue + 4, iValueLen - 5, ',');
    for (int32_t i = 0; i < 3; ++i) {
      if (!list.NextValue(eType, pszValue, iValueLen))
        return false;
      if (eType != CFX_CSSPrimitiveType::Number)
        return false;
      CFX_CSSNumberType eNumType;
      if (!ParseCSSNumber(pszValue, iValueLen, fValue, eNumType))
        return false;

      rgb[i] = eNumType == CFX_CSSNumberType::Percent
                   ? FXSYS_round(fValue * 2.55f)
                   : FXSYS_round(fValue);
    }
    *dwColor = ArgbEncode(255, rgb[0], rgb[1], rgb[2]);
    return true;
  }

  const CFX_CSSColorTable* pColor =
      GetCSSColorByName(CFX_WideStringC(pszValue, iValueLen));
  if (!pColor)
    return false;

  *dwColor = pColor->dwValue;
  return true;
}

CFX_CSSDeclaration::CFX_CSSDeclaration() {}

CFX_CSSDeclaration::~CFX_CSSDeclaration() {}

CFX_RetainPtr<CFX_CSSValue> CFX_CSSDeclaration::GetProperty(
    CFX_CSSProperty eProperty,
    bool* bImportant) const {
  for (const auto& p : properties_) {
    if (p->eProperty == eProperty) {
      *bImportant = p->bImportant;
      return p->pValue;
    }
  }
  return nullptr;
}

void CFX_CSSDeclaration::AddPropertyHolder(CFX_CSSProperty eProperty,
                                           CFX_RetainPtr<CFX_CSSValue> pValue,
                                           bool bImportant) {
  auto pHolder = pdfium::MakeUnique<CFX_CSSPropertyHolder>();
  pHolder->bImportant = bImportant;
  pHolder->eProperty = eProperty;
  pHolder->pValue = pValue;
  properties_.push_back(std::move(pHolder));
}

void CFX_CSSDeclaration::AddProperty(const CFX_CSSPropertyTable* pTable,
                                     const CFX_WideStringC& value) {
  ASSERT(!value.IsEmpty());

  const wchar_t* pszValue = value.unterminated_c_str();
  int32_t iValueLen = value.GetLength();
  bool bImportant = false;
  if (iValueLen >= 10 && pszValue[iValueLen - 10] == '!' &&
      FXSYS_wcsnicmp(L"important", pszValue + iValueLen - 9, 9) == 0) {
    if ((iValueLen -= 10) == 0)
      return;

    bImportant = true;
  }
  const uint32_t dwType = pTable->dwType;
  switch (dwType & 0x0F) {
    case CFX_CSSVALUETYPE_Primitive: {
      static const uint32_t g_ValueGuessOrder[] = {
          CFX_CSSVALUETYPE_MaybeNumber, CFX_CSSVALUETYPE_MaybeEnum,
          CFX_CSSVALUETYPE_MaybeColor, CFX_CSSVALUETYPE_MaybeString,
      };
      static const int32_t g_ValueGuessCount =
          sizeof(g_ValueGuessOrder) / sizeof(uint32_t);
      for (int32_t i = 0; i < g_ValueGuessCount; ++i) {
        const uint32_t dwMatch = dwType & g_ValueGuessOrder[i];
        if (dwMatch == 0) {
          continue;
        }
        CFX_RetainPtr<CFX_CSSValue> pCSSValue;
        switch (dwMatch) {
          case CFX_CSSVALUETYPE_MaybeNumber:
            pCSSValue = ParseNumber(pszValue, iValueLen);
            break;
          case CFX_CSSVALUETYPE_MaybeEnum:
            pCSSValue = ParseEnum(pszValue, iValueLen);
            break;
          case CFX_CSSVALUETYPE_MaybeColor:
            pCSSValue = ParseColor(pszValue, iValueLen);
            break;
          case CFX_CSSVALUETYPE_MaybeString:
            pCSSValue = ParseString(pszValue, iValueLen);
            break;
          default:
            break;
        }
        if (pCSSValue) {
          AddPropertyHolder(pTable->eName, pCSSValue, bImportant);
          return;
        }

        if ((dwType & ~(g_ValueGuessOrder[i])) == CFX_CSSVALUETYPE_Primitive)
          return;
      }
      break;
    }
    case CFX_CSSVALUETYPE_Shorthand: {
      CFX_RetainPtr<CFX_CSSValue> pWidth;
      switch (pTable->eName) {
        case CFX_CSSProperty::Font:
          ParseFontProperty(pszValue, iValueLen, bImportant);
          return;
        case CFX_CSSProperty::Border:
          if (ParseBorderProperty(pszValue, iValueLen, pWidth)) {
            AddPropertyHolder(CFX_CSSProperty::BorderLeftWidth, pWidth,
                              bImportant);
            AddPropertyHolder(CFX_CSSProperty::BorderTopWidth, pWidth,
                              bImportant);
            AddPropertyHolder(CFX_CSSProperty::BorderRightWidth, pWidth,
                              bImportant);
            AddPropertyHolder(CFX_CSSProperty::BorderBottomWidth, pWidth,
                              bImportant);
            return;
          }
          break;
        case CFX_CSSProperty::BorderLeft:
          if (ParseBorderProperty(pszValue, iValueLen, pWidth)) {
            AddPropertyHolder(CFX_CSSProperty::BorderLeftWidth, pWidth,
                              bImportant);
            return;
          }
          break;
        case CFX_CSSProperty::BorderTop:
          if (ParseBorderProperty(pszValue, iValueLen, pWidth)) {
            AddPropertyHolder(CFX_CSSProperty::BorderTopWidth, pWidth,
                              bImportant);
            return;
          }
          break;
        case CFX_CSSProperty::BorderRight:
          if (ParseBorderProperty(pszValue, iValueLen, pWidth)) {
            AddPropertyHolder(CFX_CSSProperty::BorderRightWidth, pWidth,
                              bImportant);
            return;
          }
          break;
        case CFX_CSSProperty::BorderBottom:
          if (ParseBorderProperty(pszValue, iValueLen, pWidth)) {
            AddPropertyHolder(CFX_CSSProperty::BorderBottomWidth, pWidth,
                              bImportant);
            return;
          }
          break;
        default:
          break;
      }
    } break;
    case CFX_CSSVALUETYPE_List:
      ParseValueListProperty(pTable, pszValue, iValueLen, bImportant);
      return;
    default:
      NOTREACHED();
      break;
  }
}

void CFX_CSSDeclaration::AddProperty(const CFX_WideString& prop,
                                     const CFX_WideString& value) {
  custom_properties_.push_back(
      pdfium::MakeUnique<CFX_CSSCustomProperty>(prop, value));
}

CFX_RetainPtr<CFX_CSSValue> CFX_CSSDeclaration::ParseNumber(
    const wchar_t* pszValue,
    int32_t iValueLen) {
  float fValue;
  CFX_CSSNumberType eUnit;
  if (!ParseCSSNumber(pszValue, iValueLen, fValue, eUnit))
    return nullptr;
  return pdfium::MakeRetain<CFX_CSSNumberValue>(eUnit, fValue);
}

CFX_RetainPtr<CFX_CSSValue> CFX_CSSDeclaration::ParseEnum(
    const wchar_t* pszValue,
    int32_t iValueLen) {
  const CFX_CSSPropertyValueTable* pValue =
      GetCSSPropertyValueByName(CFX_WideStringC(pszValue, iValueLen));
  return pValue ? pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName) : nullptr;
}

CFX_RetainPtr<CFX_CSSValue> CFX_CSSDeclaration::ParseColor(
    const wchar_t* pszValue,
    int32_t iValueLen) {
  FX_ARGB dwColor;
  if (!ParseCSSColor(pszValue, iValueLen, &dwColor))
    return nullptr;
  return pdfium::MakeRetain<CFX_CSSColorValue>(dwColor);
}

CFX_RetainPtr<CFX_CSSValue> CFX_CSSDeclaration::ParseString(
    const wchar_t* pszValue,
    int32_t iValueLen) {
  int32_t iOffset;
  if (!ParseCSSString(pszValue, iValueLen, &iOffset, &iValueLen))
    return nullptr;

  if (iValueLen <= 0)
    return nullptr;

  return pdfium::MakeRetain<CFX_CSSStringValue>(
      CFX_WideString(pszValue + iOffset, iValueLen));
}

void CFX_CSSDeclaration::ParseValueListProperty(
    const CFX_CSSPropertyTable* pTable,
    const wchar_t* pszValue,
    int32_t iValueLen,
    bool bImportant) {
  wchar_t separator =
      (pTable->eName == CFX_CSSProperty::FontFamily) ? ',' : ' ';
  CFX_CSSValueListParser parser(pszValue, iValueLen, separator);

  const uint32_t dwType = pTable->dwType;
  CFX_CSSPrimitiveType eType;
  std::vector<CFX_RetainPtr<CFX_CSSValue>> list;
  while (parser.NextValue(eType, pszValue, iValueLen)) {
    switch (eType) {
      case CFX_CSSPrimitiveType::Number:
        if (dwType & CFX_CSSVALUETYPE_MaybeNumber) {
          float fValue;
          CFX_CSSNumberType eNumType;
          if (ParseCSSNumber(pszValue, iValueLen, fValue, eNumType))
            list.push_back(
                pdfium::MakeRetain<CFX_CSSNumberValue>(eNumType, fValue));
        }
        break;
      case CFX_CSSPrimitiveType::String:
        if (dwType & CFX_CSSVALUETYPE_MaybeColor) {
          FX_ARGB dwColor;
          if (ParseCSSColor(pszValue, iValueLen, &dwColor)) {
            list.push_back(pdfium::MakeRetain<CFX_CSSColorValue>(dwColor));
            continue;
          }
        }
        if (dwType & CFX_CSSVALUETYPE_MaybeEnum) {
          const CFX_CSSPropertyValueTable* pValue =
              GetCSSPropertyValueByName(CFX_WideStringC(pszValue, iValueLen));
          if (pValue) {
            list.push_back(pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName));
            continue;
          }
        }
        if (dwType & CFX_CSSVALUETYPE_MaybeString) {
          list.push_back(pdfium::MakeRetain<CFX_CSSStringValue>(
              CFX_WideString(pszValue, iValueLen)));
        }
        break;
      case CFX_CSSPrimitiveType::RGB:
        if (dwType & CFX_CSSVALUETYPE_MaybeColor) {
          FX_ARGB dwColor;
          if (ParseCSSColor(pszValue, iValueLen, &dwColor)) {
            list.push_back(pdfium::MakeRetain<CFX_CSSColorValue>(dwColor));
          }
        }
        break;
      default:
        break;
    }
  }
  if (list.empty())
    return;

  switch (pTable->eName) {
    case CFX_CSSProperty::BorderWidth:
      Add4ValuesProperty(list, bImportant, CFX_CSSProperty::BorderLeftWidth,
                         CFX_CSSProperty::BorderTopWidth,
                         CFX_CSSProperty::BorderRightWidth,
                         CFX_CSSProperty::BorderBottomWidth);
      return;
    case CFX_CSSProperty::Margin:
      Add4ValuesProperty(list, bImportant, CFX_CSSProperty::MarginLeft,
                         CFX_CSSProperty::MarginTop,
                         CFX_CSSProperty::MarginRight,
                         CFX_CSSProperty::MarginBottom);
      return;
    case CFX_CSSProperty::Padding:
      Add4ValuesProperty(list, bImportant, CFX_CSSProperty::PaddingLeft,
                         CFX_CSSProperty::PaddingTop,
                         CFX_CSSProperty::PaddingRight,
                         CFX_CSSProperty::PaddingBottom);
      return;
    default: {
      auto pList = pdfium::MakeRetain<CFX_CSSValueList>(list);
      AddPropertyHolder(pTable->eName, pList, bImportant);
      return;
    }
  }
}

void CFX_CSSDeclaration::Add4ValuesProperty(
    const std::vector<CFX_RetainPtr<CFX_CSSValue>>& list,
    bool bImportant,
    CFX_CSSProperty eLeft,
    CFX_CSSProperty eTop,
    CFX_CSSProperty eRight,
    CFX_CSSProperty eBottom) {
  switch (list.size()) {
    case 1:
      AddPropertyHolder(eLeft, list[0], bImportant);
      AddPropertyHolder(eTop, list[0], bImportant);
      AddPropertyHolder(eRight, list[0], bImportant);
      AddPropertyHolder(eBottom, list[0], bImportant);
      return;
    case 2:
      AddPropertyHolder(eLeft, list[1], bImportant);
      AddPropertyHolder(eTop, list[0], bImportant);
      AddPropertyHolder(eRight, list[1], bImportant);
      AddPropertyHolder(eBottom, list[0], bImportant);
      return;
    case 3:
      AddPropertyHolder(eLeft, list[1], bImportant);
      AddPropertyHolder(eTop, list[0], bImportant);
      AddPropertyHolder(eRight, list[1], bImportant);
      AddPropertyHolder(eBottom, list[2], bImportant);
      return;
    case 4:
      AddPropertyHolder(eLeft, list[3], bImportant);
      AddPropertyHolder(eTop, list[0], bImportant);
      AddPropertyHolder(eRight, list[1], bImportant);
      AddPropertyHolder(eBottom, list[2], bImportant);
      return;
    default:
      break;
  }
}

bool CFX_CSSDeclaration::ParseBorderProperty(
    const wchar_t* pszValue,
    int32_t iValueLen,
    CFX_RetainPtr<CFX_CSSValue>& pWidth) const {
  pWidth.Reset(nullptr);

  CFX_CSSValueListParser parser(pszValue, iValueLen, ' ');
  CFX_CSSPrimitiveType eType;
  while (parser.NextValue(eType, pszValue, iValueLen)) {
    switch (eType) {
      case CFX_CSSPrimitiveType::Number: {
        if (pWidth)
          continue;

        float fValue;
        CFX_CSSNumberType eNumType;
        if (ParseCSSNumber(pszValue, iValueLen, fValue, eNumType))
          pWidth = pdfium::MakeRetain<CFX_CSSNumberValue>(eNumType, fValue);
        break;
      }
      case CFX_CSSPrimitiveType::String: {
        const CFX_CSSColorTable* pColorItem =
            GetCSSColorByName(CFX_WideStringC(pszValue, iValueLen));
        if (pColorItem)
          continue;

        const CFX_CSSPropertyValueTable* pValue =
            GetCSSPropertyValueByName(CFX_WideStringC(pszValue, iValueLen));
        if (!pValue)
          continue;

        switch (pValue->eName) {
          case CFX_CSSPropertyValue::Thin:
          case CFX_CSSPropertyValue::Thick:
          case CFX_CSSPropertyValue::Medium:
            if (!pWidth)
              pWidth = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
            break;
          default:
            break;
        }
        break;
      }
      default:
        break;
    }
  }
  if (!pWidth)
    pWidth =
        pdfium::MakeRetain<CFX_CSSNumberValue>(CFX_CSSNumberType::Number, 0.0f);

  return true;
}

void CFX_CSSDeclaration::ParseFontProperty(const wchar_t* pszValue,
                                           int32_t iValueLen,
                                           bool bImportant) {
  CFX_CSSValueListParser parser(pszValue, iValueLen, '/');
  CFX_RetainPtr<CFX_CSSValue> pStyle;
  CFX_RetainPtr<CFX_CSSValue> pVariant;
  CFX_RetainPtr<CFX_CSSValue> pWeight;
  CFX_RetainPtr<CFX_CSSValue> pFontSize;
  CFX_RetainPtr<CFX_CSSValue> pLineHeight;
  std::vector<CFX_RetainPtr<CFX_CSSValue>> familyList;
  CFX_CSSPrimitiveType eType;
  while (parser.NextValue(eType, pszValue, iValueLen)) {
    switch (eType) {
      case CFX_CSSPrimitiveType::String: {
        const CFX_CSSPropertyValueTable* pValue =
            GetCSSPropertyValueByName(CFX_WideStringC(pszValue, iValueLen));
        if (pValue) {
          switch (pValue->eName) {
            case CFX_CSSPropertyValue::XxSmall:
            case CFX_CSSPropertyValue::XSmall:
            case CFX_CSSPropertyValue::Small:
            case CFX_CSSPropertyValue::Medium:
            case CFX_CSSPropertyValue::Large:
            case CFX_CSSPropertyValue::XLarge:
            case CFX_CSSPropertyValue::XxLarge:
            case CFX_CSSPropertyValue::Smaller:
            case CFX_CSSPropertyValue::Larger:
              if (!pFontSize)
                pFontSize = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              continue;
            case CFX_CSSPropertyValue::Bold:
            case CFX_CSSPropertyValue::Bolder:
            case CFX_CSSPropertyValue::Lighter:
              if (!pWeight)
                pWeight = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              continue;
            case CFX_CSSPropertyValue::Italic:
            case CFX_CSSPropertyValue::Oblique:
              if (!pStyle)
                pStyle = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              continue;
            case CFX_CSSPropertyValue::SmallCaps:
              if (!pVariant)
                pVariant = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              continue;
            case CFX_CSSPropertyValue::Normal:
              if (!pStyle)
                pStyle = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              else if (!pVariant)
                pVariant = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              else if (!pWeight)
                pWeight = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              else if (!pFontSize)
                pFontSize = pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              else if (!pLineHeight)
                pLineHeight =
                    pdfium::MakeRetain<CFX_CSSEnumValue>(pValue->eName);
              continue;
            default:
              break;
          }
        }
        if (pFontSize) {
          familyList.push_back(pdfium::MakeRetain<CFX_CSSStringValue>(
              CFX_WideString(pszValue, iValueLen)));
        }
        parser.m_Separator = ',';
        break;
      }
      case CFX_CSSPrimitiveType::Number: {
        float fValue;
        CFX_CSSNumberType eNumType;
        if (!ParseCSSNumber(pszValue, iValueLen, fValue, eNumType))
          break;
        if (eType == CFX_CSSPrimitiveType::Number) {
          switch ((int32_t)fValue) {
            case 100:
            case 200:
            case 300:
            case 400:
            case 500:
            case 600:
            case 700:
            case 800:
            case 900:
              if (!pWeight)
                pWeight = pdfium::MakeRetain<CFX_CSSNumberValue>(
                    CFX_CSSNumberType::Number, fValue);
              continue;
          }
        }
        if (!pFontSize)
          pFontSize = pdfium::MakeRetain<CFX_CSSNumberValue>(eNumType, fValue);
        else if (!pLineHeight)
          pLineHeight =
              pdfium::MakeRetain<CFX_CSSNumberValue>(eNumType, fValue);
        break;
      }
      default:
        break;
    }
  }

  if (!pStyle) {
    pStyle = pdfium::MakeRetain<CFX_CSSEnumValue>(CFX_CSSPropertyValue::Normal);
  }
  if (!pVariant) {
    pVariant =
        pdfium::MakeRetain<CFX_CSSEnumValue>(CFX_CSSPropertyValue::Normal);
  }
  if (!pWeight) {
    pWeight =
        pdfium::MakeRetain<CFX_CSSEnumValue>(CFX_CSSPropertyValue::Normal);
  }
  if (!pFontSize) {
    pFontSize =
        pdfium::MakeRetain<CFX_CSSEnumValue>(CFX_CSSPropertyValue::Medium);
  }
  if (!pLineHeight) {
    pLineHeight =
        pdfium::MakeRetain<CFX_CSSEnumValue>(CFX_CSSPropertyValue::Normal);
  }

  AddPropertyHolder(CFX_CSSProperty::FontStyle, pStyle, bImportant);
  AddPropertyHolder(CFX_CSSProperty::FontVariant, pVariant, bImportant);
  AddPropertyHolder(CFX_CSSProperty::FontWeight, pWeight, bImportant);
  AddPropertyHolder(CFX_CSSProperty::FontSize, pFontSize, bImportant);
  AddPropertyHolder(CFX_CSSProperty::LineHeight, pLineHeight, bImportant);
  if (!familyList.empty()) {
    auto pList = pdfium::MakeRetain<CFX_CSSValueList>(familyList);
    AddPropertyHolder(CFX_CSSProperty::FontFamily, pList, bImportant);
  }
}

size_t CFX_CSSDeclaration::PropertyCountForTesting() const {
  return properties_.size();
}

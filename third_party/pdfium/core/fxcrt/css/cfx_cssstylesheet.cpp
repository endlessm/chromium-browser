// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxcrt/css/cfx_cssstylesheet.h"

#include <utility>

#include "core/fxcrt/css/cfx_cssdatatable.h"
#include "core/fxcrt/css/cfx_cssdeclaration.h"
#include "core/fxcrt/css/cfx_cssstylerule.h"
#include "core/fxcrt/fx_codepage.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"

CFX_CSSStyleSheet::CFX_CSSStyleSheet() {}

CFX_CSSStyleSheet::~CFX_CSSStyleSheet() {
  Reset();
}

void CFX_CSSStyleSheet::Reset() {
  m_RuleArray.clear();
  m_StringCache.clear();
}

int32_t CFX_CSSStyleSheet::CountRules() const {
  return pdfium::CollectionSize<int32_t>(m_RuleArray);
}

CFX_CSSStyleRule* CFX_CSSStyleSheet::GetRule(int32_t index) const {
  return m_RuleArray[index].get();
}

bool CFX_CSSStyleSheet::LoadBuffer(const wchar_t* pBuffer, int32_t iBufSize) {
  ASSERT(pBuffer);
  ASSERT(iBufSize > 0);

  auto pSyntax = pdfium::MakeUnique<CFX_CSSSyntaxParser>(pBuffer, iBufSize);
  Reset();
  CFX_CSSSyntaxStatus eStatus;
  do {
    switch (eStatus = pSyntax->DoSyntaxParse()) {
      case CFX_CSSSyntaxStatus::StyleRule:
        eStatus = LoadStyleRule(pSyntax.get(), &m_RuleArray);
        break;
      default:
        break;
    }
  } while (eStatus >= CFX_CSSSyntaxStatus::None);

  m_StringCache.clear();
  return eStatus != CFX_CSSSyntaxStatus::Error;
}

CFX_CSSSyntaxStatus CFX_CSSStyleSheet::LoadStyleRule(
    CFX_CSSSyntaxParser* pSyntax,
    std::vector<std::unique_ptr<CFX_CSSStyleRule>>* ruleArray) {
  std::vector<std::unique_ptr<CFX_CSSSelector>> selectors;

  CFX_CSSStyleRule* pStyleRule = nullptr;
  int32_t iValueLen = 0;
  const CFX_CSSPropertyTable* propertyTable = nullptr;
  CFX_WideString wsName;
  while (1) {
    switch (pSyntax->DoSyntaxParse()) {
      case CFX_CSSSyntaxStatus::Selector: {
        CFX_WideStringC strValue = pSyntax->GetCurrentString();
        auto pSelector = CFX_CSSSelector::FromString(strValue);
        if (pSelector)
          selectors.push_back(std::move(pSelector));
        break;
      }
      case CFX_CSSSyntaxStatus::PropertyName: {
        CFX_WideStringC strValue = pSyntax->GetCurrentString();
        propertyTable = CFX_GetCSSPropertyByName(strValue);
        if (!propertyTable)
          wsName = CFX_WideString(strValue);
        break;
      }
      case CFX_CSSSyntaxStatus::PropertyValue: {
        if (propertyTable || iValueLen > 0) {
          CFX_WideStringC strValue = pSyntax->GetCurrentString();
          auto* decl = pStyleRule->GetDeclaration();
          if (!strValue.IsEmpty()) {
            if (propertyTable) {
              decl->AddProperty(propertyTable, strValue);
            } else {
              decl->AddProperty(wsName, CFX_WideString(strValue));
            }
          }
        }
        break;
      }
      case CFX_CSSSyntaxStatus::DeclOpen: {
        if (!pStyleRule && !selectors.empty()) {
          auto rule = pdfium::MakeUnique<CFX_CSSStyleRule>();
          pStyleRule = rule.get();
          pStyleRule->SetSelector(&selectors);
          ruleArray->push_back(std::move(rule));
        } else {
          SkipRuleSet(pSyntax);
          return CFX_CSSSyntaxStatus::None;
        }
        break;
      }
      case CFX_CSSSyntaxStatus::DeclClose: {
        if (pStyleRule && pStyleRule->GetDeclaration()->empty()) {
          ruleArray->pop_back();
          pStyleRule = nullptr;
        }
        return CFX_CSSSyntaxStatus::None;
      }
      case CFX_CSSSyntaxStatus::EOS:
        return CFX_CSSSyntaxStatus::EOS;
      case CFX_CSSSyntaxStatus::Error:
      default:
        return CFX_CSSSyntaxStatus::Error;
    }
  }
}

void CFX_CSSStyleSheet::SkipRuleSet(CFX_CSSSyntaxParser* pSyntax) {
  while (1) {
    switch (pSyntax->DoSyntaxParse()) {
      case CFX_CSSSyntaxStatus::Selector:
      case CFX_CSSSyntaxStatus::DeclOpen:
      case CFX_CSSSyntaxStatus::PropertyName:
      case CFX_CSSSyntaxStatus::PropertyValue:
        break;
      case CFX_CSSSyntaxStatus::DeclClose:
      case CFX_CSSSyntaxStatus::EOS:
      case CFX_CSSSyntaxStatus::Error:
      default:
        return;
    }
  }
}

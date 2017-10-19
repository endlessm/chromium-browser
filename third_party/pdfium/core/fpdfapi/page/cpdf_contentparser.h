// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_PAGE_CPDF_CONTENTPARSER_H_
#define CORE_FPDFAPI_PAGE_CPDF_CONTENTPARSER_H_

#include <memory>
#include <set>
#include <vector>

#include "core/fpdfapi/page/cpdf_pageobjectholder.h"
#include "core/fpdfapi/page/cpdf_streamcontentparser.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxcrt/cfx_unowned_ptr.h"

class CPDF_AllStates;
class CPDF_Form;
class CPDF_Page;
class CPDF_StreamAcc;
class CPDF_Type3Char;

class CPDF_ContentParser {
 public:
  enum ParseStatus { Ready, ToBeContinued, Done };

  CPDF_ContentParser();
  ~CPDF_ContentParser();

  ParseStatus GetStatus() const { return m_Status; }
  const CPDF_AllStates* GetCurStates() const {
    return m_pParser ? m_pParser->GetCurStates() : nullptr;
  }
  void Start(CPDF_Page* pPage);
  void Start(CPDF_Form* pForm,
             CPDF_AllStates* pGraphicStates,
             const CFX_Matrix* pParentMatrix,
             CPDF_Type3Char* pType3Char,
             std::set<const uint8_t*>* parsedSet);
  void Continue(IFX_PauseIndicator* pPause);

 private:
  enum InternalStage {
    STAGE_GETCONTENT = 1,
    STAGE_PARSE,
    STAGE_CHECKCLIP,
  };

  ParseStatus m_Status;
  InternalStage m_InternalStage;
  CFX_UnownedPtr<CPDF_PageObjectHolder> m_pObjectHolder;
  bool m_bForm;
  CFX_UnownedPtr<CPDF_Type3Char> m_pType3Char;
  uint32_t m_nStreams;
  CFX_RetainPtr<CPDF_StreamAcc> m_pSingleStream;
  std::vector<CFX_RetainPtr<CPDF_StreamAcc>> m_StreamArray;
  uint8_t* m_pData;
  uint32_t m_Size;
  uint32_t m_CurrentOffset;
  std::unique_ptr<std::set<const uint8_t*>> m_parsedSet;
  // m_pParser has a reference to m_parsedSet, so must be below and thus
  // destroyed first.
  std::unique_ptr<CPDF_StreamContentParser> m_pParser;
};

#endif  // CORE_FPDFAPI_PAGE_CPDF_CONTENTPARSER_H_

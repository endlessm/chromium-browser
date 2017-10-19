// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FXFA_PARSER_CSCRIPT_LAYOUTPSEUDOMODEL_H_
#define XFA_FXFA_PARSER_CSCRIPT_LAYOUTPSEUDOMODEL_H_

#include <vector>

#include "xfa/fxfa/parser/cxfa_object.h"

enum XFA_LAYOUTMODEL_HWXY {
  XFA_LAYOUTMODEL_H,
  XFA_LAYOUTMODEL_W,
  XFA_LAYOUTMODEL_X,
  XFA_LAYOUTMODEL_Y
};

class CFXJSE_Arguments;
class CXFA_LayoutProcessor;

class CScript_LayoutPseudoModel : public CXFA_Object {
 public:
  explicit CScript_LayoutPseudoModel(CXFA_Document* pDocument);
  ~CScript_LayoutPseudoModel() override;

  void Ready(CFXJSE_Value* pValue, bool bSetting, XFA_ATTRIBUTE eAttribute);

  void HWXY(CFXJSE_Arguments* pArguments, XFA_LAYOUTMODEL_HWXY layoutModel);
  void H(CFXJSE_Arguments* pArguments);
  void W(CFXJSE_Arguments* pArguments);
  void X(CFXJSE_Arguments* pArguments);
  void Y(CFXJSE_Arguments* pArguments);
  void NumberedPageCount(CFXJSE_Arguments* pArguments, bool bNumbered);
  void PageCount(CFXJSE_Arguments* pArguments);
  void PageSpan(CFXJSE_Arguments* pArguments);
  void Page(CFXJSE_Arguments* pArguments);
  void PageContent(CFXJSE_Arguments* pArguments);
  void AbsPageCount(CFXJSE_Arguments* pArguments);
  void AbsPageCountInBatch(CFXJSE_Arguments* pArguments);
  void SheetCountInBatch(CFXJSE_Arguments* pArguments);
  void Relayout(CFXJSE_Arguments* pArguments);
  void AbsPageSpan(CFXJSE_Arguments* pArguments);
  void AbsPageInBatch(CFXJSE_Arguments* pArguments);
  void SheetInBatch(CFXJSE_Arguments* pArguments);
  void Sheet(CFXJSE_Arguments* pArguments);
  void RelayoutPageArea(CFXJSE_Arguments* pArguments);
  void SheetCount(CFXJSE_Arguments* pArguments);
  void AbsPage(CFXJSE_Arguments* pArguments);

 private:
  std::vector<CXFA_Node*> GetObjArray(CXFA_LayoutProcessor* pDocLayout,
                                      int32_t iPageNo,
                                      const CFX_WideString& wsType,
                                      bool bOnPageArea);

  void PageInternals(CFXJSE_Arguments* pArguments, bool bAbsPage);
  void ThrowSetReadyException() const;
};

#endif  // XFA_FXFA_PARSER_CSCRIPT_LAYOUTPSEUDOMODEL_H_

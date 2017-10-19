// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FXFA_CXFA_FFTEXTEDIT_H_
#define XFA_FXFA_CXFA_FFTEXTEDIT_H_

#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_string.h"
#include "xfa/fxfa/cxfa_fffield.h"

class CFWL_Event;
class CFWL_Widget;
class CFX_Matrix;
class CXFA_FFWidget;
class CXFA_WidgetAcc;
class IFWL_WidgetDelegate;

class CXFA_FFTextEdit : public CXFA_FFField {
 public:
  explicit CXFA_FFTextEdit(CXFA_WidgetAcc* pDataAcc);
  ~CXFA_FFTextEdit() override;

  // CXFA_FFField
  bool LoadWidget() override;
  void UpdateWidgetProperty() override;
  bool OnLButtonDown(uint32_t dwFlags, const CFX_PointF& point) override;
  bool OnRButtonDown(uint32_t dwFlags, const CFX_PointF& point) override;
  bool OnRButtonUp(uint32_t dwFlags, const CFX_PointF& point) override;
  bool OnSetFocus(CXFA_FFWidget* pOldWidget) override;
  bool OnKillFocus(CXFA_FFWidget* pNewWidget) override;
  void OnProcessMessage(CFWL_Message* pMessage) override;
  void OnProcessEvent(CFWL_Event* pEvent) override;
  void OnDrawWidget(CXFA_Graphics* pGraphics,
                    const CFX_Matrix& matrix) override;

  void OnTextChanged(CFWL_Widget* pWidget,
                     const CFX_WideString& wsChanged,
                     const CFX_WideString& wsPrevText);
  void OnTextFull(CFWL_Widget* pWidget);
  bool CheckWord(const CFX_ByteStringC& sWord);

 protected:
  uint32_t GetAlignment();

  IFWL_WidgetDelegate* m_pOldDelegate;

 private:
  bool CommitData() override;
  bool UpdateFWLData() override;
  bool IsDataChanged() override;
  void ValidateNumberField(const CFX_WideString& wsText);
};

#endif  // XFA_FXFA_CXFA_FFTEXTEDIT_H_

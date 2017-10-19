// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffpushbutton.h"

#include <utility>

#include "third_party/base/ptr_util.h"
#include "xfa/fwl/cfwl_notedriver.h"
#include "xfa/fwl/cfwl_pushbutton.h"
#include "xfa/fwl/cfwl_widgetmgr.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_fffield.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_textlayout.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxgraphics/cxfa_color.h"
#include "xfa/fxgraphics/cxfa_path.h"

CXFA_FFPushButton::CXFA_FFPushButton(CXFA_WidgetAcc* pDataAcc)
    : CXFA_FFField(pDataAcc), m_pOldDelegate(nullptr) {}

CXFA_FFPushButton::~CXFA_FFPushButton() {
  CXFA_FFPushButton::UnloadWidget();
}

void CXFA_FFPushButton::RenderWidget(CXFA_Graphics* pGS,
                                     const CFX_Matrix& matrix,
                                     uint32_t dwStatus) {
  if (!IsMatchVisibleStatus(dwStatus))
    return;

  CFX_Matrix mtRotate = GetRotateMatrix();
  mtRotate.Concat(matrix);

  CXFA_FFWidget::RenderWidget(pGS, mtRotate, dwStatus);
  RenderHighlightCaption(pGS, &mtRotate);

  CFX_RectF rtWidget = GetRectWithoutRotate();
  CFX_Matrix mt(1, 0, 0, 1, rtWidget.left, rtWidget.top);
  mt.Concat(mtRotate);
  GetApp()->GetWidgetMgr()->OnDrawWidget(m_pNormalWidget.get(), pGS, mt);
}

bool CXFA_FFPushButton::LoadWidget() {
  ASSERT(!m_pNormalWidget);
  auto pNew = pdfium::MakeUnique<CFWL_PushButton>(GetFWLApp());
  CFWL_PushButton* pPushButton = pNew.get();
  m_pOldDelegate = pPushButton->GetDelegate();
  pPushButton->SetDelegate(this);
  m_pNormalWidget = std::move(pNew);
  m_pNormalWidget->SetLayoutItem(this);

  CFWL_NoteDriver* pNoteDriver =
      m_pNormalWidget->GetOwnerApp()->GetNoteDriver();
  pNoteDriver->RegisterEventTarget(m_pNormalWidget.get(),
                                   m_pNormalWidget.get());
  m_pNormalWidget->LockUpdate();
  UpdateWidgetProperty();
  LoadHighlightCaption();
  m_pNormalWidget->UnlockUpdate();
  return CXFA_FFField::LoadWidget();
}

void CXFA_FFPushButton::UpdateWidgetProperty() {
  uint32_t dwStyleEx = 0;
  switch (m_pDataAcc->GetButtonHighlight()) {
    case XFA_ATTRIBUTEENUM_Inverted:
      dwStyleEx = XFA_FWL_PSBSTYLEEXT_HiliteInverted;
      break;
    case XFA_ATTRIBUTEENUM_Outline:
      dwStyleEx = XFA_FWL_PSBSTYLEEXT_HiliteOutLine;
      break;
    case XFA_ATTRIBUTEENUM_Push:
      dwStyleEx = XFA_FWL_PSBSTYLEEXT_HilitePush;
      break;
    default:
      break;
  }
  m_pNormalWidget->ModifyStylesEx(dwStyleEx, 0xFFFFFFFF);
}

void CXFA_FFPushButton::UnloadWidget() {
  m_pRolloverTextLayout.reset();
  m_pDownTextLayout.reset();
  m_pRollProvider.reset();
  m_pDownProvider.reset();
  CXFA_FFField::UnloadWidget();
}

bool CXFA_FFPushButton::PerformLayout() {
  CXFA_FFWidget::PerformLayout();
  CFX_RectF rtWidget = GetRectWithoutRotate();

  m_rtUI = rtWidget;
  if (CXFA_Margin mgWidget = m_pDataAcc->GetMargin())
    XFA_RectWidthoutMargin(rtWidget, mgWidget);

  CXFA_Caption caption = m_pDataAcc->GetCaption();
  m_rtCaption = rtWidget;
  if (CXFA_Margin mgCap = caption.GetMargin())
    XFA_RectWidthoutMargin(m_rtCaption, mgCap);

  LayoutHighlightCaption();
  SetFWLRect();
  if (m_pNormalWidget)
    m_pNormalWidget->Update();

  return true;
}
float CXFA_FFPushButton::GetLineWidth() {
  CXFA_Border border = m_pDataAcc->GetBorder(false);
  if (border && border.GetPresence() == XFA_ATTRIBUTEENUM_Visible) {
    CXFA_Edge edge = border.GetEdge(0);
    return edge.GetThickness();
  }
  return 0;
}

FX_ARGB CXFA_FFPushButton::GetLineColor() {
  return 0xFF000000;
}

FX_ARGB CXFA_FFPushButton::GetFillColor() {
  return 0xFFFFFFFF;
}

void CXFA_FFPushButton::LoadHighlightCaption() {
  CXFA_Caption caption = m_pDataAcc->GetCaption();
  if (!caption || caption.GetPresence() == XFA_ATTRIBUTEENUM_Hidden)
    return;

  bool bRichText;
  CFX_WideString wsRollover;
  if (m_pDataAcc->GetButtonRollover(wsRollover, bRichText)) {
    if (!m_pRollProvider) {
      m_pRollProvider = pdfium::MakeUnique<CXFA_TextProvider>(
          m_pDataAcc.Get(), XFA_TEXTPROVIDERTYPE_Rollover);
    }
    m_pRolloverTextLayout =
        pdfium::MakeUnique<CXFA_TextLayout>(m_pRollProvider.get());
  }
  CFX_WideString wsDown;
  if (m_pDataAcc->GetButtonDown(wsDown, bRichText)) {
    if (!m_pDownProvider) {
      m_pDownProvider = pdfium::MakeUnique<CXFA_TextProvider>(
          m_pDataAcc.Get(), XFA_TEXTPROVIDERTYPE_Down);
    }
    m_pDownTextLayout =
        pdfium::MakeUnique<CXFA_TextLayout>(m_pDownProvider.get());
  }
}

void CXFA_FFPushButton::LayoutHighlightCaption() {
  CFX_SizeF sz(m_rtCaption.width, m_rtCaption.height);
  LayoutCaption();
  if (m_pRolloverTextLayout)
    m_pRolloverTextLayout->Layout(sz);
  if (m_pDownTextLayout)
    m_pDownTextLayout->Layout(sz);
}

void CXFA_FFPushButton::RenderHighlightCaption(CXFA_Graphics* pGS,
                                               CFX_Matrix* pMatrix) {
  CXFA_TextLayout* pCapTextLayout = m_pDataAcc->GetCaptionTextLayout();
  CXFA_Caption caption = m_pDataAcc->GetCaption();
  if (!caption || caption.GetPresence() != XFA_ATTRIBUTEENUM_Visible)
    return;

  CFX_RenderDevice* pRenderDevice = pGS->GetRenderDevice();
  CFX_RectF rtClip = m_rtCaption;
  rtClip.Intersect(GetRectWithoutRotate());
  CFX_Matrix mt(1, 0, 0, 1, m_rtCaption.left, m_rtCaption.top);
  if (pMatrix) {
    rtClip = pMatrix->TransformRect(rtClip);
    mt.Concat(*pMatrix);
  }

  uint32_t dwState = m_pNormalWidget->GetStates();
  if (m_pDownTextLayout && (dwState & FWL_STATE_PSB_Pressed) &&
      (dwState & FWL_STATE_PSB_Hovered)) {
    if (m_pDownTextLayout->DrawString(pRenderDevice, mt, rtClip))
      return;
  } else if (m_pRolloverTextLayout && (dwState & FWL_STATE_PSB_Hovered)) {
    if (m_pRolloverTextLayout->DrawString(pRenderDevice, mt, rtClip))
      return;
  }

  if (pCapTextLayout)
    pCapTextLayout->DrawString(pRenderDevice, mt, rtClip);
}

void CXFA_FFPushButton::OnProcessMessage(CFWL_Message* pMessage) {
  m_pOldDelegate->OnProcessMessage(pMessage);
}

void CXFA_FFPushButton::OnProcessEvent(CFWL_Event* pEvent) {
  m_pOldDelegate->OnProcessEvent(pEvent);
  CXFA_FFField::OnProcessEvent(pEvent);
}

void CXFA_FFPushButton::OnDrawWidget(CXFA_Graphics* pGraphics,
                                     const CFX_Matrix& matrix) {
  if (m_pNormalWidget->GetStylesEx() & XFA_FWL_PSBSTYLEEXT_HiliteInverted) {
    if ((m_pNormalWidget->GetStates() & FWL_STATE_PSB_Pressed) &&
        (m_pNormalWidget->GetStates() & FWL_STATE_PSB_Hovered)) {
      CFX_RectF rtFill(0, 0, m_pNormalWidget->GetWidgetRect().Size());
      float fLineWith = GetLineWidth();
      rtFill.Deflate(fLineWith, fLineWith);
      CXFA_Path path;
      path.AddRectangle(rtFill.left, rtFill.top, rtFill.width, rtFill.height);
      pGraphics->SetFillColor(CXFA_Color(FXARGB_MAKE(128, 128, 255, 255)));
      pGraphics->FillPath(&path, FXFILL_WINDING, &matrix);
    }
    return;
  }

  if (m_pNormalWidget->GetStylesEx() & XFA_FWL_PSBSTYLEEXT_HiliteOutLine) {
    if ((m_pNormalWidget->GetStates() & FWL_STATE_PSB_Pressed) &&
        (m_pNormalWidget->GetStates() & FWL_STATE_PSB_Hovered)) {
      float fLineWidth = GetLineWidth();
      pGraphics->SetStrokeColor(CXFA_Color(FXARGB_MAKE(255, 128, 255, 255)));
      pGraphics->SetLineWidth(fLineWidth);

      CXFA_Path path;
      CFX_RectF rect = m_pNormalWidget->GetWidgetRect();
      path.AddRectangle(0, 0, rect.width, rect.height);
      pGraphics->StrokePath(&path, &matrix);
    }
  }
}

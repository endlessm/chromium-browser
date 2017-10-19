// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_fffield.h"

#include "xfa/fwl/cfwl_edit.h"
#include "xfa/fwl/cfwl_eventmouse.h"
#include "xfa/fwl/cfwl_messagekey.h"
#include "xfa/fwl/cfwl_messagekillfocus.h"
#include "xfa/fwl/cfwl_messagemouse.h"
#include "xfa/fwl/cfwl_messagemousewheel.h"
#include "xfa/fwl/cfwl_messagesetfocus.h"
#include "xfa/fwl/cfwl_picturebox.h"
#include "xfa/fwl/cfwl_widgetmgr.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffdocview.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_fwltheme.h"
#include "xfa/fxfa/cxfa_textlayout.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxgraphics/cxfa_color.h"
#include "xfa/fxgraphics/cxfa_path.h"

namespace {

CXFA_FFField* ToField(CXFA_LayoutItem* widget) {
  return static_cast<CXFA_FFField*>(widget);
}

}  // namespace

CXFA_FFField::CXFA_FFField(CXFA_WidgetAcc* pDataAcc)
    : CXFA_FFWidget(pDataAcc), m_pNormalWidget(nullptr) {}

CXFA_FFField::~CXFA_FFField() {
  CXFA_FFField::UnloadWidget();
}

CFX_RectF CXFA_FFField::GetBBox(uint32_t dwStatus, bool bDrawFocus) {
  if (!bDrawFocus)
    return CXFA_FFWidget::GetBBox(dwStatus);

  XFA_Element type = m_pDataAcc->GetUIType();
  if (type != XFA_Element::Button && type != XFA_Element::CheckButton &&
      type != XFA_Element::ImageEdit && type != XFA_Element::Signature &&
      type != XFA_Element::ChoiceList) {
    return CFX_RectF();
  }

  return GetRotateMatrix().TransformRect(m_rtUI);
}

void CXFA_FFField::RenderWidget(CXFA_Graphics* pGS,
                                const CFX_Matrix& matrix,
                                uint32_t dwStatus) {
  if (!IsMatchVisibleStatus(dwStatus))
    return;

  CFX_Matrix mtRotate = GetRotateMatrix();
  mtRotate.Concat(matrix);

  CXFA_FFWidget::RenderWidget(pGS, mtRotate, dwStatus);
  CXFA_Border borderUI = m_pDataAcc->GetUIBorder();
  DrawBorder(pGS, borderUI, m_rtUI, mtRotate);
  RenderCaption(pGS, &mtRotate);
  DrawHighlight(pGS, &mtRotate, dwStatus, false);

  CFX_RectF rtWidget = m_pNormalWidget->GetWidgetRect();
  CFX_Matrix mt(1, 0, 0, 1, rtWidget.left, rtWidget.top);
  mt.Concat(mtRotate);
  GetApp()->GetWidgetMgr()->OnDrawWidget(m_pNormalWidget.get(), pGS, mt);
}

void CXFA_FFField::DrawHighlight(CXFA_Graphics* pGS,
                                 CFX_Matrix* pMatrix,
                                 uint32_t dwStatus,
                                 bool bEllipse) {
  if (m_rtUI.IsEmpty() || !m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive())
    return;

  if (!(dwStatus & XFA_WidgetStatus_Highlight) ||
      m_pDataAcc->GetAccess() != XFA_ATTRIBUTEENUM_Open) {
    return;
  }

  CXFA_FFDoc* pDoc = GetDoc();
  pGS->SetFillColor(
      CXFA_Color(pDoc->GetDocEnvironment()->GetHighlightColor(pDoc)));
  CXFA_Path path;
  if (bEllipse)
    path.AddEllipse(m_rtUI);
  else
    path.AddRectangle(m_rtUI.left, m_rtUI.top, m_rtUI.width, m_rtUI.height);

  pGS->FillPath(&path, FXFILL_WINDING, pMatrix);
}

void CXFA_FFField::DrawFocus(CXFA_Graphics* pGS, CFX_Matrix* pMatrix) {
  if (!(m_dwStatus & XFA_WidgetStatus_Focused))
    return;

  pGS->SetStrokeColor(CXFA_Color(0xFF000000));

  float DashPattern[2] = {1, 1};
  pGS->SetLineDash(0.0f, DashPattern, 2);
  pGS->SetLineWidth(0);

  CXFA_Path path;
  path.AddRectangle(m_rtUI.left, m_rtUI.top, m_rtUI.width, m_rtUI.height);
  pGS->StrokePath(&path, pMatrix);
}

void CXFA_FFField::SetFWLThemeProvider() {
  if (m_pNormalWidget)
    m_pNormalWidget->SetThemeProvider(GetApp()->GetFWLTheme());
}

bool CXFA_FFField::IsLoaded() {
  return m_pNormalWidget && CXFA_FFWidget::IsLoaded();
}

bool CXFA_FFField::LoadWidget() {
  SetFWLThemeProvider();
  m_pDataAcc->LoadCaption();
  PerformLayout();
  return true;
}

void CXFA_FFField::UnloadWidget() {
  m_pNormalWidget.reset();
}

void CXFA_FFField::SetEditScrollOffset() {
  XFA_Element eType = m_pDataAcc->GetUIType();
  if (eType != XFA_Element::TextEdit && eType != XFA_Element::NumericEdit &&
      eType != XFA_Element::PasswordEdit) {
    return;
  }

  float fScrollOffset = 0;
  CXFA_FFField* pPrev = ToField(GetPrev());
  if (pPrev) {
    CFX_RectF rtMargin = m_pDataAcc->GetUIMargin();
    fScrollOffset = -rtMargin.top;
  }

  while (pPrev) {
    fScrollOffset += pPrev->m_rtUI.height;
    pPrev = ToField(pPrev->GetPrev());
  }
  static_cast<CFWL_Edit*>(m_pNormalWidget.get())
      ->SetScrollOffset(fScrollOffset);
}

bool CXFA_FFField::PerformLayout() {
  CXFA_FFWidget::PerformLayout();
  CapPlacement();
  LayoutCaption();
  SetFWLRect();
  SetEditScrollOffset();
  if (m_pNormalWidget)
    m_pNormalWidget->Update();
  return true;
}

void CXFA_FFField::CapPlacement() {
  CFX_RectF rtWidget = GetRectWithoutRotate();
  CXFA_Margin mgWidget = m_pDataAcc->GetMargin();
  if (mgWidget) {
    CXFA_LayoutItem* pItem = this;
    float fLeftInset = 0, fRightInset = 0, fTopInset = 0, fBottomInset = 0;
    mgWidget.GetLeftInset(fLeftInset);
    mgWidget.GetRightInset(fRightInset);
    mgWidget.GetTopInset(fTopInset);
    mgWidget.GetBottomInset(fBottomInset);
    if (!pItem->GetPrev() && !pItem->GetNext()) {
      rtWidget.Deflate(fLeftInset, fTopInset, fRightInset, fBottomInset);
    } else {
      if (!pItem->GetPrev())
        rtWidget.Deflate(fLeftInset, fTopInset, fRightInset, 0);
      else if (!pItem->GetNext())
        rtWidget.Deflate(fLeftInset, 0, fRightInset, fBottomInset);
      else
        rtWidget.Deflate(fLeftInset, 0, fRightInset, 0);
    }
  }

  XFA_ATTRIBUTEENUM iCapPlacement = XFA_ATTRIBUTEENUM_Unknown;
  float fCapReserve = 0;
  CXFA_Caption caption = m_pDataAcc->GetCaption();
  if (caption && caption.GetPresence() != XFA_ATTRIBUTEENUM_Hidden) {
    iCapPlacement = (XFA_ATTRIBUTEENUM)caption.GetPlacementType();
    if (iCapPlacement == XFA_ATTRIBUTEENUM_Top && GetPrev()) {
      m_rtCaption.Reset();
    } else if (iCapPlacement == XFA_ATTRIBUTEENUM_Bottom && GetNext()) {
      m_rtCaption.Reset();
    } else {
      fCapReserve = caption.GetReserve();
      CXFA_LayoutItem* pItem = this;
      if (!pItem->GetPrev() && !pItem->GetNext()) {
        m_rtCaption = rtWidget;
      } else {
        pItem = pItem->GetFirst();
        m_rtCaption = pItem->GetRect(false);
        pItem = pItem->GetNext();
        while (pItem) {
          m_rtCaption.height += pItem->GetRect(false).Height();
          pItem = pItem->GetNext();
        }
        XFA_RectWidthoutMargin(m_rtCaption, mgWidget);
      }

      CXFA_TextLayout* pCapTextLayout = m_pDataAcc->GetCaptionTextLayout();
      if (fCapReserve <= 0 && pCapTextLayout) {
        CFX_SizeF size;
        CFX_SizeF minSize;
        CFX_SizeF maxSize;
        pCapTextLayout->CalcSize(minSize, maxSize, size);
        if (iCapPlacement == XFA_ATTRIBUTEENUM_Top ||
            iCapPlacement == XFA_ATTRIBUTEENUM_Bottom) {
          fCapReserve = size.height;
        } else {
          fCapReserve = size.width;
        }
      }
    }
  }

  m_rtUI = rtWidget;
  switch (iCapPlacement) {
    case XFA_ATTRIBUTEENUM_Left: {
      m_rtCaption.width = fCapReserve;
      CapLeftRightPlacement(caption, rtWidget, iCapPlacement);
      m_rtUI.width -= fCapReserve;
      m_rtUI.left += fCapReserve;
      break;
    }
    case XFA_ATTRIBUTEENUM_Top: {
      m_rtCaption.height = fCapReserve;
      CapTopBottomPlacement(caption, rtWidget, iCapPlacement);
      m_rtUI.top += fCapReserve;
      m_rtUI.height -= fCapReserve;
      break;
    }
    case XFA_ATTRIBUTEENUM_Right: {
      m_rtCaption.left = m_rtCaption.right() - fCapReserve;
      m_rtCaption.width = fCapReserve;
      CapLeftRightPlacement(caption, rtWidget, iCapPlacement);
      m_rtUI.width -= fCapReserve;
      break;
    }
    case XFA_ATTRIBUTEENUM_Bottom: {
      m_rtCaption.top = m_rtCaption.bottom() - fCapReserve;
      m_rtCaption.height = fCapReserve;
      CapTopBottomPlacement(caption, rtWidget, iCapPlacement);
      m_rtUI.height -= fCapReserve;
      break;
    }
    case XFA_ATTRIBUTEENUM_Inline:
      break;
    default:
      break;
  }

  CXFA_Border borderUI = m_pDataAcc->GetUIBorder();
  if (borderUI) {
    CXFA_Margin margin = borderUI.GetMargin();
    if (margin)
      XFA_RectWidthoutMargin(m_rtUI, margin);
  }
  m_rtUI.Normalize();
}

void CXFA_FFField::CapTopBottomPlacement(CXFA_Caption caption,
                                         const CFX_RectF& rtWidget,
                                         int32_t iCapPlacement) {
  CFX_RectF rtUIMargin = m_pDataAcc->GetUIMargin();
  m_rtCaption.left += rtUIMargin.left;
  if (CXFA_Margin mgCap = caption.GetMargin()) {
    XFA_RectWidthoutMargin(m_rtCaption, mgCap);
    if (m_rtCaption.height < 0)
      m_rtCaption.top += m_rtCaption.height;
  }

  float fWidth = rtUIMargin.left + rtUIMargin.width;
  float fHeight = m_rtCaption.height + rtUIMargin.top + rtUIMargin.height;
  if (fWidth > rtWidget.width)
    m_rtUI.width += fWidth - rtWidget.width;

  if (fHeight == XFA_DEFAULTUI_HEIGHT && m_rtUI.height < XFA_MINUI_HEIGHT) {
    m_rtUI.height = XFA_MINUI_HEIGHT;
    m_rtCaption.top += rtUIMargin.top + rtUIMargin.height;
  } else if (fHeight > rtWidget.height) {
    m_rtUI.height += fHeight - rtWidget.height;
    if (iCapPlacement == XFA_ATTRIBUTEENUM_Bottom)
      m_rtCaption.top += fHeight - rtWidget.height;
  }
}

void CXFA_FFField::CapLeftRightPlacement(CXFA_Caption caption,
                                         const CFX_RectF& rtWidget,
                                         int32_t iCapPlacement) {
  CFX_RectF rtUIMargin = m_pDataAcc->GetUIMargin();
  m_rtCaption.top += rtUIMargin.top;
  m_rtCaption.height -= rtUIMargin.top;
  if (CXFA_Margin mgCap = caption.GetMargin()) {
    XFA_RectWidthoutMargin(m_rtCaption, mgCap);
    if (m_rtCaption.height < 0)
      m_rtCaption.top += m_rtCaption.height;
  }

  float fWidth = m_rtCaption.width + rtUIMargin.left + rtUIMargin.width;
  float fHeight = rtUIMargin.top + rtUIMargin.height;
  if (fWidth > rtWidget.width) {
    m_rtUI.width += fWidth - rtWidget.width;
    if (iCapPlacement == XFA_ATTRIBUTEENUM_Right)
      m_rtCaption.left += fWidth - rtWidget.width;
  }

  if (fHeight == XFA_DEFAULTUI_HEIGHT && m_rtUI.height < XFA_MINUI_HEIGHT) {
    m_rtUI.height = XFA_MINUI_HEIGHT;
    m_rtCaption.top += rtUIMargin.top + rtUIMargin.height;
  } else if (fHeight > rtWidget.height) {
    m_rtUI.height += fHeight - rtWidget.height;
  }
}

void CXFA_FFField::UpdateFWL() {
  if (m_pNormalWidget)
    m_pNormalWidget->Update();
}

uint32_t CXFA_FFField::UpdateUIProperty() {
  CXFA_Node* pUiNode = m_pDataAcc->GetUIChild();
  if (pUiNode && pUiNode->GetElementType() == XFA_Element::DefaultUi)
    return FWL_STYLEEXT_EDT_ReadOnly;
  return 0;
}

void CXFA_FFField::SetFWLRect() {
  if (!m_pNormalWidget)
    return;

  CFX_RectF rtUi = m_rtUI;
  if (rtUi.width < 1.0)
    rtUi.width = 1.0;
  if (!m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive()) {
    float fFontSize = m_pDataAcc->GetFontSize();
    if (rtUi.height < fFontSize)
      rtUi.height = fFontSize;
  }
  m_pNormalWidget->SetWidgetRect(rtUi);
}

bool CXFA_FFField::OnMouseEnter() {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::Enter;
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnMouseExit() {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::Leave;
  TranslateFWLMessage(&ms);
  return true;
}

CFX_PointF CXFA_FFField::FWLToClient(const CFX_PointF& point) {
  return m_pNormalWidget ? point - m_pNormalWidget->GetWidgetRect().TopLeft()
                         : point;
}

bool CXFA_FFField::OnLButtonDown(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;
  if (m_pDataAcc->GetAccess() != XFA_ATTRIBUTEENUM_Open ||
      !m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive()) {
    return false;
  }
  if (!PtInActiveRect(point))
    return false;

  SetButtonDown(true);
  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::LeftButtonDown;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnLButtonUp(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;
  if (!IsButtonDown())
    return false;

  SetButtonDown(false);
  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::LeftButtonUp;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnLButtonDblClk(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::LeftButtonDblClk;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnMouseMove(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::Move;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnMouseWheel(uint32_t dwFlags,
                                int16_t zDelta,
                                const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouseWheel ms(nullptr, m_pNormalWidget.get());
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  ms.m_delta = CFX_PointF(zDelta, 0);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnRButtonDown(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;
  if (m_pDataAcc->GetAccess() != XFA_ATTRIBUTEENUM_Open ||
      !m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive()) {
    return false;
  }
  if (!PtInActiveRect(point))
    return false;

  SetButtonDown(true);

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::RightButtonDown;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnRButtonUp(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;
  if (!IsButtonDown())
    return false;

  SetButtonDown(false);
  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::RightButtonUp;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnRButtonDblClk(uint32_t dwFlags, const CFX_PointF& point) {
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageMouse ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_MouseCommand::RightButtonDblClk;
  ms.m_dwFlags = dwFlags;
  ms.m_pos = FWLToClient(point);
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnSetFocus(CXFA_FFWidget* pOldWidget) {
  CXFA_FFWidget::OnSetFocus(pOldWidget);
  if (!m_pNormalWidget)
    return false;

  CFWL_MessageSetFocus ms(nullptr, m_pNormalWidget.get());
  TranslateFWLMessage(&ms);
  m_dwStatus |= XFA_WidgetStatus_Focused;
  AddInvalidateRect();
  return true;
}

bool CXFA_FFField::OnKillFocus(CXFA_FFWidget* pNewWidget) {
  if (!m_pNormalWidget)
    return CXFA_FFWidget::OnKillFocus(pNewWidget);

  CFWL_MessageKillFocus ms(nullptr, m_pNormalWidget.get());
  TranslateFWLMessage(&ms);
  m_dwStatus &= ~XFA_WidgetStatus_Focused;
  AddInvalidateRect();
  CXFA_FFWidget::OnKillFocus(pNewWidget);
  return true;
}

bool CXFA_FFField::OnKeyDown(uint32_t dwKeyCode, uint32_t dwFlags) {
  if (!m_pNormalWidget || !m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive())
    return false;

  CFWL_MessageKey ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_KeyCommand::KeyDown;
  ms.m_dwFlags = dwFlags;
  ms.m_dwKeyCode = dwKeyCode;
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnKeyUp(uint32_t dwKeyCode, uint32_t dwFlags) {
  if (!m_pNormalWidget || !m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive())
    return false;

  CFWL_MessageKey ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_KeyCommand::KeyUp;
  ms.m_dwFlags = dwFlags;
  ms.m_dwKeyCode = dwKeyCode;
  TranslateFWLMessage(&ms);
  return true;
}

bool CXFA_FFField::OnChar(uint32_t dwChar, uint32_t dwFlags) {
  if (!m_pDataAcc->GetDoc()->GetXFADoc()->IsInteractive())
    return false;
  if (dwChar == FWL_VKEY_Tab)
    return true;
  if (!m_pNormalWidget)
    return false;
  if (m_pDataAcc->GetAccess() != XFA_ATTRIBUTEENUM_Open)
    return false;

  CFWL_MessageKey ms(nullptr, m_pNormalWidget.get());
  ms.m_dwCmd = FWL_KeyCommand::Char;
  ms.m_dwFlags = dwFlags;
  ms.m_dwKeyCode = dwChar;
  TranslateFWLMessage(&ms);
  return true;
}

FWL_WidgetHit CXFA_FFField::OnHitTest(const CFX_PointF& point) {
  if (m_pNormalWidget &&
      m_pNormalWidget->HitTest(FWLToClient(point)) != FWL_WidgetHit::Unknown) {
    return FWL_WidgetHit::Client;
  }

  if (!GetRectWithoutRotate().Contains(point))
    return FWL_WidgetHit::Unknown;
  if (m_rtCaption.Contains(point))
    return FWL_WidgetHit::Titlebar;
  return FWL_WidgetHit::Border;
}

bool CXFA_FFField::OnSetCursor(const CFX_PointF& point) {
  return true;
}

bool CXFA_FFField::PtInActiveRect(const CFX_PointF& point) {
  return m_pNormalWidget && m_pNormalWidget->GetWidgetRect().Contains(point);
}

void CXFA_FFField::LayoutCaption() {
  CXFA_TextLayout* pCapTextLayout = m_pDataAcc->GetCaptionTextLayout();
  if (!pCapTextLayout)
    return;

  float fHeight = 0;
  pCapTextLayout->Layout(CFX_SizeF(m_rtCaption.width, m_rtCaption.height),
                         &fHeight);
  if (m_rtCaption.height < fHeight)
    m_rtCaption.height = fHeight;
}

void CXFA_FFField::RenderCaption(CXFA_Graphics* pGS, CFX_Matrix* pMatrix) {
  CXFA_TextLayout* pCapTextLayout = m_pDataAcc->GetCaptionTextLayout();
  if (!pCapTextLayout)
    return;

  CXFA_Caption caption = m_pDataAcc->GetCaption();
  if (!caption || caption.GetPresence() != XFA_ATTRIBUTEENUM_Visible)
    return;

  if (!pCapTextLayout->IsLoaded())
    pCapTextLayout->Layout(CFX_SizeF(m_rtCaption.width, m_rtCaption.height));

  CFX_RectF rtClip = m_rtCaption;
  rtClip.Intersect(GetRectWithoutRotate());
  CFX_RenderDevice* pRenderDevice = pGS->GetRenderDevice();
  CFX_Matrix mt(1, 0, 0, 1, m_rtCaption.left, m_rtCaption.top);
  if (pMatrix) {
    rtClip = pMatrix->TransformRect(rtClip);
    mt.Concat(*pMatrix);
  }
  pCapTextLayout->DrawString(pRenderDevice, mt, rtClip);
}

bool CXFA_FFField::ProcessCommittedData() {
  if (m_pDataAcc->GetAccess() != XFA_ATTRIBUTEENUM_Open)
    return false;
  if (!IsDataChanged())
    return false;
  if (CalculateOverride() != 1)
    return false;
  if (!CommitData())
    return false;

  m_pDocView->SetChangeMark();
  m_pDocView->AddValidateWidget(m_pDataAcc.Get());
  return true;
}

int32_t CXFA_FFField::CalculateOverride() {
  CXFA_WidgetAcc* pAcc = m_pDataAcc->GetExclGroup();
  if (!pAcc)
    return CalculateWidgetAcc(m_pDataAcc.Get());
  if (CalculateWidgetAcc(pAcc) == 0)
    return 0;

  CXFA_Node* pNode = pAcc->GetExclGroupFirstMember();
  if (!pNode)
    return 1;

  CXFA_WidgetAcc* pWidgetAcc = nullptr;
  while (pNode) {
    pWidgetAcc = static_cast<CXFA_WidgetAcc*>(pNode->GetWidgetData());
    if (!pWidgetAcc)
      return 1;
    if (CalculateWidgetAcc(pWidgetAcc) == 0)
      return 0;

    pNode = pWidgetAcc->GetExclGroupNextMember(pNode);
  }
  return 1;
}

int32_t CXFA_FFField::CalculateWidgetAcc(CXFA_WidgetAcc* pAcc) {
  CXFA_Calculate calc = pAcc->GetCalculate();
  if (!calc)
    return 1;

  XFA_VERSION version = pAcc->GetDoc()->GetXFADoc()->GetCurVersionMode();
  switch (calc.GetOverride()) {
    case XFA_ATTRIBUTEENUM_Error: {
      if (version <= XFA_VERSION_204)
        return 1;

      IXFA_AppProvider* pAppProvider = GetApp()->GetAppProvider();
      if (pAppProvider) {
        pAppProvider->MsgBox(L"You are not allowed to modify this field.",
                             L"Calculate Override", XFA_MBICON_Warning,
                             XFA_MB_OK);
      }
      return 0;
    }
    case XFA_ATTRIBUTEENUM_Warning: {
      if (version <= XFA_VERSION_204) {
        CXFA_Script script = calc.GetScript();
        if (!script)
          return 1;

        CFX_WideString wsExpression;
        script.GetExpression(wsExpression);
        if (wsExpression.IsEmpty())
          return 1;
      }

      if (pAcc->GetNode()->IsUserInteractive())
        return 1;

      IXFA_AppProvider* pAppProvider = GetApp()->GetAppProvider();
      if (!pAppProvider)
        return 0;

      CFX_WideString wsMessage;
      calc.GetMessageText(wsMessage);
      if (!wsMessage.IsEmpty())
        wsMessage += L"\r\n";

      wsMessage += L"Are you sure you want to modify this field?";
      if (pAppProvider->MsgBox(wsMessage, L"Calculate Override",
                               XFA_MBICON_Warning, XFA_MB_YesNo) == XFA_IDYes) {
        pAcc->GetNode()->SetFlag(XFA_NodeFlag_UserInteractive, false);
        return 1;
      }
      return 0;
    }
    case XFA_ATTRIBUTEENUM_Ignore:
      return 0;
    case XFA_ATTRIBUTEENUM_Disabled:
      pAcc->GetNode()->SetFlag(XFA_NodeFlag_UserInteractive, false);
      return 1;
    default:
      return 1;
  }
}

bool CXFA_FFField::CommitData() {
  return false;
}

bool CXFA_FFField::IsDataChanged() {
  return false;
}

void CXFA_FFField::TranslateFWLMessage(CFWL_Message* pMessage) {
  GetApp()->GetWidgetMgr()->OnProcessMessageToForm(pMessage);
}

void CXFA_FFField::OnProcessMessage(CFWL_Message* pMessage) {}

void CXFA_FFField::OnProcessEvent(CFWL_Event* pEvent) {
  switch (pEvent->GetType()) {
    case CFWL_Event::Type::Mouse: {
      CFWL_EventMouse* event = static_cast<CFWL_EventMouse*>(pEvent);
      if (event->m_dwCmd == FWL_MouseCommand::Enter) {
        CXFA_EventParam eParam;
        eParam.m_eType = XFA_EVENT_MouseEnter;
        eParam.m_pTarget = m_pDataAcc.Get();
        m_pDataAcc->ProcessEvent(XFA_ATTRIBUTEENUM_MouseEnter, &eParam);
      } else if (event->m_dwCmd == FWL_MouseCommand::Leave) {
        CXFA_EventParam eParam;
        eParam.m_eType = XFA_EVENT_MouseExit;
        eParam.m_pTarget = m_pDataAcc.Get();
        m_pDataAcc->ProcessEvent(XFA_ATTRIBUTEENUM_MouseExit, &eParam);
      } else if (event->m_dwCmd == FWL_MouseCommand::LeftButtonDown) {
        CXFA_EventParam eParam;
        eParam.m_eType = XFA_EVENT_MouseDown;
        eParam.m_pTarget = m_pDataAcc.Get();
        m_pDataAcc->ProcessEvent(XFA_ATTRIBUTEENUM_MouseDown, &eParam);
      } else if (event->m_dwCmd == FWL_MouseCommand::LeftButtonUp) {
        CXFA_EventParam eParam;
        eParam.m_eType = XFA_EVENT_MouseUp;
        eParam.m_pTarget = m_pDataAcc.Get();
        m_pDataAcc->ProcessEvent(XFA_ATTRIBUTEENUM_MouseUp, &eParam);
      }
      break;
    }
    case CFWL_Event::Type::Click: {
      CXFA_EventParam eParam;
      eParam.m_eType = XFA_EVENT_Click;
      eParam.m_pTarget = m_pDataAcc.Get();
      m_pDataAcc->ProcessEvent(XFA_ATTRIBUTEENUM_Click, &eParam);
      break;
    }
    default:
      break;
  }
}

void CXFA_FFField::OnDrawWidget(CXFA_Graphics* pGraphics,
                                const CFX_Matrix& matrix) {}

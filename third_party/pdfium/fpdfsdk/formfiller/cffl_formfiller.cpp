// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fpdfsdk/formfiller/cffl_formfiller.h"

#include <utility>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fxge/cfx_renderdevice.h"
#include "fpdfsdk/cpdfsdk_formfillenvironment.h"
#include "fpdfsdk/cpdfsdk_pageview.h"
#include "fpdfsdk/cpdfsdk_widget.h"
#include "fpdfsdk/formfiller/cba_fontmap.h"
#include "fpdfsdk/fsdk_common.h"

namespace {

CPDFSDK_Widget* CPDFSDKAnnotToWidget(CPDFSDK_Annot* annot) {
  return static_cast<CPDFSDK_Widget*>(annot);
}

}  // namespace

CFFL_FormFiller::CFFL_FormFiller(CPDFSDK_FormFillEnvironment* pFormFillEnv,
                                 CPDFSDK_Widget* pWidget)
    : m_pFormFillEnv(pFormFillEnv), m_pWidget(pWidget), m_bValid(false) {
  ASSERT(m_pFormFillEnv);
}

CFFL_FormFiller::~CFFL_FormFiller() {
  DestroyWindows();
}

void CFFL_FormFiller::DestroyWindows() {
  for (const auto& it : m_Maps) {
    CPWL_Wnd* pWnd = it.second;
    CFFL_PrivateData* pData =
        static_cast<CFFL_PrivateData*>(pWnd->GetAttachedData());
    pWnd->InvalidateProvider(this);
    pWnd->Destroy();
    delete pWnd;
    delete pData;
  }
  m_Maps.clear();
}

void CFFL_FormFiller::SetWindowRect(CPDFSDK_PageView* pPageView,
                                    const CFX_FloatRect& rcWindow) {
  if (CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false))
    pWnd->Move(CFX_FloatRect(rcWindow), true, false);
}

CFX_FloatRect CFFL_FormFiller::GetWindowRect(CPDFSDK_PageView* pPageView) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  return pWnd ? pWnd->GetWindowRect() : CFX_FloatRect();
}

FX_RECT CFFL_FormFiller::GetViewBBox(CPDFSDK_PageView* pPageView,
                                     CPDFSDK_Annot* pAnnot) {
  ASSERT(pPageView);
  ASSERT(pAnnot);

  CFX_FloatRect rcAnnot = m_pWidget->GetRect();
  if (CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false))
    rcAnnot = PWLtoFFL(pWnd->GetWindowRect());

  CFX_FloatRect rcWin = rcAnnot;
  CFX_FloatRect rcFocus = GetFocusBox(pPageView);
  if (!rcFocus.IsEmpty())
    rcWin.Union(rcFocus);

  if (!rcWin.IsEmpty()) {
    rcWin.Inflate(1, 1);
    rcWin.Normalize();
  }
  return rcWin.GetOuterRect();
}

void CFFL_FormFiller::OnDraw(CPDFSDK_PageView* pPageView,
                             CPDFSDK_Annot* pAnnot,
                             CFX_RenderDevice* pDevice,
                             const CFX_Matrix& mtUser2Device) {
  ASSERT(pAnnot->GetPDFAnnot()->GetSubtype() == CPDF_Annot::Subtype::WIDGET);

  if (CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false)) {
    CFX_Matrix mt = GetCurMatrix();
    mt.Concat(mtUser2Device);
    pWnd->DrawAppearance(pDevice, mt);
    return;
  }

  CPDFSDK_Widget* pWidget = CPDFSDKAnnotToWidget(pAnnot);
  if (!CFFL_InteractiveFormFiller::IsVisible(pWidget))
    return;

  pWidget->DrawAppearance(pDevice, mtUser2Device, CPDF_Annot::Normal, nullptr);
}

void CFFL_FormFiller::OnDrawDeactive(CPDFSDK_PageView* pPageView,
                                     CPDFSDK_Annot* pAnnot,
                                     CFX_RenderDevice* pDevice,
                                     const CFX_Matrix& mtUser2Device) {
  CPDFSDKAnnotToWidget(pAnnot)->DrawAppearance(pDevice, mtUser2Device,
                                               CPDF_Annot::Normal, nullptr);
}

void CFFL_FormFiller::OnMouseEnter(CPDFSDK_PageView* pPageView,
                                   CPDFSDK_Annot* pAnnot) {}

void CFFL_FormFiller::OnMouseExit(CPDFSDK_PageView* pPageView,
                                  CPDFSDK_Annot* pAnnot) {
  EndTimer();
  ASSERT(m_pWidget);
}

bool CFFL_FormFiller::OnLButtonDown(CPDFSDK_PageView* pPageView,
                                    CPDFSDK_Annot* pAnnot,
                                    uint32_t nFlags,
                                    const CFX_PointF& point) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, true);
  if (!pWnd)
    return false;

  m_bValid = true;
  FX_RECT rect = GetViewBBox(pPageView, pAnnot);
  InvalidateRect(rect);
  if (!rect.Contains(static_cast<int>(point.x), static_cast<int>(point.y)))
    return false;
  return pWnd->OnLButtonDown(WndtoPWL(pPageView, point), nFlags);
}

bool CFFL_FormFiller::OnLButtonUp(CPDFSDK_PageView* pPageView,
                                  CPDFSDK_Annot* pAnnot,
                                  uint32_t nFlags,
                                  const CFX_PointF& point) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return false;

  InvalidateRect(GetViewBBox(pPageView, pAnnot));
  pWnd->OnLButtonUp(WndtoPWL(pPageView, point), nFlags);
  return true;
}

bool CFFL_FormFiller::OnLButtonDblClk(CPDFSDK_PageView* pPageView,
                                      CPDFSDK_Annot* pAnnot,
                                      uint32_t nFlags,
                                      const CFX_PointF& point) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return false;

  pWnd->OnLButtonDblClk(WndtoPWL(pPageView, point), nFlags);
  return true;
}

bool CFFL_FormFiller::OnMouseMove(CPDFSDK_PageView* pPageView,
                                  CPDFSDK_Annot* pAnnot,
                                  uint32_t nFlags,
                                  const CFX_PointF& point) {
  if (m_ptOldPos != point)
    m_ptOldPos = point;

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return false;

  pWnd->OnMouseMove(WndtoPWL(pPageView, point), nFlags);
  return true;
}

bool CFFL_FormFiller::OnMouseWheel(CPDFSDK_PageView* pPageView,
                                   CPDFSDK_Annot* pAnnot,
                                   uint32_t nFlags,
                                   short zDelta,
                                   const CFX_PointF& point) {
  if (!IsValid())
    return false;

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, true);
  return pWnd && pWnd->OnMouseWheel(zDelta, WndtoPWL(pPageView, point), nFlags);
}

bool CFFL_FormFiller::OnRButtonDown(CPDFSDK_PageView* pPageView,
                                    CPDFSDK_Annot* pAnnot,
                                    uint32_t nFlags,
                                    const CFX_PointF& point) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, true);
  if (!pWnd)
    return false;

  pWnd->OnRButtonDown(WndtoPWL(pPageView, point), nFlags);
  return true;
}

bool CFFL_FormFiller::OnRButtonUp(CPDFSDK_PageView* pPageView,
                                  CPDFSDK_Annot* pAnnot,
                                  uint32_t nFlags,
                                  const CFX_PointF& point) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return false;

  pWnd->OnRButtonUp(WndtoPWL(pPageView, point), nFlags);
  return true;
}

bool CFFL_FormFiller::OnKeyDown(CPDFSDK_Annot* pAnnot,
                                uint32_t nKeyCode,
                                uint32_t nFlags) {
  if (!IsValid())
    return false;

  CPDFSDK_PageView* pPageView = GetCurPageView(true);
  ASSERT(pPageView);

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  return pWnd && pWnd->OnKeyDown(nKeyCode, nFlags);
}

bool CFFL_FormFiller::OnChar(CPDFSDK_Annot* pAnnot,
                             uint32_t nChar,
                             uint32_t nFlags) {
  if (!IsValid())
    return false;

  CPDFSDK_PageView* pPageView = GetCurPageView(true);
  ASSERT(pPageView);

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  return pWnd && pWnd->OnChar(nChar, nFlags);
}

CFX_WideString CFFL_FormFiller::GetSelectedText(CPDFSDK_Annot* pAnnot) {
  if (!IsValid())
    return CFX_WideString();

  CPDFSDK_PageView* pPageView = GetCurPageView(true);
  ASSERT(pPageView);

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  return pWnd ? pWnd->GetSelectedText() : CFX_WideString();
}

void CFFL_FormFiller::ReplaceSelection(CPDFSDK_Annot* pAnnot,
                                       const CFX_WideString& text) {
  if (!IsValid())
    return;

  CPDFSDK_PageView* pPageView = GetCurPageView(true);
  ASSERT(pPageView);

  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return;

  pWnd->ReplaceSelection(text);
}

void CFFL_FormFiller::SetFocusForAnnot(CPDFSDK_Annot* pAnnot, uint32_t nFlag) {
  CPDFSDK_Widget* pWidget = (CPDFSDK_Widget*)pAnnot;
  UnderlyingPageType* pPage = pWidget->GetUnderlyingPage();
  CPDFSDK_PageView* pPageView = m_pFormFillEnv->GetPageView(pPage, true);
  if (CPWL_Wnd* pWnd = GetPDFWindow(pPageView, true))
    pWnd->SetFocus();

  m_bValid = true;
  InvalidateRect(GetViewBBox(pPageView, pAnnot));
}

void CFFL_FormFiller::KillFocusForAnnot(CPDFSDK_Annot* pAnnot, uint32_t nFlag) {
  if (!IsValid())
    return;

  CPDFSDK_PageView* pPageView = GetCurPageView(false);
  if (!pPageView || !CommitData(pPageView, nFlag))
    return;
  if (CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false))
    pWnd->KillFocus();

  bool bDestroyPDFWindow;
  switch (m_pWidget->GetFieldType()) {
    case FIELDTYPE_PUSHBUTTON:
    case FIELDTYPE_CHECKBOX:
    case FIELDTYPE_RADIOBUTTON:
      bDestroyPDFWindow = true;
      break;
    default:
      bDestroyPDFWindow = false;
      break;
  }
  EscapeFiller(pPageView, bDestroyPDFWindow);
}

bool CFFL_FormFiller::IsValid() const {
  return m_bValid;
}

PWL_CREATEPARAM CFFL_FormFiller::GetCreateParam() {
  PWL_CREATEPARAM cp;
  cp.pParentWnd = nullptr;
  cp.pProvider.Reset(this);
  cp.rcRectWnd = GetPDFWindowRect();

  uint32_t dwCreateFlags = PWS_BORDER | PWS_BACKGROUND | PWS_VISIBLE;
  uint32_t dwFieldFlag = m_pWidget->GetFieldFlags();
  if (dwFieldFlag & FIELDFLAG_READONLY)
    dwCreateFlags |= PWS_READONLY;

  FX_COLORREF color;
  if (m_pWidget->GetFillColor(color))
    cp.sBackgroundColor = CFX_Color(color);
  if (m_pWidget->GetBorderColor(color))
    cp.sBorderColor = CFX_Color(color);

  cp.sTextColor = CFX_Color(COLORTYPE_GRAY, 0);

  if (m_pWidget->GetTextColor(color))
    cp.sTextColor = CFX_Color(color);

  cp.fFontSize = m_pWidget->GetFontSize();
  cp.dwBorderWidth = m_pWidget->GetBorderWidth();

  cp.nBorderStyle = m_pWidget->GetBorderStyle();
  switch (cp.nBorderStyle) {
    case BorderStyle::DASH:
      cp.sDash = CPWL_Dash(3, 3, 0);
      break;
    case BorderStyle::BEVELED:
    case BorderStyle::INSET:
      cp.dwBorderWidth *= 2;
      break;
    default:
      break;
  }

  if (cp.fFontSize <= 0)
    dwCreateFlags |= PWS_AUTOFONTSIZE;

  cp.dwFlags = dwCreateFlags;
  cp.pSystemHandler = m_pFormFillEnv->GetSysHandler();
  return cp;
}

CPWL_Wnd* CFFL_FormFiller::GetPDFWindow(CPDFSDK_PageView* pPageView,
                                        bool bNew) {
  ASSERT(pPageView);

  auto it = m_Maps.find(pPageView);
  const bool found = it != m_Maps.end();
  CPWL_Wnd* pWnd = found ? it->second : nullptr;
  if (!bNew)
    return pWnd;

  if (!found) {
    PWL_CREATEPARAM cp = GetCreateParam();
    cp.pAttachedWidget.Reset(m_pWidget.Get());

    CFFL_PrivateData* pPrivateData = new CFFL_PrivateData;
    pPrivateData->pWidget = m_pWidget.Get();
    pPrivateData->pPageView = pPageView;
    pPrivateData->nWidgetAge = m_pWidget->GetAppearanceAge();
    pPrivateData->nValueAge = 0;
    cp.pAttachedData = pPrivateData;
    CPWL_Wnd* pNewWnd = NewPDFWindow(cp);
    m_Maps[pPageView] = pNewWnd;
    return pNewWnd;
  }

  auto* pPrivateData = static_cast<CFFL_PrivateData*>(pWnd->GetAttachedData());
  if (pPrivateData->nWidgetAge == m_pWidget->GetAppearanceAge())
    return pWnd;

  return ResetPDFWindow(pPageView,
                        m_pWidget->GetValueAge() == pPrivateData->nValueAge);
}

void CFFL_FormFiller::DestroyPDFWindow(CPDFSDK_PageView* pPageView) {
  auto it = m_Maps.find(pPageView);
  if (it == m_Maps.end())
    return;

  CPWL_Wnd* pWnd = it->second;
  CFFL_PrivateData* pData = (CFFL_PrivateData*)pWnd->GetAttachedData();
  pWnd->Destroy();
  delete pWnd;
  delete pData;

  m_Maps.erase(it);
}

CFX_Matrix CFFL_FormFiller::GetWindowMatrix(void* pAttachedData) {
  CFX_Matrix mt;
  auto* pPrivateData = reinterpret_cast<CFFL_PrivateData*>(pAttachedData);
  if (!pAttachedData || !pPrivateData->pPageView)
    return mt;

  CFX_Matrix mtPageView;
  pPrivateData->pPageView->GetCurrentMatrix(mtPageView);
  mt = GetCurMatrix();
  mt.Concat(mtPageView);
  return mt;
}

CFX_Matrix CFFL_FormFiller::GetCurMatrix() {
  CFX_Matrix mt;
  CFX_FloatRect rcDA = m_pWidget->GetPDFAnnot()->GetRect();
  switch (m_pWidget->GetRotate()) {
    case 90:
      mt = CFX_Matrix(0, 1, -1, 0, rcDA.right - rcDA.left, 0);
      break;
    case 180:
      mt = CFX_Matrix(-1, 0, 0, -1, rcDA.right - rcDA.left,
                      rcDA.top - rcDA.bottom);
      break;
    case 270:
      mt = CFX_Matrix(0, -1, 1, 0, 0, rcDA.top - rcDA.bottom);
      break;
    case 0:
    default:
      break;
  }
  mt.e += rcDA.left;
  mt.f += rcDA.bottom;

  return mt;
}

CFX_FloatRect CFFL_FormFiller::GetPDFWindowRect() const {
  CFX_FloatRect rectAnnot = m_pWidget->GetPDFAnnot()->GetRect();

  float fWidth = rectAnnot.right - rectAnnot.left;
  float fHeight = rectAnnot.top - rectAnnot.bottom;
  if ((m_pWidget->GetRotate() / 90) & 0x01)
    std::swap(fWidth, fHeight);
  return CFX_FloatRect(0, 0, fWidth, fHeight);
}

CPDFSDK_PageView* CFFL_FormFiller::GetCurPageView(bool renew) {
  UnderlyingPageType* pPage = m_pWidget->GetUnderlyingPage();
  return m_pFormFillEnv->GetPageView(pPage, renew);
}

CFX_FloatRect CFFL_FormFiller::GetFocusBox(CPDFSDK_PageView* pPageView) {
  CPWL_Wnd* pWnd = GetPDFWindow(pPageView, false);
  if (!pWnd)
    return CFX_FloatRect();

  CFX_FloatRect rcFocus = FFLtoWnd(pPageView, PWLtoFFL(pWnd->GetFocusRect()));
  return pPageView->GetPDFPage()->GetPageBBox().Contains(rcFocus)
             ? rcFocus
             : CFX_FloatRect();
}

CFX_FloatRect CFFL_FormFiller::FFLtoPWL(const CFX_FloatRect& rect) {
  return GetCurMatrix().GetInverse().TransformRect(rect);
}

CFX_FloatRect CFFL_FormFiller::PWLtoFFL(const CFX_FloatRect& rect) {
  return GetCurMatrix().TransformRect(rect);
}

CFX_PointF CFFL_FormFiller::FFLtoPWL(const CFX_PointF& point) {
  return GetCurMatrix().GetInverse().Transform(point);
}

CFX_PointF CFFL_FormFiller::PWLtoFFL(const CFX_PointF& point) {
  return GetCurMatrix().Transform(point);
}

CFX_PointF CFFL_FormFiller::WndtoPWL(CPDFSDK_PageView* pPageView,
                                     const CFX_PointF& pt) {
  return FFLtoPWL(pt);
}

CFX_FloatRect CFFL_FormFiller::FFLtoWnd(CPDFSDK_PageView* pPageView,
                                        const CFX_FloatRect& rect) {
  return rect;
}

bool CFFL_FormFiller::CommitData(CPDFSDK_PageView* pPageView, uint32_t nFlag) {
  if (!IsDataChanged(pPageView))
    return true;

  CFFL_InteractiveFormFiller* pFormFiller =
      m_pFormFillEnv->GetInteractiveFormFiller();
  CPDFSDK_Annot::ObservedPtr pObserved(m_pWidget.Get());

  if (!pFormFiller->OnKeyStrokeCommit(&pObserved, pPageView, nFlag)) {
    if (!pObserved)
      return false;
    ResetPDFWindow(pPageView, false);
    return true;
  }
  if (!pObserved)
    return false;

  if (!pFormFiller->OnValidate(&pObserved, pPageView, nFlag)) {
    if (!pObserved)
      return false;
    ResetPDFWindow(pPageView, false);
    return true;
  }
  if (!pObserved)
    return false;

  SaveData(pPageView);
  pFormFiller->OnCalculate(&pObserved, pPageView, nFlag);
  if (!pObserved)
    return false;

  pFormFiller->OnFormat(&pObserved, pPageView, nFlag);
  if (!pObserved)
    return false;

  return true;
}

bool CFFL_FormFiller::IsDataChanged(CPDFSDK_PageView* pPageView) {
  return false;
}

void CFFL_FormFiller::SaveData(CPDFSDK_PageView* pPageView) {}

#ifdef PDF_ENABLE_XFA
bool CFFL_FormFiller::IsFieldFull(CPDFSDK_PageView* pPageView) {
  return false;
}
#endif  // PDF_ENABLE_XFA

void CFFL_FormFiller::SetChangeMark() {
  m_pFormFillEnv->OnChange();
}

void CFFL_FormFiller::GetActionData(CPDFSDK_PageView* pPageView,
                                    CPDF_AAction::AActionType type,
                                    PDFSDK_FieldAction& fa) {
  fa.sValue = m_pWidget->GetValue();
}

void CFFL_FormFiller::SetActionData(CPDFSDK_PageView* pPageView,
                                    CPDF_AAction::AActionType type,
                                    const PDFSDK_FieldAction& fa) {}

bool CFFL_FormFiller::IsActionDataChanged(CPDF_AAction::AActionType type,
                                          const PDFSDK_FieldAction& faOld,
                                          const PDFSDK_FieldAction& faNew) {
  return false;
}

void CFFL_FormFiller::SaveState(CPDFSDK_PageView* pPageView) {}

void CFFL_FormFiller::RestoreState(CPDFSDK_PageView* pPageView) {}

CPWL_Wnd* CFFL_FormFiller::ResetPDFWindow(CPDFSDK_PageView* pPageView,
                                          bool bRestoreValue) {
  return GetPDFWindow(pPageView, false);
}

void CFFL_FormFiller::TimerProc() {}

CFX_SystemHandler* CFFL_FormFiller::GetSystemHandler() const {
  return m_pFormFillEnv->GetSysHandler();
}

void CFFL_FormFiller::EscapeFiller(CPDFSDK_PageView* pPageView,
                                   bool bDestroyPDFWindow) {
  m_bValid = false;

  InvalidateRect(GetViewBBox(pPageView, m_pWidget.Get()));
  if (bDestroyPDFWindow)
    DestroyPDFWindow(pPageView);
}

void CFFL_FormFiller::InvalidateRect(const FX_RECT& rect) {
  m_pFormFillEnv->Invalidate(m_pWidget->GetUnderlyingPage(), rect);
}

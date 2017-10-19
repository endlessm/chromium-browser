// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fwl/theme/cfwl_listboxtp.h"

#include "xfa/fwl/cfwl_listbox.h"
#include "xfa/fwl/cfwl_themebackground.h"
#include "xfa/fwl/cfwl_widget.h"
#include "xfa/fxgraphics/cxfa_color.h"
#include "xfa/fxgraphics/cxfa_path.h"

CFWL_ListBoxTP::CFWL_ListBoxTP() {}

CFWL_ListBoxTP::~CFWL_ListBoxTP() {}

void CFWL_ListBoxTP::DrawBackground(CFWL_ThemeBackground* pParams) {
  if (!pParams)
    return;

  switch (pParams->m_iPart) {
    case CFWL_Part::Border: {
      DrawBorder(pParams->m_pGraphics, &pParams->m_rtPart, &pParams->m_matrix);
      break;
    }
    case CFWL_Part::Background: {
      FillSoildRect(pParams->m_pGraphics, ArgbEncode(255, 255, 255, 255),
                    &pParams->m_rtPart, &pParams->m_matrix);
      if (pParams->m_pData) {
        FillSoildRect(pParams->m_pGraphics, FWLTHEME_COLOR_Background,
                      (CFX_RectF*)pParams->m_pData, &pParams->m_matrix);
      }
      break;
    }
    case CFWL_Part::ListItem: {
      DrawListBoxItem(pParams->m_pGraphics, pParams->m_dwStates,
                      &pParams->m_rtPart, pParams->m_pData, &pParams->m_matrix);
      break;
    }
    case CFWL_Part::Icon: {
      pParams->m_pGraphics->StretchImage(pParams->m_pImage, pParams->m_rtPart,
                                         pParams->m_matrix);
      break;
    }
    case CFWL_Part::Check: {
      uint32_t color = 0xFF000000;
      if (pParams->m_dwStates == CFWL_PartState_Checked) {
        color = 0xFFFF0000;
      } else if (pParams->m_dwStates == CFWL_PartState_Normal) {
        color = 0xFF0000FF;
      }
      FillSoildRect(pParams->m_pGraphics, color, &pParams->m_rtPart,
                    &pParams->m_matrix);
    }
    default:
      break;
  }
}

void CFWL_ListBoxTP::DrawListBoxItem(CXFA_Graphics* pGraphics,
                                     uint32_t dwStates,
                                     const CFX_RectF* prtItem,
                                     void* pData,
                                     CFX_Matrix* pMatrix) {
  if (dwStates & CFWL_PartState_Selected) {
    pGraphics->SaveGraphState();
    pGraphics->SetFillColor(CXFA_Color(FWLTHEME_COLOR_BKSelected));
    CFX_RectF rt(*prtItem);
    CXFA_Path path;
#if (_FX_OS_ == _FX_MACOSX_)
    path.AddRectangle(rt.left, rt.top, rt.width - 1, rt.height - 1);
#else
    path.AddRectangle(rt.left, rt.top, rt.width, rt.height);
#endif
    pGraphics->FillPath(&path, FXFILL_WINDING, pMatrix);
    pGraphics->RestoreGraphState();
  }
  if (dwStates & CFWL_PartState_Focused && pData)
    DrawFocus(pGraphics, (CFX_RectF*)pData, pMatrix);
}

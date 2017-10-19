// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffnotify.h"

#include "fxjs/cfxjse_value.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffarc.h"
#include "xfa/fxfa/cxfa_ffbarcode.h"
#include "xfa/fxfa/cxfa_ffcheckbutton.h"
#include "xfa/fxfa/cxfa_ffcombobox.h"
#include "xfa/fxfa/cxfa_ffdatetimeedit.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffdocview.h"
#include "xfa/fxfa/cxfa_ffdraw.h"
#include "xfa/fxfa/cxfa_ffexclgroup.h"
#include "xfa/fxfa/cxfa_fffield.h"
#include "xfa/fxfa/cxfa_ffimage.h"
#include "xfa/fxfa/cxfa_ffimageedit.h"
#include "xfa/fxfa/cxfa_ffline.h"
#include "xfa/fxfa/cxfa_fflistbox.h"
#include "xfa/fxfa/cxfa_ffnumericedit.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffpasswordedit.h"
#include "xfa/fxfa/cxfa_ffpushbutton.h"
#include "xfa/fxfa/cxfa_ffrectangle.h"
#include "xfa/fxfa/cxfa_ffsignature.h"
#include "xfa/fxfa/cxfa_ffsubform.h"
#include "xfa/fxfa/cxfa_fftext.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_ffwidgethandler.h"
#include "xfa/fxfa/cxfa_fwladapterwidgetmgr.h"
#include "xfa/fxfa/cxfa_textlayout.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxfa/parser/cxfa_node.h"

namespace {

CXFA_WidgetAcc* ToWidgetAcc(void* data) {
  return static_cast<CXFA_WidgetAcc*>(data);
}

CXFA_FFListBox* ToListBox(CXFA_FFWidget* widget) {
  return static_cast<CXFA_FFListBox*>(widget);
}

CXFA_FFComboBox* ToComboBox(CXFA_FFWidget* widget) {
  return static_cast<CXFA_FFComboBox*>(widget);
}

}  // namespace

static void XFA_FFDeleteWidgetAcc(void* pData) {
  delete ToWidgetAcc(pData);
}

static XFA_MAPDATABLOCKCALLBACKINFO gs_XFADeleteWidgetAcc = {
    XFA_FFDeleteWidgetAcc, nullptr};

CXFA_FFNotify::CXFA_FFNotify(CXFA_FFDoc* pDoc) : m_pDoc(pDoc) {}

CXFA_FFNotify::~CXFA_FFNotify() {}

void CXFA_FFNotify::OnPageEvent(CXFA_ContainerLayoutItem* pSender,
                                uint32_t dwEvent) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView(pSender->GetLayout());
  if (pDocView)
    pDocView->OnPageEvent(pSender, dwEvent);
}

void CXFA_FFNotify::OnWidgetListItemAdded(CXFA_WidgetData* pSender,
                                          const wchar_t* pLabel,
                                          const wchar_t* pValue,
                                          int32_t iIndex) {
  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pSender);
  if (pWidgetAcc->GetUIType() != XFA_Element::ChoiceList)
    return;

  CXFA_FFWidget* pWidget = nullptr;
  while ((pWidget = pWidgetAcc->GetNextWidget(pWidget)) != nullptr) {
    if (pWidget->IsLoaded()) {
      if (pWidgetAcc->IsListBox())
        ToListBox(pWidget)->InsertItem(pLabel, iIndex);
      else
        ToComboBox(pWidget)->InsertItem(pLabel, iIndex);
    }
  }
}

void CXFA_FFNotify::OnWidgetListItemRemoved(CXFA_WidgetData* pSender,
                                            int32_t iIndex) {
  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pSender);
  if (pWidgetAcc->GetUIType() != XFA_Element::ChoiceList)
    return;

  CXFA_FFWidget* pWidget = nullptr;
  while ((pWidget = pWidgetAcc->GetNextWidget(pWidget)) != nullptr) {
    if (pWidget->IsLoaded()) {
      if (pWidgetAcc->IsListBox())
        ToListBox(pWidget)->DeleteItem(iIndex);
      else
        ToComboBox(pWidget)->DeleteItem(iIndex);
    }
  }
}

CXFA_LayoutItem* CXFA_FFNotify::OnCreateLayoutItem(CXFA_Node* pNode) {
  CXFA_LayoutProcessor* pLayout = m_pDoc->GetXFADoc()->GetDocLayout();
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView(pLayout);
  XFA_Element eType = pNode->GetElementType();
  if (eType == XFA_Element::PageArea)
    return new CXFA_FFPageView(pDocView, pNode);
  if (eType == XFA_Element::ContentArea)
    return new CXFA_ContainerLayoutItem(pNode);

  CXFA_WidgetAcc* pAcc = ToWidgetAcc(pNode->GetWidgetData());
  if (!pAcc)
    return new CXFA_ContentLayoutItem(pNode);

  CXFA_FFWidget* pWidget;
  switch (pAcc->GetUIType()) {
    case XFA_Element::Barcode:
      pWidget = new CXFA_FFBarcode(pAcc);
      break;
    case XFA_Element::Button:
      pWidget = new CXFA_FFPushButton(pAcc);
      break;
    case XFA_Element::CheckButton:
      pWidget = new CXFA_FFCheckButton(pAcc);
      break;
    case XFA_Element::ChoiceList: {
      if (pAcc->IsListBox())
        pWidget = new CXFA_FFListBox(pAcc);
      else
        pWidget = new CXFA_FFComboBox(pAcc);
    } break;
    case XFA_Element::DateTimeEdit:
      pWidget = new CXFA_FFDateTimeEdit(pAcc);
      break;
    case XFA_Element::ImageEdit:
      pWidget = new CXFA_FFImageEdit(pAcc);
      break;
    case XFA_Element::NumericEdit:
      pWidget = new CXFA_FFNumericEdit(pAcc);
      break;
    case XFA_Element::PasswordEdit:
      pWidget = new CXFA_FFPasswordEdit(pAcc);
      break;
    case XFA_Element::Signature:
      pWidget = new CXFA_FFSignature(pAcc);
      break;
    case XFA_Element::TextEdit:
      pWidget = new CXFA_FFTextEdit(pAcc);
      break;
    case XFA_Element::Arc:
      pWidget = new CXFA_FFArc(pAcc);
      break;
    case XFA_Element::Line:
      pWidget = new CXFA_FFLine(pAcc);
      break;
    case XFA_Element::Rectangle:
      pWidget = new CXFA_FFRectangle(pAcc);
      break;
    case XFA_Element::Text:
      pWidget = new CXFA_FFText(pAcc);
      break;
    case XFA_Element::Image:
      pWidget = new CXFA_FFImage(pAcc);
      break;
    case XFA_Element::Draw:
      pWidget = new CXFA_FFDraw(pAcc);
      break;
    case XFA_Element::Subform:
      pWidget = new CXFA_FFSubForm(pAcc);
      break;
    case XFA_Element::ExclGroup:
      pWidget = new CXFA_FFExclGroup(pAcc);
      break;
    case XFA_Element::DefaultUi:
    default:
      pWidget = nullptr;
      break;
  }

  if (pWidget)
    pWidget->SetDocView(pDocView);
  return pWidget;
}

void CXFA_FFNotify::StartFieldDrawLayout(CXFA_Node* pItem,
                                         float& fCalcWidth,
                                         float& fCalcHeight) {
  CXFA_WidgetAcc* pAcc = ToWidgetAcc(pItem->GetWidgetData());
  if (!pAcc)
    return;

  pAcc->StartWidgetLayout(fCalcWidth, fCalcHeight);
}

bool CXFA_FFNotify::FindSplitPos(CXFA_Node* pItem,
                                 int32_t iBlockIndex,
                                 float& fCalcHeightPos) {
  CXFA_WidgetAcc* pAcc = ToWidgetAcc(pItem->GetWidgetData());
  return pAcc && pAcc->FindSplitPos(iBlockIndex, fCalcHeightPos);
}

bool CXFA_FFNotify::RunScript(CXFA_Node* pScript, CXFA_Node* pFormItem) {
  bool bRet = false;
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return bRet;

  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pFormItem->GetWidgetData());
  if (!pWidgetAcc)
    return bRet;

  CXFA_EventParam EventParam;
  EventParam.m_eType = XFA_EVENT_Unknown;
  CFXJSE_Value* pRetValue = nullptr;
  int32_t iRet =
      pWidgetAcc->ExecuteScript(CXFA_Script(pScript), &EventParam, &pRetValue);
  if (iRet == XFA_EVENTERROR_Success && pRetValue) {
    bRet = pRetValue->ToBoolean();
    delete pRetValue;
  }
  return bRet;
}

int32_t CXFA_FFNotify::ExecEventByDeepFirst(CXFA_Node* pFormNode,
                                            XFA_EVENTTYPE eEventType,
                                            bool bIsFormReady,
                                            bool bRecursive,
                                            CXFA_WidgetAcc* pExclude) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return XFA_EVENTERROR_NotExist;
  return pDocView->ExecEventActivityByDeepFirst(
      pFormNode, eEventType, bIsFormReady, bRecursive,
      pExclude ? pExclude->GetNode() : nullptr);
}

void CXFA_FFNotify::AddCalcValidate(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pNode->GetWidgetData());
  if (!pWidgetAcc)
    return;

  pDocView->AddCalculateWidgetAcc(pWidgetAcc);
  pDocView->AddValidateWidget(pWidgetAcc);
}

CXFA_FFDoc* CXFA_FFNotify::GetHDOC() {
  return m_pDoc.Get();
}

IXFA_DocEnvironment* CXFA_FFNotify::GetDocEnvironment() const {
  return m_pDoc->GetDocEnvironment();
}

IXFA_AppProvider* CXFA_FFNotify::GetAppProvider() {
  return m_pDoc->GetApp()->GetAppProvider();
}

CXFA_FFWidgetHandler* CXFA_FFNotify::GetWidgetHandler() {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  return pDocView ? pDocView->GetWidgetHandler() : nullptr;
}

CXFA_FFWidget* CXFA_FFNotify::GetHWidget(CXFA_LayoutItem* pLayoutItem) {
  return XFA_GetWidgetFromLayoutItem(pLayoutItem);
}

void CXFA_FFNotify::OpenDropDownList(CXFA_FFWidget* hWidget) {
  if (hWidget->GetDataAcc()->GetUIType() != XFA_Element::ChoiceList)
    return;

  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  pDocView->LockUpdate();
  ToComboBox(hWidget)->OpenDropDownList();
  pDocView->UnlockUpdate();
  pDocView->UpdateDocView();
}

CFX_WideString CXFA_FFNotify::GetCurrentDateTime() {
  CFX_DateTime dataTime;
  dataTime.Now();

  CFX_WideString wsDateTime;
  wsDateTime.Format(L"%d%02d%02dT%02d%02d%02d", dataTime.GetYear(),
                    dataTime.GetMonth(), dataTime.GetDay(), dataTime.GetHour(),
                    dataTime.GetMinute(), dataTime.GetSecond());
  return wsDateTime;
}

void CXFA_FFNotify::ResetData(CXFA_WidgetData* pWidgetData) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  pDocView->ResetWidgetData(ToWidgetAcc(pWidgetData));
}

int32_t CXFA_FFNotify::GetLayoutStatus() {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  return pDocView ? pDocView->GetLayoutStatus() : 0;
}

void CXFA_FFNotify::RunNodeInitialize(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  pDocView->AddNewFormNode(pNode);
}

void CXFA_FFNotify::RunSubformIndexChange(CXFA_Node* pSubformNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  pDocView->AddIndexChangedSubform(pSubformNode);
}

CXFA_Node* CXFA_FFNotify::GetFocusWidgetNode() {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return nullptr;

  CXFA_WidgetAcc* pAcc = pDocView->GetFocusWidgetAcc();
  return pAcc ? pAcc->GetNode() : nullptr;
}

void CXFA_FFNotify::SetFocusWidgetNode(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  CXFA_WidgetAcc* pAcc = pNode ? ToWidgetAcc(pNode->GetWidgetData()) : nullptr;
  pDocView->SetFocusWidgetAcc(pAcc);
}

void CXFA_FFNotify::OnNodeReady(CXFA_Node* pNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  XFA_Element eType = pNode->GetElementType();
  if (XFA_IsCreateWidget(eType)) {
    CXFA_WidgetAcc* pAcc = new CXFA_WidgetAcc(pDocView, pNode);
    pNode->SetObject(XFA_ATTRIBUTE_WidgetData, pAcc, &gs_XFADeleteWidgetAcc);
    return;
  }
  switch (eType) {
    case XFA_Element::BindItems:
      pDocView->m_BindItems.push_back(pNode);
      break;
    case XFA_Element::Validate:
      pNode->SetFlag(XFA_NodeFlag_NeedsInitApp, false);
      break;
    default:
      break;
  }
}

void CXFA_FFNotify::OnValueChanging(CXFA_Node* pSender, XFA_ATTRIBUTE eAttr) {
  if (eAttr != XFA_ATTRIBUTE_Presence)
    return;
  if (pSender->GetPacketID() & XFA_XDPPACKET_Datasets)
    return;
  if (!pSender->IsFormContainer())
    return;

  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;
  if (pDocView->GetLayoutStatus() < XFA_DOCVIEW_LAYOUTSTATUS_End)
    return;

  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pSender->GetWidgetData());
  if (!pWidgetAcc)
    return;

  CXFA_FFWidget* pWidget = nullptr;
  while ((pWidget = pWidgetAcc->GetNextWidget(pWidget)) != nullptr) {
    if (pWidget->IsLoaded())
      pWidget->AddInvalidateRect();
  }
}

void CXFA_FFNotify::OnValueChanged(CXFA_Node* pSender,
                                   XFA_ATTRIBUTE eAttr,
                                   CXFA_Node* pParentNode,
                                   CXFA_Node* pWidgetNode) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  if (!(pSender->GetPacketID() & XFA_XDPPACKET_Form)) {
    if (eAttr == XFA_ATTRIBUTE_Value)
      pDocView->AddCalculateNodeNotify(pSender);
    return;
  }

  XFA_Element eType = pParentNode->GetElementType();
  bool bIsContainerNode = pParentNode->IsContainerNode();
  CXFA_WidgetAcc* pWidgetAcc = ToWidgetAcc(pWidgetNode->GetWidgetData());
  if (!pWidgetAcc)
    return;

  bool bUpdateProperty = false;
  pDocView->SetChangeMark();
  switch (eType) {
    case XFA_Element::Caption: {
      CXFA_TextLayout* pCapOut = pWidgetAcc->GetCaptionTextLayout();
      if (!pCapOut)
        return;

      pCapOut->Unload();
      break;
    }
    case XFA_Element::Ui:
    case XFA_Element::Para:
      bUpdateProperty = true;
      break;
    default:
      break;
  }
  if (bIsContainerNode && eAttr == XFA_ATTRIBUTE_Access)
    bUpdateProperty = true;

  if (eAttr == XFA_ATTRIBUTE_Value) {
    pDocView->AddCalculateNodeNotify(pSender);
    if (eType == XFA_Element::Value || bIsContainerNode) {
      if (bIsContainerNode) {
        pWidgetAcc->UpdateUIDisplay();
        pDocView->AddCalculateWidgetAcc(pWidgetAcc);
        pDocView->AddValidateWidget(pWidgetAcc);
      } else if (pWidgetNode->GetNodeItem(XFA_NODEITEM_Parent)
                     ->GetElementType() == XFA_Element::ExclGroup) {
        pWidgetAcc->UpdateUIDisplay();
      }
      return;
    }
  }

  CXFA_FFWidget* pWidget = nullptr;
  while ((pWidget = pWidgetAcc->GetNextWidget(pWidget)) != nullptr) {
    if (!pWidget->IsLoaded())
      continue;

    if (bUpdateProperty)
      pWidget->UpdateWidgetProperty();
    pWidget->PerformLayout();
    pWidget->AddInvalidateRect();
  }
}

void CXFA_FFNotify::OnChildAdded(CXFA_Node* pSender) {
  if (!pSender->IsFormContainer())
    return;

  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  bool bLayoutReady =
      !(pDocView->m_bInLayoutStatus) &&
      (pDocView->GetLayoutStatus() == XFA_DOCVIEW_LAYOUTSTATUS_End);
  if (bLayoutReady)
    m_pDoc->GetDocEnvironment()->SetChangeMark(m_pDoc.Get());
}

void CXFA_FFNotify::OnChildRemoved() {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView();
  if (!pDocView)
    return;

  bool bLayoutReady =
      !(pDocView->m_bInLayoutStatus) &&
      (pDocView->GetLayoutStatus() == XFA_DOCVIEW_LAYOUTSTATUS_End);
  if (bLayoutReady)
    m_pDoc->GetDocEnvironment()->SetChangeMark(m_pDoc.Get());
}

void CXFA_FFNotify::OnLayoutItemAdded(CXFA_LayoutProcessor* pLayout,
                                      CXFA_LayoutItem* pSender,
                                      int32_t iPageIdx,
                                      uint32_t dwStatus) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView(pLayout);
  if (!pDocView)
    return;

  CXFA_FFWidget* pWidget = XFA_GetWidgetFromLayoutItem(pSender);
  if (!pWidget)
    return;

  CXFA_FFPageView* pNewPageView = pDocView->GetPageView(iPageIdx);
  uint32_t dwFilter = XFA_WidgetStatus_Visible | XFA_WidgetStatus_Viewable |
                      XFA_WidgetStatus_Printable;
  pWidget->ModifyStatus(dwStatus, dwFilter);
  CXFA_FFPageView* pPrePageView = pWidget->GetPageView();
  if (pPrePageView != pNewPageView ||
      (dwStatus & (XFA_WidgetStatus_Visible | XFA_WidgetStatus_Viewable)) ==
          (XFA_WidgetStatus_Visible | XFA_WidgetStatus_Viewable)) {
    pWidget->SetPageView(pNewPageView);
    m_pDoc->GetDocEnvironment()->WidgetPostAdd(pWidget, pWidget->GetDataAcc());
  }
  if (pDocView->GetLayoutStatus() != XFA_DOCVIEW_LAYOUTSTATUS_End ||
      !(dwStatus & XFA_WidgetStatus_Visible)) {
    return;
  }
  if (pWidget->IsLoaded()) {
    if (pWidget->GetWidgetRect() != pWidget->RecacheWidgetRect())
      pWidget->PerformLayout();
  } else {
    pWidget->LoadWidget();
  }
  pWidget->AddInvalidateRect();
}

void CXFA_FFNotify::OnLayoutItemRemoving(CXFA_LayoutProcessor* pLayout,
                                         CXFA_LayoutItem* pSender) {
  CXFA_FFDocView* pDocView = m_pDoc->GetDocView(pLayout);
  if (!pDocView)
    return;

  CXFA_FFWidget* pWidget = XFA_GetWidgetFromLayoutItem(pSender);
  if (!pWidget)
    return;

  pDocView->DeleteLayoutItem(pWidget);
  m_pDoc->GetDocEnvironment()->WidgetPreRemove(pWidget, pWidget->GetDataAcc());
  pWidget->AddInvalidateRect();
}

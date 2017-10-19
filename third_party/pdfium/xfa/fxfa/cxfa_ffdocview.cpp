// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_ffdocview.h"

#include "core/fxcrt/fx_extension.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffbarcode.h"
#include "xfa/fxfa/cxfa_ffcheckbutton.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffdraw.h"
#include "xfa/fxfa/cxfa_ffexclgroup.h"
#include "xfa/fxfa/cxfa_fffield.h"
#include "xfa/fxfa/cxfa_ffimage.h"
#include "xfa/fxfa/cxfa_ffimageedit.h"
#include "xfa/fxfa/cxfa_ffpageview.h"
#include "xfa/fxfa/cxfa_ffpushbutton.h"
#include "xfa/fxfa/cxfa_ffsignature.h"
#include "xfa/fxfa/cxfa_ffsubform.h"
#include "xfa/fxfa/cxfa_fftext.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_ffwidgethandler.h"
#include "xfa/fxfa/cxfa_fwladapterwidgetmgr.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxfa/cxfa_widgetacciterator.h"
#include "xfa/fxfa/parser/cxfa_binditems.h"
#include "xfa/fxfa/parser/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_scriptcontext.h"
#include "xfa/fxfa/parser/xfa_resolvenode_rs.h"

#define XFA_CalcRefCount (void*)(uintptr_t) FXBSTR_ID('X', 'F', 'A', 'R')

const XFA_ATTRIBUTEENUM gs_EventActivity[] = {
    XFA_ATTRIBUTEENUM_Click,      XFA_ATTRIBUTEENUM_Change,
    XFA_ATTRIBUTEENUM_DocClose,   XFA_ATTRIBUTEENUM_DocReady,
    XFA_ATTRIBUTEENUM_Enter,      XFA_ATTRIBUTEENUM_Exit,
    XFA_ATTRIBUTEENUM_Full,       XFA_ATTRIBUTEENUM_IndexChange,
    XFA_ATTRIBUTEENUM_Initialize, XFA_ATTRIBUTEENUM_MouseDown,
    XFA_ATTRIBUTEENUM_MouseEnter, XFA_ATTRIBUTEENUM_MouseExit,
    XFA_ATTRIBUTEENUM_MouseUp,    XFA_ATTRIBUTEENUM_PostExecute,
    XFA_ATTRIBUTEENUM_PostOpen,   XFA_ATTRIBUTEENUM_PostPrint,
    XFA_ATTRIBUTEENUM_PostSave,   XFA_ATTRIBUTEENUM_PostSign,
    XFA_ATTRIBUTEENUM_PostSubmit, XFA_ATTRIBUTEENUM_PreExecute,
    XFA_ATTRIBUTEENUM_PreOpen,    XFA_ATTRIBUTEENUM_PrePrint,
    XFA_ATTRIBUTEENUM_PreSave,    XFA_ATTRIBUTEENUM_PreSign,
    XFA_ATTRIBUTEENUM_PreSubmit,  XFA_ATTRIBUTEENUM_Ready,
    XFA_ATTRIBUTEENUM_Unknown,
};

CXFA_FFDocView::CXFA_FFDocView(CXFA_FFDoc* pDoc)
    : m_bLayoutEvent(false),
      m_pListFocusWidget(nullptr),
      m_bInLayoutStatus(false),
      m_pDoc(pDoc),
      m_pXFADocLayout(nullptr),
      m_iStatus(XFA_DOCVIEW_LAYOUTSTATUS_None),
      m_iLock(0) {}

CXFA_FFDocView::~CXFA_FFDocView() {
  DestroyDocView();
}

void CXFA_FFDocView::InitLayout(CXFA_Node* pNode) {
  RunBindItems();
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Initialize, false, true,
                               nullptr);
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_IndexChange, false, true,
                               nullptr);
}
int32_t CXFA_FFDocView::StartLayout(int32_t iStartPage) {
  m_iStatus = XFA_DOCVIEW_LAYOUTSTATUS_Start;
  m_pDoc->GetXFADoc()->DoProtoMerge();
  m_pDoc->GetXFADoc()->DoDataMerge();
  m_pXFADocLayout = GetXFALayout();
  int32_t iStatus = m_pXFADocLayout->StartLayout();
  if (iStatus < 0) {
    return iStatus;
  }
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem) {
    return iStatus;
  }
  InitLayout(pRootItem);
  InitCalculate(pRootItem);
  InitValidate(pRootItem);
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, true, true, nullptr);
  m_iStatus = XFA_DOCVIEW_LAYOUTSTATUS_Start;
  return iStatus;
}

int32_t CXFA_FFDocView::DoLayout() {
  int32_t iStatus = 100;
  iStatus = m_pXFADocLayout->DoLayout();
  if (iStatus != 100)
    return iStatus;

  m_iStatus = XFA_DOCVIEW_LAYOUTSTATUS_Doing;
  return iStatus;
}

void CXFA_FFDocView::StopLayout() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem) {
    return;
  }
  CXFA_Node* pSubformNode = pRootItem->GetChild(0, XFA_Element::Subform);
  if (!pSubformNode) {
    return;
  }
  CXFA_Node* pPageSetNode =
      pSubformNode->GetFirstChildByClass(XFA_Element::PageSet);
  if (!pPageSetNode) {
    return;
  }
  RunCalculateWidgets();
  RunValidate();
  InitLayout(pPageSetNode);
  InitCalculate(pPageSetNode);
  InitValidate(pPageSetNode);
  ExecEventActivityByDeepFirst(pPageSetNode, XFA_EVENT_Ready, true, true,
                               nullptr);
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true,
                               nullptr);
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_DocReady, false, true,
                               nullptr);
  RunCalculateWidgets();
  RunValidate();
  if (RunLayout()) {
    ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true,
                                 nullptr);
  }
  m_CalculateAccs.clear();
  if (m_pFocusAcc && !m_pFocusWidget)
    SetFocusWidgetAcc(m_pFocusAcc.Get());

  m_iStatus = XFA_DOCVIEW_LAYOUTSTATUS_End;
}

int32_t CXFA_FFDocView::GetLayoutStatus() {
  return m_iStatus;
}

void CXFA_FFDocView::ShowNullTestMsg() {
  int32_t iCount = pdfium::CollectionSize<int32_t>(m_arrNullTestMsg);
  CXFA_FFApp* pApp = m_pDoc->GetApp();
  IXFA_AppProvider* pAppProvider = pApp->GetAppProvider();
  if (pAppProvider && iCount) {
    int32_t iRemain = iCount > 7 ? iCount - 7 : 0;
    iCount -= iRemain;
    CFX_WideString wsMsg;
    for (int32_t i = 0; i < iCount; i++) {
      wsMsg += m_arrNullTestMsg[i] + L"\n";
    }
    if (iRemain > 0) {
      CFX_WideString wsTemp;
      wsTemp.Format(
          L"Message limit exceeded. Remaining %d "
          L"validation errors not reported.",
          iRemain);
      wsMsg += L"\n" + wsTemp;
    }
    pAppProvider->MsgBox(wsMsg, pAppProvider->GetAppTitle(), XFA_MBICON_Status,
                         XFA_MB_OK);
  }
  m_arrNullTestMsg.clear();
}

void CXFA_FFDocView::UpdateDocView() {
  if (IsUpdateLocked())
    return;

  LockUpdate();
  for (CXFA_Node* pNode : m_NewAddedNodes) {
    InitCalculate(pNode);
    InitValidate(pNode);
    ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Ready, true, true, nullptr);
  }
  m_NewAddedNodes.clear();
  RunSubformIndexChange();
  RunCalculateWidgets();
  RunValidate();
  ShowNullTestMsg();
  if (RunLayout() && m_bLayoutEvent)
    RunEventLayoutReady();

  m_bLayoutEvent = false;
  m_CalculateAccs.clear();
  RunInvalidate();
  UnlockUpdate();
}

int32_t CXFA_FFDocView::CountPageViews() const {
  return m_pXFADocLayout ? m_pXFADocLayout->CountPages() : 0;
}

CXFA_FFPageView* CXFA_FFDocView::GetPageView(int32_t nIndex) const {
  if (!m_pXFADocLayout)
    return nullptr;
  return static_cast<CXFA_FFPageView*>(m_pXFADocLayout->GetPage(nIndex));
}

CXFA_LayoutProcessor* CXFA_FFDocView::GetXFALayout() const {
  return m_pDoc->GetXFADoc()->GetDocLayout();
}

bool CXFA_FFDocView::ResetSingleWidgetAccData(CXFA_WidgetAcc* pWidgetAcc) {
  CXFA_Node* pNode = pWidgetAcc->GetNode();
  XFA_Element eType = pNode->GetElementType();
  if (eType != XFA_Element::Field && eType != XFA_Element::ExclGroup)
    return false;

  pWidgetAcc->ResetData();
  pWidgetAcc->UpdateUIDisplay();
  if (CXFA_Validate validate = pWidgetAcc->GetValidate(false)) {
    AddValidateWidget(pWidgetAcc);
    validate.GetNode()->SetFlag(XFA_NodeFlag_NeedsInitApp, false);
  }
  return true;
}

void CXFA_FFDocView::ResetWidgetData(CXFA_WidgetAcc* pWidgetAcc) {
  m_bLayoutEvent = true;
  bool bChanged = false;
  CXFA_Node* pFormNode = nullptr;
  if (pWidgetAcc) {
    bChanged = ResetSingleWidgetAccData(pWidgetAcc);
    pFormNode = pWidgetAcc->GetNode();
  } else {
    pFormNode = GetRootSubform();
  }
  if (!pFormNode)
    return;

  if (pFormNode->GetElementType() != XFA_Element::Field &&
      pFormNode->GetElementType() != XFA_Element::ExclGroup) {
    CXFA_WidgetAccIterator Iterator(pFormNode);
    while (CXFA_WidgetAcc* pAcc = Iterator.MoveToNext()) {
      bChanged |= ResetSingleWidgetAccData(pAcc);
      if (pAcc->GetNode()->GetElementType() == XFA_Element::ExclGroup) {
        Iterator.SkipTree();
      }
    }
  }
  if (bChanged) {
    m_pDoc->GetDocEnvironment()->SetChangeMark(m_pDoc.Get());
  }
}

int32_t CXFA_FFDocView::ProcessWidgetEvent(CXFA_EventParam* pParam,
                                           CXFA_WidgetAcc* pWidgetAcc) {
  if (!pParam)
    return XFA_EVENTERROR_Error;

  if (pParam->m_eType == XFA_EVENT_Validate) {
    CFX_WideString wsValidateStr(L"preSubmit");
    CXFA_Node* pConfigItem =
        ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Config));
    if (pConfigItem) {
      CXFA_Node* pValidateNode = nullptr;
      CXFA_Node* pAcrobatNode = pConfigItem->GetChild(0, XFA_Element::Acrobat);
      pValidateNode = pAcrobatNode
                          ? pAcrobatNode->GetChild(0, XFA_Element::Validate)
                          : nullptr;
      if (!pValidateNode) {
        CXFA_Node* pPresentNode =
            pConfigItem->GetChild(0, XFA_Element::Present);
        pValidateNode = pPresentNode
                            ? pPresentNode->GetChild(0, XFA_Element::Validate)
                            : nullptr;
      }
      if (pValidateNode)
        wsValidateStr = pValidateNode->GetContent();
    }

    if (!wsValidateStr.Contains(L"preSubmit"))
      return XFA_EVENTERROR_Success;
  }

  CXFA_Node* pNode = pWidgetAcc ? pWidgetAcc->GetNode() : nullptr;
  if (!pNode) {
    CXFA_Node* pRootItem =
        ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
    if (!pRootItem)
      return XFA_EVENTERROR_Error;

    pNode = pRootItem->GetChild(0, XFA_Element::Subform);
  }
  ExecEventActivityByDeepFirst(pNode, pParam->m_eType, pParam->m_bIsFormReady,
                               true, nullptr);
  return XFA_EVENTERROR_Success;
}

CXFA_FFWidgetHandler* CXFA_FFDocView::GetWidgetHandler() {
  if (!m_pWidgetHandler)
    m_pWidgetHandler = pdfium::MakeUnique<CXFA_FFWidgetHandler>(this);
  return m_pWidgetHandler.get();
}

std::unique_ptr<CXFA_WidgetAccIterator>
CXFA_FFDocView::CreateWidgetAccIterator() {
  CXFA_Node* pFormRoot = GetRootSubform();
  if (!pFormRoot)
    return nullptr;
  return pdfium::MakeUnique<CXFA_WidgetAccIterator>(pFormRoot);
}

CXFA_FFWidget* CXFA_FFDocView::GetFocusWidget() const {
  return m_pFocusWidget.Get();
}

void CXFA_FFDocView::KillFocus() {
  if (m_pFocusWidget &&
      (m_pFocusWidget->GetStatus() & XFA_WidgetStatus_Focused)) {
    m_pFocusWidget->OnKillFocus(nullptr);
  }
  m_pFocusAcc = nullptr;
  m_pFocusWidget = nullptr;
  m_pOldFocusWidget = nullptr;
}

bool CXFA_FFDocView::SetFocus(CXFA_FFWidget* hWidget) {
  CXFA_FFWidget* pNewFocus = hWidget;
  if (m_pOldFocusWidget == pNewFocus)
    return false;

  CXFA_FFWidget* pOldFocus = m_pOldFocusWidget.Get();
  m_pOldFocusWidget = pNewFocus;
  if (pOldFocus) {
    if (m_pFocusWidget != m_pOldFocusWidget &&
        (pOldFocus->GetStatus() & XFA_WidgetStatus_Focused)) {
      m_pFocusWidget = pOldFocus;
      pOldFocus->OnKillFocus(pNewFocus);
    } else if ((pOldFocus->GetStatus() & XFA_WidgetStatus_Visible)) {
      if (!pOldFocus->IsLoaded()) {
        pOldFocus->LoadWidget();
      }
      pOldFocus->OnSetFocus(m_pFocusWidget.Get());
      m_pFocusWidget = pOldFocus;
      pOldFocus->OnKillFocus(pNewFocus);
    }
  }
  if (m_pFocusWidget == m_pOldFocusWidget)
    return false;

  pNewFocus = m_pOldFocusWidget.Get();
  if (m_pListFocusWidget && pNewFocus == m_pListFocusWidget) {
    m_pFocusAcc = nullptr;
    m_pFocusWidget = nullptr;
    m_pListFocusWidget = nullptr;
    m_pOldFocusWidget = nullptr;
    return false;
  }
  if (pNewFocus && (pNewFocus->GetStatus() & XFA_WidgetStatus_Visible)) {
    if (!pNewFocus->IsLoaded())
      pNewFocus->LoadWidget();
    pNewFocus->OnSetFocus(m_pFocusWidget.Get());
  }
  m_pFocusAcc = pNewFocus ? pNewFocus->GetDataAcc() : nullptr;
  m_pFocusWidget = pNewFocus;
  m_pOldFocusWidget = m_pFocusWidget;
  return true;
}

CXFA_WidgetAcc* CXFA_FFDocView::GetFocusWidgetAcc() {
  return m_pFocusAcc.Get();
}

void CXFA_FFDocView::SetFocusWidgetAcc(CXFA_WidgetAcc* pWidgetAcc) {
  CXFA_FFWidget* pNewFocus =
      pWidgetAcc ? pWidgetAcc->GetNextWidget(nullptr) : nullptr;
  if (SetFocus(pNewFocus)) {
    m_pFocusAcc = pWidgetAcc;
    if (m_iStatus == XFA_DOCVIEW_LAYOUTSTATUS_End)
      m_pDoc->GetDocEnvironment()->SetFocusWidget(m_pDoc.Get(),
                                                  m_pFocusWidget.Get());
  }
}

void CXFA_FFDocView::DeleteLayoutItem(CXFA_FFWidget* pWidget) {
  if (m_pFocusAcc == pWidget->GetDataAcc()) {
    m_pFocusAcc = nullptr;
    m_pFocusWidget = nullptr;
    m_pOldFocusWidget = nullptr;
  }
}

static int32_t XFA_ProcessEvent(CXFA_FFDocView* pDocView,
                                CXFA_WidgetAcc* pWidgetAcc,
                                CXFA_EventParam* pParam) {
  if (!pParam || pParam->m_eType == XFA_EVENT_Unknown) {
    return XFA_EVENTERROR_NotExist;
  }
  if (!pWidgetAcc || pWidgetAcc->GetElementType() == XFA_Element::Draw) {
    return XFA_EVENTERROR_NotExist;
  }
  switch (pParam->m_eType) {
    case XFA_EVENT_Calculate:
      return pWidgetAcc->ProcessCalculate();
    case XFA_EVENT_Validate:
      if (((CXFA_FFDoc*)pDocView->GetDoc())
              ->GetDocEnvironment()
              ->IsValidationsEnabled(pDocView->GetDoc())) {
        return pWidgetAcc->ProcessValidate(0x01);
      }
      return XFA_EVENTERROR_Disabled;
    case XFA_EVENT_InitCalculate: {
      CXFA_Calculate calc = pWidgetAcc->GetCalculate();
      if (!calc) {
        return XFA_EVENTERROR_NotExist;
      }
      if (pWidgetAcc->GetNode()->IsUserInteractive())
        return XFA_EVENTERROR_Disabled;

      CXFA_Script script = calc.GetScript();
      return pWidgetAcc->ExecuteScript(script, pParam);
    }
    default:
      break;
  }
  int32_t iRet =
      pWidgetAcc->ProcessEvent(gs_EventActivity[pParam->m_eType], pParam);
  return iRet;
}

int32_t CXFA_FFDocView::ExecEventActivityByDeepFirst(CXFA_Node* pFormNode,
                                                     XFA_EVENTTYPE eEventType,
                                                     bool bIsFormReady,
                                                     bool bRecursive,
                                                     CXFA_Node* pExclude) {
  int32_t iRet = XFA_EVENTERROR_NotExist;
  if (pFormNode == pExclude) {
    return iRet;
  }
  XFA_Element elementType = pFormNode->GetElementType();
  if (elementType == XFA_Element::Field) {
    if (eEventType == XFA_EVENT_IndexChange) {
      return iRet;
    }
    CXFA_WidgetAcc* pWidgetAcc = (CXFA_WidgetAcc*)pFormNode->GetWidgetData();
    if (!pWidgetAcc) {
      return iRet;
    }
    CXFA_EventParam eParam;
    eParam.m_eType = eEventType;
    eParam.m_pTarget = pWidgetAcc;
    eParam.m_bIsFormReady = bIsFormReady;
    return XFA_ProcessEvent(this, pWidgetAcc, &eParam);
  }
  if (bRecursive) {
    for (CXFA_Node* pNode = pFormNode->GetNodeItem(
             XFA_NODEITEM_FirstChild, XFA_ObjectType::ContainerNode);
         pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling,
                                           XFA_ObjectType::ContainerNode)) {
      elementType = pNode->GetElementType();
      if (elementType != XFA_Element::Variables &&
          elementType != XFA_Element::Draw) {
        iRet |= ExecEventActivityByDeepFirst(pNode, eEventType, bIsFormReady,
                                             bRecursive, pExclude);
      }
    }
  }
  CXFA_WidgetAcc* pWidgetAcc = (CXFA_WidgetAcc*)pFormNode->GetWidgetData();
  if (!pWidgetAcc) {
    return iRet;
  }
  CXFA_EventParam eParam;
  eParam.m_eType = eEventType;
  eParam.m_pTarget = pWidgetAcc;
  eParam.m_bIsFormReady = bIsFormReady;
  iRet |= XFA_ProcessEvent(this, pWidgetAcc, &eParam);
  return iRet;
}

CXFA_FFWidget* CXFA_FFDocView::GetWidgetByName(const CFX_WideString& wsName,
                                               CXFA_FFWidget* pRefWidget) {
  CXFA_WidgetAcc* pRefAcc = pRefWidget ? pRefWidget->GetDataAcc() : nullptr;
  CXFA_WidgetAcc* pAcc = GetWidgetAccByName(wsName, pRefAcc);
  return pAcc ? pAcc->GetNextWidget(nullptr) : nullptr;
}

CXFA_WidgetAcc* CXFA_FFDocView::GetWidgetAccByName(
    const CFX_WideString& wsName,
    CXFA_WidgetAcc* pRefWidgetAcc) {
  CFX_WideString wsExpression;
  uint32_t dwStyle = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Properties |
                     XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_Parent;
  CXFA_ScriptContext* pScriptContext = m_pDoc->GetXFADoc()->GetScriptContext();
  if (!pScriptContext)
    return nullptr;

  CXFA_Node* refNode = nullptr;
  if (pRefWidgetAcc) {
    refNode = pRefWidgetAcc->GetNode();
    wsExpression = wsName;
  } else {
    wsExpression = L"$form." + wsName;
  }
  XFA_RESOLVENODE_RS resoveNodeRS;
  int32_t iRet = pScriptContext->ResolveObjects(
      refNode, wsExpression.AsStringC(), resoveNodeRS, dwStyle);
  if (iRet < 1)
    return nullptr;

  if (resoveNodeRS.dwFlags == XFA_RESOVENODE_RSTYPE_Nodes) {
    CXFA_Node* pNode = resoveNodeRS.objects.front()->AsNode();
    if (pNode)
      return static_cast<CXFA_WidgetAcc*>(pNode->GetWidgetData());
  }
  return nullptr;
}

void CXFA_FFDocView::OnPageEvent(CXFA_ContainerLayoutItem* pSender,
                                 uint32_t dwEvent) {
  CXFA_FFPageView* pFFPageView = static_cast<CXFA_FFPageView*>(pSender);
  m_pDoc->GetDocEnvironment()->PageViewEvent(pFFPageView, dwEvent);
}

void CXFA_FFDocView::LockUpdate() {
  m_iLock++;
}

void CXFA_FFDocView::UnlockUpdate() {
  m_iLock--;
}

bool CXFA_FFDocView::IsUpdateLocked() {
  return m_iLock > 0;
}

void CXFA_FFDocView::ClearInvalidateList() {
  m_mapPageInvalidate.clear();
}

void CXFA_FFDocView::AddInvalidateRect(CXFA_FFWidget* pWidget,
                                       const CFX_RectF& rtInvalidate) {
  AddInvalidateRect(pWidget->GetPageView(), rtInvalidate);
}

void CXFA_FFDocView::AddInvalidateRect(CXFA_FFPageView* pPageView,
                                       const CFX_RectF& rtInvalidate) {
  if (m_mapPageInvalidate[pPageView]) {
    m_mapPageInvalidate[pPageView]->Union(rtInvalidate);
    return;
  }
  m_mapPageInvalidate[pPageView] = pdfium::MakeUnique<CFX_RectF>(rtInvalidate);
}

void CXFA_FFDocView::RunInvalidate() {
  for (const auto& pair : m_mapPageInvalidate)
    m_pDoc->GetDocEnvironment()->InvalidateRect(pair.first, *pair.second);
  m_mapPageInvalidate.clear();
}

bool CXFA_FFDocView::RunLayout() {
  LockUpdate();
  m_bInLayoutStatus = true;
  if (!m_pXFADocLayout->IncrementLayout() &&
      m_pXFADocLayout->StartLayout() < 100) {
    m_pXFADocLayout->DoLayout();
    UnlockUpdate();
    m_bInLayoutStatus = false;
    m_pDoc->GetDocEnvironment()->PageViewEvent(nullptr,
                                               XFA_PAGEVIEWEVENT_StopLayout);
    return true;
  }
  m_bInLayoutStatus = false;
  m_pDoc->GetDocEnvironment()->PageViewEvent(nullptr,
                                             XFA_PAGEVIEWEVENT_StopLayout);
  UnlockUpdate();
  return false;
}

void CXFA_FFDocView::RunSubformIndexChange() {
  for (CXFA_Node* pSubformNode : m_IndexChangedSubforms) {
    CXFA_WidgetAcc* pWidgetAcc =
        static_cast<CXFA_WidgetAcc*>(pSubformNode->GetWidgetData());
    if (!pWidgetAcc)
      continue;

    CXFA_EventParam eParam;
    eParam.m_eType = XFA_EVENT_IndexChange;
    eParam.m_pTarget = pWidgetAcc;
    pWidgetAcc->ProcessEvent(XFA_ATTRIBUTEENUM_IndexChange, &eParam);
  }
  m_IndexChangedSubforms.clear();
}

void CXFA_FFDocView::AddNewFormNode(CXFA_Node* pNode) {
  m_NewAddedNodes.push_back(pNode);
  InitLayout(pNode);
}

void CXFA_FFDocView::AddIndexChangedSubform(CXFA_Node* pNode) {
  ASSERT(pNode->GetElementType() == XFA_Element::Subform);
  m_IndexChangedSubforms.push_back(pNode);
}

void CXFA_FFDocView::RunDocClose() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem) {
    return;
  }
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_DocClose, false, true,
                               nullptr);
}

void CXFA_FFDocView::DestroyDocView() {
  ClearInvalidateList();
  m_iStatus = XFA_DOCVIEW_LAYOUTSTATUS_None;
  m_iLock = 0;
  m_ValidateAccs.clear();
  m_BindItems.clear();
  m_CalculateAccs.clear();
}

bool CXFA_FFDocView::IsStaticNotify() {
  return m_pDoc->GetDocType() == XFA_DocType::Static;
}

void CXFA_FFDocView::AddCalculateWidgetAcc(CXFA_WidgetAcc* pWidgetAcc) {
  CXFA_WidgetAcc* pCurrentAcc =
      !m_CalculateAccs.empty() ? m_CalculateAccs.back() : nullptr;
  if (pCurrentAcc != pWidgetAcc)
    m_CalculateAccs.push_back(pWidgetAcc);
}

void CXFA_FFDocView::AddCalculateNodeNotify(CXFA_Node* pNodeChange) {
  auto* pGlobalData =
      static_cast<CXFA_CalcData*>(pNodeChange->GetUserData(XFA_CalcData));
  if (!pGlobalData)
    return;

  for (auto* pResultAcc : pGlobalData->m_Globals) {
    if (!pResultAcc->GetNode()->HasRemovedChildren())
      AddCalculateWidgetAcc(pResultAcc);
  }
}

size_t CXFA_FFDocView::RunCalculateRecursive(size_t index) {
  while (index < m_CalculateAccs.size()) {
    CXFA_WidgetAcc* pCurAcc = m_CalculateAccs[index];
    AddCalculateNodeNotify(pCurAcc->GetNode());
    int32_t iRefCount =
        (int32_t)(uintptr_t)pCurAcc->GetNode()->GetUserData(XFA_CalcRefCount);
    iRefCount++;
    pCurAcc->GetNode()->SetUserData(XFA_CalcRefCount,
                                    (void*)(uintptr_t)iRefCount);
    if (iRefCount > 11)
      break;

    if (pCurAcc->ProcessCalculate() == XFA_EVENTERROR_Success)
      AddValidateWidget(pCurAcc);

    index = RunCalculateRecursive(++index);
  }
  return index;
}

int32_t CXFA_FFDocView::RunCalculateWidgets() {
  if (!m_pDoc->GetDocEnvironment()->IsCalculationsEnabled(m_pDoc.Get()))
    return XFA_EVENTERROR_Disabled;

  if (!m_CalculateAccs.empty())
    RunCalculateRecursive(0);

  for (CXFA_WidgetAcc* pCurAcc : m_CalculateAccs)
    pCurAcc->GetNode()->SetUserData(XFA_CalcRefCount, (void*)(uintptr_t)0);

  m_CalculateAccs.clear();
  return XFA_EVENTERROR_Success;
}

void CXFA_FFDocView::AddValidateWidget(CXFA_WidgetAcc* pWidget) {
  if (!pdfium::ContainsValue(m_ValidateAccs, pWidget))
    m_ValidateAccs.push_back(pWidget);
}

bool CXFA_FFDocView::InitCalculate(CXFA_Node* pNode) {
  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_InitCalculate, false, true,
                               nullptr);
  return true;
}

bool CXFA_FFDocView::InitValidate(CXFA_Node* pNode) {
  if (!m_pDoc->GetDocEnvironment()->IsValidationsEnabled(m_pDoc.Get()))
    return false;

  ExecEventActivityByDeepFirst(pNode, XFA_EVENT_Validate, false, true, nullptr);
  m_ValidateAccs.clear();
  return true;
}

bool CXFA_FFDocView::RunValidate() {
  if (!m_pDoc->GetDocEnvironment()->IsValidationsEnabled(m_pDoc.Get()))
    return false;

  for (CXFA_WidgetAcc* pAcc : m_ValidateAccs) {
    if (!pAcc->GetNode()->HasRemovedChildren())
      pAcc->ProcessValidate();
  }
  m_ValidateAccs.clear();
  return true;
}
bool CXFA_FFDocView::RunEventLayoutReady() {
  CXFA_Node* pRootItem =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pRootItem) {
    return false;
  }
  ExecEventActivityByDeepFirst(pRootItem, XFA_EVENT_Ready, false, true,
                               nullptr);
  RunLayout();
  return true;
}

void CXFA_FFDocView::RunBindItems() {
  for (auto* item : m_BindItems) {
    if (item->HasRemovedChildren())
      continue;

    CXFA_Node* pWidgetNode = item->GetNodeItem(XFA_NODEITEM_Parent);
    CXFA_WidgetAcc* pAcc =
        static_cast<CXFA_WidgetAcc*>(pWidgetNode->GetWidgetData());
    if (!pAcc)
      continue;

    CXFA_BindItems binditems(item);
    CXFA_ScriptContext* pScriptContext =
        pWidgetNode->GetDocument()->GetScriptContext();
    CFX_WideStringC wsRef;
    binditems.GetRef(wsRef);
    uint32_t dwStyle = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Properties |
                       XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_Parent |
                       XFA_RESOLVENODE_ALL;
    XFA_RESOLVENODE_RS rs;
    pScriptContext->ResolveObjects(pWidgetNode, wsRef, rs, dwStyle);
    pAcc->DeleteItem(-1, false, false);
    if (rs.dwFlags != XFA_RESOVENODE_RSTYPE_Nodes || rs.objects.empty())
      continue;

    CFX_WideStringC wsValueRef, wsLabelRef;
    binditems.GetValueRef(wsValueRef);
    binditems.GetLabelRef(wsLabelRef);
    const bool bUseValue = wsLabelRef.IsEmpty() || wsLabelRef == wsValueRef;
    const bool bLabelUseContent = wsLabelRef.IsEmpty() || wsLabelRef == L"$";
    const bool bValueUseContent = wsValueRef.IsEmpty() || wsValueRef == L"$";
    CFX_WideString wsValue;
    CFX_WideString wsLabel;
    uint32_t uValueHash = FX_HashCode_GetW(wsValueRef, false);
    for (CXFA_Object* refObject : rs.objects) {
      CXFA_Node* refNode = refObject->AsNode();
      if (!refNode)
        continue;
      if (bValueUseContent) {
        wsValue = refNode->GetContent();
      } else {
        CXFA_Node* nodeValue = refNode->GetFirstChildByName(uValueHash);
        wsValue = nodeValue ? nodeValue->GetContent() : refNode->GetContent();
      }
      if (!bUseValue) {
        if (bLabelUseContent) {
          wsLabel = refNode->GetContent();
        } else {
          CXFA_Node* nodeLabel = refNode->GetFirstChildByName(wsLabelRef);
          if (nodeLabel)
            wsLabel = nodeLabel->GetContent();
        }
      } else {
        wsLabel = wsValue;
      }
      pAcc->InsertItem(wsLabel, wsValue, false);
    }
  }
  m_BindItems.clear();
}

void CXFA_FFDocView::SetChangeMark() {
  if (m_iStatus < XFA_DOCVIEW_LAYOUTSTATUS_End)
    return;

  m_pDoc->GetDocEnvironment()->SetChangeMark(m_pDoc.Get());
}

CXFA_Node* CXFA_FFDocView::GetRootSubform() {
  CXFA_Node* pFormPacketNode =
      ToNode(m_pDoc->GetXFADoc()->GetXFAObject(XFA_HASHCODE_Form));
  if (!pFormPacketNode)
    return nullptr;

  return pFormPacketNode->GetFirstChildByClass(XFA_Element::Subform);
}

// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/parser/cxfa_layoutpagemgr.h"

#include "third_party/base/stl_util.h"
#include "xfa/fxfa/cxfa_ffnotify.h"
#include "xfa/fxfa/parser/cxfa_containerlayoutitem.h"
#include "xfa/fxfa/parser/cxfa_contentlayoutitem.h"
#include "xfa/fxfa/parser/cxfa_document.h"
#include "xfa/fxfa/parser/cxfa_itemlayoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_localemgr.h"
#include "xfa/fxfa/parser/cxfa_measurement.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_object.h"
#include "xfa/fxfa/parser/cxfa_scriptcontext.h"
#include "xfa/fxfa/parser/cxfa_traversestrategy_contentareacontainerlayoutitem.h"
#include "xfa/fxfa/parser/cxfa_traversestrategy_layoutitem.h"
#include "xfa/fxfa/parser/cxfa_traversestrategy_xfacontainernode.h"
#include "xfa/fxfa/parser/cxfa_traversestrategy_xfanode.h"
#include "xfa/fxfa/parser/xfa_document_datamerger_imp.h"
#include "xfa/fxfa/parser/xfa_resolvenode_rs.h"
#include "xfa/fxfa/parser/xfa_utils.h"

namespace {

class PageSetContainerLayoutItem {
 public:
  static CXFA_ContainerLayoutItem* GetFirstChild(
      CXFA_ContainerLayoutItem* pLayoutItem) {
    if (pLayoutItem->m_pFormNode->GetElementType() != XFA_Element::PageSet)
      return nullptr;

    CXFA_ContainerLayoutItem* pChildItem =
        static_cast<CXFA_ContainerLayoutItem*>(pLayoutItem->m_pFirstChild);
    while (pChildItem &&
           pChildItem->m_pFormNode->GetElementType() != XFA_Element::PageSet) {
      pChildItem =
          static_cast<CXFA_ContainerLayoutItem*>(pChildItem->m_pNextSibling);
    }
    return pChildItem;
  }

  static CXFA_ContainerLayoutItem* GetNextSibling(
      CXFA_ContainerLayoutItem* pLayoutItem) {
    CXFA_ContainerLayoutItem* pChildItem =
        static_cast<CXFA_ContainerLayoutItem*>(pLayoutItem->m_pNextSibling);
    while (pChildItem &&
           pChildItem->m_pFormNode->GetElementType() != XFA_Element::PageSet) {
      pChildItem =
          static_cast<CXFA_ContainerLayoutItem*>(pChildItem->m_pNextSibling);
    }
    return pChildItem;
  }

  static CXFA_ContainerLayoutItem* GetParent(
      CXFA_ContainerLayoutItem* pLayoutItem) {
    return static_cast<CXFA_ContainerLayoutItem*>(pLayoutItem->m_pParent);
  }
};

uint32_t GetRelevant(CXFA_Node* pFormItem, uint32_t dwParentRelvant) {
  uint32_t dwRelevant = XFA_WidgetStatus_Viewable | XFA_WidgetStatus_Printable;
  CFX_WideStringC wsRelevant;
  if (pFormItem->TryCData(XFA_ATTRIBUTE_Relevant, wsRelevant)) {
    if (wsRelevant == L"+print" || wsRelevant == L"print")
      dwRelevant &= ~XFA_WidgetStatus_Viewable;
    else if (wsRelevant == L"-print")
      dwRelevant &= ~XFA_WidgetStatus_Printable;
  }

  if (!(dwParentRelvant & XFA_WidgetStatus_Viewable) &&
      (dwRelevant != XFA_WidgetStatus_Viewable)) {
    dwRelevant &= ~XFA_WidgetStatus_Viewable;
  }

  if (!(dwParentRelvant & XFA_WidgetStatus_Printable) &&
      (dwRelevant != XFA_WidgetStatus_Printable)) {
    dwRelevant &= ~XFA_WidgetStatus_Printable;
  }
  return dwRelevant;
}

void SyncContainer(CXFA_FFNotify* pNotify,
                   CXFA_LayoutProcessor* pDocLayout,
                   CXFA_LayoutItem* pContainerItem,
                   uint32_t dwRelevant,
                   bool bVisible,
                   int32_t nPageIndex) {
  bool bVisibleItem = false;
  uint32_t dwStatus = 0;
  uint32_t dwRelevantContainer = 0;
  if (bVisible) {
    XFA_ATTRIBUTEENUM eAttributeValue =
        pContainerItem->m_pFormNode->GetEnum(XFA_ATTRIBUTE_Presence);
    if (eAttributeValue == XFA_ATTRIBUTEENUM_Visible ||
        eAttributeValue == XFA_ATTRIBUTEENUM_Unknown) {
      bVisibleItem = true;
    }
    dwRelevantContainer = GetRelevant(pContainerItem->m_pFormNode, dwRelevant);
    dwStatus =
        (bVisibleItem ? XFA_WidgetStatus_Visible : 0) | dwRelevantContainer;
  }
  pNotify->OnLayoutItemAdded(pDocLayout, pContainerItem, nPageIndex, dwStatus);
  for (CXFA_LayoutItem* pChild = pContainerItem->m_pFirstChild; pChild;
       pChild = pChild->m_pNextSibling) {
    if (pChild->IsContentLayoutItem()) {
      SyncContainer(pNotify, pDocLayout, pChild, dwRelevantContainer,
                    bVisibleItem, nPageIndex);
    }
  }
}

void ReorderLayoutItemToTail(CXFA_ContainerLayoutItem* pLayoutItem) {
  CXFA_ContainerLayoutItem* pParentLayoutItem =
      static_cast<CXFA_ContainerLayoutItem*>(pLayoutItem->m_pParent);
  if (!pParentLayoutItem)
    return;

  pParentLayoutItem->RemoveChild(pLayoutItem);
  pParentLayoutItem->AddChild(pLayoutItem);
}

void RemoveLayoutItem(CXFA_ContainerLayoutItem* pLayoutItem) {
  CXFA_ContainerLayoutItem* pParentLayoutItem =
      static_cast<CXFA_ContainerLayoutItem*>(pLayoutItem->m_pParent);
  if (!pParentLayoutItem)
    return;

  pParentLayoutItem->RemoveChild(pLayoutItem);
}

CXFA_Node* ResolveBreakTarget(CXFA_Node* pPageSetRoot,
                              bool bNewExprStyle,
                              CFX_WideStringC& wsTargetExpr) {
  CXFA_Document* pDocument = pPageSetRoot->GetDocument();
  if (wsTargetExpr.IsEmpty())
    return nullptr;

  CFX_WideString wsTargetAll(wsTargetExpr);
  wsTargetAll.TrimLeft();
  wsTargetAll.TrimRight();
  int32_t iSplitIndex = 0;
  bool bTargetAllFind = true;
  while (iSplitIndex != -1) {
    CFX_WideString wsExpr;
    pdfium::Optional<FX_STRSIZE> iSplitNextIndex = 0;
    if (!bTargetAllFind) {
      iSplitNextIndex = wsTargetAll.Find(' ', iSplitIndex);
      if (!iSplitNextIndex.has_value())
        return nullptr;
      wsExpr =
          wsTargetAll.Mid(iSplitIndex, iSplitNextIndex.value() - iSplitIndex);
    } else {
      wsExpr = wsTargetAll;
    }
    if (wsExpr.IsEmpty())
      return nullptr;

    bTargetAllFind = false;
    if (wsExpr[0] == '#') {
      CXFA_Node* pNode = pDocument->GetNodeByID(
          ToNode(pDocument->GetXFAObject(XFA_HASHCODE_Template)),
          wsExpr.Right(wsExpr.GetLength() - 1).AsStringC());
      if (pNode)
        return pNode;
    } else if (bNewExprStyle) {
      CFX_WideString wsProcessedTarget = wsExpr;
      if (wsExpr.Left(4) == L"som(" && wsExpr.Right(1) == L")") {
        wsProcessedTarget = wsExpr.Mid(4, wsExpr.GetLength() - 5);
      }
      XFA_RESOLVENODE_RS rs;
      int32_t iCount = pDocument->GetScriptContext()->ResolveObjects(
          pPageSetRoot, wsProcessedTarget.AsStringC(), rs,
          XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Properties |
              XFA_RESOLVENODE_Attributes | XFA_RESOLVENODE_Siblings |
              XFA_RESOLVENODE_Parent);
      if (iCount > 0 && rs.objects.front()->IsNode())
        return rs.objects.front()->AsNode();
    }
    iSplitIndex = iSplitNextIndex.value();
  }
  return nullptr;
}

void SetLayoutGeneratedNodeFlag(CXFA_Node* pNode) {
  pNode->SetFlag(XFA_NodeFlag_LayoutGeneratedNode, false);
  pNode->ClearFlag(XFA_NodeFlag_UnusedNode);
}

bool CheckContentAreaNotUsed(
    CXFA_ContainerLayoutItem* pPageAreaLayoutItem,
    CXFA_Node* pContentArea,
    CXFA_ContainerLayoutItem*& pContentAreaLayoutItem) {
  for (CXFA_ContainerLayoutItem* pLayoutItem =
           static_cast<CXFA_ContainerLayoutItem*>(
               pPageAreaLayoutItem->m_pFirstChild);
       pLayoutItem; pLayoutItem = static_cast<CXFA_ContainerLayoutItem*>(
                        pLayoutItem->m_pNextSibling)) {
    if (pLayoutItem->m_pFormNode == pContentArea) {
      if (!pLayoutItem->m_pFirstChild) {
        pContentAreaLayoutItem = pLayoutItem;
        return true;
      }
      return false;
    }
  }
  return true;
}

void SyncRemoveLayoutItem(CXFA_LayoutItem* pParentLayoutItem,
                          CXFA_FFNotify* pNotify,
                          CXFA_LayoutProcessor* pDocLayout) {
  CXFA_LayoutItem* pNextLayoutItem;
  CXFA_LayoutItem* pCurLayoutItem = pParentLayoutItem->m_pFirstChild;
  while (pCurLayoutItem) {
    pNextLayoutItem = pCurLayoutItem->m_pNextSibling;
    if (pCurLayoutItem->m_pFirstChild)
      SyncRemoveLayoutItem(pCurLayoutItem, pNotify, pDocLayout);

    pNotify->OnLayoutItemRemoving(pDocLayout, pCurLayoutItem);
    delete pCurLayoutItem;
    pCurLayoutItem = pNextLayoutItem;
  }
}

}  // namespace

class CXFA_ContainerRecord {
 public:
  CXFA_ContainerRecord(CXFA_ContainerLayoutItem* pPageSet = nullptr,
                       CXFA_ContainerLayoutItem* pPageArea = nullptr,
                       CXFA_ContainerLayoutItem* pContentArea = nullptr)
      : pCurPageSet(pPageSet),
        pCurPageArea(pPageArea),
        pCurContentArea(pContentArea) {}

  CXFA_ContainerLayoutItem* pCurPageSet;
  CXFA_ContainerLayoutItem* pCurPageArea;
  CXFA_ContainerLayoutItem* pCurContentArea;
};

CXFA_LayoutPageMgr::CXFA_LayoutPageMgr(CXFA_LayoutProcessor* pLayoutProcessor)
    : m_pLayoutProcessor(pLayoutProcessor),
      m_pTemplatePageSetRoot(nullptr),
      m_pPageSetLayoutItemRoot(nullptr),
      m_pPageSetCurRoot(nullptr),
      m_CurrentContainerRecordIter(m_ProposedContainerRecords.end()),
      m_pCurPageArea(nullptr),
      m_nAvailPages(0),
      m_nCurPageCount(0),
      m_ePageSetMode(XFA_ATTRIBUTEENUM_OrderedOccurrence),
      m_bCreateOverFlowPage(false) {}

CXFA_LayoutPageMgr::~CXFA_LayoutPageMgr() {
  ClearData();
  CXFA_LayoutItem* pLayoutItem = GetRootLayoutItem();
  CXFA_LayoutItem* pNextLayout = nullptr;
  for (; pLayoutItem; pLayoutItem = pNextLayout) {
    pNextLayout = pLayoutItem->m_pNextSibling;
    XFA_ReleaseLayoutItem(pLayoutItem);
  }
}

bool CXFA_LayoutPageMgr::InitLayoutPage(CXFA_Node* pFormNode) {
  PrepareLayout();
  CXFA_Node* pTemplateNode = pFormNode->GetTemplateNode();
  if (!pTemplateNode)
    return false;

  m_pTemplatePageSetRoot = pTemplateNode->GetProperty(0, XFA_Element::PageSet);
  ASSERT(m_pTemplatePageSetRoot);
  if (m_pPageSetLayoutItemRoot) {
    m_pPageSetLayoutItemRoot->m_pParent = nullptr;
    m_pPageSetLayoutItemRoot->m_pFirstChild = nullptr;
    m_pPageSetLayoutItemRoot->m_pNextSibling = nullptr;
    m_pPageSetLayoutItemRoot->m_pFormNode = m_pTemplatePageSetRoot;
  } else {
    m_pPageSetLayoutItemRoot =
        new CXFA_ContainerLayoutItem(m_pTemplatePageSetRoot);
  }
  m_pPageSetCurRoot = m_pPageSetLayoutItemRoot;
  m_pTemplatePageSetRoot->SetUserData(XFA_LAYOUTITEMKEY,
                                      (void*)m_pPageSetLayoutItemRoot);
  XFA_ATTRIBUTEENUM eRelation =
      m_pTemplatePageSetRoot->GetEnum(XFA_ATTRIBUTE_Relation);
  if (eRelation != XFA_ATTRIBUTEENUM_Unknown)
    m_ePageSetMode = eRelation;

  InitPageSetMap();
  CXFA_Node* pPageArea = nullptr;
  int32_t iCount = 0;
  for (pPageArea = m_pTemplatePageSetRoot->GetNodeItem(XFA_NODEITEM_FirstChild);
       pPageArea;
       pPageArea = pPageArea->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pPageArea->GetElementType() == XFA_Element::PageArea) {
      iCount++;
      if (pPageArea->GetFirstChildByClass(XFA_Element::ContentArea))
        return true;
    }
  }
  if (iCount > 0)
    return false;

  CXFA_Document* pDocument = pTemplateNode->GetDocument();
  pPageArea = m_pTemplatePageSetRoot->GetChild(0, XFA_Element::PageArea);
  if (!pPageArea) {
    pPageArea = pDocument->CreateNode(m_pTemplatePageSetRoot->GetPacketID(),
                                      XFA_Element::PageArea);
    if (!pPageArea)
      return false;

    m_pTemplatePageSetRoot->InsertChild(pPageArea, nullptr);
    pPageArea->SetFlag(XFA_NodeFlag_Initialized, true);
  }
  CXFA_Node* pContentArea = pPageArea->GetChild(0, XFA_Element::ContentArea);
  if (!pContentArea) {
    pContentArea = pDocument->CreateNode(pPageArea->GetPacketID(),
                                         XFA_Element::ContentArea);
    if (!pContentArea)
      return false;

    pPageArea->InsertChild(pContentArea, nullptr);
    pContentArea->SetFlag(XFA_NodeFlag_Initialized, true);
    pContentArea->SetMeasure(XFA_ATTRIBUTE_X,
                             CXFA_Measurement(0.25f, XFA_UNIT_In));
    pContentArea->SetMeasure(XFA_ATTRIBUTE_Y,
                             CXFA_Measurement(0.25f, XFA_UNIT_In));
    pContentArea->SetMeasure(XFA_ATTRIBUTE_W,
                             CXFA_Measurement(8.0f, XFA_UNIT_In));
    pContentArea->SetMeasure(XFA_ATTRIBUTE_H,
                             CXFA_Measurement(10.5f, XFA_UNIT_In));
  }
  CXFA_Node* pMedium = pPageArea->GetChild(0, XFA_Element::Medium);
  if (!pMedium) {
    pMedium =
        pDocument->CreateNode(pPageArea->GetPacketID(), XFA_Element::Medium);
    if (!pContentArea)
      return false;

    pPageArea->InsertChild(pMedium, nullptr);
    pMedium->SetFlag(XFA_NodeFlag_Initialized, true);
    pMedium->SetMeasure(XFA_ATTRIBUTE_Short,
                        CXFA_Measurement(8.5f, XFA_UNIT_In));
    pMedium->SetMeasure(XFA_ATTRIBUTE_Long,
                        CXFA_Measurement(11.0f, XFA_UNIT_In));
  }
  return true;
}

bool CXFA_LayoutPageMgr::PrepareFirstPage(CXFA_Node* pRootSubform) {
  bool bProBreakBefore = false;
  CXFA_Node* pBreakBeforeNode = nullptr;
  while (pRootSubform) {
    for (CXFA_Node* pBreakNode =
             pRootSubform->GetNodeItem(XFA_NODEITEM_FirstChild);
         pBreakNode;
         pBreakNode = pBreakNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      XFA_Element eType = pBreakNode->GetElementType();
      if (eType == XFA_Element::BreakBefore ||
          (eType == XFA_Element::Break &&
           pBreakNode->GetEnum(XFA_ATTRIBUTE_Before) !=
               XFA_ATTRIBUTEENUM_Auto)) {
        bProBreakBefore = true;
        pBreakBeforeNode = pBreakNode;
        break;
      }
    }
    if (bProBreakBefore)
      break;

    bProBreakBefore = true;
    pRootSubform = pRootSubform->GetFirstChildByClass(XFA_Element::Subform);
    while (pRootSubform &&
           !XFA_ItemLayoutProcessor_IsTakingSpace(pRootSubform)) {
      pRootSubform =
          pRootSubform->GetNextSameClassSibling(XFA_Element::Subform);
    }
  }
  CXFA_Node *pLeader, *pTrailer;
  if (pBreakBeforeNode &&
      ExecuteBreakBeforeOrAfter(pBreakBeforeNode, true, pLeader, pTrailer)) {
    m_CurrentContainerRecordIter = m_ProposedContainerRecords.begin();
    return true;
  }
  return AppendNewPage(true);
}

bool CXFA_LayoutPageMgr::AppendNewPage(bool bFirstTemPage) {
  if (m_CurrentContainerRecordIter != GetTailPosition())
    return true;

  CXFA_Node* pPageNode = GetNextAvailPageArea(nullptr);
  if (!pPageNode)
    return false;

  if (bFirstTemPage &&
      m_CurrentContainerRecordIter == m_ProposedContainerRecords.end()) {
    m_CurrentContainerRecordIter = m_ProposedContainerRecords.begin();
  }
  return !bFirstTemPage ||
         m_CurrentContainerRecordIter != m_ProposedContainerRecords.end();
}

void CXFA_LayoutPageMgr::RemoveLayoutRecord(CXFA_ContainerRecord* pNewRecord,
                                            CXFA_ContainerRecord* pPrevRecord) {
  if (!pNewRecord || !pPrevRecord)
    return;
  if (pNewRecord->pCurPageSet != pPrevRecord->pCurPageSet) {
    RemoveLayoutItem(pNewRecord->pCurPageSet);
    return;
  }
  if (pNewRecord->pCurPageArea != pPrevRecord->pCurPageArea) {
    RemoveLayoutItem(pNewRecord->pCurPageArea);
    return;
  }
  if (pNewRecord->pCurContentArea != pPrevRecord->pCurContentArea) {
    RemoveLayoutItem(pNewRecord->pCurContentArea);
    return;
  }
}

void CXFA_LayoutPageMgr::ReorderPendingLayoutRecordToTail(
    CXFA_ContainerRecord* pNewRecord,
    CXFA_ContainerRecord* pPrevRecord) {
  if (!pNewRecord || !pPrevRecord)
    return;
  if (pNewRecord->pCurPageSet != pPrevRecord->pCurPageSet) {
    ReorderLayoutItemToTail(pNewRecord->pCurPageSet);
    return;
  }
  if (pNewRecord->pCurPageArea != pPrevRecord->pCurPageArea) {
    ReorderLayoutItemToTail(pNewRecord->pCurPageArea);
    return;
  }
  if (pNewRecord->pCurContentArea != pPrevRecord->pCurContentArea) {
    ReorderLayoutItemToTail(pNewRecord->pCurContentArea);
    return;
  }
}

void CXFA_LayoutPageMgr::SubmitContentItem(
    CXFA_ContentLayoutItem* pContentLayoutItem,
    XFA_ItemLayoutProcessorResult eStatus) {
  if (pContentLayoutItem) {
    GetCurrentContainerRecord()->pCurContentArea->AddChild(pContentLayoutItem);
    m_bCreateOverFlowPage = false;
  }

  if (eStatus != XFA_ItemLayoutProcessorResult::Done) {
    if (eStatus == XFA_ItemLayoutProcessorResult::PageFullBreak &&
        m_CurrentContainerRecordIter == GetTailPosition()) {
      AppendNewPage();
    }
    m_CurrentContainerRecordIter = GetTailPosition();
    m_pCurPageArea = GetCurrentContainerRecord()->pCurPageArea->m_pFormNode;
  }
}

float CXFA_LayoutPageMgr::GetAvailHeight() {
  CXFA_ContainerLayoutItem* pLayoutItem =
      GetCurrentContainerRecord()->pCurContentArea;
  if (!pLayoutItem || !pLayoutItem->m_pFormNode)
    return 0.0f;

  float fAvailHeight =
      pLayoutItem->m_pFormNode->GetMeasure(XFA_ATTRIBUTE_H).ToUnit(XFA_UNIT_Pt);
  if (fAvailHeight >= XFA_LAYOUT_FLOAT_PERCISION)
    return fAvailHeight;
  if (m_CurrentContainerRecordIter == m_ProposedContainerRecords.begin())
    return 0.0f;
  return FLT_MAX;
}

bool XFA_LayoutPageMgr_RunBreakTestScript(CXFA_Node* pTestScript) {
  CFX_WideString wsExpression;
  pTestScript->TryContent(wsExpression);
  if (wsExpression.IsEmpty())
    return true;
  return pTestScript->GetDocument()->GetNotify()->RunScript(
      pTestScript, pTestScript->GetNodeItem(XFA_NODEITEM_Parent,
                                            XFA_ObjectType::ContainerNode));
}

CXFA_ContainerRecord* CXFA_LayoutPageMgr::CreateContainerRecord(
    CXFA_Node* pPageNode,
    bool bCreateNew) {
  CXFA_ContainerRecord* pNewRecord = new CXFA_ContainerRecord();
  if (m_CurrentContainerRecordIter != m_ProposedContainerRecords.end()) {
    if (!IsPageSetRootOrderedOccurrence() || !pPageNode) {
      *pNewRecord = *GetCurrentContainerRecord();
      m_ProposedContainerRecords.push_back(pNewRecord);
      return pNewRecord;
    }
    CXFA_Node* pPageSet = pPageNode->GetNodeItem(XFA_NODEITEM_Parent);
    if (!bCreateNew) {
      if (pPageSet == m_pTemplatePageSetRoot) {
        pNewRecord->pCurPageSet = m_pPageSetCurRoot;
      } else {
        CXFA_ContainerLayoutItem* pParentLayoutItem =
            static_cast<CXFA_ContainerLayoutItem*>(
                pPageSet->GetUserData(XFA_LAYOUTITEMKEY));
        if (!pParentLayoutItem)
          pParentLayoutItem = m_pPageSetCurRoot;

        pNewRecord->pCurPageSet = pParentLayoutItem;
      }
    } else {
      CXFA_ContainerLayoutItem* pParentPageSetLayout = nullptr;
      if (pPageSet == GetCurrentContainerRecord()->pCurPageSet->m_pFormNode) {
        pParentPageSetLayout = static_cast<CXFA_ContainerLayoutItem*>(
            GetCurrentContainerRecord()->pCurPageSet->m_pParent);
      } else {
        pParentPageSetLayout = static_cast<CXFA_ContainerLayoutItem*>(
            pPageSet->GetNodeItem(XFA_NODEITEM_Parent)
                ->GetUserData(XFA_LAYOUTITEMKEY));
      }
      CXFA_ContainerLayoutItem* pPageSetLayoutItem =
          new CXFA_ContainerLayoutItem(pPageSet);
      pPageSet->SetUserData(XFA_LAYOUTITEMKEY, (void*)pPageSetLayoutItem);
      if (!pParentPageSetLayout) {
        CXFA_ContainerLayoutItem* pPrePageSet = m_pPageSetLayoutItemRoot;
        while (pPrePageSet->m_pNextSibling) {
          pPrePageSet = static_cast<CXFA_ContainerLayoutItem*>(
              pPrePageSet->m_pNextSibling);
        }

        pPrePageSet->m_pNextSibling = pPageSetLayoutItem;
        m_pPageSetCurRoot = pPageSetLayoutItem;
      } else {
        pParentPageSetLayout->AddChild(pPageSetLayoutItem);
      }
      pNewRecord->pCurPageSet = pPageSetLayoutItem;
    }
  } else {
    if (pPageNode) {
      CXFA_Node* pPageSet = pPageNode->GetNodeItem(XFA_NODEITEM_Parent);
      if (pPageSet == m_pTemplatePageSetRoot) {
        pNewRecord->pCurPageSet = m_pPageSetLayoutItemRoot;
      } else {
        CXFA_ContainerLayoutItem* pPageSetLayoutItem =
            new CXFA_ContainerLayoutItem(pPageSet);
        pPageSet->SetUserData(XFA_LAYOUTITEMKEY, (void*)pPageSetLayoutItem);
        m_pPageSetLayoutItemRoot->AddChild(pPageSetLayoutItem);
        pNewRecord->pCurPageSet = pPageSetLayoutItem;
      }
    } else {
      pNewRecord->pCurPageSet = m_pPageSetLayoutItemRoot;
    }
  }
  m_ProposedContainerRecords.push_back(pNewRecord);
  return pNewRecord;
}

void CXFA_LayoutPageMgr::AddPageAreaLayoutItem(CXFA_ContainerRecord* pNewRecord,
                                               CXFA_Node* pNewPageArea) {
  CXFA_ContainerLayoutItem* pNewPageAreaLayoutItem = nullptr;
  if (pdfium::IndexInBounds(m_PageArray, m_nAvailPages)) {
    CXFA_ContainerLayoutItem* pContainerItem = m_PageArray[m_nAvailPages];
    pContainerItem->m_pFormNode = pNewPageArea;
    m_nAvailPages++;
    pNewPageAreaLayoutItem = pContainerItem;
  } else {
    CXFA_FFNotify* pNotify = pNewPageArea->GetDocument()->GetNotify();
    auto* pContainerItem = static_cast<CXFA_ContainerLayoutItem*>(
        pNotify->OnCreateLayoutItem(pNewPageArea));
    m_PageArray.push_back(pContainerItem);
    m_nAvailPages++;
    pNotify->OnPageEvent(pContainerItem, XFA_PAGEVIEWEVENT_PostRemoved);
    pNewPageAreaLayoutItem = pContainerItem;
  }
  pNewRecord->pCurPageSet->AddChild(pNewPageAreaLayoutItem);
  pNewRecord->pCurPageArea = pNewPageAreaLayoutItem;
  pNewRecord->pCurContentArea = nullptr;
}

void CXFA_LayoutPageMgr::AddContentAreaLayoutItem(
    CXFA_ContainerRecord* pNewRecord,
    CXFA_Node* pContentArea) {
  if (!pContentArea) {
    pNewRecord->pCurContentArea = nullptr;
    return;
  }
  CXFA_ContainerLayoutItem* pNewContentAreaLayoutItem =
      new CXFA_ContainerLayoutItem(pContentArea);
  ASSERT(pNewRecord->pCurPageArea);
  pNewRecord->pCurPageArea->AddChild(pNewContentAreaLayoutItem);
  pNewRecord->pCurContentArea = pNewContentAreaLayoutItem;
}

void CXFA_LayoutPageMgr::FinishPaginatedPageSets() {
  CXFA_ContainerLayoutItem* pRootPageSetLayoutItem = m_pPageSetLayoutItemRoot;
  for (; pRootPageSetLayoutItem;
       pRootPageSetLayoutItem = static_cast<CXFA_ContainerLayoutItem*>(
           pRootPageSetLayoutItem->m_pNextSibling)) {
    CXFA_NodeIteratorTemplate<CXFA_ContainerLayoutItem,
                              PageSetContainerLayoutItem>
        sIterator(pRootPageSetLayoutItem);
    for (CXFA_ContainerLayoutItem* pPageSetLayoutItem = sIterator.GetCurrent();
         pPageSetLayoutItem; pPageSetLayoutItem = sIterator.MoveToNext()) {
      XFA_ATTRIBUTEENUM ePageRelation =
          pPageSetLayoutItem->m_pFormNode->GetEnum(XFA_ATTRIBUTE_Relation);
      switch (ePageRelation) {
        case XFA_ATTRIBUTEENUM_OrderedOccurrence:
        default: { ProcessLastPageSet(); } break;
        case XFA_ATTRIBUTEENUM_SimplexPaginated:
        case XFA_ATTRIBUTEENUM_DuplexPaginated: {
          CXFA_LayoutItem* pLastPageAreaLayoutItem = nullptr;
          int32_t nPageAreaCount = 0;
          for (CXFA_LayoutItem* pPageAreaLayoutItem =
                   pPageSetLayoutItem->m_pFirstChild;
               pPageAreaLayoutItem;
               pPageAreaLayoutItem = pPageAreaLayoutItem->m_pNextSibling) {
            if (pPageAreaLayoutItem->m_pFormNode->GetElementType() !=
                XFA_Element::PageArea) {
              continue;
            }
            nPageAreaCount++;
            pLastPageAreaLayoutItem = pPageAreaLayoutItem;
          }
          if (!pLastPageAreaLayoutItem)
            break;

          if (!FindPageAreaFromPageSet_SimplexDuplex(
                  pPageSetLayoutItem->m_pFormNode, nullptr, nullptr, nullptr,
                  true, true, nPageAreaCount == 1 ? XFA_ATTRIBUTEENUM_Only
                                                  : XFA_ATTRIBUTEENUM_Last) &&
              (nPageAreaCount == 1 &&
               !FindPageAreaFromPageSet_SimplexDuplex(
                   pPageSetLayoutItem->m_pFormNode, nullptr, nullptr, nullptr,
                   true, true, XFA_ATTRIBUTEENUM_Last))) {
            break;
          }
          CXFA_Node* pNode = m_pCurPageArea;
          XFA_ATTRIBUTEENUM eCurChoice =
              pNode->GetEnum(XFA_ATTRIBUTE_PagePosition);
          if (eCurChoice == XFA_ATTRIBUTEENUM_Last) {
            XFA_ATTRIBUTEENUM eOddOrEven = XFA_ATTRIBUTEENUM_Any;
            pNode->TryEnum(XFA_ATTRIBUTE_OddOrEven, eOddOrEven);
            XFA_ATTRIBUTEENUM eLastChoice =
                pLastPageAreaLayoutItem->m_pFormNode->GetEnum(
                    XFA_ATTRIBUTE_PagePosition);
            if (eLastChoice == XFA_ATTRIBUTEENUM_First &&
                (ePageRelation == XFA_ATTRIBUTEENUM_SimplexPaginated ||
                 eOddOrEven != XFA_ATTRIBUTEENUM_Odd)) {
              CXFA_ContainerRecord* pRecord = CreateContainerRecord();
              AddPageAreaLayoutItem(pRecord, pNode);
              break;
            }
          }
          bool bUsable = true;
          std::vector<float> rgUsedHeights;
          for (CXFA_LayoutItem* pChildLayoutItem =
                   pLastPageAreaLayoutItem->m_pFirstChild;
               pChildLayoutItem;
               pChildLayoutItem = pChildLayoutItem->m_pNextSibling) {
            if (pChildLayoutItem->m_pFormNode->GetElementType() !=
                XFA_Element::ContentArea) {
              continue;
            }
            float fUsedHeight = 0;
            for (CXFA_LayoutItem* pContentChildLayoutItem =
                     pChildLayoutItem->m_pFirstChild;
                 pContentChildLayoutItem;
                 pContentChildLayoutItem =
                     pContentChildLayoutItem->m_pNextSibling) {
              if (CXFA_ContentLayoutItem* pContent =
                      pContentChildLayoutItem->AsContentLayoutItem()) {
                fUsedHeight += pContent->m_sSize.height;
              }
            }
            rgUsedHeights.push_back(fUsedHeight);
          }
          int32_t iCurContentAreaIndex = -1;
          for (CXFA_Node* pContentAreaNode =
                   pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
               pContentAreaNode;
               pContentAreaNode =
                   pContentAreaNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
            if (pContentAreaNode->GetElementType() !=
                XFA_Element::ContentArea) {
              continue;
            }
            iCurContentAreaIndex++;
            if (rgUsedHeights[iCurContentAreaIndex] >
                pContentAreaNode->GetMeasure(XFA_ATTRIBUTE_H)
                        .ToUnit(XFA_UNIT_Pt) +
                    XFA_LAYOUT_FLOAT_PERCISION) {
              bUsable = false;
              break;
            }
          }
          if (bUsable) {
            CXFA_LayoutItem* pChildLayoutItem =
                pLastPageAreaLayoutItem->m_pFirstChild;
            CXFA_Node* pContentAreaNode =
                pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
            pLastPageAreaLayoutItem->m_pFormNode = pNode;
            while (pChildLayoutItem && pContentAreaNode) {
              if (pChildLayoutItem->m_pFormNode->GetElementType() !=
                  XFA_Element::ContentArea) {
                pChildLayoutItem = pChildLayoutItem->m_pNextSibling;
                continue;
              }
              if (pContentAreaNode->GetElementType() !=
                  XFA_Element::ContentArea) {
                pContentAreaNode =
                    pContentAreaNode->GetNodeItem(XFA_NODEITEM_NextSibling);
                continue;
              }
              pChildLayoutItem->m_pFormNode = pContentAreaNode;
              pChildLayoutItem = pChildLayoutItem->m_pNextSibling;
              pContentAreaNode =
                  pContentAreaNode->GetNodeItem(XFA_NODEITEM_NextSibling);
            }
          } else if (pNode->GetEnum(XFA_ATTRIBUTE_PagePosition) ==
                     XFA_ATTRIBUTEENUM_Last) {
            CXFA_ContainerRecord* pRecord = CreateContainerRecord();
            AddPageAreaLayoutItem(pRecord, pNode);
          }
        } break;
      }
    }
  }
}

int32_t CXFA_LayoutPageMgr::GetPageCount() const {
  return pdfium::CollectionSize<int32_t>(m_PageArray);
}

CXFA_ContainerLayoutItem* CXFA_LayoutPageMgr::GetPage(int32_t index) const {
  if (!pdfium::IndexInBounds(m_PageArray, index))
    return nullptr;
  return m_PageArray[index];
}

int32_t CXFA_LayoutPageMgr::GetPageIndex(
    const CXFA_ContainerLayoutItem* pPage) const {
  auto it = std::find(m_PageArray.begin(), m_PageArray.end(), pPage);
  return it != m_PageArray.end() ? it - m_PageArray.begin() : -1;
}

bool CXFA_LayoutPageMgr::RunBreak(XFA_Element eBreakType,
                                  XFA_ATTRIBUTEENUM eTargetType,
                                  CXFA_Node* pTarget,
                                  bool bStartNew) {
  bool bRet = false;
  switch (eTargetType) {
    case XFA_ATTRIBUTEENUM_ContentArea:
      if (pTarget && pTarget->GetElementType() != XFA_Element::ContentArea)
        pTarget = nullptr;
      if (!pTarget ||
          m_CurrentContainerRecordIter == m_ProposedContainerRecords.end() ||
          pTarget !=
              GetCurrentContainerRecord()->pCurContentArea->m_pFormNode ||
          bStartNew) {
        CXFA_Node* pPageArea = nullptr;
        if (pTarget)
          pPageArea = pTarget->GetNodeItem(XFA_NODEITEM_Parent);

        pPageArea = GetNextAvailPageArea(pPageArea, pTarget);
        bRet = !!pPageArea;
      }
      break;
    case XFA_ATTRIBUTEENUM_PageArea:
      if (pTarget && pTarget->GetElementType() != XFA_Element::PageArea)
        pTarget = nullptr;
      if (!pTarget ||
          m_CurrentContainerRecordIter == m_ProposedContainerRecords.end() ||
          pTarget != GetCurrentContainerRecord()->pCurPageArea->m_pFormNode ||
          bStartNew) {
        CXFA_Node* pPageArea = GetNextAvailPageArea(pTarget, nullptr, true);
        bRet = !!pPageArea;
      }
      break;
    case XFA_ATTRIBUTEENUM_PageOdd:
      if (pTarget && pTarget->GetElementType() != XFA_Element::PageArea)
        pTarget = nullptr;
      break;
    case XFA_ATTRIBUTEENUM_PageEven:
      if (pTarget && pTarget->GetElementType() != XFA_Element::PageArea)
        pTarget = nullptr;
      break;
    case XFA_ATTRIBUTEENUM_Auto:
    default:
      break;
  }
  return bRet;
}

bool CXFA_LayoutPageMgr::ExecuteBreakBeforeOrAfter(
    CXFA_Node* pCurNode,
    bool bBefore,
    CXFA_Node*& pBreakLeaderTemplate,
    CXFA_Node*& pBreakTrailerTemplate) {
  XFA_Element eType = pCurNode->GetElementType();
  switch (eType) {
    case XFA_Element::BreakBefore:
    case XFA_Element::BreakAfter: {
      CFX_WideStringC wsBreakLeader, wsBreakTrailer;
      CXFA_Node* pFormNode = pCurNode->GetNodeItem(
          XFA_NODEITEM_Parent, XFA_ObjectType::ContainerNode);
      CXFA_Node* pContainer = pFormNode->GetTemplateNode();
      bool bStartNew = pCurNode->GetInteger(XFA_ATTRIBUTE_StartNew) != 0;
      CXFA_Node* pScript = pCurNode->GetFirstChildByClass(XFA_Element::Script);
      if (pScript && !XFA_LayoutPageMgr_RunBreakTestScript(pScript))
        return false;

      CFX_WideStringC wsTarget = pCurNode->GetCData(XFA_ATTRIBUTE_Target);
      CXFA_Node* pTarget =
          ResolveBreakTarget(m_pTemplatePageSetRoot, true, wsTarget);
      wsBreakTrailer = pCurNode->GetCData(XFA_ATTRIBUTE_Trailer);
      wsBreakLeader = pCurNode->GetCData(XFA_ATTRIBUTE_Leader);
      pBreakLeaderTemplate =
          ResolveBreakTarget(pContainer, true, wsBreakLeader);
      pBreakTrailerTemplate =
          ResolveBreakTarget(pContainer, true, wsBreakTrailer);
      if (RunBreak(eType, pCurNode->GetEnum(XFA_ATTRIBUTE_TargetType), pTarget,
                   bStartNew)) {
        return true;
      }
      if (!m_ProposedContainerRecords.empty() &&
          m_CurrentContainerRecordIter == m_ProposedContainerRecords.begin() &&
          eType == XFA_Element::BreakBefore) {
        CXFA_Node* pParentNode = pFormNode->GetNodeItem(
            XFA_NODEITEM_Parent, XFA_ObjectType::ContainerNode);
        if (!pParentNode ||
            pFormNode !=
                pParentNode->GetNodeItem(XFA_NODEITEM_FirstChild,
                                         XFA_ObjectType::ContainerNode)) {
          break;
        }
        pParentNode = pParentNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (!pParentNode ||
            pParentNode->GetElementType() != XFA_Element::Form) {
          break;
        }
        return true;
      }
      break;
    }
    case XFA_Element::Break: {
      bool bStartNew = pCurNode->GetInteger(XFA_ATTRIBUTE_StartNew) != 0;
      CFX_WideStringC wsTarget = pCurNode->GetCData(
          bBefore ? XFA_ATTRIBUTE_BeforeTarget : XFA_ATTRIBUTE_AfterTarget);
      CXFA_Node* pTarget =
          ResolveBreakTarget(m_pTemplatePageSetRoot, true, wsTarget);
      if (RunBreak(bBefore ? XFA_Element::BreakBefore : XFA_Element::BreakAfter,
                   pCurNode->GetEnum(bBefore ? XFA_ATTRIBUTE_Before
                                             : XFA_ATTRIBUTE_After),
                   pTarget, bStartNew)) {
        return true;
      }
      break;
    }
    default:
      break;
  }
  return false;
}

bool CXFA_LayoutPageMgr::ProcessBreakBeforeOrAfter(
    CXFA_Node* pBreakNode,
    bool bBefore,
    CXFA_Node*& pBreakLeaderNode,
    CXFA_Node*& pBreakTrailerNode,
    bool& bCreatePage) {
  CXFA_Node* pLeaderTemplate = nullptr;
  CXFA_Node* pTrailerTemplate = nullptr;
  CXFA_Node* pFormNode = pBreakNode->GetNodeItem(XFA_NODEITEM_Parent,
                                                 XFA_ObjectType::ContainerNode);
  if (XFA_ItemLayoutProcessor_IsTakingSpace(pFormNode)) {
    bCreatePage = ExecuteBreakBeforeOrAfter(pBreakNode, bBefore,
                                            pLeaderTemplate, pTrailerTemplate);
    CXFA_Document* pDocument = pBreakNode->GetDocument();
    CXFA_Node* pDataScope = nullptr;
    pFormNode = pFormNode->GetNodeItem(XFA_NODEITEM_Parent,
                                       XFA_ObjectType::ContainerNode);
    if (pLeaderTemplate) {
      if (!pDataScope)
        pDataScope = XFA_DataMerge_FindDataScope(pFormNode);

      pBreakLeaderNode = pDocument->DataMerge_CopyContainer(
          pLeaderTemplate, pFormNode, pDataScope, true, true, true);
      pDocument->DataMerge_UpdateBindingRelations(pBreakLeaderNode);
      SetLayoutGeneratedNodeFlag(pBreakLeaderNode);
    }
    if (pTrailerTemplate) {
      if (!pDataScope)
        pDataScope = XFA_DataMerge_FindDataScope(pFormNode);

      pBreakTrailerNode = pDocument->DataMerge_CopyContainer(
          pTrailerTemplate, pFormNode, pDataScope, true, true, true);
      pDocument->DataMerge_UpdateBindingRelations(pBreakTrailerNode);
      SetLayoutGeneratedNodeFlag(pBreakTrailerNode);
    }
    return true;
  }
  return false;
}

bool CXFA_LayoutPageMgr::ProcessBookendLeaderOrTrailer(
    CXFA_Node* pBookendNode,
    bool bLeader,
    CXFA_Node*& pBookendAppendNode) {
  CXFA_Node* pLeaderTemplate = nullptr;
  CXFA_Node* pFormNode = pBookendNode->GetNodeItem(
      XFA_NODEITEM_Parent, XFA_ObjectType::ContainerNode);
  if (ResolveBookendLeaderOrTrailer(pBookendNode, bLeader, pLeaderTemplate)) {
    CXFA_Document* pDocument = pBookendNode->GetDocument();
    CXFA_Node* pDataScope = nullptr;
    if (pLeaderTemplate) {
      if (!pDataScope)
        pDataScope = XFA_DataMerge_FindDataScope(pFormNode);

      pBookendAppendNode = pDocument->DataMerge_CopyContainer(
          pLeaderTemplate, pFormNode, pDataScope, true, true, true);
      pDocument->DataMerge_UpdateBindingRelations(pBookendAppendNode);
      SetLayoutGeneratedNodeFlag(pBookendAppendNode);
      return true;
    }
  }
  return false;
}

CXFA_Node* CXFA_LayoutPageMgr::BreakOverflow(CXFA_Node* pOverflowNode,
                                             CXFA_Node*& pLeaderTemplate,
                                             CXFA_Node*& pTrailerTemplate,
                                             bool bCreatePage) {
  CXFA_Node* pContainer =
      pOverflowNode
          ->GetNodeItem(XFA_NODEITEM_Parent, XFA_ObjectType::ContainerNode)
          ->GetTemplateNode();
  if (pOverflowNode->GetElementType() == XFA_Element::Break) {
    CFX_WideStringC wsOverflowLeader;
    CFX_WideStringC wsOverflowTarget;
    CFX_WideStringC wsOverflowTrailer;
    pOverflowNode->TryCData(XFA_ATTRIBUTE_OverflowLeader, wsOverflowLeader);
    pOverflowNode->TryCData(XFA_ATTRIBUTE_OverflowTrailer, wsOverflowTrailer);
    pOverflowNode->TryCData(XFA_ATTRIBUTE_OverflowTarget, wsOverflowTarget);
    if (!wsOverflowLeader.IsEmpty() || !wsOverflowTrailer.IsEmpty() ||
        !wsOverflowTarget.IsEmpty()) {
      if (!wsOverflowTarget.IsEmpty() && bCreatePage &&
          !m_bCreateOverFlowPage) {
        CXFA_Node* pTarget =
            ResolveBreakTarget(m_pTemplatePageSetRoot, true, wsOverflowTarget);
        if (pTarget) {
          m_bCreateOverFlowPage = true;
          switch (pTarget->GetElementType()) {
            case XFA_Element::PageArea:
              RunBreak(XFA_Element::Overflow, XFA_ATTRIBUTEENUM_PageArea,
                       pTarget, true);
              break;
            case XFA_Element::ContentArea:
              RunBreak(XFA_Element::Overflow, XFA_ATTRIBUTEENUM_ContentArea,
                       pTarget, true);
              break;
            default:
              break;
          }
        }
      }
      if (!bCreatePage) {
        pLeaderTemplate =
            ResolveBreakTarget(pContainer, true, wsOverflowLeader);
        pTrailerTemplate =
            ResolveBreakTarget(pContainer, true, wsOverflowTrailer);
      }
      return pOverflowNode;
    }
    return nullptr;
  }

  if (pOverflowNode->GetElementType() != XFA_Element::Overflow)
    return nullptr;

  CFX_WideStringC wsOverflowLeader;
  CFX_WideStringC wsOverflowTrailer;
  CFX_WideStringC wsOverflowTarget;
  pOverflowNode->TryCData(XFA_ATTRIBUTE_Leader, wsOverflowLeader);
  pOverflowNode->TryCData(XFA_ATTRIBUTE_Trailer, wsOverflowTrailer);
  pOverflowNode->TryCData(XFA_ATTRIBUTE_Target, wsOverflowTarget);
  if (!wsOverflowTarget.IsEmpty() && bCreatePage && !m_bCreateOverFlowPage) {
    CXFA_Node* pTarget =
        ResolveBreakTarget(m_pTemplatePageSetRoot, true, wsOverflowTarget);
    if (pTarget) {
      m_bCreateOverFlowPage = true;
      switch (pTarget->GetElementType()) {
        case XFA_Element::PageArea:
          RunBreak(XFA_Element::Overflow, XFA_ATTRIBUTEENUM_PageArea, pTarget,
                   true);
          break;
        case XFA_Element::ContentArea:
          RunBreak(XFA_Element::Overflow, XFA_ATTRIBUTEENUM_ContentArea,
                   pTarget, true);
          break;
        default:
          break;
      }
    }
  }
  if (!bCreatePage) {
    pLeaderTemplate = ResolveBreakTarget(pContainer, true, wsOverflowLeader);
    pTrailerTemplate = ResolveBreakTarget(pContainer, true, wsOverflowTrailer);
  }
  return pOverflowNode;
}

bool CXFA_LayoutPageMgr::ProcessOverflow(CXFA_Node* pFormNode,
                                         CXFA_Node*& pLeaderNode,
                                         CXFA_Node*& pTrailerNode,
                                         bool bDataMerge,
                                         bool bCreatePage) {
  if (!pFormNode)
    return false;

  CXFA_Node* pLeaderTemplate = nullptr;
  CXFA_Node* pTrailerTemplate = nullptr;
  bool bIsOverflowNode = false;
  if (pFormNode->GetElementType() == XFA_Element::Overflow ||
      pFormNode->GetElementType() == XFA_Element::Break) {
    bIsOverflowNode = true;
  }
  for (CXFA_Node* pCurNode =
           bIsOverflowNode ? pFormNode
                           : pFormNode->GetNodeItem(XFA_NODEITEM_FirstChild);
       pCurNode; pCurNode = pCurNode->GetNodeItem((XFA_NODEITEM_NextSibling))) {
    if (BreakOverflow(pCurNode, pLeaderTemplate, pTrailerTemplate,
                      bCreatePage)) {
      if (bIsOverflowNode)
        pFormNode = pCurNode->GetNodeItem(XFA_NODEITEM_Parent);

      CXFA_Document* pDocument = pCurNode->GetDocument();
      CXFA_Node* pDataScope = nullptr;
      if (pLeaderTemplate) {
        if (!pDataScope)
          pDataScope = XFA_DataMerge_FindDataScope(pFormNode);

        pLeaderNode = pDocument->DataMerge_CopyContainer(
            pLeaderTemplate, pFormNode, pDataScope, true, true, true);
        pDocument->DataMerge_UpdateBindingRelations(pLeaderNode);
        SetLayoutGeneratedNodeFlag(pLeaderNode);
      }
      if (pTrailerTemplate) {
        if (!pDataScope)
          pDataScope = XFA_DataMerge_FindDataScope(pFormNode);

        pTrailerNode = pDocument->DataMerge_CopyContainer(
            pTrailerTemplate, pFormNode, pDataScope, true, true, true);
        pDocument->DataMerge_UpdateBindingRelations(pTrailerNode);
        SetLayoutGeneratedNodeFlag(pTrailerNode);
      }
      return true;
    }
    if (bIsOverflowNode) {
      break;
    }
  }
  return false;
}

bool CXFA_LayoutPageMgr::ResolveBookendLeaderOrTrailer(
    CXFA_Node* pBookendNode,
    bool bLeader,
    CXFA_Node*& pBookendAppendTemplate) {
  CFX_WideStringC wsBookendLeader;
  CXFA_Node* pContainer =
      pBookendNode
          ->GetNodeItem(XFA_NODEITEM_Parent, XFA_ObjectType::ContainerNode)
          ->GetTemplateNode();
  if (pBookendNode->GetElementType() == XFA_Element::Break) {
    pBookendNode->TryCData(
        bLeader ? XFA_ATTRIBUTE_BookendLeader : XFA_ATTRIBUTE_BookendTrailer,
        wsBookendLeader);
    if (!wsBookendLeader.IsEmpty()) {
      pBookendAppendTemplate =
          ResolveBreakTarget(pContainer, false, wsBookendLeader);
      return true;
    }
    return false;
  } else if (pBookendNode->GetElementType() == XFA_Element::Bookend) {
    pBookendNode->TryCData(
        bLeader ? XFA_ATTRIBUTE_Leader : XFA_ATTRIBUTE_Trailer,
        wsBookendLeader);
    pBookendAppendTemplate =
        ResolveBreakTarget(pContainer, true, wsBookendLeader);
    return true;
  }
  return false;
}

bool CXFA_LayoutPageMgr::FindPageAreaFromPageSet(CXFA_Node* pPageSet,
                                                 CXFA_Node* pStartChild,
                                                 CXFA_Node* pTargetPageArea,
                                                 CXFA_Node* pTargetContentArea,
                                                 bool bNewPage,
                                                 bool bQuery) {
  if (!pPageSet && !pStartChild)
    return false;

  if (IsPageSetRootOrderedOccurrence()) {
    return FindPageAreaFromPageSet_Ordered(pPageSet, pStartChild,
                                           pTargetPageArea, pTargetContentArea,
                                           bNewPage, bQuery);
  }
  XFA_ATTRIBUTEENUM ePreferredPosition =
      m_CurrentContainerRecordIter != m_ProposedContainerRecords.end()
          ? XFA_ATTRIBUTEENUM_Rest
          : XFA_ATTRIBUTEENUM_First;
  return FindPageAreaFromPageSet_SimplexDuplex(
      pPageSet, pStartChild, pTargetPageArea, pTargetContentArea, bNewPage,
      bQuery, ePreferredPosition);
}

bool CXFA_LayoutPageMgr::FindPageAreaFromPageSet_Ordered(
    CXFA_Node* pPageSet,
    CXFA_Node* pStartChild,
    CXFA_Node* pTargetPageArea,
    CXFA_Node* pTargetContentArea,
    bool bNewPage,
    bool bQuery) {
  int32_t iPageSetCount = 0;
  if (!pStartChild && !bQuery) {
    auto it = m_pPageSetMap.find(pPageSet);
    if (it != m_pPageSetMap.end())
      iPageSetCount = it->second;
    int32_t iMax = -1;
    CXFA_Node* pOccurNode = pPageSet->GetFirstChildByClass(XFA_Element::Occur);
    if (pOccurNode)
      pOccurNode->TryInteger(XFA_ATTRIBUTE_Max, iMax, false);
    if (iMax >= 0 && iMax <= iPageSetCount)
      return false;
  }
  bool bRes = false;
  CXFA_Node* pCurrentNode =
      pStartChild ? pStartChild->GetNodeItem(XFA_NODEITEM_NextSibling)
                  : pPageSet->GetNodeItem(XFA_NODEITEM_FirstChild);
  for (; pCurrentNode;
       pCurrentNode = pCurrentNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pCurrentNode->GetElementType() == XFA_Element::PageArea) {
      if ((pTargetPageArea == pCurrentNode || !pTargetPageArea)) {
        if (!pCurrentNode->GetFirstChildByClass(XFA_Element::ContentArea)) {
          if (pTargetPageArea == pCurrentNode) {
            CreateMinPageRecord(pCurrentNode, true);
            pTargetPageArea = nullptr;
          }
          continue;
        }
        if (!bQuery) {
          CXFA_ContainerRecord* pNewRecord =
              CreateContainerRecord(pCurrentNode, !pStartChild);
          AddPageAreaLayoutItem(pNewRecord, pCurrentNode);
          if (!pTargetContentArea) {
            pTargetContentArea =
                pCurrentNode->GetFirstChildByClass(XFA_Element::ContentArea);
          }
          AddContentAreaLayoutItem(pNewRecord, pTargetContentArea);
        }
        m_pCurPageArea = pCurrentNode;
        m_nCurPageCount = 1;
        bRes = true;
        break;
      }
      if (!bQuery)
        CreateMinPageRecord(pCurrentNode, false);
    } else if (pCurrentNode->GetElementType() == XFA_Element::PageSet) {
      if (FindPageAreaFromPageSet_Ordered(pCurrentNode, nullptr,
                                          pTargetPageArea, pTargetContentArea,
                                          bNewPage, bQuery)) {
        bRes = true;
        break;
      }
      if (!bQuery)
        CreateMinPageSetRecord(pCurrentNode, true);
    }
  }
  if (!pStartChild && bRes && !bQuery)
    m_pPageSetMap[pPageSet] = ++iPageSetCount;
  return bRes;
}

bool CXFA_LayoutPageMgr::FindPageAreaFromPageSet_SimplexDuplex(
    CXFA_Node* pPageSet,
    CXFA_Node* pStartChild,
    CXFA_Node* pTargetPageArea,
    CXFA_Node* pTargetContentArea,
    bool bNewPage,
    bool bQuery,
    XFA_ATTRIBUTEENUM ePreferredPosition) {
  const XFA_ATTRIBUTEENUM eFallbackPosition = XFA_ATTRIBUTEENUM_Any;
  CXFA_Node* pPreferredPageArea = nullptr;
  CXFA_Node* pFallbackPageArea = nullptr;
  CXFA_Node* pCurrentNode = nullptr;
  if (!pStartChild || pStartChild->GetElementType() == XFA_Element::PageArea)
    pCurrentNode = pPageSet->GetNodeItem(XFA_NODEITEM_FirstChild);
  else
    pCurrentNode = pStartChild->GetNodeItem(XFA_NODEITEM_NextSibling);

  for (; pCurrentNode;
       pCurrentNode = pCurrentNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pCurrentNode->GetElementType() == XFA_Element::PageArea) {
      if (!MatchPageAreaOddOrEven(pCurrentNode, false))
        continue;

      XFA_ATTRIBUTEENUM eCurPagePosition =
          pCurrentNode->GetEnum(XFA_ATTRIBUTE_PagePosition);
      if (ePreferredPosition == XFA_ATTRIBUTEENUM_Last) {
        if (eCurPagePosition != ePreferredPosition)
          continue;
        if (m_ePageSetMode == XFA_ATTRIBUTEENUM_SimplexPaginated ||
            pCurrentNode->GetEnum(XFA_ATTRIBUTE_OddOrEven) ==
                XFA_ATTRIBUTEENUM_Any) {
          pPreferredPageArea = pCurrentNode;
          break;
        }
        CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
        AddPageAreaLayoutItem(pNewRecord, pCurrentNode);
        AddContentAreaLayoutItem(pNewRecord, pCurrentNode->GetFirstChildByClass(
                                                 XFA_Element::ContentArea));
        pPreferredPageArea = pCurrentNode;
        return false;
      }
      if (ePreferredPosition == XFA_ATTRIBUTEENUM_Only) {
        if (eCurPagePosition != ePreferredPosition)
          continue;
        if (m_ePageSetMode != XFA_ATTRIBUTEENUM_DuplexPaginated ||
            pCurrentNode->GetEnum(XFA_ATTRIBUTE_OddOrEven) ==
                XFA_ATTRIBUTEENUM_Any) {
          pPreferredPageArea = pCurrentNode;
          break;
        }
        return false;
      }
      if ((pTargetPageArea == pCurrentNode || !pTargetPageArea)) {
        if (!pCurrentNode->GetFirstChildByClass(XFA_Element::ContentArea)) {
          if (pTargetPageArea == pCurrentNode) {
            CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
            AddPageAreaLayoutItem(pNewRecord, pCurrentNode);
            pTargetPageArea = nullptr;
          }
          continue;
        }
        if ((ePreferredPosition == XFA_ATTRIBUTEENUM_Rest &&
             eCurPagePosition == XFA_ATTRIBUTEENUM_Any) ||
            eCurPagePosition == ePreferredPosition) {
          pPreferredPageArea = pCurrentNode;
          break;
        } else if (eCurPagePosition == eFallbackPosition &&
                   !pFallbackPageArea) {
          pFallbackPageArea = pCurrentNode;
        }
      } else if (pTargetPageArea &&
                 !MatchPageAreaOddOrEven(pTargetPageArea, false)) {
        CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
        AddPageAreaLayoutItem(pNewRecord, pCurrentNode);
        AddContentAreaLayoutItem(pNewRecord, pCurrentNode->GetFirstChildByClass(
                                                 XFA_Element::ContentArea));
      }
    } else if (pCurrentNode->GetElementType() == XFA_Element::PageSet) {
      if (FindPageAreaFromPageSet_SimplexDuplex(
              pCurrentNode, nullptr, pTargetPageArea, pTargetContentArea,
              bNewPage, bQuery, ePreferredPosition)) {
        break;
      }
    }
  }

  CXFA_Node* pCurPageArea = nullptr;
  if (pPreferredPageArea)
    pCurPageArea = pPreferredPageArea;
  else if (pFallbackPageArea)
    pCurPageArea = pFallbackPageArea;

  if (!pCurPageArea)
    return false;

  if (!bQuery) {
    CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
    AddPageAreaLayoutItem(pNewRecord, pCurPageArea);
    if (!pTargetContentArea) {
      pTargetContentArea =
          pCurPageArea->GetFirstChildByClass(XFA_Element::ContentArea);
    }
    AddContentAreaLayoutItem(pNewRecord, pTargetContentArea);
  }
  m_pCurPageArea = pCurPageArea;
  return true;
}

bool CXFA_LayoutPageMgr::MatchPageAreaOddOrEven(CXFA_Node* pPageArea,
                                                bool bLastMatch) {
  if (m_ePageSetMode != XFA_ATTRIBUTEENUM_DuplexPaginated)
    return true;

  XFA_ATTRIBUTEENUM eOddOrEven = XFA_ATTRIBUTEENUM_Any;
  pPageArea->TryEnum(XFA_ATTRIBUTE_OddOrEven, eOddOrEven);
  if (eOddOrEven != XFA_ATTRIBUTEENUM_Any) {
    int32_t iPageCount = GetPageCount();
    if (bLastMatch) {
      return eOddOrEven == XFA_ATTRIBUTEENUM_Odd ? iPageCount % 2 == 1
                                                 : iPageCount % 2 == 0;
    }
    return eOddOrEven == XFA_ATTRIBUTEENUM_Odd ? iPageCount % 2 == 0
                                               : iPageCount % 2 == 1;
  }
  return true;
}

CXFA_Node* CXFA_LayoutPageMgr::GetNextAvailPageArea(
    CXFA_Node* pTargetPageArea,
    CXFA_Node* pTargetContentArea,
    bool bNewPage,
    bool bQuery) {
  if (!m_pCurPageArea) {
    FindPageAreaFromPageSet(m_pTemplatePageSetRoot, nullptr, pTargetPageArea,
                            pTargetContentArea, bNewPage, bQuery);
    ASSERT(m_pCurPageArea);
    return m_pCurPageArea;
  }

  if (!pTargetPageArea || pTargetPageArea == m_pCurPageArea) {
    if (!bNewPage && GetNextContentArea(pTargetContentArea))
      return m_pCurPageArea;

    if (IsPageSetRootOrderedOccurrence()) {
      int32_t iMax = -1;
      CXFA_Node* pOccurNode =
          m_pCurPageArea->GetFirstChildByClass(XFA_Element::Occur);
      if (pOccurNode)
        pOccurNode->TryInteger(XFA_ATTRIBUTE_Max, iMax, false);
      if ((iMax < 0 || m_nCurPageCount < iMax)) {
        if (!bQuery) {
          CXFA_ContainerRecord* pNewRecord =
              CreateContainerRecord(m_pCurPageArea);
          AddPageAreaLayoutItem(pNewRecord, m_pCurPageArea);
          if (!pTargetContentArea) {
            pTargetContentArea =
                m_pCurPageArea->GetFirstChildByClass(XFA_Element::ContentArea);
          }
          AddContentAreaLayoutItem(pNewRecord, pTargetContentArea);
        }
        m_nCurPageCount++;
        return m_pCurPageArea;
      }
    }
  }

  if (!bQuery && IsPageSetRootOrderedOccurrence())
    CreateMinPageRecord(m_pCurPageArea, false, true);
  if (FindPageAreaFromPageSet(m_pCurPageArea->GetNodeItem(XFA_NODEITEM_Parent),
                              m_pCurPageArea, pTargetPageArea,
                              pTargetContentArea, bNewPage, bQuery)) {
    return m_pCurPageArea;
  }

  CXFA_Node* pPageSet = m_pCurPageArea->GetNodeItem(XFA_NODEITEM_Parent);
  while (true) {
    if (FindPageAreaFromPageSet(pPageSet, nullptr, pTargetPageArea,
                                pTargetContentArea, bNewPage, bQuery)) {
      return m_pCurPageArea;
    }
    if (!bQuery && IsPageSetRootOrderedOccurrence())
      CreateMinPageSetRecord(pPageSet);
    if (FindPageAreaFromPageSet(nullptr, pPageSet, pTargetPageArea,
                                pTargetContentArea, bNewPage, bQuery)) {
      return m_pCurPageArea;
    }
    if (pPageSet == m_pTemplatePageSetRoot)
      break;

    pPageSet = pPageSet->GetNodeItem(XFA_NODEITEM_Parent);
  }
  return nullptr;
}

bool CXFA_LayoutPageMgr::GetNextContentArea(CXFA_Node* pContentArea) {
  CXFA_Node* pCurContentNode =
      GetCurrentContainerRecord()->pCurContentArea->m_pFormNode;
  if (!pContentArea) {
    pContentArea =
        pCurContentNode->GetNextSameClassSibling(XFA_Element::ContentArea);
    if (!pContentArea)
      return false;
  } else {
    if (pContentArea->GetNodeItem(XFA_NODEITEM_Parent) != m_pCurPageArea)
      return false;

    CXFA_ContainerLayoutItem* pContentAreaLayout = nullptr;
    if (!CheckContentAreaNotUsed(GetCurrentContainerRecord()->pCurPageArea,
                                 pContentArea, pContentAreaLayout)) {
      return false;
    }
    if (pContentAreaLayout) {
      if (pContentAreaLayout->m_pFormNode != pCurContentNode) {
        CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
        pNewRecord->pCurContentArea = pContentAreaLayout;
        return true;
      }
      return false;
    }
  }

  CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
  AddContentAreaLayoutItem(pNewRecord, pContentArea);
  return true;
}

void CXFA_LayoutPageMgr::InitPageSetMap() {
  if (!IsPageSetRootOrderedOccurrence())
    return;

  CXFA_NodeIterator sIterator(m_pTemplatePageSetRoot);
  for (CXFA_Node* pPageSetNode = sIterator.GetCurrent(); pPageSetNode;
       pPageSetNode = sIterator.MoveToNext()) {
    if (pPageSetNode->GetElementType() == XFA_Element::PageSet) {
      XFA_ATTRIBUTEENUM eRelation =
          pPageSetNode->GetEnum(XFA_ATTRIBUTE_Relation);
      if (eRelation == XFA_ATTRIBUTEENUM_OrderedOccurrence)
        m_pPageSetMap[pPageSetNode] = 0;
    }
  }
}

int32_t CXFA_LayoutPageMgr::CreateMinPageRecord(CXFA_Node* pPageArea,
                                                bool bTargetPageArea,
                                                bool bCreateLast) {
  if (!pPageArea)
    return 0;

  CXFA_Node* pOccurNode = pPageArea->GetFirstChildByClass(XFA_Element::Occur);
  int32_t iMin = 0;
  if ((pOccurNode && pOccurNode->TryInteger(XFA_ATTRIBUTE_Min, iMin, false)) ||
      bTargetPageArea) {
    CXFA_Node* pContentArea =
        pPageArea->GetFirstChildByClass(XFA_Element::ContentArea);
    if (iMin < 1 && bTargetPageArea && !pContentArea)
      iMin = 1;

    int32_t i = 0;
    if (bCreateLast)
      i = m_nCurPageCount;

    for (; i < iMin; i++) {
      CXFA_ContainerRecord* pNewRecord = CreateContainerRecord();
      AddPageAreaLayoutItem(pNewRecord, pPageArea);
      AddContentAreaLayoutItem(pNewRecord, pContentArea);
    }
  }
  return iMin;
}

void CXFA_LayoutPageMgr::CreateMinPageSetRecord(CXFA_Node* pPageSet,
                                                bool bCreateAll) {
  if (!pPageSet)
    return;

  auto it = m_pPageSetMap.find(pPageSet);
  if (it == m_pPageSetMap.end())
    return;

  int32_t iCurSetCount = it->second;
  if (bCreateAll)
    iCurSetCount = 0;

  CXFA_Node* pOccurNode = pPageSet->GetFirstChildByClass(XFA_Element::Occur);
  int32_t iMin = 0;
  if (pOccurNode && pOccurNode->TryInteger(XFA_ATTRIBUTE_Min, iMin, false)) {
    if (iCurSetCount < iMin) {
      for (int32_t i = 0; i < iMin - iCurSetCount; i++) {
        for (CXFA_Node* pCurrentPageNode =
                 pPageSet->GetNodeItem(XFA_NODEITEM_FirstChild);
             pCurrentPageNode; pCurrentPageNode = pCurrentPageNode->GetNodeItem(
                                   XFA_NODEITEM_NextSibling)) {
          if (pCurrentPageNode->GetElementType() == XFA_Element::PageArea) {
            CreateMinPageRecord(pCurrentPageNode, false);
          } else if (pCurrentPageNode->GetElementType() ==
                     XFA_Element::PageSet) {
            CreateMinPageSetRecord(pCurrentPageNode, true);
          }
        }
      }
      m_pPageSetMap[pPageSet] = iMin;
    }
  }
}

void CXFA_LayoutPageMgr::CreateNextMinRecord(CXFA_Node* pRecordNode) {
  if (!pRecordNode)
    return;

  for (CXFA_Node* pCurrentNode =
           pRecordNode->GetNodeItem(XFA_NODEITEM_NextSibling);
       pCurrentNode;
       pCurrentNode = pCurrentNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pCurrentNode->GetElementType() == XFA_Element::PageArea)
      CreateMinPageRecord(pCurrentNode, false);
    else if (pCurrentNode->GetElementType() == XFA_Element::PageSet)
      CreateMinPageSetRecord(pCurrentNode, true);
  }
}

void CXFA_LayoutPageMgr::ProcessLastPageSet() {
  CreateMinPageRecord(m_pCurPageArea, false, true);
  CreateNextMinRecord(m_pCurPageArea);
  CXFA_Node* pPageSet = m_pCurPageArea->GetNodeItem(XFA_NODEITEM_Parent);
  while (true) {
    CreateMinPageSetRecord(pPageSet);
    if (pPageSet == m_pTemplatePageSetRoot)
      break;

    CreateNextMinRecord(pPageSet);
    pPageSet = pPageSet->GetNodeItem(XFA_NODEITEM_Parent);
  }
}

bool CXFA_LayoutPageMgr::GetNextAvailContentHeight(float fChildHeight) {
  CXFA_Node* pCurContentNode =
      GetCurrentContainerRecord()->pCurContentArea->m_pFormNode;
  if (!pCurContentNode)
    return false;

  pCurContentNode =
      pCurContentNode->GetNextSameClassSibling(XFA_Element::ContentArea);
  if (pCurContentNode) {
    float fNextContentHeight =
        pCurContentNode->GetMeasure(XFA_ATTRIBUTE_H).ToUnit(XFA_UNIT_Pt);
    return fNextContentHeight > fChildHeight;
  }

  CXFA_Node* pPageNode = GetCurrentContainerRecord()->pCurPageArea->m_pFormNode;
  CXFA_Node* pOccurNode = pPageNode->GetFirstChildByClass(XFA_Element::Occur);
  int32_t iMax = 0;
  if (pOccurNode && pOccurNode->TryInteger(XFA_ATTRIBUTE_Max, iMax, false)) {
    if (m_nCurPageCount == iMax) {
      CXFA_Node* pSrcPage = m_pCurPageArea;
      int32_t nSrcPageCount = m_nCurPageCount;
      auto psSrcIter = GetTailPosition();
      CXFA_Node* pNextPage =
          GetNextAvailPageArea(nullptr, nullptr, false, true);
      m_pCurPageArea = pSrcPage;
      m_nCurPageCount = nSrcPageCount;
      CXFA_ContainerRecord* pPrevRecord = *psSrcIter++;
      while (psSrcIter != m_ProposedContainerRecords.end()) {
        auto psSaveIter = psSrcIter;
        CXFA_ContainerRecord* pInsertRecord = *psSrcIter++;
        RemoveLayoutRecord(pInsertRecord, pPrevRecord);
        delete pInsertRecord;
        m_ProposedContainerRecords.erase(psSaveIter);
      }
      if (pNextPage) {
        CXFA_Node* pContentArea =
            pNextPage->GetFirstChildByClass(XFA_Element::ContentArea);
        if (pContentArea) {
          float fNextContentHeight =
              pContentArea->GetMeasure(XFA_ATTRIBUTE_H).ToUnit(XFA_UNIT_Pt);
          if (fNextContentHeight > fChildHeight)
            return true;
        }
      }
      return false;
    }
  }

  CXFA_Node* pContentArea =
      pPageNode->GetFirstChildByClass(XFA_Element::ContentArea);
  float fNextContentHeight =
      pContentArea->GetMeasure(XFA_ATTRIBUTE_H).ToUnit(XFA_UNIT_Pt);
  if (fNextContentHeight < XFA_LAYOUT_FLOAT_PERCISION)
    return true;
  if (fNextContentHeight > fChildHeight)
    return true;
  return false;
}

void CXFA_LayoutPageMgr::ClearData() {
  if (!m_pTemplatePageSetRoot)
    return;

  auto sPos = m_ProposedContainerRecords.begin();
  while (sPos != m_ProposedContainerRecords.end()) {
    CXFA_ContainerRecord* pRecord = *sPos++;
    delete pRecord;
  }
  m_ProposedContainerRecords.clear();
  m_CurrentContainerRecordIter = m_ProposedContainerRecords.end();
  m_pCurPageArea = nullptr;
  m_nCurPageCount = 0;
  m_bCreateOverFlowPage = false;
  m_pPageSetMap.clear();
}

void CXFA_LayoutPageMgr::SaveLayoutItem(CXFA_LayoutItem* pParentLayoutItem) {
  CXFA_LayoutItem* pNextLayoutItem;
  CXFA_LayoutItem* pCurLayoutItem = pParentLayoutItem->m_pFirstChild;
  while (pCurLayoutItem) {
    pNextLayoutItem = pCurLayoutItem->m_pNextSibling;
    if (pCurLayoutItem->IsContentLayoutItem()) {
      if (pCurLayoutItem->m_pFormNode->HasRemovedChildren()) {
        CXFA_FFNotify* pNotify =
            m_pTemplatePageSetRoot->GetDocument()->GetNotify();
        CXFA_LayoutProcessor* pDocLayout =
            m_pTemplatePageSetRoot->GetDocument()->GetDocLayout();
        if (pCurLayoutItem->m_pFirstChild)
          SyncRemoveLayoutItem(pCurLayoutItem, pNotify, pDocLayout);

        pNotify->OnLayoutItemRemoving(pDocLayout, pCurLayoutItem);
        delete pCurLayoutItem;
        pCurLayoutItem = pNextLayoutItem;
        continue;
      }

      if (pCurLayoutItem->m_pFormNode->IsLayoutGeneratedNode()) {
        CXFA_NodeIteratorTemplate<CXFA_Node, CXFA_TraverseStrategy_XFANode>
            sIterator(pCurLayoutItem->m_pFormNode);
        for (CXFA_Node* pNode = sIterator.GetCurrent(); pNode;
             pNode = sIterator.MoveToNext()) {
          pNode->SetFlag(XFA_NodeFlag_UnusedNode, false);
        }
      }
    }

    if (pCurLayoutItem->m_pFirstChild)
      SaveLayoutItem(pCurLayoutItem);

    pCurLayoutItem->m_pParent = nullptr;
    pCurLayoutItem->m_pNextSibling = nullptr;
    pCurLayoutItem->m_pFirstChild = nullptr;
    if (!pCurLayoutItem->IsContentLayoutItem() &&
        pCurLayoutItem->m_pFormNode->GetElementType() !=
            XFA_Element::PageArea) {
      delete pCurLayoutItem;
    }
    pCurLayoutItem = pNextLayoutItem;
  }
}

CXFA_Node* CXFA_LayoutPageMgr::QueryOverflow(CXFA_Node* pFormNode) {
  for (CXFA_Node* pCurNode = pFormNode->GetNodeItem(XFA_NODEITEM_FirstChild);
       pCurNode; pCurNode = pCurNode->GetNodeItem((XFA_NODEITEM_NextSibling))) {
    if (pCurNode->GetElementType() == XFA_Element::Break) {
      CFX_WideStringC wsOverflowLeader;
      CFX_WideStringC wsOverflowTarget;
      CFX_WideStringC wsOverflowTrailer;
      pCurNode->TryCData(XFA_ATTRIBUTE_OverflowLeader, wsOverflowLeader);
      pCurNode->TryCData(XFA_ATTRIBUTE_OverflowTrailer, wsOverflowTrailer);
      pCurNode->TryCData(XFA_ATTRIBUTE_OverflowTarget, wsOverflowTarget);
      if (!wsOverflowLeader.IsEmpty() || !wsOverflowTrailer.IsEmpty() ||
          !wsOverflowTarget.IsEmpty()) {
        return pCurNode;
      }
      return nullptr;
    }
    if (pCurNode->GetElementType() == XFA_Element::Overflow)
      return pCurNode;
  }
  return nullptr;
}

void CXFA_LayoutPageMgr::MergePageSetContents() {
  CXFA_Document* pDocument = m_pTemplatePageSetRoot->GetDocument();
  CXFA_FFNotify* pNotify = pDocument->GetNotify();
  CXFA_LayoutProcessor* pDocLayout = pDocument->GetDocLayout();
  CXFA_ContainerLayoutItem* pRootLayout = GetRootLayoutItem();
  for (CXFA_Node* pPageNode : pDocument->m_pPendingPageSet) {
    CXFA_NodeIteratorTemplate<CXFA_Node, CXFA_TraverseStrategy_XFANode>
        sIterator(pPageNode);
    for (CXFA_Node* pNode = sIterator.GetCurrent(); pNode;
         pNode = sIterator.MoveToNext()) {
      if (pNode->IsContainerNode()) {
        CXFA_Node* pBindNode = pNode->GetBindData();
        if (pBindNode) {
          pBindNode->RemoveBindItem(pNode);
          pNode->SetObject(XFA_ATTRIBUTE_BindingNode, nullptr);
        }
      }
      pNode->SetFlag(XFA_NodeFlag_UnusedNode, true);
    }
  }

  int32_t iIndex = 0;
  for (; pRootLayout; pRootLayout = static_cast<CXFA_ContainerLayoutItem*>(
                          pRootLayout->m_pNextSibling)) {
    CXFA_Node* pPendingPageSet = nullptr;
    CXFA_NodeIteratorTemplate<
        CXFA_ContainerLayoutItem,
        CXFA_TraverseStrategy_ContentAreaContainerLayoutItem>
        iterator(pRootLayout);
    CXFA_ContainerLayoutItem* pRootPageSetContainerItem = iterator.GetCurrent();
    ASSERT(pRootPageSetContainerItem->m_pFormNode->GetElementType() ==
           XFA_Element::PageSet);
    if (iIndex <
        pdfium::CollectionSize<int32_t>(pDocument->m_pPendingPageSet)) {
      pPendingPageSet = pDocument->m_pPendingPageSet[iIndex];
      iIndex++;
    }
    if (!pPendingPageSet) {
      if (pRootPageSetContainerItem->m_pFormNode->GetPacketID() ==
          XFA_XDPPACKET_Template) {
        pPendingPageSet =
            pRootPageSetContainerItem->m_pFormNode->CloneTemplateToForm(false);
      } else {
        pPendingPageSet = pRootPageSetContainerItem->m_pFormNode;
      }
    }
    if (pRootPageSetContainerItem->m_pFormNode->GetUserData(
            XFA_LAYOUTITEMKEY) == pRootPageSetContainerItem) {
      pRootPageSetContainerItem->m_pFormNode->SetUserData(XFA_LAYOUTITEMKEY,
                                                          nullptr);
    }
    pRootPageSetContainerItem->m_pFormNode = pPendingPageSet;
    pPendingPageSet->ClearFlag(XFA_NodeFlag_UnusedNode);
    for (CXFA_ContainerLayoutItem* pContainerItem = iterator.MoveToNext();
         pContainerItem; pContainerItem = iterator.MoveToNext()) {
      CXFA_Node* pNode = pContainerItem->m_pFormNode;
      if (pNode->GetPacketID() != XFA_XDPPACKET_Template)
        continue;

      switch (pNode->GetElementType()) {
        case XFA_Element::PageSet: {
          CXFA_Node* pParentNode = pContainerItem->m_pParent->m_pFormNode;
          pContainerItem->m_pFormNode = XFA_NodeMerge_CloneOrMergeContainer(
              pDocument, pParentNode, pContainerItem->m_pFormNode, true,
              nullptr);
          break;
        }
        case XFA_Element::PageArea: {
          CXFA_LayoutItem* pFormLayout = pContainerItem;
          CXFA_Node* pParentNode = pContainerItem->m_pParent->m_pFormNode;
          bool bIsExistForm = true;
          for (int32_t iLevel = 0; iLevel < 3; iLevel++) {
            pFormLayout = pFormLayout->m_pFirstChild;
            if (iLevel == 2) {
              while (pFormLayout &&
                     !XFA_ItemLayoutProcessor_IsTakingSpace(
                         pFormLayout->m_pFormNode)) {
                pFormLayout = pFormLayout->m_pNextSibling;
              }
            }
            if (!pFormLayout) {
              bIsExistForm = false;
              break;
            }
          }
          if (bIsExistForm) {
            CXFA_Node* pNewSubform = pFormLayout->m_pFormNode;
            if (pContainerItem->m_pOldSubform &&
                pContainerItem->m_pOldSubform != pNewSubform) {
              CXFA_Node* pExistingNode = XFA_DataMerge_FindFormDOMInstance(
                  pDocument, pContainerItem->m_pFormNode->GetElementType(),
                  pContainerItem->m_pFormNode->GetNameHash(), pParentNode);
              CXFA_ContainerIterator sIterator(pExistingNode);
              for (CXFA_Node* pIter = sIterator.GetCurrent(); pIter;
                   pIter = sIterator.MoveToNext()) {
                if (pIter->GetElementType() != XFA_Element::ContentArea) {
                  CXFA_LayoutItem* pLayoutItem = static_cast<CXFA_LayoutItem*>(
                      pIter->GetUserData(XFA_LAYOUTITEMKEY));
                  if (pLayoutItem) {
                    pNotify->OnLayoutItemRemoving(pDocLayout, pLayoutItem);
                    delete pLayoutItem;
                  }
                }
              }
              if (pExistingNode) {
                pParentNode->RemoveChild(pExistingNode);
              }
            }
            pContainerItem->m_pOldSubform = pNewSubform;
          }
          pContainerItem->m_pFormNode = pDocument->DataMerge_CopyContainer(
              pContainerItem->m_pFormNode, pParentNode,
              ToNode(pDocument->GetXFAObject(XFA_HASHCODE_Record)), true, true,
              true);
          break;
        }
        case XFA_Element::ContentArea: {
          CXFA_Node* pParentNode = pContainerItem->m_pParent->m_pFormNode;
          for (CXFA_Node* pChildNode =
                   pParentNode->GetNodeItem(XFA_NODEITEM_FirstChild);
               pChildNode;
               pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
            if (pChildNode->GetTemplateNode() != pContainerItem->m_pFormNode) {
              continue;
            }
            pContainerItem->m_pFormNode = pChildNode;
            break;
          }
          break;
        }
        default:
          break;
      }
    }
    if (!pPendingPageSet->GetNodeItem(XFA_NODEITEM_Parent)) {
      CXFA_Node* pFormToplevelSubform =
          pDocument->GetXFAObject(XFA_HASHCODE_Form)
              ->AsNode()
              ->GetFirstChildByClass(XFA_Element::Subform);
      pFormToplevelSubform->InsertChild(pPendingPageSet);
    }
    pDocument->DataMerge_UpdateBindingRelations(pPendingPageSet);
    pPendingPageSet->SetFlag(XFA_NodeFlag_Initialized, true);
  }

  CXFA_Node* pPageSet = GetRootLayoutItem()->m_pFormNode;
  while (pPageSet) {
    CXFA_Node* pNextPageSet =
        pPageSet->GetNextSameClassSibling(XFA_Element::PageSet);
    CXFA_NodeIteratorTemplate<CXFA_Node, CXFA_TraverseStrategy_XFANode>
        sIterator(pPageSet);
    CXFA_Node* pNode = sIterator.GetCurrent();
    while (pNode) {
      if (pNode->IsUnusedNode()) {
        if (pNode->IsContainerNode()) {
          XFA_Element eType = pNode->GetElementType();
          if (eType == XFA_Element::PageArea || eType == XFA_Element::PageSet) {
            CXFA_ContainerIterator iteChild(pNode);
            CXFA_Node* pChildNode = iteChild.MoveToNext();
            for (; pChildNode; pChildNode = iteChild.MoveToNext()) {
              CXFA_LayoutItem* pLayoutItem = static_cast<CXFA_LayoutItem*>(
                  pChildNode->GetUserData(XFA_LAYOUTITEMKEY));
              if (pLayoutItem) {
                pNotify->OnLayoutItemRemoving(pDocLayout, pLayoutItem);
                delete pLayoutItem;
              }
            }
          } else if (eType != XFA_Element::ContentArea) {
            CXFA_LayoutItem* pLayoutItem = static_cast<CXFA_LayoutItem*>(
                pNode->GetUserData(XFA_LAYOUTITEMKEY));
            if (pLayoutItem) {
              pNotify->OnLayoutItemRemoving(pDocLayout, pLayoutItem);
              delete pLayoutItem;
            }
          }
          CXFA_Node* pNext = sIterator.SkipChildrenAndMoveToNext();
          pNode->GetNodeItem(XFA_NODEITEM_Parent)->RemoveChild(pNode);
          pNode = pNext;
        } else {
          pNode->ClearFlag(XFA_NodeFlag_UnusedNode);
          pNode->SetFlag(XFA_NodeFlag_Initialized, true);
          pNode = sIterator.MoveToNext();
        }
      } else {
        pNode->SetFlag(XFA_NodeFlag_Initialized, true);
        pNode = sIterator.MoveToNext();
      }
    }
    pPageSet = pNextPageSet;
  }
}

void CXFA_LayoutPageMgr::LayoutPageSetContents() {
  CXFA_ContainerLayoutItem* pRootLayoutItem = GetRootLayoutItem();
  for (; pRootLayoutItem;
       pRootLayoutItem = static_cast<CXFA_ContainerLayoutItem*>(
           pRootLayoutItem->m_pNextSibling)) {
    CXFA_NodeIteratorTemplate<
        CXFA_ContainerLayoutItem,
        CXFA_TraverseStrategy_ContentAreaContainerLayoutItem>
        iterator(pRootLayoutItem);
    for (CXFA_ContainerLayoutItem* pContainerItem = iterator.GetCurrent();
         pContainerItem; pContainerItem = iterator.MoveToNext()) {
      CXFA_Node* pNode = pContainerItem->m_pFormNode;
      switch (pNode->GetElementType()) {
        case XFA_Element::PageArea:
          m_pLayoutProcessor->GetRootRootItemLayoutProcessor()
              ->DoLayoutPageArea(pContainerItem);
          break;
        default:
          break;
      }
    }
  }
}

void CXFA_LayoutPageMgr::SyncLayoutData() {
  MergePageSetContents();
  LayoutPageSetContents();
  CXFA_FFNotify* pNotify = m_pTemplatePageSetRoot->GetDocument()->GetNotify();
  int32_t nPageIdx = -1;
  CXFA_ContainerLayoutItem* pRootLayoutItem = GetRootLayoutItem();
  for (; pRootLayoutItem;
       pRootLayoutItem = static_cast<CXFA_ContainerLayoutItem*>(
           pRootLayoutItem->m_pNextSibling)) {
    CXFA_NodeIteratorTemplate<
        CXFA_ContainerLayoutItem,
        CXFA_TraverseStrategy_ContentAreaContainerLayoutItem>
        iteratorParent(pRootLayoutItem);
    for (CXFA_ContainerLayoutItem* pContainerItem = iteratorParent.GetCurrent();
         pContainerItem; pContainerItem = iteratorParent.MoveToNext()) {
      switch (pContainerItem->m_pFormNode->GetElementType()) {
        case XFA_Element::PageArea: {
          nPageIdx++;
          uint32_t dwRelevant =
              XFA_WidgetStatus_Viewable | XFA_WidgetStatus_Printable;
          CXFA_NodeIteratorTemplate<CXFA_LayoutItem,
                                    CXFA_TraverseStrategy_LayoutItem>
              iterator(pContainerItem);
          CXFA_LayoutItem* pChildLayoutItem = iterator.GetCurrent();
          while (pChildLayoutItem) {
            CXFA_ContentLayoutItem* pContentItem =
                pChildLayoutItem->AsContentLayoutItem();
            if (!pContentItem) {
              pChildLayoutItem = iterator.MoveToNext();
              continue;
            }
            bool bVisible =
                (pContentItem->m_pFormNode->GetEnum(XFA_ATTRIBUTE_Presence) ==
                 XFA_ATTRIBUTEENUM_Visible);
            uint32_t dwRelevantChild =
                GetRelevant(pContentItem->m_pFormNode, dwRelevant);
            SyncContainer(pNotify, m_pLayoutProcessor, pContentItem,
                          dwRelevantChild, bVisible, nPageIdx);
            pChildLayoutItem = iterator.SkipChildrenAndMoveToNext();
          }
          break;
        }
        default:
          break;
      }
    }
  }

  int32_t nPage = pdfium::CollectionSize<int32_t>(m_PageArray);
  for (int32_t i = nPage - 1; i >= m_nAvailPages; i--) {
    CXFA_ContainerLayoutItem* pPage = m_PageArray[i];
    m_PageArray.erase(m_PageArray.begin() + i);
    pNotify->OnPageEvent(pPage, XFA_PAGEVIEWEVENT_PostRemoved);
    delete pPage;
  }
  ClearData();
}

void XFA_ReleaseLayoutItem_NoPageArea(CXFA_LayoutItem* pLayoutItem) {
  CXFA_LayoutItem *pNext, *pNode = pLayoutItem->m_pFirstChild;
  while (pNode) {
    pNext = pNode->m_pNextSibling;
    pNode->m_pParent = nullptr;
    XFA_ReleaseLayoutItem_NoPageArea(pNode);
    pNode = pNext;
  }
  if (pLayoutItem->m_pFormNode->GetElementType() != XFA_Element::PageArea)
    delete pLayoutItem;
}

void CXFA_LayoutPageMgr::PrepareLayout() {
  m_pPageSetCurRoot = nullptr;
  m_ePageSetMode = XFA_ATTRIBUTEENUM_OrderedOccurrence;
  m_nAvailPages = 0;
  ClearData();
  if (!m_pPageSetLayoutItemRoot)
    return;

  CXFA_ContainerLayoutItem* pRootLayoutItem = m_pPageSetLayoutItemRoot;
  if (pRootLayoutItem &&
      pRootLayoutItem->m_pFormNode->GetPacketID() == XFA_XDPPACKET_Form) {
    CXFA_Node* pPageSetFormNode = pRootLayoutItem->m_pFormNode;
    pRootLayoutItem->m_pFormNode->GetDocument()->m_pPendingPageSet.clear();
    if (pPageSetFormNode->HasRemovedChildren()) {
      XFA_ReleaseLayoutItem(pRootLayoutItem);
      m_pPageSetLayoutItemRoot = nullptr;
      pRootLayoutItem = nullptr;
      pPageSetFormNode = nullptr;
      m_PageArray.clear();
    }
    while (pPageSetFormNode) {
      CXFA_Node* pNextPageSet =
          pPageSetFormNode->GetNextSameClassSibling(XFA_Element::PageSet);
      pPageSetFormNode->GetNodeItem(XFA_NODEITEM_Parent)
          ->RemoveChild(pPageSetFormNode, false);
      pRootLayoutItem->m_pFormNode->GetDocument()->m_pPendingPageSet.push_back(
          pPageSetFormNode);
      pPageSetFormNode = pNextPageSet;
    }
  }
  pRootLayoutItem = m_pPageSetLayoutItemRoot;
  CXFA_ContainerLayoutItem* pNextLayout = nullptr;
  for (; pRootLayoutItem; pRootLayoutItem = pNextLayout) {
    pNextLayout =
        static_cast<CXFA_ContainerLayoutItem*>(pRootLayoutItem->m_pNextSibling);
    SaveLayoutItem(pRootLayoutItem);
    delete pRootLayoutItem;
  }
  m_pPageSetLayoutItemRoot = nullptr;
}

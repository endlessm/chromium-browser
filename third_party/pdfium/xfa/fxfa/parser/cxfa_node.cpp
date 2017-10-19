// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/parser/cxfa_node.h"

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "core/fxcrt/cfx_decimal.h"
#include "core/fxcrt/cfx_memorystream.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/xml/cfx_xmlelement.h"
#include "core/fxcrt/xml/cfx_xmlnode.h"
#include "core/fxcrt/xml/cfx_xmltext.h"
#include "fxjs/cfxjse_value.h"
#include "third_party/base/logging.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"
#include "xfa/fxfa/cxfa_eventparam.h"
#include "xfa/fxfa/cxfa_ffnotify.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/parser/cxfa_arraynodelist.h"
#include "xfa/fxfa/parser/cxfa_attachnodelist.h"
#include "xfa/fxfa/parser/cxfa_document.h"
#include "xfa/fxfa/parser/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_measurement.h"
#include "xfa/fxfa/parser/cxfa_occur.h"
#include "xfa/fxfa/parser/cxfa_scriptcontext.h"
#include "xfa/fxfa/parser/cxfa_simple_parser.h"
#include "xfa/fxfa/parser/cxfa_traversestrategy_xfacontainernode.h"
#include "xfa/fxfa/parser/xfa_basic_data.h"

namespace {

void XFA_DeleteWideString(void* pData) {
  delete static_cast<CFX_WideString*>(pData);
}

void XFA_CopyWideString(void*& pData) {
  if (pData) {
    CFX_WideString* pNewData = new CFX_WideString(*(CFX_WideString*)pData);
    pData = pNewData;
  }
}

XFA_MAPDATABLOCKCALLBACKINFO deleteWideStringCallBack = {XFA_DeleteWideString,
                                                         XFA_CopyWideString};

void XFA_DataNodeDeleteBindItem(void* pData) {
  delete static_cast<std::vector<CXFA_Node*>*>(pData);
}

XFA_MAPDATABLOCKCALLBACKINFO deleteBindItemCallBack = {
    XFA_DataNodeDeleteBindItem, nullptr};

int32_t GetCount(CXFA_Node* pInstMgrNode) {
  ASSERT(pInstMgrNode);
  int32_t iCount = 0;
  uint32_t dwNameHash = 0;
  for (CXFA_Node* pNode = pInstMgrNode->GetNodeItem(XFA_NODEITEM_NextSibling);
       pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    XFA_Element eCurType = pNode->GetElementType();
    if (eCurType == XFA_Element::InstanceManager)
      break;
    if ((eCurType != XFA_Element::Subform) &&
        (eCurType != XFA_Element::SubformSet)) {
      continue;
    }
    if (iCount == 0) {
      CFX_WideStringC wsName = pNode->GetCData(XFA_ATTRIBUTE_Name);
      CFX_WideStringC wsInstName = pInstMgrNode->GetCData(XFA_ATTRIBUTE_Name);
      if (wsInstName.GetLength() < 1 || wsInstName[0] != '_' ||
          wsInstName.Right(wsInstName.GetLength() - 1) != wsName) {
        return iCount;
      }
      dwNameHash = pNode->GetNameHash();
    }
    if (dwNameHash != pNode->GetNameHash())
      break;

    iCount++;
  }
  return iCount;
}

std::vector<CXFA_Node*> NodesSortedByDocumentIdx(
    const std::set<CXFA_Node*>& rgNodeSet) {
  if (rgNodeSet.empty())
    return std::vector<CXFA_Node*>();

  std::vector<CXFA_Node*> rgNodeArray;
  CXFA_Node* pCommonParent =
      (*rgNodeSet.begin())->GetNodeItem(XFA_NODEITEM_Parent);
  for (CXFA_Node* pNode = pCommonParent->GetNodeItem(XFA_NODEITEM_FirstChild);
       pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pdfium::ContainsValue(rgNodeSet, pNode))
      rgNodeArray.push_back(pNode);
  }
  return rgNodeArray;
}

using CXFA_NodeSetPair = std::pair<std::set<CXFA_Node*>, std::set<CXFA_Node*>>;
using CXFA_NodeSetPairMap =
    std::map<uint32_t, std::unique_ptr<CXFA_NodeSetPair>>;
using CXFA_NodeSetPairMapMap =
    std::map<CXFA_Node*, std::unique_ptr<CXFA_NodeSetPairMap>>;

CXFA_NodeSetPair* NodeSetPairForNode(CXFA_Node* pNode,
                                     CXFA_NodeSetPairMapMap* pMap) {
  CXFA_Node* pParentNode = pNode->GetNodeItem(XFA_NODEITEM_Parent);
  uint32_t dwNameHash = pNode->GetNameHash();
  if (!pParentNode || !dwNameHash)
    return nullptr;

  if (!(*pMap)[pParentNode])
    (*pMap)[pParentNode] = pdfium::MakeUnique<CXFA_NodeSetPairMap>();

  CXFA_NodeSetPairMap* pNodeSetPairMap = (*pMap)[pParentNode].get();
  if (!(*pNodeSetPairMap)[dwNameHash])
    (*pNodeSetPairMap)[dwNameHash] = pdfium::MakeUnique<CXFA_NodeSetPair>();

  return (*pNodeSetPairMap)[dwNameHash].get();
}

void ReorderDataNodes(const std::set<CXFA_Node*>& sSet1,
                      const std::set<CXFA_Node*>& sSet2,
                      bool bInsertBefore) {
  CXFA_NodeSetPairMapMap rgMap;
  for (CXFA_Node* pNode : sSet1) {
    CXFA_NodeSetPair* pNodeSetPair = NodeSetPairForNode(pNode, &rgMap);
    if (pNodeSetPair)
      pNodeSetPair->first.insert(pNode);
  }
  for (CXFA_Node* pNode : sSet2) {
    CXFA_NodeSetPair* pNodeSetPair = NodeSetPairForNode(pNode, &rgMap);
    if (pNodeSetPair) {
      if (pdfium::ContainsValue(pNodeSetPair->first, pNode))
        pNodeSetPair->first.erase(pNode);
      else
        pNodeSetPair->second.insert(pNode);
    }
  }
  for (const auto& iter1 : rgMap) {
    CXFA_NodeSetPairMap* pNodeSetPairMap = iter1.second.get();
    if (!pNodeSetPairMap)
      continue;

    for (const auto& iter2 : *pNodeSetPairMap) {
      CXFA_NodeSetPair* pNodeSetPair = iter2.second.get();
      if (!pNodeSetPair)
        continue;
      if (!pNodeSetPair->first.empty() && !pNodeSetPair->second.empty()) {
        std::vector<CXFA_Node*> rgNodeArray1 =
            NodesSortedByDocumentIdx(pNodeSetPair->first);
        std::vector<CXFA_Node*> rgNodeArray2 =
            NodesSortedByDocumentIdx(pNodeSetPair->second);
        CXFA_Node* pParentNode = nullptr;
        CXFA_Node* pBeforeNode = nullptr;
        if (bInsertBefore) {
          pBeforeNode = rgNodeArray2.front();
          pParentNode = pBeforeNode->GetNodeItem(XFA_NODEITEM_Parent);
        } else {
          CXFA_Node* pLastNode = rgNodeArray2.back();
          pParentNode = pLastNode->GetNodeItem(XFA_NODEITEM_Parent);
          pBeforeNode = pLastNode->GetNodeItem(XFA_NODEITEM_NextSibling);
        }
        for (auto* pCurNode : rgNodeArray1) {
          pParentNode->RemoveChild(pCurNode);
          pParentNode->InsertChild(pCurNode, pBeforeNode);
        }
      }
    }
    pNodeSetPairMap->clear();
  }
}

CXFA_Node* GetItem(CXFA_Node* pInstMgrNode, int32_t iIndex) {
  ASSERT(pInstMgrNode);
  int32_t iCount = 0;
  uint32_t dwNameHash = 0;
  for (CXFA_Node* pNode = pInstMgrNode->GetNodeItem(XFA_NODEITEM_NextSibling);
       pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    XFA_Element eCurType = pNode->GetElementType();
    if (eCurType == XFA_Element::InstanceManager)
      break;
    if ((eCurType != XFA_Element::Subform) &&
        (eCurType != XFA_Element::SubformSet)) {
      continue;
    }
    if (iCount == 0) {
      CFX_WideStringC wsName = pNode->GetCData(XFA_ATTRIBUTE_Name);
      CFX_WideStringC wsInstName = pInstMgrNode->GetCData(XFA_ATTRIBUTE_Name);
      if (wsInstName.GetLength() < 1 || wsInstName[0] != '_' ||
          wsInstName.Right(wsInstName.GetLength() - 1) != wsName) {
        return nullptr;
      }
      dwNameHash = pNode->GetNameHash();
    }
    if (dwNameHash != pNode->GetNameHash())
      break;

    iCount++;
    if (iCount > iIndex)
      return pNode;
  }
  return nullptr;
}

void InsertItem(CXFA_Node* pInstMgrNode,
                CXFA_Node* pNewInstance,
                int32_t iPos,
                int32_t iCount = -1,
                bool bMoveDataBindingNodes = true) {
  if (iCount < 0)
    iCount = GetCount(pInstMgrNode);
  if (iPos < 0)
    iPos = iCount;
  if (iPos == iCount) {
    CXFA_Node* pNextSibling =
        iCount > 0
            ? GetItem(pInstMgrNode, iCount - 1)
                  ->GetNodeItem(XFA_NODEITEM_NextSibling)
            : pInstMgrNode->GetNodeItem(XFA_NODEITEM_NextSibling);
    pInstMgrNode->GetNodeItem(XFA_NODEITEM_Parent)
        ->InsertChild(pNewInstance, pNextSibling);
    if (bMoveDataBindingNodes) {
      std::set<CXFA_Node*> sNew;
      std::set<CXFA_Node*> sAfter;
      CXFA_NodeIteratorTemplate<CXFA_Node,
                                CXFA_TraverseStrategy_XFAContainerNode>
          sIteratorNew(pNewInstance);
      for (CXFA_Node* pNode = sIteratorNew.GetCurrent(); pNode;
           pNode = sIteratorNew.MoveToNext()) {
        CXFA_Node* pDataNode = pNode->GetBindData();
        if (!pDataNode)
          continue;

        sNew.insert(pDataNode);
      }
      CXFA_NodeIteratorTemplate<CXFA_Node,
                                CXFA_TraverseStrategy_XFAContainerNode>
          sIteratorAfter(pNextSibling);
      for (CXFA_Node* pNode = sIteratorAfter.GetCurrent(); pNode;
           pNode = sIteratorAfter.MoveToNext()) {
        CXFA_Node* pDataNode = pNode->GetBindData();
        if (!pDataNode)
          continue;

        sAfter.insert(pDataNode);
      }
      ReorderDataNodes(sNew, sAfter, false);
    }
  } else {
    CXFA_Node* pBeforeInstance = GetItem(pInstMgrNode, iPos);
    pInstMgrNode->GetNodeItem(XFA_NODEITEM_Parent)
        ->InsertChild(pNewInstance, pBeforeInstance);
    if (bMoveDataBindingNodes) {
      std::set<CXFA_Node*> sNew;
      std::set<CXFA_Node*> sBefore;
      CXFA_NodeIteratorTemplate<CXFA_Node,
                                CXFA_TraverseStrategy_XFAContainerNode>
          sIteratorNew(pNewInstance);
      for (CXFA_Node* pNode = sIteratorNew.GetCurrent(); pNode;
           pNode = sIteratorNew.MoveToNext()) {
        CXFA_Node* pDataNode = pNode->GetBindData();
        if (!pDataNode)
          continue;

        sNew.insert(pDataNode);
      }
      CXFA_NodeIteratorTemplate<CXFA_Node,
                                CXFA_TraverseStrategy_XFAContainerNode>
          sIteratorBefore(pBeforeInstance);
      for (CXFA_Node* pNode = sIteratorBefore.GetCurrent(); pNode;
           pNode = sIteratorBefore.MoveToNext()) {
        CXFA_Node* pDataNode = pNode->GetBindData();
        if (!pDataNode)
          continue;

        sBefore.insert(pDataNode);
      }
      ReorderDataNodes(sNew, sBefore, true);
    }
  }
}

void RemoveItem(CXFA_Node* pInstMgrNode,
                CXFA_Node* pRemoveInstance,
                bool bRemoveDataBinding = true) {
  pInstMgrNode->GetNodeItem(XFA_NODEITEM_Parent)->RemoveChild(pRemoveInstance);
  if (!bRemoveDataBinding)
    return;

  CXFA_NodeIteratorTemplate<CXFA_Node, CXFA_TraverseStrategy_XFAContainerNode>
      sIterator(pRemoveInstance);
  for (CXFA_Node* pFormNode = sIterator.GetCurrent(); pFormNode;
       pFormNode = sIterator.MoveToNext()) {
    CXFA_Node* pDataNode = pFormNode->GetBindData();
    if (!pDataNode)
      continue;

    if (pDataNode->RemoveBindItem(pFormNode) == 0) {
      if (CXFA_Node* pDataParent =
              pDataNode->GetNodeItem(XFA_NODEITEM_Parent)) {
        pDataParent->RemoveChild(pDataNode);
      }
    }
    pFormNode->SetObject(XFA_ATTRIBUTE_BindingNode, nullptr);
  }
}

CXFA_Node* CreateInstance(CXFA_Node* pInstMgrNode, bool bDataMerge) {
  CXFA_Document* pDocument = pInstMgrNode->GetDocument();
  CXFA_Node* pTemplateNode = pInstMgrNode->GetTemplateNode();
  CXFA_Node* pFormParent = pInstMgrNode->GetNodeItem(XFA_NODEITEM_Parent);
  CXFA_Node* pDataScope = nullptr;
  for (CXFA_Node* pRootBoundNode = pFormParent;
       pRootBoundNode && pRootBoundNode->IsContainerNode();
       pRootBoundNode = pRootBoundNode->GetNodeItem(XFA_NODEITEM_Parent)) {
    pDataScope = pRootBoundNode->GetBindData();
    if (pDataScope)
      break;
  }
  if (!pDataScope) {
    pDataScope = ToNode(pDocument->GetXFAObject(XFA_HASHCODE_Record));
    ASSERT(pDataScope);
  }
  CXFA_Node* pInstance = pDocument->DataMerge_CopyContainer(
      pTemplateNode, pFormParent, pDataScope, true, bDataMerge, true);
  if (pInstance) {
    pDocument->DataMerge_UpdateBindingRelations(pInstance);
    pFormParent->RemoveChild(pInstance);
  }
  return pInstance;
}

struct XFA_ExecEventParaInfo {
 public:
  uint32_t m_uHash;
  const wchar_t* m_lpcEventName;
  XFA_EVENTTYPE m_eventType;
  uint32_t m_validFlags;
};
static const XFA_ExecEventParaInfo gs_eventParaInfos[] = {
    {0x02a6c55a, L"postSubmit", XFA_EVENT_PostSubmit, 0},
    {0x0ab466bb, L"preSubmit", XFA_EVENT_PreSubmit, 0},
    {0x109d7ce7, L"mouseEnter", XFA_EVENT_MouseEnter, 5},
    {0x17fad373, L"postPrint", XFA_EVENT_PostPrint, 0},
    {0x1bfc72d9, L"preOpen", XFA_EVENT_PreOpen, 7},
    {0x2196a452, L"initialize", XFA_EVENT_Initialize, 1},
    {0x27410f03, L"mouseExit", XFA_EVENT_MouseExit, 5},
    {0x33c43dec, L"docClose", XFA_EVENT_DocClose, 0},
    {0x361fa1b6, L"preSave", XFA_EVENT_PreSave, 0},
    {0x36f1c6d8, L"preSign", XFA_EVENT_PreSign, 6},
    {0x4731d6ba, L"exit", XFA_EVENT_Exit, 2},
    {0x56bf456b, L"docReady", XFA_EVENT_DocReady, 0},
    {0x7233018a, L"validate", XFA_EVENT_Validate, 1},
    {0x8808385e, L"indexChange", XFA_EVENT_IndexChange, 3},
    {0x891f4606, L"change", XFA_EVENT_Change, 4},
    {0x9528a7b4, L"prePrint", XFA_EVENT_PrePrint, 0},
    {0x9f693b21, L"mouseDown", XFA_EVENT_MouseDown, 5},
    {0xcdce56b3, L"full", XFA_EVENT_Full, 4},
    {0xd576d08e, L"mouseUp", XFA_EVENT_MouseUp, 5},
    {0xd95657a6, L"click", XFA_EVENT_Click, 4},
    {0xdbfbe02e, L"calculate", XFA_EVENT_Calculate, 1},
    {0xe25fa7b8, L"postOpen", XFA_EVENT_PostOpen, 7},
    {0xe28dce7e, L"enter", XFA_EVENT_Enter, 2},
    {0xfc82d695, L"postSave", XFA_EVENT_PostSave, 0},
    {0xfd54fbb7, L"postSign", XFA_EVENT_PostSign, 6},
};

const XFA_ExecEventParaInfo* GetEventParaInfoByName(
    const CFX_WideStringC& wsEventName) {
  uint32_t uHash = FX_HashCode_GetW(wsEventName, false);
  int32_t iStart = 0;
  int32_t iEnd = (sizeof(gs_eventParaInfos) / sizeof(gs_eventParaInfos[0])) - 1;
  do {
    int32_t iMid = (iStart + iEnd) / 2;
    const XFA_ExecEventParaInfo* eventParaInfo = &gs_eventParaInfos[iMid];
    if (uHash == eventParaInfo->m_uHash)
      return eventParaInfo;
    if (uHash < eventParaInfo->m_uHash)
      iEnd = iMid - 1;
    else
      iStart = iMid + 1;
  } while (iStart <= iEnd);
  return nullptr;
}

void StrToRGB(const CFX_WideString& strRGB,
              int32_t& r,
              int32_t& g,
              int32_t& b) {
  r = 0;
  g = 0;
  b = 0;

  wchar_t zero = '0';
  int32_t iIndex = 0;
  int32_t iLen = strRGB.GetLength();
  for (int32_t i = 0; i < iLen; ++i) {
    wchar_t ch = strRGB[i];
    if (ch == L',')
      ++iIndex;
    if (iIndex > 2)
      break;

    int32_t iValue = ch - zero;
    if (iValue >= 0 && iValue <= 9) {
      switch (iIndex) {
        case 0:
          r = r * 10 + iValue;
          break;
        case 1:
          g = g * 10 + iValue;
          break;
        default:
          b = b * 10 + iValue;
          break;
      }
    }
  }
}

enum XFA_KEYTYPE {
  XFA_KEYTYPE_Custom,
  XFA_KEYTYPE_Element,
};

void* GetMapKey_Custom(const CFX_WideStringC& wsKey) {
  uint32_t dwKey = FX_HashCode_GetW(wsKey, false);
  return (void*)(uintptr_t)((dwKey << 1) | XFA_KEYTYPE_Custom);
}

void* GetMapKey_Element(XFA_Element eType, XFA_ATTRIBUTE eAttribute) {
  return (void*)(uintptr_t)((static_cast<int32_t>(eType) << 16) |
                            (eAttribute << 8) | XFA_KEYTYPE_Element);
}

const XFA_ATTRIBUTEINFO* GetAttributeOfElement(XFA_Element eElement,
                                               XFA_ATTRIBUTE eAttribute,
                                               uint32_t dwPacket) {
  int32_t iCount = 0;
  const uint8_t* pAttr = XFA_GetElementAttributes(eElement, iCount);
  if (!pAttr || iCount < 1)
    return nullptr;

  if (!std::binary_search(pAttr, pAttr + iCount, eAttribute))
    return nullptr;

  const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttribute);
  ASSERT(pInfo);
  if (dwPacket == XFA_XDPPACKET_UNKNOWN)
    return pInfo;
  return (dwPacket & pInfo->dwPackets) ? pInfo : nullptr;
}

const XFA_ATTRIBUTEENUMINFO* GetAttributeEnumByID(XFA_ATTRIBUTEENUM eName) {
  return g_XFAEnumData + eName;
}

}  // namespace

static void XFA_DefaultFreeData(void* pData) {}

static XFA_MAPDATABLOCKCALLBACKINFO gs_XFADefaultFreeData = {
    XFA_DefaultFreeData, nullptr};

XFA_MAPMODULEDATA::XFA_MAPMODULEDATA() {}

XFA_MAPMODULEDATA::~XFA_MAPMODULEDATA() {}

CXFA_Node::CXFA_Node(CXFA_Document* pDoc,
                     uint16_t ePacket,
                     XFA_ObjectType oType,
                     XFA_Element eType,
                     const CFX_WideStringC& elementName)
    : CXFA_Object(pDoc, oType, eType, elementName),
      m_pNext(nullptr),
      m_pChild(nullptr),
      m_pLastChild(nullptr),
      m_pParent(nullptr),
      m_pXMLNode(nullptr),
      m_ePacket(ePacket),
      m_uNodeFlags(XFA_NodeFlag_None),
      m_dwNameHash(0),
      m_pAuxNode(nullptr),
      m_pMapModuleData(nullptr) {
  ASSERT(m_pDocument);
}

CXFA_Node::~CXFA_Node() {
  ASSERT(!m_pParent);
  RemoveMapModuleKey();
  CXFA_Node* pNode = m_pChild;
  while (pNode) {
    CXFA_Node* pNext = pNode->m_pNext;
    pNode->m_pParent = nullptr;
    delete pNode;
    pNode = pNext;
  }
  if (m_pXMLNode && IsOwnXMLNode())
    delete m_pXMLNode;
}

CXFA_Node* CXFA_Node::Clone(bool bRecursive) {
  CXFA_Node* pClone = m_pDocument->CreateNode(m_ePacket, m_elementType);
  if (!pClone)
    return nullptr;

  MergeAllData(pClone);
  pClone->UpdateNameHash();
  if (IsNeedSavingXMLNode()) {
    std::unique_ptr<CFX_XMLNode> pCloneXML;
    if (IsAttributeInXML()) {
      CFX_WideString wsName;
      GetAttribute(XFA_ATTRIBUTE_Name, wsName, false);
      auto pCloneXMLElement = pdfium::MakeUnique<CFX_XMLElement>(wsName);
      CFX_WideStringC wsValue = GetCData(XFA_ATTRIBUTE_Value);
      if (!wsValue.IsEmpty()) {
        pCloneXMLElement->SetTextData(CFX_WideString(wsValue));
      }
      pCloneXML.reset(pCloneXMLElement.release());
      pClone->SetEnum(XFA_ATTRIBUTE_Contains, XFA_ATTRIBUTEENUM_Unknown);
    } else {
      pCloneXML = m_pXMLNode->Clone();
    }
    pClone->SetXMLMappingNode(pCloneXML.release());
    pClone->SetFlag(XFA_NodeFlag_OwnXMLNode, false);
  }
  if (bRecursive) {
    for (CXFA_Node* pChild = GetNodeItem(XFA_NODEITEM_FirstChild); pChild;
         pChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      pClone->InsertChild(pChild->Clone(bRecursive));
    }
  }
  pClone->SetFlag(XFA_NodeFlag_Initialized, true);
  pClone->SetObject(XFA_ATTRIBUTE_BindingNode, nullptr);
  return pClone;
}

CXFA_Node* CXFA_Node::GetNodeItem(XFA_NODEITEM eItem) const {
  switch (eItem) {
    case XFA_NODEITEM_NextSibling:
      return m_pNext;
    case XFA_NODEITEM_FirstChild:
      return m_pChild;
    case XFA_NODEITEM_Parent:
      return m_pParent;
    case XFA_NODEITEM_PrevSibling:
      if (m_pParent) {
        CXFA_Node* pSibling = m_pParent->m_pChild;
        CXFA_Node* pPrev = nullptr;
        while (pSibling && pSibling != this) {
          pPrev = pSibling;
          pSibling = pSibling->m_pNext;
        }
        return pPrev;
      }
      return nullptr;
    default:
      break;
  }
  return nullptr;
}

CXFA_Node* CXFA_Node::GetNodeItem(XFA_NODEITEM eItem,
                                  XFA_ObjectType eType) const {
  CXFA_Node* pNode = nullptr;
  switch (eItem) {
    case XFA_NODEITEM_NextSibling:
      pNode = m_pNext;
      while (pNode && pNode->GetObjectType() != eType)
        pNode = pNode->m_pNext;
      break;
    case XFA_NODEITEM_FirstChild:
      pNode = m_pChild;
      while (pNode && pNode->GetObjectType() != eType)
        pNode = pNode->m_pNext;
      break;
    case XFA_NODEITEM_Parent:
      pNode = m_pParent;
      while (pNode && pNode->GetObjectType() != eType)
        pNode = pNode->m_pParent;
      break;
    case XFA_NODEITEM_PrevSibling:
      if (m_pParent) {
        CXFA_Node* pSibling = m_pParent->m_pChild;
        while (pSibling && pSibling != this) {
          if (eType == pSibling->GetObjectType())
            pNode = pSibling;

          pSibling = pSibling->m_pNext;
        }
      }
      break;
    default:
      break;
  }
  return pNode;
}

std::vector<CXFA_Node*> CXFA_Node::GetNodeList(uint32_t dwTypeFilter,
                                               XFA_Element eTypeFilter) {
  std::vector<CXFA_Node*> nodes;
  if (eTypeFilter != XFA_Element::Unknown) {
    for (CXFA_Node* pChild = m_pChild; pChild; pChild = pChild->m_pNext) {
      if (pChild->GetElementType() == eTypeFilter)
        nodes.push_back(pChild);
    }
  } else if (dwTypeFilter ==
             (XFA_NODEFILTER_Children | XFA_NODEFILTER_Properties)) {
    for (CXFA_Node* pChild = m_pChild; pChild; pChild = pChild->m_pNext)
      nodes.push_back(pChild);
  } else if (dwTypeFilter != 0) {
    bool bFilterChildren = !!(dwTypeFilter & XFA_NODEFILTER_Children);
    bool bFilterProperties = !!(dwTypeFilter & XFA_NODEFILTER_Properties);
    bool bFilterOneOfProperties =
        !!(dwTypeFilter & XFA_NODEFILTER_OneOfProperty);
    CXFA_Node* pChild = m_pChild;
    while (pChild) {
      const XFA_PROPERTY* pProperty = XFA_GetPropertyOfElement(
          GetElementType(), pChild->GetElementType(), XFA_XDPPACKET_UNKNOWN);
      if (pProperty) {
        if (bFilterProperties) {
          nodes.push_back(pChild);
        } else if (bFilterOneOfProperties &&
                   (pProperty->uFlags & XFA_PROPERTYFLAG_OneOf)) {
          nodes.push_back(pChild);
        } else if (bFilterChildren &&
                   (pChild->GetElementType() == XFA_Element::Variables ||
                    pChild->GetElementType() == XFA_Element::PageSet)) {
          nodes.push_back(pChild);
        }
      } else if (bFilterChildren) {
        nodes.push_back(pChild);
      }
      pChild = pChild->m_pNext;
    }
    if (bFilterOneOfProperties && nodes.empty()) {
      int32_t iProperties = 0;
      const XFA_PROPERTY* pProperty =
          XFA_GetElementProperties(GetElementType(), iProperties);
      if (!pProperty || iProperties < 1)
        return nodes;
      for (int32_t i = 0; i < iProperties; i++) {
        if (pProperty[i].uFlags & XFA_PROPERTYFLAG_DefaultOneOf) {
          const XFA_PACKETINFO* pPacket = XFA_GetPacketByID(GetPacketID());
          CXFA_Node* pNewNode =
              m_pDocument->CreateNode(pPacket, pProperty[i].eName);
          if (!pNewNode)
            break;
          InsertChild(pNewNode, nullptr);
          pNewNode->SetFlag(XFA_NodeFlag_Initialized, true);
          nodes.push_back(pNewNode);
          break;
        }
      }
    }
  }
  return nodes;
}

CXFA_Node* CXFA_Node::CreateSamePacketNode(XFA_Element eType,
                                           uint32_t dwFlags) {
  CXFA_Node* pNode = m_pDocument->CreateNode(m_ePacket, eType);
  pNode->SetFlag(dwFlags, true);
  return pNode;
}

CXFA_Node* CXFA_Node::CloneTemplateToForm(bool bRecursive) {
  ASSERT(m_ePacket == XFA_XDPPACKET_Template);
  CXFA_Node* pClone =
      m_pDocument->CreateNode(XFA_XDPPACKET_Form, m_elementType);
  if (!pClone)
    return nullptr;

  pClone->SetTemplateNode(this);
  pClone->UpdateNameHash();
  pClone->SetXMLMappingNode(GetXMLMappingNode());
  if (bRecursive) {
    for (CXFA_Node* pChild = GetNodeItem(XFA_NODEITEM_FirstChild); pChild;
         pChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      pClone->InsertChild(pChild->CloneTemplateToForm(bRecursive));
    }
  }
  pClone->SetFlag(XFA_NodeFlag_Initialized, true);
  return pClone;
}

CXFA_Node* CXFA_Node::GetTemplateNode() const {
  return m_pAuxNode;
}

void CXFA_Node::SetTemplateNode(CXFA_Node* pTemplateNode) {
  m_pAuxNode = pTemplateNode;
}

CXFA_Node* CXFA_Node::GetBindData() {
  ASSERT(GetPacketID() == XFA_XDPPACKET_Form);
  return static_cast<CXFA_Node*>(GetObject(XFA_ATTRIBUTE_BindingNode));
}

std::vector<CXFA_Node*> CXFA_Node::GetBindItems() {
  if (BindsFormItems()) {
    void* pBinding = nullptr;
    TryObject(XFA_ATTRIBUTE_BindingNode, pBinding);
    return *static_cast<std::vector<CXFA_Node*>*>(pBinding);
  }
  std::vector<CXFA_Node*> result;
  CXFA_Node* pFormNode =
      static_cast<CXFA_Node*>(GetObject(XFA_ATTRIBUTE_BindingNode));
  if (pFormNode)
    result.push_back(pFormNode);
  return result;
}

int32_t CXFA_Node::AddBindItem(CXFA_Node* pFormNode) {
  ASSERT(pFormNode);
  if (BindsFormItems()) {
    void* pBinding = nullptr;
    TryObject(XFA_ATTRIBUTE_BindingNode, pBinding);
    auto* pItems = static_cast<std::vector<CXFA_Node*>*>(pBinding);
    if (!pdfium::ContainsValue(*pItems, pFormNode))
      pItems->push_back(pFormNode);
    return pdfium::CollectionSize<int32_t>(*pItems);
  }
  CXFA_Node* pOldFormItem =
      static_cast<CXFA_Node*>(GetObject(XFA_ATTRIBUTE_BindingNode));
  if (!pOldFormItem) {
    SetObject(XFA_ATTRIBUTE_BindingNode, pFormNode);
    return 1;
  }
  if (pOldFormItem == pFormNode)
    return 1;

  std::vector<CXFA_Node*>* pItems = new std::vector<CXFA_Node*>;
  SetObject(XFA_ATTRIBUTE_BindingNode, pItems, &deleteBindItemCallBack);
  pItems->push_back(pOldFormItem);
  pItems->push_back(pFormNode);
  m_uNodeFlags |= XFA_NodeFlag_BindFormItems;
  return 2;
}

int32_t CXFA_Node::RemoveBindItem(CXFA_Node* pFormNode) {
  if (BindsFormItems()) {
    void* pBinding = nullptr;
    TryObject(XFA_ATTRIBUTE_BindingNode, pBinding);
    auto* pItems = static_cast<std::vector<CXFA_Node*>*>(pBinding);
    auto iter = std::find(pItems->begin(), pItems->end(), pFormNode);
    if (iter != pItems->end()) {
      *iter = pItems->back();
      pItems->pop_back();
      if (pItems->size() == 1) {
        SetObject(XFA_ATTRIBUTE_BindingNode,
                  (*pItems)[0]);  // Invalidates pItems.
        m_uNodeFlags &= ~XFA_NodeFlag_BindFormItems;
        return 1;
      }
    }
    return pdfium::CollectionSize<int32_t>(*pItems);
  }
  CXFA_Node* pOldFormItem =
      static_cast<CXFA_Node*>(GetObject(XFA_ATTRIBUTE_BindingNode));
  if (pOldFormItem != pFormNode)
    return pOldFormItem ? 1 : 0;

  SetObject(XFA_ATTRIBUTE_BindingNode, nullptr);
  return 0;
}

bool CXFA_Node::HasBindItem() {
  return GetPacketID() == XFA_XDPPACKET_Datasets &&
         GetObject(XFA_ATTRIBUTE_BindingNode);
}

CXFA_WidgetData* CXFA_Node::GetWidgetData() {
  return (CXFA_WidgetData*)GetObject(XFA_ATTRIBUTE_WidgetData);
}

CXFA_WidgetData* CXFA_Node::GetContainerWidgetData() {
  if (GetPacketID() != XFA_XDPPACKET_Form)
    return nullptr;
  XFA_Element eType = GetElementType();
  if (eType == XFA_Element::ExclGroup)
    return nullptr;
  CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
  if (pParentNode && pParentNode->GetElementType() == XFA_Element::ExclGroup)
    return nullptr;

  if (eType == XFA_Element::Field) {
    CXFA_WidgetData* pFieldWidgetData = GetWidgetData();
    if (pFieldWidgetData &&
        pFieldWidgetData->GetChoiceListOpen() ==
            XFA_ATTRIBUTEENUM_MultiSelect) {
      return nullptr;
    } else {
      CFX_WideString wsPicture;
      if (pFieldWidgetData) {
        pFieldWidgetData->GetPictureContent(wsPicture,
                                            XFA_VALUEPICTURE_DataBind);
      }
      if (!wsPicture.IsEmpty())
        return pFieldWidgetData;
      CXFA_Node* pDataNode = GetBindData();
      if (!pDataNode)
        return nullptr;
      pFieldWidgetData = nullptr;
      for (CXFA_Node* pFormNode : pDataNode->GetBindItems()) {
        if (!pFormNode || pFormNode->HasRemovedChildren())
          continue;
        pFieldWidgetData = pFormNode->GetWidgetData();
        if (pFieldWidgetData) {
          pFieldWidgetData->GetPictureContent(wsPicture,
                                              XFA_VALUEPICTURE_DataBind);
        }
        if (!wsPicture.IsEmpty())
          break;
        pFieldWidgetData = nullptr;
      }
      return pFieldWidgetData;
    }
  }
  CXFA_Node* pGrandNode =
      pParentNode ? pParentNode->GetNodeItem(XFA_NODEITEM_Parent) : nullptr;
  CXFA_Node* pValueNode =
      (pParentNode && pParentNode->GetElementType() == XFA_Element::Value)
          ? pParentNode
          : nullptr;
  if (!pValueNode) {
    pValueNode =
        (pGrandNode && pGrandNode->GetElementType() == XFA_Element::Value)
            ? pGrandNode
            : nullptr;
  }
  CXFA_Node* pParentOfValueNode =
      pValueNode ? pValueNode->GetNodeItem(XFA_NODEITEM_Parent) : nullptr;
  return pParentOfValueNode ? pParentOfValueNode->GetContainerWidgetData()
                            : nullptr;
}

bool CXFA_Node::GetLocaleName(CFX_WideString& wsLocaleName) {
  CXFA_Node* pForm = GetDocument()->GetXFAObject(XFA_HASHCODE_Form)->AsNode();
  CXFA_Node* pTopSubform = pForm->GetFirstChildByClass(XFA_Element::Subform);
  ASSERT(pTopSubform);
  CXFA_Node* pLocaleNode = this;
  bool bLocale = false;
  do {
    bLocale = pLocaleNode->TryCData(XFA_ATTRIBUTE_Locale, wsLocaleName, false);
    if (!bLocale) {
      pLocaleNode = pLocaleNode->GetNodeItem(XFA_NODEITEM_Parent);
    }
  } while (pLocaleNode && pLocaleNode != pTopSubform && !bLocale);
  if (bLocale)
    return true;
  CXFA_Node* pConfig = ToNode(GetDocument()->GetXFAObject(XFA_HASHCODE_Config));
  wsLocaleName = GetDocument()->GetLocalMgr()->GetConfigLocaleName(pConfig);
  if (!wsLocaleName.IsEmpty())
    return true;
  if (pTopSubform &&
      pTopSubform->TryCData(XFA_ATTRIBUTE_Locale, wsLocaleName, false)) {
    return true;
  }
  IFX_Locale* pLocale = GetDocument()->GetLocalMgr()->GetDefLocale();
  if (pLocale) {
    wsLocaleName = pLocale->GetName();
    return true;
  }
  return false;
}

XFA_ATTRIBUTEENUM CXFA_Node::GetIntact() {
  CXFA_Node* pKeep = GetFirstChildByClass(XFA_Element::Keep);
  XFA_ATTRIBUTEENUM eLayoutType = GetEnum(XFA_ATTRIBUTE_Layout);
  if (pKeep) {
    XFA_ATTRIBUTEENUM eIntact;
    if (pKeep->TryEnum(XFA_ATTRIBUTE_Intact, eIntact, false)) {
      if (eIntact == XFA_ATTRIBUTEENUM_None &&
          eLayoutType == XFA_ATTRIBUTEENUM_Row &&
          m_pDocument->GetCurVersionMode() < XFA_VERSION_208) {
        CXFA_Node* pPreviewRow = GetNodeItem(XFA_NODEITEM_PrevSibling,
                                             XFA_ObjectType::ContainerNode);
        if (pPreviewRow &&
            pPreviewRow->GetEnum(XFA_ATTRIBUTE_Layout) ==
                XFA_ATTRIBUTEENUM_Row) {
          XFA_ATTRIBUTEENUM eValue;
          if (pKeep->TryEnum(XFA_ATTRIBUTE_Previous, eValue, false) &&
              (eValue == XFA_ATTRIBUTEENUM_ContentArea ||
               eValue == XFA_ATTRIBUTEENUM_PageArea)) {
            return XFA_ATTRIBUTEENUM_ContentArea;
          }
          CXFA_Node* pNode =
              pPreviewRow->GetFirstChildByClass(XFA_Element::Keep);
          if (pNode && pNode->TryEnum(XFA_ATTRIBUTE_Next, eValue, false) &&
              (eValue == XFA_ATTRIBUTEENUM_ContentArea ||
               eValue == XFA_ATTRIBUTEENUM_PageArea)) {
            return XFA_ATTRIBUTEENUM_ContentArea;
          }
        }
      }
      return eIntact;
    }
  }
  switch (GetElementType()) {
    case XFA_Element::Subform:
      switch (eLayoutType) {
        case XFA_ATTRIBUTEENUM_Position:
        case XFA_ATTRIBUTEENUM_Row:
          return XFA_ATTRIBUTEENUM_ContentArea;
        case XFA_ATTRIBUTEENUM_Tb:
        case XFA_ATTRIBUTEENUM_Table:
        case XFA_ATTRIBUTEENUM_Lr_tb:
        case XFA_ATTRIBUTEENUM_Rl_tb:
          return XFA_ATTRIBUTEENUM_None;
        default:
          break;
      }
      break;
    case XFA_Element::Field: {
      CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
      if (!pParentNode ||
          pParentNode->GetElementType() == XFA_Element::PageArea)
        return XFA_ATTRIBUTEENUM_ContentArea;
      if (pParentNode->GetIntact() == XFA_ATTRIBUTEENUM_None) {
        XFA_ATTRIBUTEENUM eParLayout =
            pParentNode->GetEnum(XFA_ATTRIBUTE_Layout);
        if (eParLayout == XFA_ATTRIBUTEENUM_Position ||
            eParLayout == XFA_ATTRIBUTEENUM_Row ||
            eParLayout == XFA_ATTRIBUTEENUM_Table) {
          return XFA_ATTRIBUTEENUM_None;
        }
        XFA_VERSION version = m_pDocument->GetCurVersionMode();
        if (eParLayout == XFA_ATTRIBUTEENUM_Tb && version < XFA_VERSION_208) {
          CXFA_Measurement measureH;
          if (TryMeasure(XFA_ATTRIBUTE_H, measureH, false))
            return XFA_ATTRIBUTEENUM_ContentArea;
        }
        return XFA_ATTRIBUTEENUM_None;
      }
      return XFA_ATTRIBUTEENUM_ContentArea;
    }
    case XFA_Element::Draw:
      return XFA_ATTRIBUTEENUM_ContentArea;
    default:
      break;
  }
  return XFA_ATTRIBUTEENUM_None;
}

CXFA_Node* CXFA_Node::GetDataDescriptionNode() {
  if (m_ePacket == XFA_XDPPACKET_Datasets)
    return m_pAuxNode;
  return nullptr;
}

void CXFA_Node::SetDataDescriptionNode(CXFA_Node* pDataDescriptionNode) {
  ASSERT(m_ePacket == XFA_XDPPACKET_Datasets);
  m_pAuxNode = pDataDescriptionNode;
}

void CXFA_Node::Script_TreeClass_ResolveNode(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"resolveNode");
    return;
  }
  CFX_WideString wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext)
    return;
  CXFA_Node* refNode = this;
  if (refNode->GetElementType() == XFA_Element::Xfa)
    refNode = ToNode(pScriptContext->GetThisObject());
  uint32_t dwFlag = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Attributes |
                    XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                    XFA_RESOLVENODE_Siblings;
  XFA_RESOLVENODE_RS resoveNodeRS;
  int32_t iRet = pScriptContext->ResolveObjects(
      refNode, wsExpression.AsStringC(), resoveNodeRS, dwFlag);
  if (iRet < 1) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  if (resoveNodeRS.dwFlags == XFA_RESOVENODE_RSTYPE_Nodes) {
    CXFA_Object* pObject = resoveNodeRS.objects.front();
    pArguments->GetReturnValue()->Assign(
        pScriptContext->GetJSValueFromMap(pObject));
  } else {
    const XFA_SCRIPTATTRIBUTEINFO* lpAttributeInfo =
        resoveNodeRS.pScriptAttribute;
    if (lpAttributeInfo && lpAttributeInfo->eValueType == XFA_SCRIPT_Object) {
      auto pValue =
          pdfium::MakeUnique<CFXJSE_Value>(pScriptContext->GetRuntime());
      (resoveNodeRS.objects.front()->*(lpAttributeInfo->lpfnCallback))(
          pValue.get(), false, (XFA_ATTRIBUTE)lpAttributeInfo->eAttribute);
      pArguments->GetReturnValue()->Assign(pValue.get());
    } else {
      pArguments->GetReturnValue()->SetNull();
    }
  }
}

void CXFA_Node::Script_TreeClass_ResolveNodes(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"resolveNodes");
    return;
  }
  CFX_WideString wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (!pValue)
    return;
  uint32_t dwFlag = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Attributes |
                    XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                    XFA_RESOLVENODE_Siblings;
  CXFA_Node* refNode = this;
  if (refNode->GetElementType() == XFA_Element::Xfa)
    refNode = ToNode(m_pDocument->GetScriptContext()->GetThisObject());
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag, refNode);
}

void CXFA_Node::Script_Som_ResolveNodeList(CFXJSE_Value* pValue,
                                           CFX_WideString wsExpression,
                                           uint32_t dwFlag,
                                           CXFA_Node* refNode) {
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext)
    return;
  XFA_RESOLVENODE_RS resoveNodeRS;
  if (!refNode)
    refNode = this;
  pScriptContext->ResolveObjects(refNode, wsExpression.AsStringC(),
                                 resoveNodeRS, dwFlag);
  CXFA_ArrayNodeList* pNodeList = new CXFA_ArrayNodeList(m_pDocument.Get());
  if (resoveNodeRS.dwFlags == XFA_RESOVENODE_RSTYPE_Nodes) {
    for (CXFA_Object* pObject : resoveNodeRS.objects) {
      if (pObject->IsNode())
        pNodeList->Append(pObject->AsNode());
    }
  } else {
    CXFA_ValueArray valueArray(pScriptContext->GetRuntime());
    if (resoveNodeRS.GetAttributeResult(&valueArray) > 0) {
      for (CXFA_Object* pObject : valueArray.GetAttributeObject()) {
        if (pObject->IsNode())
          pNodeList->Append(pObject->AsNode());
      }
    }
  }
  pValue->SetObject(pNodeList, pScriptContext->GetJseNormalClass());
}

void CXFA_Node::Script_TreeClass_All(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  uint32_t dwFlag = XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_ALL;
  CFX_WideString wsName;
  GetAttribute(XFA_ATTRIBUTE_Name, wsName);
  CFX_WideString wsExpression = wsName + L"[*]";
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag);
}

void CXFA_Node::Script_TreeClass_Nodes(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext)
    return;
  if (bSetting) {
    CFX_WideString wsMessage = L"Unable to set ";
    FXJSE_ThrowMessage(wsMessage.UTF8Encode().AsStringC());
  } else {
    CXFA_AttachNodeList* pNodeList =
        new CXFA_AttachNodeList(m_pDocument.Get(), this);
    pValue->SetObject(pNodeList, pScriptContext->GetJseNormalClass());
  }
}

void CXFA_Node::Script_TreeClass_ClassAll(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  uint32_t dwFlag = XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_ALL;
  CFX_WideString wsExpression = L"#" + GetClassName() + L"[*]";
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag);
}

void CXFA_Node::Script_TreeClass_Parent(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  CXFA_Node* pParent = GetNodeItem(XFA_NODEITEM_Parent);
  if (pParent) {
    pValue->Assign(m_pDocument->GetScriptContext()->GetJSValueFromMap(pParent));
  } else {
    pValue->SetNull();
  }
}

void CXFA_Node::Script_TreeClass_Index(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(GetNodeSameNameIndex());
}

void CXFA_Node::Script_TreeClass_ClassIndex(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(GetNodeSameClassIndex());
}

void CXFA_Node::Script_TreeClass_SomExpression(CFXJSE_Value* pValue,
                                               bool bSetting,
                                               XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  CFX_WideString wsSOMExpression;
  GetSOMExpression(wsSOMExpression);
  pValue->SetString(wsSOMExpression.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_NodeClass_ApplyXSL(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"applyXSL");
    return;
  }
  CFX_WideString wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)wsExpression;
}

void CXFA_Node::Script_NodeClass_AssignNode(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    ThrowParamCountMismatchException(L"assignNode");
    return;
  }
  CFX_WideString wsExpression;
  CFX_WideString wsValue;
  int32_t iAction = 0;
  wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  if (iLength >= 2) {
    wsValue =
        CFX_WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringC());
  }
  if (iLength >= 3)
    iAction = pArguments->GetInt32(2);
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)wsExpression;
  (void)wsValue;
  (void)iAction;
}

void CXFA_Node::Script_NodeClass_Clone(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"clone");
    return;
  }
  bool bClone = !!pArguments->GetInt32(0);
  CXFA_Node* pCloneNode = Clone(bClone);
  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pCloneNode));
}

void CXFA_Node::Script_NodeClass_GetAttribute(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"getAttribute");
    return;
  }
  CFX_WideString wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  CFX_WideString wsValue;
  GetAttribute(wsExpression.AsStringC(), wsValue);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetString(wsValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_NodeClass_GetElement(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 2) {
    ThrowParamCountMismatchException(L"getElement");
    return;
  }
  CFX_WideString wsExpression;
  int32_t iValue = 0;
  wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  if (iLength >= 2)
    iValue = pArguments->GetInt32(1);
  CXFA_Node* pNode =
      GetProperty(iValue, XFA_GetElementTypeForName(wsExpression.AsStringC()));
  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pNode));
}

void CXFA_Node::Script_NodeClass_IsPropertySpecified(
    CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    ThrowParamCountMismatchException(L"isPropertySpecified");
    return;
  }
  CFX_WideString wsExpression;
  bool bParent = true;
  int32_t iIndex = 0;
  wsExpression =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  if (iLength >= 2)
    bParent = !!pArguments->GetInt32(1);
  if (iLength >= 3)
    iIndex = pArguments->GetInt32(2);
  bool bHas = false;
  const XFA_ATTRIBUTEINFO* pAttributeInfo =
      XFA_GetAttributeByName(wsExpression.AsStringC());
  CFX_WideString wsValue;
  if (pAttributeInfo)
    bHas = HasAttribute(pAttributeInfo->eName);
  if (!bHas) {
    XFA_Element eType = XFA_GetElementTypeForName(wsExpression.AsStringC());
    bHas = !!GetProperty(iIndex, eType);
    if (!bHas && bParent && m_pParent) {
      // Also check on the parent.
      bHas = m_pParent->HasAttribute(pAttributeInfo->eName);
      if (!bHas)
        bHas = !!m_pParent->GetProperty(iIndex, eType);
    }
  }
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(bHas);
}

void CXFA_Node::Script_NodeClass_LoadXML(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    ThrowParamCountMismatchException(L"loadXML");
    return;
  }

  bool bIgnoreRoot = true;
  bool bOverwrite = 0;
  CFX_ByteString wsExpression = pArguments->GetUTF8String(0);
  if (wsExpression.IsEmpty())
    return;
  if (iLength >= 2)
    bIgnoreRoot = !!pArguments->GetInt32(1);
  if (iLength >= 3)
    bOverwrite = !!pArguments->GetInt32(2);
  auto pParser =
      pdfium::MakeUnique<CXFA_SimpleParser>(m_pDocument.Get(), false);
  if (!pParser)
    return;
  CFX_XMLNode* pXMLNode = pParser->ParseXMLData(wsExpression);
  if (!pXMLNode)
    return;
  if (bIgnoreRoot &&
      (pXMLNode->GetType() != FX_XMLNODE_Element ||
       XFA_RecognizeRichText(static_cast<CFX_XMLElement*>(pXMLNode)))) {
    bIgnoreRoot = false;
  }
  CXFA_Node* pFakeRoot = Clone(false);
  CFX_WideStringC wsContentType = GetCData(XFA_ATTRIBUTE_ContentType);
  if (!wsContentType.IsEmpty()) {
    pFakeRoot->SetCData(XFA_ATTRIBUTE_ContentType,
                        CFX_WideString(wsContentType));
  }

  std::unique_ptr<CFX_XMLNode> pFakeXMLRoot(pFakeRoot->GetXMLMappingNode());
  if (!pFakeXMLRoot) {
    CFX_XMLNode* pThisXMLRoot = GetXMLMappingNode();
    pFakeXMLRoot = pThisXMLRoot ? pThisXMLRoot->Clone() : nullptr;
  }
  if (!pFakeXMLRoot) {
    pFakeXMLRoot =
        pdfium::MakeUnique<CFX_XMLElement>(CFX_WideString(GetClassName()));
  }

  if (bIgnoreRoot) {
    CFX_XMLNode* pXMLChild = pXMLNode->GetNodeItem(CFX_XMLNode::FirstChild);
    while (pXMLChild) {
      CFX_XMLNode* pXMLSibling =
          pXMLChild->GetNodeItem(CFX_XMLNode::NextSibling);
      pXMLNode->RemoveChildNode(pXMLChild);
      pFakeXMLRoot->InsertChildNode(pXMLChild);
      pXMLChild = pXMLSibling;
    }
  } else {
    CFX_XMLNode* pXMLParent = pXMLNode->GetNodeItem(CFX_XMLNode::Parent);
    if (pXMLParent) {
      pXMLParent->RemoveChildNode(pXMLNode);
    }
    pFakeXMLRoot->InsertChildNode(pXMLNode);
  }
  pParser->ConstructXFANode(pFakeRoot, pFakeXMLRoot.get());
  pFakeRoot = pParser->GetRootNode();
  if (!pFakeRoot)
    return;

  if (bOverwrite) {
    CXFA_Node* pChild = GetNodeItem(XFA_NODEITEM_FirstChild);
    CXFA_Node* pNewChild = pFakeRoot->GetNodeItem(XFA_NODEITEM_FirstChild);
    int32_t index = 0;
    while (pNewChild) {
      CXFA_Node* pItem = pNewChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      pFakeRoot->RemoveChild(pNewChild);
      InsertChild(index++, pNewChild);
      pNewChild->SetFlag(XFA_NodeFlag_Initialized, true);
      pNewChild = pItem;
    }
    while (pChild) {
      CXFA_Node* pItem = pChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      RemoveChild(pChild);
      pFakeRoot->InsertChild(pChild);
      pChild = pItem;
    }
    if (GetPacketID() == XFA_XDPPACKET_Form &&
        GetElementType() == XFA_Element::ExData) {
      CFX_XMLNode* pTempXMLNode = GetXMLMappingNode();
      SetXMLMappingNode(pFakeXMLRoot.release());
      SetFlag(XFA_NodeFlag_OwnXMLNode, false);
      if (pTempXMLNode && !pTempXMLNode->GetNodeItem(CFX_XMLNode::Parent))
        pFakeXMLRoot.reset(pTempXMLNode);
      else
        pFakeXMLRoot = nullptr;
    }
    MoveBufferMapData(pFakeRoot, this, XFA_CalcData, true);
  } else {
    CXFA_Node* pChild = pFakeRoot->GetNodeItem(XFA_NODEITEM_FirstChild);
    while (pChild) {
      CXFA_Node* pItem = pChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      pFakeRoot->RemoveChild(pChild);
      InsertChild(pChild);
      pChild->SetFlag(XFA_NodeFlag_Initialized, true);
      pChild = pItem;
    }
  }
  if (pFakeXMLRoot) {
    pFakeRoot->SetXMLMappingNode(pFakeXMLRoot.release());
    pFakeRoot->SetFlag(XFA_NodeFlag_OwnXMLNode, false);
  }
  pFakeRoot->SetFlag(XFA_NodeFlag_HasRemovedChildren, false);
}

void CXFA_Node::Script_NodeClass_SaveFilteredXML(CFXJSE_Arguments* pArguments) {
  // TODO(weili): Check whether we need to implement this, pdfium:501.
}

void CXFA_Node::Script_NodeClass_SaveXML(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 0 || iLength > 1) {
    ThrowParamCountMismatchException(L"saveXML");
    return;
  }
  bool bPrettyMode = false;
  if (iLength == 1) {
    if (pArguments->GetUTF8String(0) != "pretty") {
      ThrowArgumentMismatchException();
      return;
    }
    bPrettyMode = true;
  }
  CFX_WideString bsXMLHeader = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  if (GetPacketID() == XFA_XDPPACKET_Form ||
      GetPacketID() == XFA_XDPPACKET_Datasets) {
    CFX_XMLNode* pElement = nullptr;
    if (GetPacketID() == XFA_XDPPACKET_Datasets) {
      pElement = GetXMLMappingNode();
      if (!pElement || pElement->GetType() != FX_XMLNODE_Element) {
        pArguments->GetReturnValue()->SetString(
            bsXMLHeader.UTF8Encode().AsStringC());
        return;
      }
      XFA_DataExporter_DealWithDataGroupNode(this);
    }
    auto pMemoryStream = pdfium::MakeRetain<CFX_MemoryStream>(true);
    auto pStream =
        pdfium::MakeRetain<CFX_SeekableStreamProxy>(pMemoryStream, true);
    pStream->SetCodePage(FX_CODEPAGE_UTF8);
    pStream->WriteString(bsXMLHeader.AsStringC());

    if (GetPacketID() == XFA_XDPPACKET_Form)
      XFA_DataExporter_RegenerateFormFile(this, pStream, nullptr, true);
    else
      pElement->SaveXMLNode(pStream);
    // TODO(weili): Check whether we need to save pretty print XML, pdfium:501.
    // For now, just put it here to avoid unused variable warning.
    (void)bPrettyMode;
    pArguments->GetReturnValue()->SetString(
        CFX_ByteStringC(pMemoryStream->GetBuffer(), pMemoryStream->GetSize()));
    return;
  }
  pArguments->GetReturnValue()->SetString("");
}

void CXFA_Node::Script_NodeClass_SetAttribute(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 2) {
    ThrowParamCountMismatchException(L"setAttribute");
    return;
  }
  CFX_WideString wsAttributeValue =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringC());
  CFX_WideString wsAttribute =
      CFX_WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringC());
  SetAttribute(wsAttribute.AsStringC(), wsAttributeValue.AsStringC(), true);
}

void CXFA_Node::Script_NodeClass_SetElement(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1 && iLength != 2) {
    ThrowParamCountMismatchException(L"setElement");
    return;
  }
  CXFA_Node* pNode = nullptr;
  CFX_WideString wsName;
  pNode = static_cast<CXFA_Node*>(pArguments->GetObject(0));
  if (iLength == 2)
    wsName = CFX_WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringC());
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)pNode;
  (void)wsName;
}

void CXFA_Node::Script_NodeClass_Ns(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CFX_WideString wsNameSpace;
  TryNamespace(wsNameSpace);
  pValue->SetString(wsNameSpace.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_NodeClass_Model(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(GetModelNode()));
}

void CXFA_Node::Script_NodeClass_IsContainer(CFXJSE_Value* pValue,
                                             bool bSetting,
                                             XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetBoolean(IsContainerNode());
}

void CXFA_Node::Script_NodeClass_IsNull(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  if (GetElementType() == XFA_Element::Subform) {
    pValue->SetBoolean(false);
    return;
  }
  CFX_WideString strValue;
  pValue->SetBoolean(!TryContent(strValue) || strValue.IsEmpty());
}

void CXFA_Node::Script_NodeClass_OneOfChild(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  std::vector<CXFA_Node*> properties =
      GetNodeList(XFA_NODEFILTER_OneOfProperty);
  if (!properties.empty()) {
    pValue->Assign(
        m_pDocument->GetScriptContext()->GetJSValueFromMap(properties.front()));
  }
}

void CXFA_Node::Script_ContainerClass_GetDelta(CFXJSE_Arguments* pArguments) {}

void CXFA_Node::Script_ContainerClass_GetDeltas(CFXJSE_Arguments* pArguments) {
  CXFA_ArrayNodeList* pFormNodes = new CXFA_ArrayNodeList(m_pDocument.Get());
  pArguments->GetReturnValue()->SetObject(
      pFormNodes, m_pDocument->GetScriptContext()->GetJseNormalClass());
}
void CXFA_Node::Script_ModelClass_ClearErrorList(CFXJSE_Arguments* pArguments) {
}

void CXFA_Node::Script_ModelClass_CreateNode(CFXJSE_Arguments* pArguments) {
  Script_Template_CreateNode(pArguments);
}

void CXFA_Node::Script_ModelClass_IsCompatibleNS(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1) {
    ThrowParamCountMismatchException(L"isCompatibleNS");
    return;
  }
  CFX_WideString wsNameSpace;
  if (iLength >= 1) {
    CFX_ByteString bsNameSpace = pArguments->GetUTF8String(0);
    wsNameSpace = CFX_WideString::FromUTF8(bsNameSpace.AsStringC());
  }
  CFX_WideString wsNodeNameSpace;
  TryNamespace(wsNodeNameSpace);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(wsNodeNameSpace == wsNameSpace);
}

void CXFA_Node::Script_ModelClass_Context(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_ModelClass_AliasNode(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Attribute_Integer(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetInteger(eAttribute, pValue->ToInteger(), true);
  } else {
    pValue->SetInteger(GetInteger(eAttribute));
  }
}

void CXFA_Node::Script_Attribute_IntegerRead(CFXJSE_Value* pValue,
                                             bool bSetting,
                                             XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(GetInteger(eAttribute));
}

void CXFA_Node::Script_Attribute_BOOL(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetBoolean(eAttribute, pValue->ToBoolean(), true);
  } else {
    pValue->SetString(GetBoolean(eAttribute) ? "1" : "0");
  }
}

void CXFA_Node::Script_Attribute_BOOLRead(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetString(GetBoolean(eAttribute) ? "1" : "0");
}

void CXFA_Node::Script_Attribute_SendAttributeChangeMessage(
    XFA_ATTRIBUTE eAttribute,
    bool bScriptModify) {
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (!pLayoutPro)
    return;

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  uint32_t dwPacket = GetPacketID();
  if (!(dwPacket & XFA_XDPPACKET_Form)) {
    pNotify->OnValueChanged(this, eAttribute, this, this);
    return;
  }

  bool bNeedFindContainer = false;
  switch (GetElementType()) {
    case XFA_Element::Caption:
      bNeedFindContainer = true;
      pNotify->OnValueChanged(this, eAttribute, this,
                              GetNodeItem(XFA_NODEITEM_Parent));
      break;
    case XFA_Element::Font:
    case XFA_Element::Para: {
      bNeedFindContainer = true;
      CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
      if (pParentNode->GetElementType() == XFA_Element::Caption) {
        pNotify->OnValueChanged(this, eAttribute, pParentNode,
                                pParentNode->GetNodeItem(XFA_NODEITEM_Parent));
      } else {
        pNotify->OnValueChanged(this, eAttribute, this, pParentNode);
      }
    } break;
    case XFA_Element::Margin: {
      bNeedFindContainer = true;
      CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
      XFA_Element eParentType = pParentNode->GetElementType();
      if (pParentNode->IsContainerNode()) {
        pNotify->OnValueChanged(this, eAttribute, this, pParentNode);
      } else if (eParentType == XFA_Element::Caption) {
        pNotify->OnValueChanged(this, eAttribute, pParentNode,
                                pParentNode->GetNodeItem(XFA_NODEITEM_Parent));
      } else {
        CXFA_Node* pNode = pParentNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pNode && pNode->GetElementType() == XFA_Element::Ui) {
          pNotify->OnValueChanged(this, eAttribute, pNode,
                                  pNode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      }
    } break;
    case XFA_Element::Comb: {
      CXFA_Node* pEditNode = GetNodeItem(XFA_NODEITEM_Parent);
      XFA_Element eUIType = pEditNode->GetElementType();
      if (pEditNode && (eUIType == XFA_Element::DateTimeEdit ||
                        eUIType == XFA_Element::NumericEdit ||
                        eUIType == XFA_Element::TextEdit)) {
        CXFA_Node* pUINode = pEditNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pUINode) {
          pNotify->OnValueChanged(this, eAttribute, pUINode,
                                  pUINode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      }
    } break;
    case XFA_Element::Button:
    case XFA_Element::Barcode:
    case XFA_Element::ChoiceList:
    case XFA_Element::DateTimeEdit:
    case XFA_Element::NumericEdit:
    case XFA_Element::PasswordEdit:
    case XFA_Element::TextEdit: {
      CXFA_Node* pUINode = GetNodeItem(XFA_NODEITEM_Parent);
      if (pUINode) {
        pNotify->OnValueChanged(this, eAttribute, pUINode,
                                pUINode->GetNodeItem(XFA_NODEITEM_Parent));
      }
    } break;
    case XFA_Element::CheckButton: {
      bNeedFindContainer = true;
      CXFA_Node* pUINode = GetNodeItem(XFA_NODEITEM_Parent);
      if (pUINode) {
        pNotify->OnValueChanged(this, eAttribute, pUINode,
                                pUINode->GetNodeItem(XFA_NODEITEM_Parent));
      }
    } break;
    case XFA_Element::Keep:
    case XFA_Element::Bookend:
    case XFA_Element::Break:
    case XFA_Element::BreakAfter:
    case XFA_Element::BreakBefore:
    case XFA_Element::Overflow:
      bNeedFindContainer = true;
      break;
    case XFA_Element::Area:
    case XFA_Element::Draw:
    case XFA_Element::ExclGroup:
    case XFA_Element::Field:
    case XFA_Element::Subform:
    case XFA_Element::SubformSet:
      pLayoutPro->AddChangedContainer(this);
      pNotify->OnValueChanged(this, eAttribute, this, this);
      break;
    case XFA_Element::Sharptext:
    case XFA_Element::Sharpxml:
    case XFA_Element::SharpxHTML: {
      CXFA_Node* pTextNode = GetNodeItem(XFA_NODEITEM_Parent);
      if (!pTextNode) {
        return;
      }
      CXFA_Node* pValueNode = pTextNode->GetNodeItem(XFA_NODEITEM_Parent);
      if (!pValueNode) {
        return;
      }
      XFA_Element eType = pValueNode->GetElementType();
      if (eType == XFA_Element::Value) {
        bNeedFindContainer = true;
        CXFA_Node* pNode = pValueNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pNode && pNode->IsContainerNode()) {
          if (bScriptModify) {
            pValueNode = pNode;
          }
          pNotify->OnValueChanged(this, eAttribute, pValueNode, pNode);
        } else {
          pNotify->OnValueChanged(this, eAttribute, pNode,
                                  pNode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      } else {
        if (eType == XFA_Element::Items) {
          CXFA_Node* pNode = pValueNode->GetNodeItem(XFA_NODEITEM_Parent);
          if (pNode && pNode->IsContainerNode()) {
            pNotify->OnValueChanged(this, eAttribute, pValueNode, pNode);
          }
        }
      }
    } break;
    default:
      break;
  }
  if (bNeedFindContainer) {
    CXFA_Node* pParent = this;
    while (pParent) {
      if (pParent->IsContainerNode())
        break;

      pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
    }
    if (pParent) {
      pLayoutPro->AddChangedContainer(pParent);
    }
  }
}

void CXFA_Node::Script_Attribute_String(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    CFX_WideString wsValue = pValue->ToWideString();
    SetAttribute(eAttribute, wsValue.AsStringC(), true);
    if (eAttribute == XFA_ATTRIBUTE_Use &&
        GetElementType() == XFA_Element::Desc) {
      CXFA_Node* pTemplateNode =
          ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Template));
      CXFA_Node* pProtoRoot =
          pTemplateNode->GetFirstChildByClass(XFA_Element::Subform)
              ->GetFirstChildByClass(XFA_Element::Proto);

      CFX_WideString wsID;
      CFX_WideString wsSOM;
      if (!wsValue.IsEmpty()) {
        if (wsValue[0] == '#') {
          wsID = CFX_WideString(wsValue.c_str() + 1, wsValue.GetLength() - 1);
        } else {
          wsSOM = wsValue;
        }
      }
      CXFA_Node* pProtoNode = nullptr;
      if (!wsSOM.IsEmpty()) {
        uint32_t dwFlag = XFA_RESOLVENODE_Children |
                          XFA_RESOLVENODE_Attributes |
                          XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                          XFA_RESOLVENODE_Siblings;
        XFA_RESOLVENODE_RS resoveNodeRS;
        int32_t iRet = m_pDocument->GetScriptContext()->ResolveObjects(
            pProtoRoot, wsSOM.AsStringC(), resoveNodeRS, dwFlag);
        if (iRet > 0 && resoveNodeRS.objects.front()->IsNode()) {
          pProtoNode = resoveNodeRS.objects.front()->AsNode();
        }
      } else if (!wsID.IsEmpty()) {
        pProtoNode = m_pDocument->GetNodeByID(pProtoRoot, wsID.AsStringC());
      }
      if (pProtoNode) {
        CXFA_Node* pHeadChild = GetNodeItem(XFA_NODEITEM_FirstChild);
        while (pHeadChild) {
          CXFA_Node* pSibling =
              pHeadChild->GetNodeItem(XFA_NODEITEM_NextSibling);
          RemoveChild(pHeadChild);
          pHeadChild = pSibling;
        }
        CXFA_Node* pProtoForm = pProtoNode->CloneTemplateToForm(true);
        pHeadChild = pProtoForm->GetNodeItem(XFA_NODEITEM_FirstChild);
        while (pHeadChild) {
          CXFA_Node* pSibling =
              pHeadChild->GetNodeItem(XFA_NODEITEM_NextSibling);
          pProtoForm->RemoveChild(pHeadChild);
          InsertChild(pHeadChild);
          pHeadChild = pSibling;
        }
        m_pDocument->RemovePurgeNode(pProtoForm);
        delete pProtoForm;
      }
    }
  } else {
    CFX_WideString wsValue;
    GetAttribute(eAttribute, wsValue);
    pValue->SetString(wsValue.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Attribute_StringRead(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CFX_WideString wsValue;
  GetAttribute(eAttribute, wsValue);
  pValue->SetString(wsValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_WsdlConnection_Execute(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    ThrowParamCountMismatchException(L"execute");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(false);
}

void CXFA_Node::Script_Delta_Restore(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"restore");
}

void CXFA_Node::Script_Delta_CurrentValue(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Delta_SavedValue(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Delta_Target(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Som_Message(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_SOM_MESSAGETYPE iMessageType) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  bool bNew = false;
  CXFA_Validate validate = pWidgetData->GetValidate(false);
  if (!validate) {
    validate = pWidgetData->GetValidate(true);
    bNew = true;
  }
  if (bSetting) {
    switch (iMessageType) {
      case XFA_SOM_ValidationMessage:
        validate.SetScriptMessageText(pValue->ToWideString());
        break;
      case XFA_SOM_FormatMessage:
        validate.SetFormatMessageText(pValue->ToWideString());
        break;
      case XFA_SOM_MandatoryMessage:
        validate.SetNullMessageText(pValue->ToWideString());
        break;
      default:
        break;
    }
    if (!bNew) {
      CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
      if (!pNotify) {
        return;
      }
      pNotify->AddCalcValidate(this);
    }
  } else {
    CFX_WideString wsMessage;
    switch (iMessageType) {
      case XFA_SOM_ValidationMessage:
        validate.GetScriptMessageText(wsMessage);
        break;
      case XFA_SOM_FormatMessage:
        validate.GetFormatMessageText(wsMessage);
        break;
      case XFA_SOM_MandatoryMessage:
        validate.GetNullMessageText(wsMessage);
        break;
      default:
        break;
    }
    pValue->SetString(wsMessage.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_ValidationMessage(CFXJSE_Value* pValue,
                                             bool bSetting,
                                             XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_ValidationMessage);
}

void CXFA_Node::Script_Field_Length(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pValue->SetInteger(0);
    return;
  }
  pValue->SetInteger(pWidgetData->CountChoiceListItems(true));
}

void CXFA_Node::Script_Som_DefaultValue(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  XFA_Element eType = GetElementType();
  if (eType == XFA_Element::Field) {
    Script_Field_DefaultValue(pValue, bSetting, eAttribute);
    return;
  }
  if (eType == XFA_Element::Draw) {
    Script_Draw_DefaultValue(pValue, bSetting, eAttribute);
    return;
  }
  if (eType == XFA_Element::Boolean) {
    Script_Boolean_Value(pValue, bSetting, eAttribute);
    return;
  }
  if (bSetting) {
    CFX_WideString wsNewValue;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      wsNewValue = pValue->ToWideString();

    CFX_WideString wsFormatValue(wsNewValue);
    CXFA_WidgetData* pContainerWidgetData = nullptr;
    if (GetPacketID() == XFA_XDPPACKET_Datasets) {
      CFX_WideString wsPicture;
      for (CXFA_Node* pFormNode : GetBindItems()) {
        if (!pFormNode || pFormNode->HasRemovedChildren())
          continue;
        pContainerWidgetData = pFormNode->GetContainerWidgetData();
        if (pContainerWidgetData) {
          pContainerWidgetData->GetPictureContent(wsPicture,
                                                  XFA_VALUEPICTURE_DataBind);
        }
        if (!wsPicture.IsEmpty())
          break;
        pContainerWidgetData = nullptr;
      }
    } else if (GetPacketID() == XFA_XDPPACKET_Form) {
      pContainerWidgetData = GetContainerWidgetData();
    }
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewValue, wsFormatValue);
    }
    SetScriptContent(wsNewValue, wsFormatValue, true, true);
  } else {
    CFX_WideString content = GetScriptContent(true);
    if (content.IsEmpty() && eType != XFA_Element::Text &&
        eType != XFA_Element::SubmitUrl) {
      pValue->SetNull();
    } else if (eType == XFA_Element::Integer) {
      pValue->SetInteger(FXSYS_wtoi(content.c_str()));
    } else if (eType == XFA_Element::Float || eType == XFA_Element::Decimal) {
      CFX_Decimal decimal(content.AsStringC());
      pValue->SetFloat((float)(double)decimal);
    } else {
      pValue->SetString(content.UTF8Encode().AsStringC());
    }
  }
}

void CXFA_Node::Script_Som_DefaultValue_Read(CFXJSE_Value* pValue,
                                             bool bSetting,
                                             XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CFX_WideString content = GetScriptContent(true);
  if (content.IsEmpty()) {
    pValue->SetNull();
    return;
  }
  pValue->SetString(content.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Boolean_Value(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    CFX_ByteString newValue;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      newValue = pValue->ToString();

    int32_t iValue = FXSYS_atoi(newValue.c_str());
    CFX_WideString wsNewValue(iValue == 0 ? L"0" : L"1");
    CFX_WideString wsFormatValue(wsNewValue);
    CXFA_WidgetData* pContainerWidgetData = GetContainerWidgetData();
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewValue, wsFormatValue);
    }
    SetScriptContent(wsNewValue, wsFormatValue, true, true);
  } else {
    CFX_WideString wsValue = GetScriptContent(true);
    pValue->SetBoolean(wsValue == L"1");
  }
}

void CXFA_Node::Script_Som_BorderColor(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  int32_t iSize = border.CountEdges();
  if (bSetting) {
    int32_t r = 0;
    int32_t g = 0;
    int32_t b = 0;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB rgb = ArgbEncode(100, r, g, b);
    for (int32_t i = 0; i < iSize; ++i) {
      CXFA_Edge edge = border.GetEdge(i);
      edge.SetColor(rgb);
    }
  } else {
    CXFA_Edge edge = border.GetEdge(0);
    FX_ARGB color = edge.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    CFX_WideString strColor;
    strColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(strColor.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_BorderWidth(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  int32_t iSize = border.CountEdges();
  CFX_WideString wsThickness;
  if (bSetting) {
    wsThickness = pValue->ToWideString();
    for (int32_t i = 0; i < iSize; ++i) {
      CXFA_Edge edge = border.GetEdge(i);
      CXFA_Measurement thickness(wsThickness.AsStringC());
      edge.SetMSThickness(thickness);
    }
  } else {
    CXFA_Edge edge = border.GetEdge(0);
    CXFA_Measurement thickness = edge.GetMSThickness();
    thickness.ToString(&wsThickness);
    pValue->SetString(wsThickness.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_FillColor(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  CXFA_Fill borderfill = border.GetFill(true);
  CXFA_Node* pNode = borderfill.GetNode();
  if (!pNode) {
    return;
  }
  if (bSetting) {
    int32_t r;
    int32_t g;
    int32_t b;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB color = ArgbEncode(0xff, r, g, b);
    borderfill.SetColor(color);
  } else {
    FX_ARGB color = borderfill.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    CFX_WideString wsColor;
    wsColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(wsColor.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_DataNode(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CXFA_Node* pDataNode = GetBindData();
  if (!pDataNode) {
    pValue->SetNull();
    return;
  }

  pValue->Assign(m_pDocument->GetScriptContext()->GetJSValueFromMap(pDataNode));
}

void CXFA_Node::Script_Draw_DefaultValue(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    if (pValue && pValue->IsString()) {
      CXFA_WidgetData* pWidgetData = GetWidgetData();
      ASSERT(pWidgetData);
      XFA_Element uiType = pWidgetData->GetUIType();
      if (uiType == XFA_Element::Text) {
        CFX_WideString wsNewValue = pValue->ToWideString();
        CFX_WideString wsFormatValue(wsNewValue);
        SetScriptContent(wsNewValue, wsFormatValue, true, true);
      }
    }
  } else {
    CFX_WideString content = GetScriptContent(true);
    if (content.IsEmpty())
      pValue->SetNull();
    else
      pValue->SetString(content.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Field_DefaultValue(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    if (pValue && pValue->IsNull()) {
      pWidgetData->m_bPreNull = pWidgetData->m_bIsNull;
      pWidgetData->m_bIsNull = true;
    } else {
      pWidgetData->m_bPreNull = pWidgetData->m_bIsNull;
      pWidgetData->m_bIsNull = false;
    }
    CFX_WideString wsNewText;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      wsNewText = pValue->ToWideString();

    CXFA_Node* pUIChild = pWidgetData->GetUIChild();
    if (pUIChild->GetElementType() == XFA_Element::NumericEdit) {
      int32_t iLeadDigits = 0;
      int32_t iFracDigits = 0;
      pWidgetData->GetLeadDigits(iLeadDigits);
      pWidgetData->GetFracDigits(iFracDigits);
      wsNewText =
          pWidgetData->NumericLimit(wsNewText, iLeadDigits, iFracDigits);
    }
    CXFA_WidgetData* pContainerWidgetData = GetContainerWidgetData();
    CFX_WideString wsFormatText(wsNewText);
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewText, wsFormatText);
    }
    SetScriptContent(wsNewText, wsFormatText, true, true);
  } else {
    CFX_WideString content = GetScriptContent(true);
    if (content.IsEmpty()) {
      pValue->SetNull();
    } else {
      CXFA_Node* pUIChild = pWidgetData->GetUIChild();
      CXFA_Value defVal = pWidgetData->GetFormValue();
      CXFA_Node* pNode = defVal.GetNode()->GetNodeItem(XFA_NODEITEM_FirstChild);
      if (pNode && pNode->GetElementType() == XFA_Element::Decimal) {
        if (pUIChild->GetElementType() == XFA_Element::NumericEdit &&
            (pNode->GetInteger(XFA_ATTRIBUTE_FracDigits) == -1)) {
          pValue->SetString(content.UTF8Encode().AsStringC());
        } else {
          CFX_Decimal decimal(content.AsStringC());
          pValue->SetFloat((float)(double)decimal);
        }
      } else if (pNode && pNode->GetElementType() == XFA_Element::Integer) {
        pValue->SetInteger(FXSYS_wtoi(content.c_str()));
      } else if (pNode && pNode->GetElementType() == XFA_Element::Boolean) {
        pValue->SetBoolean(FXSYS_wtoi(content.c_str()) == 0 ? false : true);
      } else if (pNode && pNode->GetElementType() == XFA_Element::Float) {
        CFX_Decimal decimal(content.AsStringC());
        pValue->SetFloat((float)(double)decimal);
      } else {
        pValue->SetString(content.UTF8Encode().AsStringC());
      }
    }
  }
}

void CXFA_Node::Script_Field_EditValue(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetValue(pValue->ToWideString(), XFA_VALUEPICTURE_Edit);
  } else {
    CFX_WideString wsValue;
    pWidgetData->GetValue(wsValue, XFA_VALUEPICTURE_Edit);
    pValue->SetString(wsValue.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_FontColor(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Font font = pWidgetData->GetFont(true);
  CXFA_Node* pNode = font.GetNode();
  if (!pNode) {
    return;
  }
  if (bSetting) {
    int32_t r;
    int32_t g;
    int32_t b;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB color = ArgbEncode(0xff, r, g, b);
    font.SetColor(color);
  } else {
    FX_ARGB color = font.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    CFX_WideString wsColor;
    wsColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(wsColor.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Field_FormatMessage(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_FormatMessage);
}

void CXFA_Node::Script_Field_FormattedValue(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetValue(pValue->ToWideString(), XFA_VALUEPICTURE_Display);
  } else {
    CFX_WideString wsValue;
    pWidgetData->GetValue(wsValue, XFA_VALUEPICTURE_Display);
    pValue->SetString(wsValue.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_Mandatory(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Validate validate = pWidgetData->GetValidate(true);
  if (bSetting) {
    validate.SetNullTest(pValue->ToWideString());
  } else {
    int32_t iValue = validate.GetNullTest();
    const XFA_ATTRIBUTEENUMINFO* pInfo =
        GetAttributeEnumByID((XFA_ATTRIBUTEENUM)iValue);
    CFX_WideString wsValue;
    if (pInfo)
      wsValue = pInfo->pName;
    pValue->SetString(wsValue.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Som_MandatoryMessage(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_MandatoryMessage);
}

void CXFA_Node::Script_Field_ParentSubform(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetNull();
}

void CXFA_Node::Script_Field_SelectedIndex(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    int32_t iIndex = pValue->ToInteger();
    if (iIndex == -1) {
      pWidgetData->ClearAllSelections();
      return;
    }
    pWidgetData->SetItemState(iIndex, true, true, true, true);
  } else {
    pValue->SetInteger(pWidgetData->GetSelectedItem(0));
  }
}

void CXFA_Node::Script_Field_ClearItems(CFXJSE_Arguments* pArguments) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  pWidgetData->DeleteItem(-1, true, false);
}

void CXFA_Node::Script_Field_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  CFX_ByteString eventString = pArguments->GetUTF8String(0);
  int32_t iRet = execSingleEventByName(
      CFX_WideString::FromUTF8(eventString.AsStringC()).AsStringC(),
      XFA_Element::Field);
  if (eventString != "validate")
    return;

  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CXFA_Node::Script_Field_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Initialize, false, false);
}

void CXFA_Node::Script_Field_DeleteItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"deleteItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bValue = pWidgetData->DeleteItem(iIndex, true, true);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(bValue);
}

void CXFA_Node::Script_Field_GetSaveItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"getSaveItem");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  if (iIndex < 0) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CFX_WideString wsValue;
  if (!pWidgetData->GetChoiceListItem(wsValue, iIndex, true)) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->SetString(wsValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Field_BoundItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"boundItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CFX_ByteString bsValue = pArguments->GetUTF8String(0);
  CFX_WideString wsValue = CFX_WideString::FromUTF8(bsValue.AsStringC());
  CFX_WideString wsBoundValue;
  pWidgetData->GetItemValue(wsValue.AsStringC(), wsBoundValue);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetString(wsBoundValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Field_GetItemState(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"getItemState");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bValue = pWidgetData->GetItemState(iIndex);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(bValue);
}

void CXFA_Node::Script_Field_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Calculate, false, false);
}

void CXFA_Node::Script_Field_SetItems(CFXJSE_Arguments* pArguments) {}

void CXFA_Node::Script_Field_GetDisplayItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    ThrowParamCountMismatchException(L"getDisplayItem");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  if (iIndex < 0) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CFX_WideString wsValue;
  if (!pWidgetData->GetChoiceListItem(wsValue, iIndex, false)) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->SetString(wsValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Field_SetItemState(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 2) {
    ThrowParamCountMismatchException(L"setItemState");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData)
    return;

  int32_t iIndex = pArguments->GetInt32(0);
  if (pArguments->GetInt32(1) != 0) {
    pWidgetData->SetItemState(iIndex, true, true, true, true);
  } else {
    if (pWidgetData->GetItemState(iIndex))
      pWidgetData->SetItemState(iIndex, false, true, true, true);
  }
}

void CXFA_Node::Script_Field_AddItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 2) {
    ThrowParamCountMismatchException(L"addItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CFX_WideString wsLabel;
  CFX_WideString wsValue;
  if (iLength >= 1) {
    CFX_ByteString bsLabel = pArguments->GetUTF8String(0);
    wsLabel = CFX_WideString::FromUTF8(bsLabel.AsStringC());
  }
  if (iLength >= 2) {
    CFX_ByteString bsValue = pArguments->GetUTF8String(1);
    wsValue = CFX_WideString::FromUTF8(bsValue.AsStringC());
  }
  pWidgetData->InsertItem(wsLabel, wsValue, true);
}

void CXFA_Node::Script_Field_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet =
      pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Validate, false, false);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CXFA_Node::Script_ExclGroup_ErrorText(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting)
    ThrowInvalidPropertyException();
}

void CXFA_Node::Script_ExclGroup_DefaultAndRawValue(CFXJSE_Value* pValue,
                                                    bool bSetting,
                                                    XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetSelectedMemberByValue(pValue->ToWideString().AsStringC(),
                                          true, true, true);
  } else {
    CFX_WideString wsValue = GetScriptContent(true);
    XFA_VERSION curVersion = GetDocument()->GetCurVersionMode();
    if (wsValue.IsEmpty() && curVersion >= XFA_VERSION_300) {
      pValue->SetNull();
    } else {
      pValue->SetString(wsValue.UTF8Encode().AsStringC());
    }
  }
}

void CXFA_Node::Script_ExclGroup_Transient(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_ExclGroup_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  CFX_ByteString eventString = pArguments->GetUTF8String(0);
  execSingleEventByName(
      CFX_WideString::FromUTF8(eventString.AsStringC()).AsStringC(),
      XFA_Element::ExclGroup);
}

void CXFA_Node::Script_ExclGroup_SelectedMember(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc < 0 || argc > 1) {
    ThrowParamCountMismatchException(L"selectedMember");
    return;
  }

  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }

  CXFA_Node* pReturnNode = nullptr;
  if (argc == 0) {
    pReturnNode = pWidgetData->GetSelectedMember();
  } else {
    CFX_ByteString szName;
    szName = pArguments->GetUTF8String(0);
    pReturnNode = pWidgetData->SetSelectedMember(
        CFX_WideString::FromUTF8(szName.AsStringC()).AsStringC(), true);
  }
  if (!pReturnNode) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pReturnNode));
}

void CXFA_Node::Script_ExclGroup_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Initialize);
}

void CXFA_Node::Script_ExclGroup_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Calculate);
}

void CXFA_Node::Script_ExclGroup_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CXFA_Node::Script_Som_InstanceIndex(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    int32_t iTo = pValue->ToInteger();
    int32_t iFrom = Subform_and_SubformSet_InstanceIndex();
    CXFA_Node* pManagerNode = nullptr;
    for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
         pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
      if (pNode->GetElementType() == XFA_Element::InstanceManager) {
        pManagerNode = pNode;
        break;
      }
    }
    if (pManagerNode) {
      pManagerNode->InstanceManager_MoveInstance(iTo, iFrom);
      CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
      if (!pNotify) {
        return;
      }
      CXFA_Node* pToInstance = GetItem(pManagerNode, iTo);
      if (pToInstance &&
          pToInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pToInstance);
      }
      CXFA_Node* pFromInstance = GetItem(pManagerNode, iFrom);
      if (pFromInstance &&
          pFromInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pFromInstance);
      }
    }
  } else {
    pValue->SetInteger(Subform_and_SubformSet_InstanceIndex());
  }
}

void CXFA_Node::Script_Subform_InstanceManager(CFXJSE_Value* pValue,
                                               bool bSetting,
                                               XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }

  CFX_WideStringC wsName = GetCData(XFA_ATTRIBUTE_Name);
  CXFA_Node* pInstanceMgr = nullptr;
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
    if (pNode->GetElementType() == XFA_Element::InstanceManager) {
      CFX_WideStringC wsInstMgrName = pNode->GetCData(XFA_ATTRIBUTE_Name);
      if (wsInstMgrName.GetLength() >= 1 && wsInstMgrName[0] == '_' &&
          wsInstMgrName.Right(wsInstMgrName.GetLength() - 1) == wsName) {
        pInstanceMgr = pNode;
      }
      break;
    }
  }
  if (!pInstanceMgr) {
    pValue->SetNull();
    return;
  }

  pValue->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pInstanceMgr));
}

void CXFA_Node::Script_Subform_Locale(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetCData(XFA_ATTRIBUTE_Locale, pValue->ToWideString(), true, true);
  } else {
    CFX_WideString wsLocaleName;
    GetLocaleName(wsLocaleName);
    pValue->SetString(wsLocaleName.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Subform_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  CFX_ByteString eventString = pArguments->GetUTF8String(0);
  execSingleEventByName(
      CFX_WideString::FromUTF8(eventString.AsStringC()).AsStringC(),
      XFA_Element::Subform);
}

void CXFA_Node::Script_Subform_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Initialize);
}

void CXFA_Node::Script_Subform_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Calculate);
}

void CXFA_Node::Script_Subform_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CXFA_Node::Script_Subform_GetInvalidObjects(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"getInvalidObjects");
}

int32_t CXFA_Node::Subform_and_SubformSet_InstanceIndex() {
  int32_t index = 0;
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
    if ((pNode->GetElementType() == XFA_Element::Subform) ||
        (pNode->GetElementType() == XFA_Element::SubformSet)) {
      index++;
    } else {
      break;
    }
  }
  return index;
}

void CXFA_Node::Script_Template_FormNodes(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"formNodes");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_Template_Remerge(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"remerge");
    return;
  }
  m_pDocument->DoDataRemerge(true);
}

void CXFA_Node::Script_Template_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_Template_CreateNode(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc <= 0 || argc >= 4) {
    ThrowParamCountMismatchException(L"createNode");
    return;
  }

  CFX_WideString strName;
  CFX_WideString strNameSpace;
  CFX_ByteString bsTagName = pArguments->GetUTF8String(0);
  CFX_WideString strTagName = CFX_WideString::FromUTF8(bsTagName.AsStringC());
  if (argc > 1) {
    CFX_ByteString bsName = pArguments->GetUTF8String(1);
    strName = CFX_WideString::FromUTF8(bsName.AsStringC());
    if (argc == 3) {
      CFX_ByteString bsNameSpace = pArguments->GetUTF8String(2);
      strNameSpace = CFX_WideString::FromUTF8(bsNameSpace.AsStringC());
    }
  }

  XFA_Element eType = XFA_GetElementTypeForName(strTagName.AsStringC());
  CXFA_Node* pNewNode = CreateSamePacketNode(eType);
  if (!pNewNode) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }

  if (strName.IsEmpty()) {
    pArguments->GetReturnValue()->Assign(
        m_pDocument->GetScriptContext()->GetJSValueFromMap(pNewNode));
    return;
  }

  if (!GetAttributeOfElement(eType, XFA_ATTRIBUTE_Name,
                             XFA_XDPPACKET_UNKNOWN)) {
    ThrowMissingPropertyException(strTagName, L"name");
    return;
  }

  pNewNode->SetAttribute(XFA_ATTRIBUTE_Name, strName.AsStringC(), true);
  if (pNewNode->GetPacketID() == XFA_XDPPACKET_Datasets)
    pNewNode->CreateXMLMappingNode();

  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pNewNode));
}

void CXFA_Node::Script_Template_Recalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"recalculate");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_Template_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_Template_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execValidate");
    return;
  }
  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_Manifest_Evaluate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"evaluate");
    return;
  }

  CXFA_WidgetData* pWidgetData = GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CXFA_Node::Script_InstanceManager_Max(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  CXFA_Occur nodeOccur(GetOccurNode());
  pValue->SetInteger(nodeOccur.GetMax());
}

void CXFA_Node::Script_InstanceManager_Min(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  CXFA_Occur nodeOccur(GetOccurNode());
  pValue->SetInteger(nodeOccur.GetMin());
}

void CXFA_Node::Script_InstanceManager_Count(CFXJSE_Value* pValue,
                                             bool bSetting,
                                             XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    int32_t iDesired = pValue->ToInteger();
    InstanceManager_SetInstances(iDesired);
  } else {
    pValue->SetInteger(GetCount(this));
  }
}

void CXFA_Node::Script_InstanceManager_MoveInstance(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 2) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iFrom = pArguments->GetInt32(0);
  int32_t iTo = pArguments->GetInt32(1);
  InstanceManager_MoveInstance(iTo, iFrom);
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    return;
  }
  CXFA_Node* pToInstance = GetItem(this, iTo);
  if (pToInstance && pToInstance->GetElementType() == XFA_Element::Subform) {
    pNotify->RunSubformIndexChange(pToInstance);
  }
  CXFA_Node* pFromInstance = GetItem(this, iFrom);
  if (pFromInstance &&
      pFromInstance->GetElementType() == XFA_Element::Subform) {
    pNotify->RunSubformIndexChange(pFromInstance);
  }
}

void CXFA_Node::Script_InstanceManager_RemoveInstance(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  int32_t iCount = GetCount(this);
  if (iIndex < 0 || iIndex >= iCount) {
    ThrowIndexOutOfBoundsException();
    return;
  }
  CXFA_Occur nodeOccur(GetOccurNode());
  int32_t iMin = nodeOccur.GetMin();
  if (iCount - 1 < iMin) {
    ThrowTooManyOccurancesException(L"min");
    return;
  }
  CXFA_Node* pRemoveInstance = GetItem(this, iIndex);
  RemoveItem(this, pRemoveInstance);
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (pNotify) {
    for (int32_t i = iIndex; i < iCount - 1; i++) {
      CXFA_Node* pSubformInstance = GetItem(this, i);
      if (pSubformInstance &&
          pSubformInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pSubformInstance);
      }
    }
  }
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form)));
}

void CXFA_Node::Script_InstanceManager_SetInstances(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iDesired = pArguments->GetInt32(0);
  InstanceManager_SetInstances(iDesired);
}

void CXFA_Node::Script_InstanceManager_AddInstance(
    CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    ThrowParamCountMismatchException(L"addInstance");
    return;
  }
  bool fFlags = true;
  if (argc == 1) {
    fFlags = pArguments->GetInt32(0) == 0 ? false : true;
  }
  int32_t iCount = GetCount(this);
  CXFA_Occur nodeOccur(GetOccurNode());
  int32_t iMax = nodeOccur.GetMax();
  if (iMax >= 0 && iCount >= iMax) {
    ThrowTooManyOccurancesException(L"max");
    return;
  }
  CXFA_Node* pNewInstance = CreateInstance(this, fFlags);
  InsertItem(this, pNewInstance, iCount, iCount, false);
  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pNewInstance));
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    return;
  }
  pNotify->RunNodeInitialize(pNewInstance);
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form)));
}

void CXFA_Node::Script_InstanceManager_InsertInstance(
    CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 1 && argc != 2) {
    ThrowParamCountMismatchException(L"insertInstance");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bBind = false;
  if (argc == 2) {
    bBind = pArguments->GetInt32(1) == 0 ? false : true;
  }
  CXFA_Occur nodeOccur(GetOccurNode());
  int32_t iCount = GetCount(this);
  if (iIndex < 0 || iIndex > iCount) {
    ThrowIndexOutOfBoundsException();
    return;
  }
  int32_t iMax = nodeOccur.GetMax();
  if (iMax >= 0 && iCount >= iMax) {
    ThrowTooManyOccurancesException(L"max");
    return;
  }
  CXFA_Node* pNewInstance = CreateInstance(this, bBind);
  InsertItem(this, pNewInstance, iIndex, iCount, true);
  pArguments->GetReturnValue()->Assign(
      m_pDocument->GetScriptContext()->GetJSValueFromMap(pNewInstance));
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    return;
  }
  pNotify->RunNodeInitialize(pNewInstance);
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form)));
}

int32_t CXFA_Node::InstanceManager_SetInstances(int32_t iDesired) {
  CXFA_Occur nodeOccur(GetOccurNode());
  int32_t iMax = nodeOccur.GetMax();
  int32_t iMin = nodeOccur.GetMin();
  if (iDesired < iMin) {
    ThrowTooManyOccurancesException(L"min");
    return 1;
  }
  if ((iMax >= 0) && (iDesired > iMax)) {
    ThrowTooManyOccurancesException(L"max");
    return 2;
  }
  int32_t iCount = GetCount(this);
  if (iDesired == iCount) {
    return 0;
  }
  if (iDesired < iCount) {
    CFX_WideStringC wsInstManagerName = GetCData(XFA_ATTRIBUTE_Name);
    CFX_WideString wsInstanceName = CFX_WideString(
        wsInstManagerName.IsEmpty()
            ? wsInstManagerName
            : wsInstManagerName.Right(wsInstManagerName.GetLength() - 1));
    uint32_t dInstanceNameHash =
        FX_HashCode_GetW(wsInstanceName.AsStringC(), false);
    CXFA_Node* pPrevSibling =
        (iDesired == 0) ? this : GetItem(this, iDesired - 1);
    while (iCount > iDesired) {
      CXFA_Node* pRemoveInstance =
          pPrevSibling->GetNodeItem(XFA_NODEITEM_NextSibling);
      if (pRemoveInstance->GetElementType() != XFA_Element::Subform &&
          pRemoveInstance->GetElementType() != XFA_Element::SubformSet) {
        continue;
      }
      if (pRemoveInstance->GetElementType() == XFA_Element::InstanceManager) {
        NOTREACHED();
        break;
      }
      if (pRemoveInstance->GetNameHash() == dInstanceNameHash) {
        RemoveItem(this, pRemoveInstance);
        iCount--;
      }
    }
  } else if (iDesired > iCount) {
    while (iCount < iDesired) {
      CXFA_Node* pNewInstance = CreateInstance(this, true);
      InsertItem(this, pNewInstance, iCount, iCount, false);
      iCount++;
      CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
      if (!pNotify) {
        return 0;
      }
      pNotify->RunNodeInitialize(pNewInstance);
    }
  }
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (pLayoutPro) {
    pLayoutPro->AddChangedContainer(
        ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form)));
  }
  return 0;
}

int32_t CXFA_Node::InstanceManager_MoveInstance(int32_t iTo, int32_t iFrom) {
  int32_t iCount = GetCount(this);
  if (iFrom > iCount || iTo > iCount - 1) {
    ThrowIndexOutOfBoundsException();
    return 1;
  }
  if (iFrom < 0 || iTo < 0 || iFrom == iTo) {
    return 0;
  }
  CXFA_Node* pMoveInstance = GetItem(this, iFrom);
  RemoveItem(this, pMoveInstance, false);
  InsertItem(this, pMoveInstance, iTo, iCount - 1, true);
  CXFA_LayoutProcessor* pLayoutPro = m_pDocument->GetLayoutProcessor();
  if (pLayoutPro) {
    pLayoutPro->AddChangedContainer(
        ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form)));
  }
  return 0;
}

void CXFA_Node::Script_Occur_Max(CFXJSE_Value* pValue,
                                 bool bSetting,
                                 XFA_ATTRIBUTE eAttribute) {
  CXFA_Occur occur(this);
  if (bSetting) {
    int32_t iMax = pValue->ToInteger();
    occur.SetMax(iMax);
  } else {
    pValue->SetInteger(occur.GetMax());
  }
}

void CXFA_Node::Script_Occur_Min(CFXJSE_Value* pValue,
                                 bool bSetting,
                                 XFA_ATTRIBUTE eAttribute) {
  CXFA_Occur occur(this);
  if (bSetting) {
    int32_t iMin = pValue->ToInteger();
    occur.SetMin(iMin);
  } else {
    pValue->SetInteger(occur.GetMin());
  }
}

void CXFA_Node::Script_Desc_Metadata(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    ThrowParamCountMismatchException(L"metadata");
    return;
  }
  pArguments->GetReturnValue()->SetString("");
}

void CXFA_Node::Script_Form_FormNodes(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"formNodes");
    return;
  }

  CXFA_Node* pDataNode = static_cast<CXFA_Node*>(pArguments->GetObject(0));
  if (!pDataNode) {
    ThrowArgumentMismatchException();
    return;
  }

  std::vector<CXFA_Node*> formItems;
  CXFA_ArrayNodeList* pFormNodes = new CXFA_ArrayNodeList(m_pDocument.Get());
  pFormNodes->SetArrayNodeList(formItems);
  pArguments->GetReturnValue()->SetObject(
      pFormNodes, m_pDocument->GetScriptContext()->GetJseNormalClass());
}

void CXFA_Node::Script_Form_Remerge(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"remerge");
    return;
  }

  m_pDocument->DoDataRemerge(true);
}

void CXFA_Node::Script_Form_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Initialize);
}

void CXFA_Node::Script_Form_Recalculate(CFXJSE_Arguments* pArguments) {
  CXFA_EventParam* pEventParam =
      m_pDocument->GetScriptContext()->GetEventParam();
  if (pEventParam->m_eType == XFA_EVENT_Calculate ||
      pEventParam->m_eType == XFA_EVENT_InitCalculate) {
    return;
  }
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"recalculate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;
  if (pArguments->GetInt32(0) != 0)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Calculate);
  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Validate);
  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Ready, true);
}

void CXFA_Node::Script_Form_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Calculate);
}

void CXFA_Node::Script_Form_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(this, XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CXFA_Node::Script_Form_Checksum(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetAttribute(XFA_ATTRIBUTE_Checksum, pValue->ToWideString().AsStringC());
    return;
  }
  CFX_WideString wsChecksum;
  GetAttribute(XFA_ATTRIBUTE_Checksum, wsChecksum, false);
  pValue->SetString(wsChecksum.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Packet_GetAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"getAttribute");
    return;
  }
  CFX_ByteString bsAttributeName = pArguments->GetUTF8String(0);
  CFX_WideString wsAttributeValue;
  CFX_XMLNode* pXMLNode = GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    wsAttributeValue = static_cast<CFX_XMLElement*>(pXMLNode)->GetString(
        CFX_WideString::FromUTF8(bsAttributeName.AsStringC()).c_str());
  }
  pArguments->GetReturnValue()->SetString(
      wsAttributeValue.UTF8Encode().AsStringC());
}

void CXFA_Node::Script_Packet_SetAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 2) {
    ThrowParamCountMismatchException(L"setAttribute");
    return;
  }
  CFX_ByteString bsValue = pArguments->GetUTF8String(0);
  CFX_ByteString bsName = pArguments->GetUTF8String(1);
  CFX_XMLNode* pXMLNode = GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    static_cast<CFX_XMLElement*>(pXMLNode)->SetString(
        CFX_WideString::FromUTF8(bsName.AsStringC()),
        CFX_WideString::FromUTF8(bsValue.AsStringC()));
  }
  pArguments->GetReturnValue()->SetNull();
}

void CXFA_Node::Script_Packet_RemoveAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    ThrowParamCountMismatchException(L"removeAttribute");
    return;
  }

  CFX_ByteString bsName = pArguments->GetUTF8String(0);
  CFX_WideString wsName = CFX_WideString::FromUTF8(bsName.AsStringC());
  CFX_XMLNode* pXMLNode = GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
    if (pXMLElement->HasAttribute(wsName.c_str())) {
      pXMLElement->RemoveAttribute(wsName.c_str());
    }
  }
  pArguments->GetReturnValue()->SetNull();
}

void CXFA_Node::Script_Packet_Content(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    CFX_XMLNode* pXMLNode = GetXMLMappingNode();
    if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
      CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
      pXMLElement->SetTextData(pValue->ToWideString());
    }
  } else {
    CFX_WideString wsTextData;
    CFX_XMLNode* pXMLNode = GetXMLMappingNode();
    if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
      CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
      wsTextData = pXMLElement->GetTextData();
    }
    pValue->SetString(wsTextData.UTF8Encode().AsStringC());
  }
}

void CXFA_Node::Script_Source_Next(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"next");
}

void CXFA_Node::Script_Source_CancelBatch(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"cancelBatch");
}

void CXFA_Node::Script_Source_First(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"first");
}

void CXFA_Node::Script_Source_UpdateBatch(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"updateBatch");
}

void CXFA_Node::Script_Source_Previous(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"previous");
}

void CXFA_Node::Script_Source_IsBOF(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"isBOF");
}

void CXFA_Node::Script_Source_IsEOF(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"isEOF");
}

void CXFA_Node::Script_Source_Cancel(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"cancel");
}

void CXFA_Node::Script_Source_Update(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"update");
}

void CXFA_Node::Script_Source_Open(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"open");
}

void CXFA_Node::Script_Source_Delete(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"delete");
}

void CXFA_Node::Script_Source_AddNew(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"addNew");
}

void CXFA_Node::Script_Source_Requery(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"requery");
}

void CXFA_Node::Script_Source_Resync(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"resync");
}

void CXFA_Node::Script_Source_Close(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"close");
}

void CXFA_Node::Script_Source_Last(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"last");
}

void CXFA_Node::Script_Source_HasDataChanged(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    ThrowParamCountMismatchException(L"hasDataChanged");
}

void CXFA_Node::Script_Source_Db(CFXJSE_Value* pValue,
                                 bool bSetting,
                                 XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Xfa_This(CFXJSE_Value* pValue,
                                bool bSetting,
                                XFA_ATTRIBUTE eAttribute) {
  if (!bSetting) {
    CXFA_Object* pThis = m_pDocument->GetScriptContext()->GetThisObject();
    ASSERT(pThis);
    pValue->Assign(m_pDocument->GetScriptContext()->GetJSValueFromMap(pThis));
  }
}

void CXFA_Node::Script_Handler_Version(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_SubmitFormat_Mode(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Extras_Type(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_ATTRIBUTE eAttribute) {}

void CXFA_Node::Script_Script_Stateless(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ThrowInvalidPropertyException();
    return;
  }
  pValue->SetString(FX_UTF8Encode(CFX_WideStringC(L"0", 1)).AsStringC());
}

void CXFA_Node::Script_Encrypt_Format(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {}

bool CXFA_Node::HasAttribute(XFA_ATTRIBUTE eAttr, bool bCanInherit) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  return HasMapModuleKey(pKey, bCanInherit);
}

bool CXFA_Node::SetAttribute(XFA_ATTRIBUTE eAttr,
                             const CFX_WideStringC& wsValue,
                             bool bNotify) {
  const XFA_ATTRIBUTEINFO* pAttr = XFA_GetAttributeByID(eAttr);
  if (!pAttr)
    return false;

  XFA_ATTRIBUTETYPE eType = pAttr->eType;
  if (eType == XFA_ATTRIBUTETYPE_NOTSURE) {
    const XFA_NOTSUREATTRIBUTE* pNotsure =
        XFA_GetNotsureAttribute(GetElementType(), pAttr->eName);
    eType = pNotsure ? pNotsure->eType : XFA_ATTRIBUTETYPE_Cdata;
  }
  switch (eType) {
    case XFA_ATTRIBUTETYPE_Enum: {
      const XFA_ATTRIBUTEENUMINFO* pEnum = XFA_GetAttributeEnumByName(wsValue);
      return SetEnum(pAttr->eName,
                     pEnum ? pEnum->eName
                           : (XFA_ATTRIBUTEENUM)(intptr_t)(pAttr->pDefValue),
                     bNotify);
    } break;
    case XFA_ATTRIBUTETYPE_Cdata:
      return SetCData(pAttr->eName, CFX_WideString(wsValue), bNotify);
    case XFA_ATTRIBUTETYPE_Boolean:
      return SetBoolean(pAttr->eName, wsValue != L"0", bNotify);
    case XFA_ATTRIBUTETYPE_Integer:
      return SetInteger(pAttr->eName,
                        FXSYS_round(FXSYS_wcstof(wsValue.unterminated_c_str(),
                                                 wsValue.GetLength(), nullptr)),
                        bNotify);
    case XFA_ATTRIBUTETYPE_Measure:
      return SetMeasure(pAttr->eName, CXFA_Measurement(wsValue), bNotify);
    default:
      break;
  }
  return false;
}

bool CXFA_Node::GetAttribute(XFA_ATTRIBUTE eAttr,
                             CFX_WideString& wsValue,
                             bool bUseDefault) {
  const XFA_ATTRIBUTEINFO* pAttr = XFA_GetAttributeByID(eAttr);
  if (!pAttr)
    return false;

  XFA_ATTRIBUTETYPE eType = pAttr->eType;
  if (eType == XFA_ATTRIBUTETYPE_NOTSURE) {
    const XFA_NOTSUREATTRIBUTE* pNotsure =
        XFA_GetNotsureAttribute(GetElementType(), pAttr->eName);
    eType = pNotsure ? pNotsure->eType : XFA_ATTRIBUTETYPE_Cdata;
  }
  switch (eType) {
    case XFA_ATTRIBUTETYPE_Enum: {
      XFA_ATTRIBUTEENUM eValue;
      if (!TryEnum(pAttr->eName, eValue, bUseDefault))
        return false;

      wsValue = GetAttributeEnumByID(eValue)->pName;
      return true;
    }
    case XFA_ATTRIBUTETYPE_Cdata: {
      CFX_WideStringC wsValueC;
      if (!TryCData(pAttr->eName, wsValueC, bUseDefault))
        return false;

      wsValue = wsValueC;
      return true;
    }
    case XFA_ATTRIBUTETYPE_Boolean: {
      bool bValue;
      if (!TryBoolean(pAttr->eName, bValue, bUseDefault))
        return false;

      wsValue = bValue ? L"1" : L"0";
      return true;
    }
    case XFA_ATTRIBUTETYPE_Integer: {
      int32_t iValue;
      if (!TryInteger(pAttr->eName, iValue, bUseDefault))
        return false;

      wsValue.Format(L"%d", iValue);
      return true;
    }
    case XFA_ATTRIBUTETYPE_Measure: {
      CXFA_Measurement mValue;
      if (!TryMeasure(pAttr->eName, mValue, bUseDefault))
        return false;

      mValue.ToString(&wsValue);
      return true;
    }
    default:
      return false;
  }
}

bool CXFA_Node::SetAttribute(const CFX_WideStringC& wsAttr,
                             const CFX_WideStringC& wsValue,
                             bool bNotify) {
  const XFA_ATTRIBUTEINFO* pAttributeInfo = XFA_GetAttributeByName(wsValue);
  if (pAttributeInfo) {
    return SetAttribute(pAttributeInfo->eName, wsValue, bNotify);
  }
  void* pKey = GetMapKey_Custom(wsAttr);
  SetMapModuleString(pKey, wsValue);
  return true;
}

bool CXFA_Node::GetAttribute(const CFX_WideStringC& wsAttr,
                             CFX_WideString& wsValue,
                             bool bUseDefault) {
  const XFA_ATTRIBUTEINFO* pAttributeInfo = XFA_GetAttributeByName(wsAttr);
  if (pAttributeInfo) {
    return GetAttribute(pAttributeInfo->eName, wsValue, bUseDefault);
  }
  void* pKey = GetMapKey_Custom(wsAttr);
  CFX_WideStringC wsValueC;
  if (GetMapModuleString(pKey, wsValueC)) {
    wsValue = wsValueC;
  }
  return true;
}

bool CXFA_Node::RemoveAttribute(const CFX_WideStringC& wsAttr) {
  void* pKey = GetMapKey_Custom(wsAttr);
  RemoveMapModuleKey(pKey);
  return true;
}

bool CXFA_Node::TryBoolean(XFA_ATTRIBUTE eAttr,
                           bool& bValue,
                           bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Boolean, bUseDefault, pValue))
    return false;
  bValue = !!pValue;
  return true;
}

bool CXFA_Node::TryInteger(XFA_ATTRIBUTE eAttr,
                           int32_t& iValue,
                           bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Integer, bUseDefault, pValue))
    return false;
  iValue = (int32_t)(uintptr_t)pValue;
  return true;
}

bool CXFA_Node::TryEnum(XFA_ATTRIBUTE eAttr,
                        XFA_ATTRIBUTEENUM& eValue,
                        bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Enum, bUseDefault, pValue))
    return false;
  eValue = (XFA_ATTRIBUTEENUM)(uintptr_t)pValue;
  return true;
}

bool CXFA_Node::SetMeasure(XFA_ATTRIBUTE eAttr,
                           CXFA_Measurement mValue,
                           bool bNotify) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  OnChanging(eAttr, bNotify);
  SetMapModuleBuffer(pKey, &mValue, sizeof(CXFA_Measurement));
  OnChanged(eAttr, bNotify, false);
  return true;
}

bool CXFA_Node::TryMeasure(XFA_ATTRIBUTE eAttr,
                           CXFA_Measurement& mValue,
                           bool bUseDefault) const {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  void* pValue;
  int32_t iBytes;
  if (GetMapModuleBuffer(pKey, pValue, iBytes) && iBytes == sizeof(mValue)) {
    memcpy(&mValue, pValue, sizeof(mValue));
    return true;
  }
  if (bUseDefault &&
      XFA_GetAttributeDefaultValue(pValue, GetElementType(), eAttr,
                                   XFA_ATTRIBUTETYPE_Measure, m_ePacket)) {
    memcpy(&mValue, pValue, sizeof(mValue));
    return true;
  }
  return false;
}

CXFA_Measurement CXFA_Node::GetMeasure(XFA_ATTRIBUTE eAttr) const {
  CXFA_Measurement mValue;
  return TryMeasure(eAttr, mValue, true) ? mValue : CXFA_Measurement();
}

bool CXFA_Node::SetCData(XFA_ATTRIBUTE eAttr,
                         const CFX_WideString& wsValue,
                         bool bNotify,
                         bool bScriptModify) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  OnChanging(eAttr, bNotify);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    CFX_WideString* pClone = new CFX_WideString(wsValue);
    SetUserData(pKey, pClone, &deleteWideStringCallBack);
  } else {
    SetMapModuleString(pKey, wsValue.AsStringC());
    if (eAttr == XFA_ATTRIBUTE_Name)
      UpdateNameHash();
  }
  OnChanged(eAttr, bNotify, bScriptModify);

  if (!IsNeedSavingXMLNode() || eAttr == XFA_ATTRIBUTE_QualifiedName ||
      eAttr == XFA_ATTRIBUTE_BindingNode) {
    return true;
  }

  if (eAttr == XFA_ATTRIBUTE_Name &&
      (m_elementType == XFA_Element::DataValue ||
       m_elementType == XFA_Element::DataGroup)) {
    return true;
  }

  if (eAttr == XFA_ATTRIBUTE_Value) {
    FX_XMLNODETYPE eXMLType = m_pXMLNode->GetType();
    switch (eXMLType) {
      case FX_XMLNODE_Element:
        if (IsAttributeInXML()) {
          static_cast<CFX_XMLElement*>(m_pXMLNode)
              ->SetString(CFX_WideString(GetCData(XFA_ATTRIBUTE_QualifiedName)),
                          wsValue);
        } else {
          bool bDeleteChildren = true;
          if (GetPacketID() == XFA_XDPPACKET_Datasets) {
            for (CXFA_Node* pChildDataNode =
                     GetNodeItem(XFA_NODEITEM_FirstChild);
                 pChildDataNode; pChildDataNode = pChildDataNode->GetNodeItem(
                                     XFA_NODEITEM_NextSibling)) {
              if (!pChildDataNode->GetBindItems().empty()) {
                bDeleteChildren = false;
                break;
              }
            }
          }
          if (bDeleteChildren) {
            static_cast<CFX_XMLElement*>(m_pXMLNode)->DeleteChildren();
          }
          static_cast<CFX_XMLElement*>(m_pXMLNode)->SetTextData(wsValue);
        }
        break;
      case FX_XMLNODE_Text:
        static_cast<CFX_XMLText*>(m_pXMLNode)->SetText(wsValue);
        break;
      default:
        ASSERT(0);
    }
    return true;
  }

  const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttr);
  if (pInfo) {
    ASSERT(m_pXMLNode->GetType() == FX_XMLNODE_Element);
    CFX_WideString wsAttrName = pInfo->pName;
    if (pInfo->eName == XFA_ATTRIBUTE_ContentType) {
      wsAttrName = L"xfa:" + wsAttrName;
    }
    static_cast<CFX_XMLElement*>(m_pXMLNode)->SetString(wsAttrName, wsValue);
  }
  return true;
}

bool CXFA_Node::SetAttributeValue(const CFX_WideString& wsValue,
                                  const CFX_WideString& wsXMLValue,
                                  bool bNotify,
                                  bool bScriptModify) {
  void* pKey = GetMapKey_Element(GetElementType(), XFA_ATTRIBUTE_Value);
  OnChanging(XFA_ATTRIBUTE_Value, bNotify);
  CFX_WideString* pClone = new CFX_WideString(wsValue);
  SetUserData(pKey, pClone, &deleteWideStringCallBack);
  OnChanged(XFA_ATTRIBUTE_Value, bNotify, bScriptModify);
  if (IsNeedSavingXMLNode()) {
    FX_XMLNODETYPE eXMLType = m_pXMLNode->GetType();
    switch (eXMLType) {
      case FX_XMLNODE_Element:
        if (IsAttributeInXML()) {
          static_cast<CFX_XMLElement*>(m_pXMLNode)
              ->SetString(CFX_WideString(GetCData(XFA_ATTRIBUTE_QualifiedName)),
                          wsXMLValue);
        } else {
          bool bDeleteChildren = true;
          if (GetPacketID() == XFA_XDPPACKET_Datasets) {
            for (CXFA_Node* pChildDataNode =
                     GetNodeItem(XFA_NODEITEM_FirstChild);
                 pChildDataNode; pChildDataNode = pChildDataNode->GetNodeItem(
                                     XFA_NODEITEM_NextSibling)) {
              if (!pChildDataNode->GetBindItems().empty()) {
                bDeleteChildren = false;
                break;
              }
            }
          }
          if (bDeleteChildren) {
            static_cast<CFX_XMLElement*>(m_pXMLNode)->DeleteChildren();
          }
          static_cast<CFX_XMLElement*>(m_pXMLNode)->SetTextData(wsXMLValue);
        }
        break;
      case FX_XMLNODE_Text:
        static_cast<CFX_XMLText*>(m_pXMLNode)->SetText(wsXMLValue);
        break;
      default:
        ASSERT(0);
    }
  }
  return true;
}

bool CXFA_Node::TryCData(XFA_ATTRIBUTE eAttr,
                         CFX_WideString& wsValue,
                         bool bUseDefault,
                         bool bProto) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    CFX_WideString* pStr = (CFX_WideString*)GetUserData(pKey, bProto);
    if (pStr) {
      wsValue = *pStr;
      return true;
    }
  } else {
    CFX_WideStringC wsValueC;
    if (GetMapModuleString(pKey, wsValueC)) {
      wsValue = wsValueC;
      return true;
    }
  }
  if (!bUseDefault) {
    return false;
  }
  void* pValue = nullptr;
  if (XFA_GetAttributeDefaultValue(pValue, GetElementType(), eAttr,
                                   XFA_ATTRIBUTETYPE_Cdata, m_ePacket)) {
    wsValue = (const wchar_t*)pValue;
    return true;
  }
  return false;
}

bool CXFA_Node::TryCData(XFA_ATTRIBUTE eAttr,
                         CFX_WideStringC& wsValue,
                         bool bUseDefault,
                         bool bProto) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    CFX_WideString* pStr = (CFX_WideString*)GetUserData(pKey, bProto);
    if (pStr) {
      wsValue = pStr->AsStringC();
      return true;
    }
  } else {
    if (GetMapModuleString(pKey, wsValue)) {
      return true;
    }
  }
  if (!bUseDefault) {
    return false;
  }
  void* pValue = nullptr;
  if (XFA_GetAttributeDefaultValue(pValue, GetElementType(), eAttr,
                                   XFA_ATTRIBUTETYPE_Cdata, m_ePacket)) {
    wsValue = (CFX_WideStringC)(const wchar_t*)pValue;
    return true;
  }
  return false;
}

bool CXFA_Node::SetObject(XFA_ATTRIBUTE eAttr,
                          void* pData,
                          XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  return SetUserData(pKey, pData, pCallbackInfo);
}

bool CXFA_Node::TryObject(XFA_ATTRIBUTE eAttr, void*& pData) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  pData = GetUserData(pKey);
  return !!pData;
}

bool CXFA_Node::SetValue(XFA_ATTRIBUTE eAttr,
                         XFA_ATTRIBUTETYPE eType,
                         void* pValue,
                         bool bNotify) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  OnChanging(eAttr, bNotify);
  SetMapModuleValue(pKey, pValue);
  OnChanged(eAttr, bNotify, false);
  if (IsNeedSavingXMLNode()) {
    ASSERT(m_pXMLNode->GetType() == FX_XMLNODE_Element);
    const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttr);
    if (pInfo) {
      switch (eType) {
        case XFA_ATTRIBUTETYPE_Enum:
          static_cast<CFX_XMLElement*>(m_pXMLNode)
              ->SetString(
                  pInfo->pName,
                  GetAttributeEnumByID((XFA_ATTRIBUTEENUM)(uintptr_t)pValue)
                      ->pName);
          break;
        case XFA_ATTRIBUTETYPE_Boolean:
          static_cast<CFX_XMLElement*>(m_pXMLNode)
              ->SetString(pInfo->pName, pValue ? L"1" : L"0");
          break;
        case XFA_ATTRIBUTETYPE_Integer: {
          CFX_WideString wsValue;
          wsValue.Format(
              L"%d", static_cast<int32_t>(reinterpret_cast<uintptr_t>(pValue)));
          static_cast<CFX_XMLElement*>(m_pXMLNode)
              ->SetString(pInfo->pName, wsValue);
          break;
        }
        default:
          ASSERT(0);
      }
    }
  }
  return true;
}

bool CXFA_Node::GetValue(XFA_ATTRIBUTE eAttr,
                         XFA_ATTRIBUTETYPE eType,
                         bool bUseDefault,
                         void*& pValue) {
  void* pKey = GetMapKey_Element(GetElementType(), eAttr);
  if (GetMapModuleValue(pKey, pValue)) {
    return true;
  }
  if (!bUseDefault) {
    return false;
  }
  return XFA_GetAttributeDefaultValue(pValue, GetElementType(), eAttr, eType,
                                      m_ePacket);
}

bool CXFA_Node::SetUserData(void* pKey,
                            void* pData,
                            XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  SetMapModuleBuffer(pKey, &pData, sizeof(void*),
                     pCallbackInfo ? pCallbackInfo : &gs_XFADefaultFreeData);
  return true;
}

bool CXFA_Node::TryUserData(void* pKey, void*& pData, bool bProtoAlso) {
  int32_t iBytes = 0;
  if (!GetMapModuleBuffer(pKey, pData, iBytes, bProtoAlso)) {
    return false;
  }
  return iBytes == sizeof(void*) && memcpy(&pData, pData, iBytes);
}

bool CXFA_Node::SetScriptContent(const CFX_WideString& wsContent,
                                 const CFX_WideString& wsXMLValue,
                                 bool bNotify,
                                 bool bScriptModify,
                                 bool bSyncData) {
  CXFA_Node* pNode = nullptr;
  CXFA_Node* pBindNode = nullptr;
  switch (GetObjectType()) {
    case XFA_ObjectType::ContainerNode: {
      if (XFA_FieldIsMultiListBox(this)) {
        CXFA_Node* pValue = GetProperty(0, XFA_Element::Value);
        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        ASSERT(pChildValue);
        pChildValue->SetCData(XFA_ATTRIBUTE_ContentType, L"text/xml");
        pChildValue->SetScriptContent(wsContent, wsContent, bNotify,
                                      bScriptModify, false);
        CXFA_Node* pBind = GetBindData();
        if (bSyncData && pBind) {
          std::vector<CFX_WideString> wsSaveTextArray;
          size_t iSize = 0;
          if (!wsContent.IsEmpty()) {
            FX_STRSIZE iStart = 0;
            FX_STRSIZE iLength = wsContent.GetLength();
            auto iEnd = wsContent.Find(L'\n', iStart);
            iEnd = !iEnd.has_value() ? iLength : iEnd;
            while (iEnd.value() >= iStart) {
              wsSaveTextArray.push_back(
                  wsContent.Mid(iStart, iEnd.value() - iStart));
              iStart = iEnd.value() + 1;
              if (iStart >= iLength) {
                break;
              }
              iEnd = wsContent.Find(L'\n', iStart);
              if (!iEnd.has_value()) {
                wsSaveTextArray.push_back(
                    wsContent.Mid(iStart, iLength - iStart));
              }
            }
            iSize = wsSaveTextArray.size();
          }
          if (iSize == 0) {
            while (CXFA_Node* pChildNode =
                       pBind->GetNodeItem(XFA_NODEITEM_FirstChild)) {
              pBind->RemoveChild(pChildNode);
            }
          } else {
            std::vector<CXFA_Node*> valueNodes = pBind->GetNodeList(
                XFA_NODEFILTER_Children, XFA_Element::DataValue);
            size_t iDatas = valueNodes.size();
            if (iDatas < iSize) {
              size_t iAddNodes = iSize - iDatas;
              CXFA_Node* pValueNodes = nullptr;
              while (iAddNodes-- > 0) {
                pValueNodes =
                    pBind->CreateSamePacketNode(XFA_Element::DataValue);
                pValueNodes->SetCData(XFA_ATTRIBUTE_Name, L"value");
                pValueNodes->CreateXMLMappingNode();
                pBind->InsertChild(pValueNodes);
              }
              pValueNodes = nullptr;
            } else if (iDatas > iSize) {
              size_t iDelNodes = iDatas - iSize;
              while (iDelNodes-- > 0) {
                pBind->RemoveChild(pBind->GetNodeItem(XFA_NODEITEM_FirstChild));
              }
            }
            int32_t i = 0;
            for (CXFA_Node* pValueNode =
                     pBind->GetNodeItem(XFA_NODEITEM_FirstChild);
                 pValueNode; pValueNode = pValueNode->GetNodeItem(
                                 XFA_NODEITEM_NextSibling)) {
              pValueNode->SetAttributeValue(wsSaveTextArray[i],
                                            wsSaveTextArray[i], false);
              i++;
            }
          }
          for (CXFA_Node* pArrayNode : pBind->GetBindItems()) {
            if (pArrayNode != this) {
              pArrayNode->SetScriptContent(wsContent, wsContent, bNotify,
                                           bScriptModify, false);
            }
          }
        }
        break;
      } else if (GetElementType() == XFA_Element::ExclGroup) {
        pNode = this;
      } else {
        CXFA_Node* pValue = GetProperty(0, XFA_Element::Value);
        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        ASSERT(pChildValue);
        pChildValue->SetScriptContent(wsContent, wsContent, bNotify,
                                      bScriptModify, false);
      }
      pBindNode = GetBindData();
      if (pBindNode && bSyncData) {
        pBindNode->SetScriptContent(wsContent, wsXMLValue, bNotify,
                                    bScriptModify, false);
        for (CXFA_Node* pArrayNode : pBindNode->GetBindItems()) {
          if (pArrayNode != this) {
            pArrayNode->SetScriptContent(wsContent, wsContent, bNotify, true,
                                         false);
          }
        }
      }
      pBindNode = nullptr;
      break;
    }
    case XFA_ObjectType::ContentNode: {
      CFX_WideString wsContentType;
      if (GetElementType() == XFA_Element::ExData) {
        GetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType, false);
        if (wsContentType == L"text/html") {
          wsContentType = L"";
          SetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType.AsStringC());
        }
      }
      CXFA_Node* pContentRawDataNode = GetNodeItem(XFA_NODEITEM_FirstChild);
      if (!pContentRawDataNode) {
        pContentRawDataNode = CreateSamePacketNode(
            (wsContentType == L"text/xml") ? XFA_Element::Sharpxml
                                           : XFA_Element::Sharptext);
        InsertChild(pContentRawDataNode);
      }
      return pContentRawDataNode->SetScriptContent(
          wsContent, wsXMLValue, bNotify, bScriptModify, bSyncData);
    } break;
    case XFA_ObjectType::NodeC:
    case XFA_ObjectType::TextNode:
      pNode = this;
      break;
    case XFA_ObjectType::NodeV:
      pNode = this;
      if (bSyncData && GetPacketID() == XFA_XDPPACKET_Form) {
        CXFA_Node* pParent = GetNodeItem(XFA_NODEITEM_Parent);
        if (pParent) {
          pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
        }
        if (pParent && pParent->GetElementType() == XFA_Element::Value) {
          pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
          if (pParent && pParent->IsContainerNode()) {
            pBindNode = pParent->GetBindData();
            if (pBindNode) {
              pBindNode->SetScriptContent(wsContent, wsXMLValue, bNotify,
                                          bScriptModify, false);
            }
          }
        }
      }
      break;
    default:
      if (GetElementType() == XFA_Element::DataValue) {
        pNode = this;
        pBindNode = this;
      }
      break;
  }
  if (pNode) {
    SetAttributeValue(wsContent, wsXMLValue, bNotify, bScriptModify);
    if (pBindNode && bSyncData) {
      for (CXFA_Node* pArrayNode : pBindNode->GetBindItems()) {
        pArrayNode->SetScriptContent(wsContent, wsContent, bNotify,
                                     bScriptModify, false);
      }
    }
    return true;
  }
  return false;
}

bool CXFA_Node::SetContent(const CFX_WideString& wsContent,
                           const CFX_WideString& wsXMLValue,
                           bool bNotify,
                           bool bScriptModify,
                           bool bSyncData) {
  return SetScriptContent(wsContent, wsXMLValue, bNotify, bScriptModify,
                          bSyncData);
}

CFX_WideString CXFA_Node::GetScriptContent(bool bScriptModify) {
  CFX_WideString wsContent;
  return TryContent(wsContent, bScriptModify) ? wsContent : CFX_WideString();
}

CFX_WideString CXFA_Node::GetContent() {
  return GetScriptContent();
}

bool CXFA_Node::TryContent(CFX_WideString& wsContent,
                           bool bScriptModify,
                           bool bProto) {
  CXFA_Node* pNode = nullptr;
  switch (GetObjectType()) {
    case XFA_ObjectType::ContainerNode:
      if (GetElementType() == XFA_Element::ExclGroup) {
        pNode = this;
      } else {
        CXFA_Node* pValue = GetChild(0, XFA_Element::Value);
        if (!pValue) {
          return false;
        }
        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        if (pChildValue && XFA_FieldIsMultiListBox(this)) {
          pChildValue->SetAttribute(XFA_ATTRIBUTE_ContentType, L"text/xml");
        }
        return pChildValue
                   ? pChildValue->TryContent(wsContent, bScriptModify, bProto)
                   : false;
      }
      break;
    case XFA_ObjectType::ContentNode: {
      CXFA_Node* pContentRawDataNode = GetNodeItem(XFA_NODEITEM_FirstChild);
      if (!pContentRawDataNode) {
        XFA_Element element = XFA_Element::Sharptext;
        if (GetElementType() == XFA_Element::ExData) {
          CFX_WideString wsContentType;
          GetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType, false);
          if (wsContentType == L"text/html") {
            element = XFA_Element::SharpxHTML;
          } else if (wsContentType == L"text/xml") {
            element = XFA_Element::Sharpxml;
          }
        }
        pContentRawDataNode = CreateSamePacketNode(element);
        InsertChild(pContentRawDataNode);
      }
      return pContentRawDataNode->TryContent(wsContent, bScriptModify, bProto);
    }
    case XFA_ObjectType::NodeC:
    case XFA_ObjectType::NodeV:
    case XFA_ObjectType::TextNode:
      pNode = this;
    default:
      if (GetElementType() == XFA_Element::DataValue) {
        pNode = this;
      }
      break;
  }
  if (pNode) {
    if (bScriptModify) {
      CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
      if (pScriptContext) {
        m_pDocument->GetScriptContext()->AddNodesOfRunScript(this);
      }
    }
    return TryCData(XFA_ATTRIBUTE_Value, wsContent, false, bProto);
  }
  return false;
}

CXFA_Node* CXFA_Node::GetModelNode() {
  switch (GetPacketID()) {
    case XFA_XDPPACKET_XDP:
      return m_pDocument->GetRoot();
    case XFA_XDPPACKET_Config:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Config));
    case XFA_XDPPACKET_Template:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Template));
    case XFA_XDPPACKET_Form:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Form));
    case XFA_XDPPACKET_Datasets:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Datasets));
    case XFA_XDPPACKET_LocaleSet:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_LocaleSet));
    case XFA_XDPPACKET_ConnectionSet:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_ConnectionSet));
    case XFA_XDPPACKET_SourceSet:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_SourceSet));
    case XFA_XDPPACKET_Xdc:
      return ToNode(m_pDocument->GetXFAObject(XFA_HASHCODE_Xdc));
    default:
      return this;
  }
}

bool CXFA_Node::TryNamespace(CFX_WideString& wsNamespace) {
  wsNamespace.clear();
  if (IsModelNode() || GetElementType() == XFA_Element::Packet) {
    CFX_XMLNode* pXMLNode = GetXMLMappingNode();
    if (!pXMLNode || pXMLNode->GetType() != FX_XMLNODE_Element) {
      return false;
    }
    wsNamespace = static_cast<CFX_XMLElement*>(pXMLNode)->GetNamespaceURI();
    return true;
  } else if (GetPacketID() == XFA_XDPPACKET_Datasets) {
    CFX_XMLNode* pXMLNode = GetXMLMappingNode();
    if (!pXMLNode) {
      return false;
    }
    if (pXMLNode->GetType() != FX_XMLNODE_Element) {
      return true;
    }
    if (GetElementType() == XFA_Element::DataValue &&
        GetEnum(XFA_ATTRIBUTE_Contains) == XFA_ATTRIBUTEENUM_MetaData) {
      return XFA_FDEExtension_ResolveNamespaceQualifier(
          static_cast<CFX_XMLElement*>(pXMLNode),
          GetCData(XFA_ATTRIBUTE_QualifiedName), &wsNamespace);
    }
    wsNamespace = static_cast<CFX_XMLElement*>(pXMLNode)->GetNamespaceURI();
    return true;
  } else {
    CXFA_Node* pModelNode = GetModelNode();
    return pModelNode->TryNamespace(wsNamespace);
  }
}

CXFA_Node* CXFA_Node::GetProperty(int32_t index,
                                  XFA_Element eProperty,
                                  bool bCreateProperty) {
  XFA_Element eType = GetElementType();
  uint32_t dwPacket = GetPacketID();
  const XFA_PROPERTY* pProperty =
      XFA_GetPropertyOfElement(eType, eProperty, dwPacket);
  if (!pProperty || index >= pProperty->uOccur)
    return nullptr;

  CXFA_Node* pNode = m_pChild;
  int32_t iCount = 0;
  for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eProperty) {
      iCount++;
      if (iCount > index) {
        return pNode;
      }
    }
  }
  if (!bCreateProperty)
    return nullptr;

  if (pProperty->uFlags & XFA_PROPERTYFLAG_OneOf) {
    pNode = m_pChild;
    for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      const XFA_PROPERTY* pExistProperty =
          XFA_GetPropertyOfElement(eType, pNode->GetElementType(), dwPacket);
      if (pExistProperty && (pExistProperty->uFlags & XFA_PROPERTYFLAG_OneOf))
        return nullptr;
    }
  }

  const XFA_PACKETINFO* pPacket = XFA_GetPacketByID(dwPacket);
  CXFA_Node* pNewNode = nullptr;
  for (; iCount <= index; iCount++) {
    pNewNode = m_pDocument->CreateNode(pPacket, eProperty);
    if (!pNewNode)
      return nullptr;
    InsertChild(pNewNode, nullptr);
    pNewNode->SetFlag(XFA_NodeFlag_Initialized, true);
  }
  return pNewNode;
}

int32_t CXFA_Node::CountChildren(XFA_Element eType, bool bOnlyChild) {
  CXFA_Node* pNode = m_pChild;
  int32_t iCount = 0;
  for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eType || eType == XFA_Element::Unknown) {
      if (bOnlyChild) {
        const XFA_PROPERTY* pProperty = XFA_GetPropertyOfElement(
            GetElementType(), pNode->GetElementType(), XFA_XDPPACKET_UNKNOWN);
        if (pProperty) {
          continue;
        }
      }
      iCount++;
    }
  }
  return iCount;
}

CXFA_Node* CXFA_Node::GetChild(int32_t index,
                               XFA_Element eType,
                               bool bOnlyChild) {
  ASSERT(index > -1);
  CXFA_Node* pNode = m_pChild;
  int32_t iCount = 0;
  for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eType || eType == XFA_Element::Unknown) {
      if (bOnlyChild) {
        const XFA_PROPERTY* pProperty = XFA_GetPropertyOfElement(
            GetElementType(), pNode->GetElementType(), XFA_XDPPACKET_UNKNOWN);
        if (pProperty) {
          continue;
        }
      }
      iCount++;
      if (iCount > index) {
        return pNode;
      }
    }
  }
  return nullptr;
}

int32_t CXFA_Node::InsertChild(int32_t index, CXFA_Node* pNode) {
  ASSERT(!pNode->m_pNext);
  pNode->m_pParent = this;
  bool ret = m_pDocument->RemovePurgeNode(pNode);
  ASSERT(ret);
  (void)ret;  // Avoid unused variable warning.

  if (!m_pChild || index == 0) {
    if (index > 0) {
      return -1;
    }
    pNode->m_pNext = m_pChild;
    m_pChild = pNode;
    index = 0;
  } else if (index < 0) {
    m_pLastChild->m_pNext = pNode;
  } else {
    CXFA_Node* pPrev = m_pChild;
    int32_t iCount = 0;
    while (++iCount != index && pPrev->m_pNext) {
      pPrev = pPrev->m_pNext;
    }
    if (index > 0 && index != iCount) {
      return -1;
    }
    pNode->m_pNext = pPrev->m_pNext;
    pPrev->m_pNext = pNode;
    index = iCount;
  }
  if (!pNode->m_pNext) {
    m_pLastChild = pNode;
  }
  ASSERT(m_pLastChild);
  ASSERT(!m_pLastChild->m_pNext);
  pNode->ClearFlag(XFA_NodeFlag_HasRemovedChildren);
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (pNotify)
    pNotify->OnChildAdded(this);

  if (IsNeedSavingXMLNode() && pNode->m_pXMLNode) {
    ASSERT(!pNode->m_pXMLNode->GetNodeItem(CFX_XMLNode::Parent));
    m_pXMLNode->InsertChildNode(pNode->m_pXMLNode, index);
    pNode->ClearFlag(XFA_NodeFlag_OwnXMLNode);
  }
  return index;
}

bool CXFA_Node::InsertChild(CXFA_Node* pNode, CXFA_Node* pBeforeNode) {
  if (!pNode || pNode->m_pParent ||
      (pBeforeNode && pBeforeNode->m_pParent != this)) {
    NOTREACHED();
    return false;
  }
  bool ret = m_pDocument->RemovePurgeNode(pNode);
  ASSERT(ret);
  (void)ret;  // Avoid unused variable warning.

  int32_t nIndex = -1;
  pNode->m_pParent = this;
  if (!m_pChild || pBeforeNode == m_pChild) {
    pNode->m_pNext = m_pChild;
    m_pChild = pNode;
    nIndex = 0;
  } else if (!pBeforeNode) {
    pNode->m_pNext = m_pLastChild->m_pNext;
    m_pLastChild->m_pNext = pNode;
  } else {
    nIndex = 1;
    CXFA_Node* pPrev = m_pChild;
    while (pPrev->m_pNext != pBeforeNode) {
      pPrev = pPrev->m_pNext;
      nIndex++;
    }
    pNode->m_pNext = pPrev->m_pNext;
    pPrev->m_pNext = pNode;
  }
  if (!pNode->m_pNext) {
    m_pLastChild = pNode;
  }
  ASSERT(m_pLastChild);
  ASSERT(!m_pLastChild->m_pNext);
  pNode->ClearFlag(XFA_NodeFlag_HasRemovedChildren);
  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (pNotify)
    pNotify->OnChildAdded(this);

  if (IsNeedSavingXMLNode() && pNode->m_pXMLNode) {
    ASSERT(!pNode->m_pXMLNode->GetNodeItem(CFX_XMLNode::Parent));
    m_pXMLNode->InsertChildNode(pNode->m_pXMLNode, nIndex);
    pNode->ClearFlag(XFA_NodeFlag_OwnXMLNode);
  }
  return true;
}

CXFA_Node* CXFA_Node::Deprecated_GetPrevSibling() {
  if (!m_pParent) {
    return nullptr;
  }
  for (CXFA_Node* pSibling = m_pParent->m_pChild; pSibling;
       pSibling = pSibling->m_pNext) {
    if (pSibling->m_pNext == this) {
      return pSibling;
    }
  }
  return nullptr;
}

bool CXFA_Node::RemoveChild(CXFA_Node* pNode, bool bNotify) {
  if (!pNode || pNode->m_pParent != this) {
    NOTREACHED();
    return false;
  }
  if (m_pChild == pNode) {
    m_pChild = pNode->m_pNext;
    if (m_pLastChild == pNode) {
      m_pLastChild = pNode->m_pNext;
    }
    pNode->m_pNext = nullptr;
    pNode->m_pParent = nullptr;
  } else {
    CXFA_Node* pPrev = pNode->Deprecated_GetPrevSibling();
    pPrev->m_pNext = pNode->m_pNext;
    if (m_pLastChild == pNode) {
      m_pLastChild = pNode->m_pNext ? pNode->m_pNext : pPrev;
    }
    pNode->m_pNext = nullptr;
    pNode->m_pParent = nullptr;
  }
  ASSERT(!m_pLastChild || !m_pLastChild->m_pNext);
  OnRemoved(bNotify);
  pNode->SetFlag(XFA_NodeFlag_HasRemovedChildren, true);
  m_pDocument->AddPurgeNode(pNode);
  if (IsNeedSavingXMLNode() && pNode->m_pXMLNode) {
    if (pNode->IsAttributeInXML()) {
      ASSERT(pNode->m_pXMLNode == m_pXMLNode &&
             m_pXMLNode->GetType() == FX_XMLNODE_Element);
      if (pNode->m_pXMLNode->GetType() == FX_XMLNODE_Element) {
        CFX_XMLElement* pXMLElement =
            static_cast<CFX_XMLElement*>(pNode->m_pXMLNode);
        CFX_WideStringC wsAttributeName =
            pNode->GetCData(XFA_ATTRIBUTE_QualifiedName);
        // TODO(tsepez): check usage of c_str() below.
        pXMLElement->RemoveAttribute(wsAttributeName.unterminated_c_str());
      }
      CFX_WideString wsName;
      pNode->GetAttribute(XFA_ATTRIBUTE_Name, wsName, false);
      CFX_XMLElement* pNewXMLElement = new CFX_XMLElement(wsName);
      CFX_WideStringC wsValue = GetCData(XFA_ATTRIBUTE_Value);
      if (!wsValue.IsEmpty()) {
        pNewXMLElement->SetTextData(CFX_WideString(wsValue));
      }
      pNode->m_pXMLNode = pNewXMLElement;
      pNode->SetEnum(XFA_ATTRIBUTE_Contains, XFA_ATTRIBUTEENUM_Unknown);
    } else {
      m_pXMLNode->RemoveChildNode(pNode->m_pXMLNode);
    }
    pNode->SetFlag(XFA_NodeFlag_OwnXMLNode, false);
  }
  return true;
}

CXFA_Node* CXFA_Node::GetFirstChildByName(const CFX_WideStringC& wsName) const {
  return GetFirstChildByName(FX_HashCode_GetW(wsName, false));
}

CXFA_Node* CXFA_Node::GetFirstChildByName(uint32_t dwNameHash) const {
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_FirstChild); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetNameHash() == dwNameHash) {
      return pNode;
    }
  }
  return nullptr;
}

CXFA_Node* CXFA_Node::GetFirstChildByClass(XFA_Element eType) const {
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_FirstChild); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eType) {
      return pNode;
    }
  }
  return nullptr;
}

CXFA_Node* CXFA_Node::GetNextSameNameSibling(uint32_t dwNameHash) const {
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_NextSibling); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetNameHash() == dwNameHash) {
      return pNode;
    }
  }
  return nullptr;
}

CXFA_Node* CXFA_Node::GetNextSameNameSibling(
    const CFX_WideStringC& wsNodeName) const {
  return GetNextSameNameSibling(FX_HashCode_GetW(wsNodeName, false));
}

CXFA_Node* CXFA_Node::GetNextSameClassSibling(XFA_Element eType) const {
  for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_NextSibling); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eType) {
      return pNode;
    }
  }
  return nullptr;
}

int32_t CXFA_Node::GetNodeSameNameIndex() const {
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext) {
    return -1;
  }
  return pScriptContext->GetIndexByName(const_cast<CXFA_Node*>(this));
}

int32_t CXFA_Node::GetNodeSameClassIndex() const {
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext) {
    return -1;
  }
  return pScriptContext->GetIndexByClassName(const_cast<CXFA_Node*>(this));
}

void CXFA_Node::GetSOMExpression(CFX_WideString& wsSOMExpression) {
  CXFA_ScriptContext* pScriptContext = m_pDocument->GetScriptContext();
  if (!pScriptContext) {
    return;
  }
  pScriptContext->GetSomExpression(this, wsSOMExpression);
}

CXFA_Node* CXFA_Node::GetInstanceMgrOfSubform() {
  CXFA_Node* pInstanceMgr = nullptr;
  if (m_ePacket == XFA_XDPPACKET_Form) {
    CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
    if (!pParentNode || pParentNode->GetElementType() == XFA_Element::Area) {
      return pInstanceMgr;
    }
    for (CXFA_Node* pNode = GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
         pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
      XFA_Element eType = pNode->GetElementType();
      if ((eType == XFA_Element::Subform || eType == XFA_Element::SubformSet) &&
          pNode->m_dwNameHash != m_dwNameHash) {
        break;
      }
      if (eType == XFA_Element::InstanceManager) {
        CFX_WideStringC wsName = GetCData(XFA_ATTRIBUTE_Name);
        CFX_WideStringC wsInstName = pNode->GetCData(XFA_ATTRIBUTE_Name);
        if (wsInstName.GetLength() > 0 && wsInstName[0] == '_' &&
            wsInstName.Right(wsInstName.GetLength() - 1) == wsName) {
          pInstanceMgr = pNode;
        }
        break;
      }
    }
  }
  return pInstanceMgr;
}

CXFA_Node* CXFA_Node::GetOccurNode() {
  return GetFirstChildByClass(XFA_Element::Occur);
}

bool CXFA_Node::HasFlag(XFA_NodeFlag dwFlag) const {
  if (m_uNodeFlags & dwFlag)
    return true;
  if (dwFlag == XFA_NodeFlag_HasRemovedChildren)
    return m_pParent && m_pParent->HasFlag(dwFlag);
  return false;
}

void CXFA_Node::SetFlag(uint32_t dwFlag, bool bNotify) {
  if (dwFlag == XFA_NodeFlag_Initialized && bNotify && !IsInitialized()) {
    CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
    if (pNotify) {
      pNotify->OnNodeReady(this);
    }
  }
  m_uNodeFlags |= dwFlag;
}

void CXFA_Node::ClearFlag(uint32_t dwFlag) {
  m_uNodeFlags &= ~dwFlag;
}

bool CXFA_Node::IsAttributeInXML() {
  return GetEnum(XFA_ATTRIBUTE_Contains) == XFA_ATTRIBUTEENUM_MetaData;
}

void CXFA_Node::OnRemoved(bool bNotify) {
  if (!bNotify)
    return;

  CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
  if (pNotify)
    pNotify->OnChildRemoved();
}

void CXFA_Node::OnChanging(XFA_ATTRIBUTE eAttr, bool bNotify) {
  if (bNotify && IsInitialized()) {
    CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
    if (pNotify) {
      pNotify->OnValueChanging(this, eAttr);
    }
  }
}

void CXFA_Node::OnChanged(XFA_ATTRIBUTE eAttr,
                          bool bNotify,
                          bool bScriptModify) {
  if (bNotify && IsInitialized()) {
    Script_Attribute_SendAttributeChangeMessage(eAttr, bScriptModify);
  }
}

int32_t CXFA_Node::execSingleEventByName(const CFX_WideStringC& wsEventName,
                                         XFA_Element eType) {
  int32_t iRet = XFA_EVENTERROR_NotExist;
  const XFA_ExecEventParaInfo* eventParaInfo =
      GetEventParaInfoByName(wsEventName);
  if (eventParaInfo) {
    uint32_t validFlags = eventParaInfo->m_validFlags;
    CXFA_FFNotify* pNotify = m_pDocument->GetNotify();
    if (!pNotify) {
      return iRet;
    }
    if (validFlags == 1) {
      iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType);
    } else if (validFlags == 2) {
      iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                           false, false);
    } else if (validFlags == 3) {
      if (eType == XFA_Element::Subform) {
        iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                             false, false);
      }
    } else if (validFlags == 4) {
      if (eType == XFA_Element::ExclGroup || eType == XFA_Element::Field) {
        CXFA_Node* pParentNode = GetNodeItem(XFA_NODEITEM_Parent);
        if (pParentNode &&
            pParentNode->GetElementType() == XFA_Element::ExclGroup) {
          iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                               false, false);
        }
        iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                             false, false);
      }
    } else if (validFlags == 5) {
      if (eType == XFA_Element::Field) {
        iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                             false, false);
      }
    } else if (validFlags == 6) {
      CXFA_WidgetData* pWidgetData = GetWidgetData();
      if (pWidgetData) {
        CXFA_Node* pUINode = pWidgetData->GetUIChild();
        if (pUINode->m_elementType == XFA_Element::Signature) {
          iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                               false, false);
        }
      }
    } else if (validFlags == 7) {
      CXFA_WidgetData* pWidgetData = GetWidgetData();
      if (pWidgetData) {
        CXFA_Node* pUINode = pWidgetData->GetUIChild();
        if ((pUINode->m_elementType == XFA_Element::ChoiceList) &&
            (!pWidgetData->IsListBox())) {
          iRet = pNotify->ExecEventByDeepFirst(this, eventParaInfo->m_eventType,
                                               false, false);
        }
      }
    }
  }
  return iRet;
}

void CXFA_Node::UpdateNameHash() {
  const XFA_NOTSUREATTRIBUTE* pNotsure =
      XFA_GetNotsureAttribute(GetElementType(), XFA_ATTRIBUTE_Name);
  CFX_WideStringC wsName;
  if (!pNotsure || pNotsure->eType == XFA_ATTRIBUTETYPE_Cdata) {
    wsName = GetCData(XFA_ATTRIBUTE_Name);
    m_dwNameHash = FX_HashCode_GetW(wsName, false);
  } else if (pNotsure->eType == XFA_ATTRIBUTETYPE_Enum) {
    wsName = GetAttributeEnumByID(GetEnum(XFA_ATTRIBUTE_Name))->pName;
    m_dwNameHash = FX_HashCode_GetW(wsName, false);
  }
}

CFX_XMLNode* CXFA_Node::CreateXMLMappingNode() {
  if (!m_pXMLNode) {
    CFX_WideString wsTag(GetCData(XFA_ATTRIBUTE_Name));
    m_pXMLNode = new CFX_XMLElement(wsTag);
    SetFlag(XFA_NodeFlag_OwnXMLNode, false);
  }
  return m_pXMLNode;
}

bool CXFA_Node::IsNeedSavingXMLNode() {
  return m_pXMLNode && (GetPacketID() == XFA_XDPPACKET_Datasets ||
                        GetElementType() == XFA_Element::Xfa);
}

XFA_MAPMODULEDATA* CXFA_Node::CreateMapModuleData() {
  if (!m_pMapModuleData)
    m_pMapModuleData = new XFA_MAPMODULEDATA;
  return m_pMapModuleData;
}

XFA_MAPMODULEDATA* CXFA_Node::GetMapModuleData() const {
  return m_pMapModuleData;
}

void CXFA_Node::SetMapModuleValue(void* pKey, void* pValue) {
  XFA_MAPMODULEDATA* pModule = CreateMapModuleData();
  pModule->m_ValueMap[pKey] = pValue;
}

bool CXFA_Node::GetMapModuleValue(void* pKey, void*& pValue) {
  for (CXFA_Node* pNode = this; pNode; pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->GetMapModuleData();
    if (pModule) {
      auto it = pModule->m_ValueMap.find(pKey);
      if (it != pModule->m_ValueMap.end()) {
        pValue = it->second;
        return true;
      }
    }
    if (pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  return false;
}

void CXFA_Node::SetMapModuleString(void* pKey, const CFX_WideStringC& wsValue) {
  SetMapModuleBuffer(pKey, (void*)wsValue.unterminated_c_str(),
                     wsValue.GetLength() * sizeof(wchar_t));
}

bool CXFA_Node::GetMapModuleString(void* pKey, CFX_WideStringC& wsValue) {
  void* pValue;
  int32_t iBytes;
  if (!GetMapModuleBuffer(pKey, pValue, iBytes))
    return false;
  // Defensive measure: no out-of-bounds pointers even if zero length.
  int32_t iChars = iBytes / sizeof(wchar_t);
  wsValue = CFX_WideStringC(iChars ? (const wchar_t*)pValue : nullptr, iChars);
  return true;
}

void CXFA_Node::SetMapModuleBuffer(
    void* pKey,
    void* pValue,
    int32_t iBytes,
    XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  XFA_MAPMODULEDATA* pModule = CreateMapModuleData();
  XFA_MAPDATABLOCK*& pBuffer = pModule->m_BufferMap[pKey];
  if (!pBuffer) {
    pBuffer =
        (XFA_MAPDATABLOCK*)FX_Alloc(uint8_t, sizeof(XFA_MAPDATABLOCK) + iBytes);
  } else if (pBuffer->iBytes != iBytes) {
    if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree) {
      pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
    }
    pBuffer = (XFA_MAPDATABLOCK*)FX_Realloc(uint8_t, pBuffer,
                                            sizeof(XFA_MAPDATABLOCK) + iBytes);
  } else if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree) {
    pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
  }
  if (!pBuffer)
    return;

  pBuffer->pCallbackInfo = pCallbackInfo;
  pBuffer->iBytes = iBytes;
  memcpy(pBuffer->GetData(), pValue, iBytes);
}

bool CXFA_Node::GetMapModuleBuffer(void* pKey,
                                   void*& pValue,
                                   int32_t& iBytes,
                                   bool bProtoAlso) const {
  XFA_MAPDATABLOCK* pBuffer = nullptr;
  for (const CXFA_Node* pNode = this; pNode; pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->GetMapModuleData();
    if (pModule) {
      auto it = pModule->m_BufferMap.find(pKey);
      if (it != pModule->m_BufferMap.end()) {
        pBuffer = it->second;
        break;
      }
    }
    if (!bProtoAlso || pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  if (!pBuffer)
    return false;

  pValue = pBuffer->GetData();
  iBytes = pBuffer->iBytes;
  return true;
}

bool CXFA_Node::HasMapModuleKey(void* pKey, bool bProtoAlso) {
  for (CXFA_Node* pNode = this; pNode; pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->GetMapModuleData();
    if (pModule) {
      auto it1 = pModule->m_ValueMap.find(pKey);
      if (it1 != pModule->m_ValueMap.end())
        return true;

      auto it2 = pModule->m_BufferMap.find(pKey);
      if (it2 != pModule->m_BufferMap.end())
        return true;
    }
    if (!bProtoAlso || pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  return false;
}

void CXFA_Node::RemoveMapModuleKey(void* pKey) {
  XFA_MAPMODULEDATA* pModule = GetMapModuleData();
  if (!pModule)
    return;

  if (pKey) {
    auto it = pModule->m_BufferMap.find(pKey);
    if (it != pModule->m_BufferMap.end()) {
      XFA_MAPDATABLOCK* pBuffer = it->second;
      if (pBuffer) {
        if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree)
          pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
        FX_Free(pBuffer);
      }
      pModule->m_BufferMap.erase(it);
    }
    pModule->m_ValueMap.erase(pKey);
    return;
  }

  for (auto& pair : pModule->m_BufferMap) {
    XFA_MAPDATABLOCK* pBuffer = pair.second;
    if (pBuffer) {
      if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree)
        pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
      FX_Free(pBuffer);
    }
  }
  pModule->m_BufferMap.clear();
  pModule->m_ValueMap.clear();
  delete pModule;
}

void CXFA_Node::MergeAllData(void* pDstModule) {
  XFA_MAPMODULEDATA* pDstModuleData =
      static_cast<CXFA_Node*>(pDstModule)->CreateMapModuleData();
  XFA_MAPMODULEDATA* pSrcModuleData = GetMapModuleData();
  if (!pSrcModuleData)
    return;

  for (const auto& pair : pSrcModuleData->m_ValueMap)
    pDstModuleData->m_ValueMap[pair.first] = pair.second;

  for (const auto& pair : pSrcModuleData->m_BufferMap) {
    XFA_MAPDATABLOCK* pSrcBuffer = pair.second;
    XFA_MAPDATABLOCK*& pDstBuffer = pDstModuleData->m_BufferMap[pair.first];
    if (pSrcBuffer->pCallbackInfo && pSrcBuffer->pCallbackInfo->pFree &&
        !pSrcBuffer->pCallbackInfo->pCopy) {
      if (pDstBuffer) {
        pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
        pDstModuleData->m_BufferMap.erase(pair.first);
      }
      continue;
    }
    if (!pDstBuffer) {
      pDstBuffer = (XFA_MAPDATABLOCK*)FX_Alloc(
          uint8_t, sizeof(XFA_MAPDATABLOCK) + pSrcBuffer->iBytes);
    } else if (pDstBuffer->iBytes != pSrcBuffer->iBytes) {
      if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pFree) {
        pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
      }
      pDstBuffer = (XFA_MAPDATABLOCK*)FX_Realloc(
          uint8_t, pDstBuffer, sizeof(XFA_MAPDATABLOCK) + pSrcBuffer->iBytes);
    } else if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pFree) {
      pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
    }
    if (!pDstBuffer) {
      continue;
    }
    pDstBuffer->pCallbackInfo = pSrcBuffer->pCallbackInfo;
    pDstBuffer->iBytes = pSrcBuffer->iBytes;
    memcpy(pDstBuffer->GetData(), pSrcBuffer->GetData(), pSrcBuffer->iBytes);
    if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pCopy) {
      pDstBuffer->pCallbackInfo->pCopy(*(void**)pDstBuffer->GetData());
    }
  }
}

void CXFA_Node::MoveBufferMapData(CXFA_Node* pDstModule, void* pKey) {
  if (!pDstModule) {
    return;
  }
  bool bNeedMove = true;
  if (!pKey) {
    bNeedMove = false;
  }
  if (pDstModule->GetElementType() != GetElementType()) {
    bNeedMove = false;
  }
  XFA_MAPMODULEDATA* pSrcModuleData = nullptr;
  XFA_MAPMODULEDATA* pDstModuleData = nullptr;
  if (bNeedMove) {
    pSrcModuleData = GetMapModuleData();
    if (!pSrcModuleData) {
      bNeedMove = false;
    }
    pDstModuleData = pDstModule->CreateMapModuleData();
  }
  if (bNeedMove) {
    auto it = pSrcModuleData->m_BufferMap.find(pKey);
    if (it != pSrcModuleData->m_BufferMap.end()) {
      XFA_MAPDATABLOCK* pBufferBlockData = it->second;
      if (pBufferBlockData) {
        pSrcModuleData->m_BufferMap.erase(pKey);
        pDstModuleData->m_BufferMap[pKey] = pBufferBlockData;
      }
    }
  }
  if (pDstModule->IsNodeV()) {
    CFX_WideString wsValue = pDstModule->GetScriptContent(false);
    CFX_WideString wsFormatValue(wsValue);
    CXFA_WidgetData* pWidgetData = pDstModule->GetContainerWidgetData();
    if (pWidgetData) {
      pWidgetData->GetFormatDataValue(wsValue, wsFormatValue);
    }
    pDstModule->SetScriptContent(wsValue, wsFormatValue, true, true);
  }
}

void CXFA_Node::MoveBufferMapData(CXFA_Node* pSrcModule,
                                  CXFA_Node* pDstModule,
                                  void* pKey,
                                  bool bRecursive) {
  if (!pSrcModule || !pDstModule || !pKey) {
    return;
  }
  if (bRecursive) {
    CXFA_Node* pSrcChild = pSrcModule->GetNodeItem(XFA_NODEITEM_FirstChild);
    CXFA_Node* pDstChild = pDstModule->GetNodeItem(XFA_NODEITEM_FirstChild);
    for (; pSrcChild && pDstChild;
         pSrcChild = pSrcChild->GetNodeItem(XFA_NODEITEM_NextSibling),
         pDstChild = pDstChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      MoveBufferMapData(pSrcChild, pDstChild, pKey, true);
    }
  }
  pSrcModule->MoveBufferMapData(pDstModule, pKey);
}

void CXFA_Node::ThrowMissingPropertyException(
    const CFX_WideString& obj,
    const CFX_WideString& prop) const {
  ThrowException(L"'%s' doesn't have property '%s'.", obj.c_str(),
                 prop.c_str());
}

void CXFA_Node::ThrowTooManyOccurancesException(
    const CFX_WideString& obj) const {
  ThrowException(
      L"The element [%s] has violated its allowable number of occurrences.",
      obj.c_str());
}

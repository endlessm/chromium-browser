// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/parser/cxfa_dataexporter.h"

#include <vector>

#include "core/fxcrt/cfx_memorystream.h"
#include "core/fxcrt/cfx_widetextbuf.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/xml/cfx_xmldoc.h"
#include "core/fxcrt/xml/cfx_xmlelement.h"
#include "core/fxcrt/xml/cfx_xmlnode.h"
#include "third_party/base/stl_util.h"
#include "xfa/fxfa/parser/cxfa_document.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_widgetdata.h"
#include "xfa/fxfa/parser/xfa_utils.h"

namespace {

CFX_WideString ExportEncodeAttribute(const CFX_WideString& str) {
  CFX_WideTextBuf textBuf;
  int32_t iLen = str.GetLength();
  for (int32_t i = 0; i < iLen; i++) {
    switch (str[i]) {
      case '&':
        textBuf << L"&amp;";
        break;
      case '<':
        textBuf << L"&lt;";
        break;
      case '>':
        textBuf << L"&gt;";
        break;
      case '\'':
        textBuf << L"&apos;";
        break;
      case '\"':
        textBuf << L"&quot;";
        break;
      default:
        textBuf.AppendChar(str[i]);
    }
  }
  return textBuf.MakeString();
}

bool IsXMLValidChar(wchar_t ch) {
  return ch == 0x09 || ch == 0x0A || ch == 0x0D ||
         (ch >= 0x20 && ch <= 0xD7FF) || (ch >= 0xE000 && ch <= 0xFFFD);
}

CFX_WideString ExportEncodeContent(const CFX_WideStringC& str) {
  CFX_WideTextBuf textBuf;
  int32_t iLen = str.GetLength();
  for (int32_t i = 0; i < iLen; i++) {
    wchar_t ch = str[i];
    if (!IsXMLValidChar(ch))
      continue;

    if (ch == '&') {
      textBuf << L"&amp;";
    } else if (ch == '<') {
      textBuf << L"&lt;";
    } else if (ch == '>') {
      textBuf << L"&gt;";
    } else if (ch == '\'') {
      textBuf << L"&apos;";
    } else if (ch == '\"') {
      textBuf << L"&quot;";
    } else if (ch == ' ') {
      if (i && str[i - 1] != ' ') {
        textBuf.AppendChar(' ');
      } else {
        textBuf << L"&#x20;";
      }
    } else {
      textBuf.AppendChar(str[i]);
    }
  }
  return textBuf.MakeString();
}

void SaveAttribute(CXFA_Node* pNode,
                   XFA_ATTRIBUTE eName,
                   const CFX_WideStringC& wsName,
                   bool bProto,
                   CFX_WideString& wsOutput) {
  CFX_WideString wsValue;
  if ((!bProto && !pNode->HasAttribute((XFA_ATTRIBUTE)eName, bProto)) ||
      !pNode->GetAttribute((XFA_ATTRIBUTE)eName, wsValue, false)) {
    return;
  }
  wsValue = ExportEncodeAttribute(wsValue);
  wsOutput += L" ";
  wsOutput += wsName;
  wsOutput += L"=\"";
  wsOutput += wsValue;
  wsOutput += L"\"";
}

bool AttributeSaveInDataModel(CXFA_Node* pNode, XFA_ATTRIBUTE eAttribute) {
  bool bSaveInDataModel = false;
  if (pNode->GetElementType() != XFA_Element::Image)
    return bSaveInDataModel;

  CXFA_Node* pValueNode = pNode->GetNodeItem(XFA_NODEITEM_Parent);
  if (!pValueNode || pValueNode->GetElementType() != XFA_Element::Value)
    return bSaveInDataModel;

  CXFA_Node* pFieldNode = pValueNode->GetNodeItem(XFA_NODEITEM_Parent);
  if (pFieldNode && pFieldNode->GetBindData() &&
      eAttribute == XFA_ATTRIBUTE_Href) {
    bSaveInDataModel = true;
  }
  return bSaveInDataModel;
}

bool ContentNodeNeedtoExport(CXFA_Node* pContentNode) {
  CFX_WideString wsContent;
  if (!pContentNode->TryContent(wsContent, false, false))
    return false;

  ASSERT(pContentNode->IsContentNode());
  CXFA_Node* pParentNode = pContentNode->GetNodeItem(XFA_NODEITEM_Parent);
  if (!pParentNode || pParentNode->GetElementType() != XFA_Element::Value)
    return true;

  CXFA_Node* pGrandParentNode = pParentNode->GetNodeItem(XFA_NODEITEM_Parent);
  if (!pGrandParentNode || !pGrandParentNode->IsContainerNode())
    return true;
  if (pGrandParentNode->GetBindData())
    return false;

  CXFA_WidgetData* pWidgetData = pGrandParentNode->GetWidgetData();
  XFA_Element eUIType = pWidgetData->GetUIType();
  if (eUIType == XFA_Element::PasswordEdit)
    return false;
  return true;
}

void RecognizeXFAVersionNumber(CXFA_Node* pTemplateRoot,
                               CFX_WideString& wsVersionNumber) {
  wsVersionNumber.clear();
  if (!pTemplateRoot)
    return;

  CFX_WideString wsTemplateNS;
  if (!pTemplateRoot->TryNamespace(wsTemplateNS))
    return;

  XFA_VERSION eVersion =
      pTemplateRoot->GetDocument()->RecognizeXFAVersionNumber(wsTemplateNS);
  if (eVersion == XFA_VERSION_UNKNOWN)
    eVersion = XFA_VERSION_DEFAULT;

  wsVersionNumber.Format(L"%i.%i", eVersion / 100, eVersion % 100);
}

void RegenerateFormFile_Changed(CXFA_Node* pNode,
                                CFX_WideTextBuf& buf,
                                bool bSaveXML) {
  CFX_WideString wsAttrs;
  int32_t iAttrs = 0;
  const uint8_t* pAttrs =
      XFA_GetElementAttributes(pNode->GetElementType(), iAttrs);
  while (iAttrs--) {
    const XFA_ATTRIBUTEINFO* pAttr =
        XFA_GetAttributeByID((XFA_ATTRIBUTE)pAttrs[iAttrs]);
    if (pAttr->eName == XFA_ATTRIBUTE_Name ||
        (AttributeSaveInDataModel(pNode, pAttr->eName) && !bSaveXML)) {
      continue;
    }
    CFX_WideString wsAttr;
    SaveAttribute(pNode, pAttr->eName, pAttr->pName, bSaveXML, wsAttr);
    wsAttrs += wsAttr;
  }

  CFX_WideString wsChildren;
  switch (pNode->GetObjectType()) {
    case XFA_ObjectType::ContentNode: {
      if (!bSaveXML && !ContentNodeNeedtoExport(pNode))
        break;

      CXFA_Node* pRawValueNode = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
      while (pRawValueNode &&
             pRawValueNode->GetElementType() != XFA_Element::SharpxHTML &&
             pRawValueNode->GetElementType() != XFA_Element::Sharptext &&
             pRawValueNode->GetElementType() != XFA_Element::Sharpxml) {
        pRawValueNode = pRawValueNode->GetNodeItem(XFA_NODEITEM_NextSibling);
      }
      if (!pRawValueNode)
        break;

      CFX_WideString wsContentType;
      pNode->GetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType, false);
      if (pRawValueNode->GetElementType() == XFA_Element::SharpxHTML &&
          wsContentType == L"text/html") {
        CFX_XMLNode* pExDataXML = pNode->GetXMLMappingNode();
        if (!pExDataXML)
          break;

        CFX_XMLNode* pRichTextXML =
            pExDataXML->GetNodeItem(CFX_XMLNode::FirstChild);
        if (!pRichTextXML)
          break;

        auto pMemStream = pdfium::MakeRetain<CFX_MemoryStream>(true);
        auto pTempStream =
            pdfium::MakeRetain<CFX_SeekableStreamProxy>(pMemStream, true);

        pTempStream->SetCodePage(FX_CODEPAGE_UTF8);
        pRichTextXML->SaveXMLNode(pTempStream);
        wsChildren += CFX_WideString::FromUTF8(
            CFX_ByteStringC(pMemStream->GetBuffer(), pMemStream->GetSize()));
      } else if (pRawValueNode->GetElementType() == XFA_Element::Sharpxml &&
                 wsContentType == L"text/xml") {
        CFX_WideString wsRawValue;
        pRawValueNode->GetAttribute(XFA_ATTRIBUTE_Value, wsRawValue, false);
        if (wsRawValue.IsEmpty())
          break;

        std::vector<CFX_WideString> wsSelTextArray;
        FX_STRSIZE iStart = 0;
        auto iEnd = wsRawValue.Find(L'\n', iStart);
        iEnd = !iEnd.has_value() ? wsRawValue.GetLength() : iEnd;
        while (iEnd.has_value() && iEnd >= iStart) {
          wsSelTextArray.push_back(
              wsRawValue.Mid(iStart, iEnd.value() - iStart));
          iStart = iEnd.value() + 1;
          if (iStart >= wsRawValue.GetLength())
            break;
          iEnd = wsRawValue.Find(L'\n', iStart);
        }
        CXFA_Node* pParentNode = pNode->GetNodeItem(XFA_NODEITEM_Parent);
        ASSERT(pParentNode);
        CXFA_Node* pGrandparentNode =
            pParentNode->GetNodeItem(XFA_NODEITEM_Parent);
        ASSERT(pGrandparentNode);
        CFX_WideString bodyTagName;
        bodyTagName = pGrandparentNode->GetCData(XFA_ATTRIBUTE_Name);
        if (bodyTagName.IsEmpty())
          bodyTagName = L"ListBox1";

        buf << L"<";
        buf << bodyTagName;
        buf << L" xmlns=\"\"\n>";
        for (int32_t i = 0; i < pdfium::CollectionSize<int32_t>(wsSelTextArray);
             i++) {
          buf << L"<value\n>";
          buf << ExportEncodeContent(wsSelTextArray[i].AsStringC());
          buf << L"</value\n>";
        }
        buf << L"</";
        buf << bodyTagName;
        buf << L"\n>";
        wsChildren += buf.AsStringC();
        buf.Clear();
      } else {
        CFX_WideStringC wsValue = pRawValueNode->GetCData(XFA_ATTRIBUTE_Value);
        wsChildren += ExportEncodeContent(wsValue);
      }
      break;
    }
    case XFA_ObjectType::TextNode:
    case XFA_ObjectType::NodeC:
    case XFA_ObjectType::NodeV: {
      CFX_WideStringC wsValue = pNode->GetCData(XFA_ATTRIBUTE_Value);
      wsChildren += ExportEncodeContent(wsValue);
      break;
    }
    default:
      if (pNode->GetElementType() == XFA_Element::Items) {
        CXFA_Node* pTemplateNode = pNode->GetTemplateNode();
        if (!pTemplateNode ||
            pTemplateNode->CountChildren(XFA_Element::Unknown) !=
                pNode->CountChildren(XFA_Element::Unknown)) {
          bSaveXML = true;
        }
      }
      CFX_WideTextBuf newBuf;
      CXFA_Node* pChildNode = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
      while (pChildNode) {
        RegenerateFormFile_Changed(pChildNode, newBuf, bSaveXML);
        wsChildren += newBuf.AsStringC();
        newBuf.Clear();
        pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling);
      }
      if (!bSaveXML && !wsChildren.IsEmpty() &&
          pNode->GetElementType() == XFA_Element::Items) {
        wsChildren.clear();
        bSaveXML = true;
        CXFA_Node* pChild = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
        while (pChild) {
          RegenerateFormFile_Changed(pChild, newBuf, bSaveXML);
          wsChildren += newBuf.AsStringC();
          newBuf.Clear();
          pChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling);
        }
      }
      break;
  }

  if (!wsChildren.IsEmpty() || !wsAttrs.IsEmpty() ||
      pNode->HasAttribute(XFA_ATTRIBUTE_Name)) {
    CFX_WideStringC wsElement = pNode->GetClassName();
    CFX_WideString wsName;
    SaveAttribute(pNode, XFA_ATTRIBUTE_Name, L"name", true, wsName);
    buf << L"<";
    buf << wsElement;
    buf << wsName;
    buf << wsAttrs;
    if (wsChildren.IsEmpty()) {
      buf << L"\n/>";
    } else {
      buf << L"\n>";
      buf << wsChildren;
      buf << L"</";
      buf << wsElement;
      buf << L"\n>";
    }
  }
}

void RegenerateFormFile_Container(
    CXFA_Node* pNode,
    const CFX_RetainPtr<CFX_SeekableStreamProxy>& pStream,
    bool bSaveXML) {
  XFA_Element eType = pNode->GetElementType();
  if (eType == XFA_Element::Field || eType == XFA_Element::Draw ||
      !pNode->IsContainerNode()) {
    CFX_WideTextBuf buf;
    RegenerateFormFile_Changed(pNode, buf, bSaveXML);
    FX_STRSIZE nLen = buf.GetLength();
    if (nLen > 0)
      pStream->WriteString(buf.AsStringC());
    return;
  }

  CFX_WideStringC wsElement(pNode->GetClassName());
  pStream->WriteString(L"<");
  pStream->WriteString(wsElement);

  CFX_WideString wsOutput;
  SaveAttribute(pNode, XFA_ATTRIBUTE_Name, L"name", true, wsOutput);

  CFX_WideString wsAttrs;
  int32_t iAttrs = 0;
  const uint8_t* pAttrs =
      XFA_GetElementAttributes(pNode->GetElementType(), iAttrs);
  while (iAttrs--) {
    const XFA_ATTRIBUTEINFO* pAttr =
        XFA_GetAttributeByID((XFA_ATTRIBUTE)pAttrs[iAttrs]);
    if (pAttr->eName == XFA_ATTRIBUTE_Name)
      continue;

    CFX_WideString wsAttr;
    SaveAttribute(pNode, pAttr->eName, pAttr->pName, false, wsAttr);
    wsOutput += wsAttr;
  }

  if (!wsOutput.IsEmpty())
    pStream->WriteString(wsOutput.AsStringC());

  CXFA_Node* pChildNode = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
  if (pChildNode) {
    pStream->WriteString(L"\n>");
    while (pChildNode) {
      RegenerateFormFile_Container(pChildNode, pStream, bSaveXML);
      pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling);
    }
    pStream->WriteString(L"</");
    pStream->WriteString(wsElement);
    pStream->WriteString(L"\n>");
  } else {
    pStream->WriteString(L"\n/>");
  }
}

}  // namespace

void XFA_DataExporter_RegenerateFormFile(
    CXFA_Node* pNode,
    const CFX_RetainPtr<CFX_SeekableStreamProxy>& pStream,
    const char* pChecksum,
    bool bSaveXML) {
  if (pNode->IsModelNode()) {
    pStream->WriteString(L"<form");
    if (pChecksum) {
      CFX_WideString wsChecksum = CFX_WideString::FromUTF8(pChecksum);
      pStream->WriteString(L" checksum=\"");
      pStream->WriteString(wsChecksum.AsStringC());
      pStream->WriteString(L"\"");
    }
    pStream->WriteString(L" xmlns=\"");

    const wchar_t* pURI = XFA_GetPacketByIndex(XFA_PACKET_Form)->pURI;
    pStream->WriteString(CFX_WideStringC(pURI, FXSYS_wcslen(pURI)));

    CFX_WideString wsVersionNumber;
    RecognizeXFAVersionNumber(
        ToNode(pNode->GetDocument()->GetXFAObject(XFA_HASHCODE_Template)),
        wsVersionNumber);
    if (wsVersionNumber.IsEmpty())
      wsVersionNumber = L"2.8";

    wsVersionNumber += L"/\"\n>";
    pStream->WriteString(wsVersionNumber.AsStringC());

    CXFA_Node* pChildNode = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
    while (pChildNode) {
      RegenerateFormFile_Container(pChildNode, pStream, false);
      pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling);
    }
    pStream->WriteString(L"</form\n>");
  } else {
    RegenerateFormFile_Container(pNode, pStream, bSaveXML);
  }
}

void XFA_DataExporter_DealWithDataGroupNode(CXFA_Node* pDataNode) {
  if (!pDataNode || pDataNode->GetElementType() == XFA_Element::DataValue)
    return;

  int32_t iChildNum = 0;
  for (CXFA_Node* pChildNode = pDataNode->GetNodeItem(XFA_NODEITEM_FirstChild);
       pChildNode;
       pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    iChildNum++;
    XFA_DataExporter_DealWithDataGroupNode(pChildNode);
  }

  if (pDataNode->GetElementType() != XFA_Element::DataGroup)
    return;

  if (iChildNum > 0) {
    CFX_XMLNode* pXMLNode = pDataNode->GetXMLMappingNode();
    ASSERT(pXMLNode->GetType() == FX_XMLNODE_Element);
    CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
    if (pXMLElement->HasAttribute(L"xfa:dataNode"))
      pXMLElement->RemoveAttribute(L"xfa:dataNode");

    return;
  }

  CFX_XMLNode* pXMLNode = pDataNode->GetXMLMappingNode();
  ASSERT(pXMLNode->GetType() == FX_XMLNODE_Element);
  static_cast<CFX_XMLElement*>(pXMLNode)->SetString(L"xfa:dataNode",
                                                    L"dataGroup");
}

CXFA_DataExporter::CXFA_DataExporter(CXFA_Document* pDocument)
    : m_pDocument(pDocument) {
  ASSERT(m_pDocument);
}

CXFA_DataExporter::~CXFA_DataExporter() {}

bool CXFA_DataExporter::Export(
    const CFX_RetainPtr<IFX_SeekableStream>& pWrite) {
  return Export(pWrite, m_pDocument->GetRoot(), 0, nullptr);
}

bool CXFA_DataExporter::Export(const CFX_RetainPtr<IFX_SeekableStream>& pWrite,
                               CXFA_Node* pNode,
                               uint32_t dwFlag,
                               const char* pChecksum) {
  ASSERT(pWrite);
  if (!pWrite)
    return false;

  auto pStream = pdfium::MakeRetain<CFX_SeekableStreamProxy>(pWrite, true);
  pStream->SetCodePage(FX_CODEPAGE_UTF8);
  return Export(pStream, pNode, dwFlag, pChecksum);
}

bool CXFA_DataExporter::Export(
    const CFX_RetainPtr<CFX_SeekableStreamProxy>& pStream,
    CXFA_Node* pNode,
    uint32_t dwFlag,
    const char* pChecksum) {
  CFX_XMLDoc* pXMLDoc = m_pDocument->GetXMLDoc();
  if (pNode->IsModelNode()) {
    switch (pNode->GetPacketID()) {
      case XFA_XDPPACKET_XDP: {
        pStream->WriteString(
            L"<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\">");
        for (CXFA_Node* pChild = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
             pChild; pChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
          Export(pStream, pChild, dwFlag, pChecksum);
        }
        pStream->WriteString(L"</xdp:xdp\n>");
        break;
      }
      case XFA_XDPPACKET_Datasets: {
        CFX_XMLElement* pElement =
            static_cast<CFX_XMLElement*>(pNode->GetXMLMappingNode());
        if (!pElement || pElement->GetType() != FX_XMLNODE_Element)
          return false;

        CXFA_Node* pDataNode = pNode->GetNodeItem(XFA_NODEITEM_FirstChild);
        ASSERT(pDataNode);
        XFA_DataExporter_DealWithDataGroupNode(pDataNode);
        pXMLDoc->SaveXMLNode(pStream, pElement);
        break;
      }
      case XFA_XDPPACKET_Form: {
        XFA_DataExporter_RegenerateFormFile(pNode, pStream, pChecksum);
        break;
      }
      case XFA_XDPPACKET_Template:
      default: {
        CFX_XMLElement* pElement =
            static_cast<CFX_XMLElement*>(pNode->GetXMLMappingNode());
        if (!pElement || pElement->GetType() != FX_XMLNODE_Element)
          return false;

        pXMLDoc->SaveXMLNode(pStream, pElement);
        break;
      }
    }
    return true;
  }

  CXFA_Node* pDataNode = pNode->GetNodeItem(XFA_NODEITEM_Parent);
  CXFA_Node* pExportNode = pNode;
  for (CXFA_Node* pChildNode = pDataNode->GetNodeItem(XFA_NODEITEM_FirstChild);
       pChildNode;
       pChildNode = pChildNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pChildNode != pNode) {
      pExportNode = pDataNode;
      break;
    }
  }
  CFX_XMLElement* pElement =
      static_cast<CFX_XMLElement*>(pExportNode->GetXMLMappingNode());
  if (!pElement || pElement->GetType() != FX_XMLNODE_Element)
    return false;

  XFA_DataExporter_DealWithDataGroupNode(pExportNode);
  pElement->SetString(L"xmlns:xfa", L"http://www.xfa.org/schema/xfa-data/1.0/");
  pXMLDoc->SaveXMLNode(pStream, pElement);
  pElement->RemoveAttribute(L"xmlns:xfa");

  return true;
}

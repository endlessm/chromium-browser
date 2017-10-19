// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/cxfa_widgetacc.h"

#include <algorithm>
#include <vector>

#include "core/fxcrt/xml/cfx_xmlelement.h"
#include "core/fxcrt/xml/cfx_xmlnode.h"
#include "third_party/base/stl_util.h"
#include "xfa/fde/cfde_textout.h"
#include "xfa/fxfa/cxfa_ffapp.h"
#include "xfa/fxfa/cxfa_ffdoc.h"
#include "xfa/fxfa/cxfa_ffdocview.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/cxfa_fontmgr.h"
#include "xfa/fxfa/cxfa_textlayout.h"
#include "xfa/fxfa/cxfa_textprovider.h"
#include "xfa/fxfa/parser/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_localevalue.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_scriptcontext.h"
#include "xfa/fxfa/parser/xfa_utils.h"

class CXFA_WidgetLayoutData {
 public:
  CXFA_WidgetLayoutData() : m_fWidgetHeight(-1) {}
  virtual ~CXFA_WidgetLayoutData() {}

  float m_fWidgetHeight;
};

namespace {

void FFDeleteCalcData(void* pData) {
  if (pData)
    delete ((CXFA_CalcData*)pData);
}

static XFA_MAPDATABLOCKCALLBACKINFO gs_XFADeleteCalcData = {FFDeleteCalcData,
                                                            nullptr};

class CXFA_TextLayoutData : public CXFA_WidgetLayoutData {
 public:
  CXFA_TextLayoutData() {}
  ~CXFA_TextLayoutData() override {}

  CXFA_TextLayout* GetTextLayout() const { return m_pTextLayout.get(); }
  CXFA_TextProvider* GetTextProvider() const { return m_pTextProvider.get(); }

  void LoadText(CXFA_WidgetAcc* pAcc) {
    if (m_pTextLayout)
      return;

    m_pTextProvider =
        pdfium::MakeUnique<CXFA_TextProvider>(pAcc, XFA_TEXTPROVIDERTYPE_Text);
    m_pTextLayout = pdfium::MakeUnique<CXFA_TextLayout>(m_pTextProvider.get());
  }

 private:
  std::unique_ptr<CXFA_TextLayout> m_pTextLayout;
  std::unique_ptr<CXFA_TextProvider> m_pTextProvider;
};

class CXFA_ImageLayoutData : public CXFA_WidgetLayoutData {
 public:
  CXFA_ImageLayoutData()
      : m_bNamedImage(false), m_iImageXDpi(0), m_iImageYDpi(0) {}

  ~CXFA_ImageLayoutData() override {}

  bool LoadImageData(CXFA_WidgetAcc* pAcc) {
    if (m_pDIBitmap)
      return true;

    CXFA_Value value = pAcc->GetFormValue();
    if (!value)
      return false;

    CXFA_Image imageObj = value.GetImage();
    if (!imageObj)
      return false;

    CXFA_FFDoc* pFFDoc = pAcc->GetDoc();
    pAcc->SetImageImage(XFA_LoadImageData(pFFDoc, &imageObj, m_bNamedImage,
                                          m_iImageXDpi, m_iImageYDpi));
    return !!m_pDIBitmap;
  }

  CFX_RetainPtr<CFX_DIBitmap> m_pDIBitmap;
  bool m_bNamedImage;
  int32_t m_iImageXDpi;
  int32_t m_iImageYDpi;
};

class CXFA_FieldLayoutData : public CXFA_WidgetLayoutData {
 public:
  CXFA_FieldLayoutData() {}
  ~CXFA_FieldLayoutData() override {}

  bool LoadCaption(CXFA_WidgetAcc* pAcc) {
    if (m_pCapTextLayout)
      return true;
    CXFA_Caption caption = pAcc->GetCaption();
    if (!caption || caption.GetPresence() == XFA_ATTRIBUTEENUM_Hidden)
      return false;
    m_pCapTextProvider = pdfium::MakeUnique<CXFA_TextProvider>(
        pAcc, XFA_TEXTPROVIDERTYPE_Caption);
    m_pCapTextLayout =
        pdfium::MakeUnique<CXFA_TextLayout>(m_pCapTextProvider.get());
    return true;
  }

  std::unique_ptr<CXFA_TextLayout> m_pCapTextLayout;
  std::unique_ptr<CXFA_TextProvider> m_pCapTextProvider;
  std::unique_ptr<CFDE_TextOut> m_pTextOut;
  std::vector<float> m_FieldSplitArray;
};

class CXFA_TextEditData : public CXFA_FieldLayoutData {
 public:
};

class CXFA_ImageEditData : public CXFA_FieldLayoutData {
 public:
  CXFA_ImageEditData()
      : m_bNamedImage(false), m_iImageXDpi(0), m_iImageYDpi(0) {}

  ~CXFA_ImageEditData() override {}

  bool LoadImageData(CXFA_WidgetAcc* pAcc) {
    if (m_pDIBitmap)
      return true;

    CXFA_Value value = pAcc->GetFormValue();
    if (!value)
      return false;

    CXFA_Image imageObj = value.GetImage();
    CXFA_FFDoc* pFFDoc = pAcc->GetDoc();
    pAcc->SetImageEditImage(XFA_LoadImageData(pFFDoc, &imageObj, m_bNamedImage,
                                              m_iImageXDpi, m_iImageYDpi));
    return !!m_pDIBitmap;
  }

  CFX_RetainPtr<CFX_DIBitmap> m_pDIBitmap;
  bool m_bNamedImage;
  int32_t m_iImageXDpi;
  int32_t m_iImageYDpi;
};

}  // namespace

CXFA_WidgetAcc::CXFA_WidgetAcc(CXFA_FFDocView* pDocView, CXFA_Node* pNode)
    : CXFA_WidgetData(pNode), m_pDocView(pDocView), m_nRecursionDepth(0) {}

CXFA_WidgetAcc::~CXFA_WidgetAcc() {}

bool CXFA_WidgetAcc::GetName(CFX_WideString& wsName, int32_t iNameType) {
  if (iNameType == 0) {
    m_pNode->TryCData(XFA_ATTRIBUTE_Name, wsName);
    return !wsName.IsEmpty();
  }
  m_pNode->GetSOMExpression(wsName);
  if (iNameType == 2 && wsName.GetLength() >= 15) {
    CFX_WideStringC wsPre = L"xfa[0].form[0].";
    if (wsPre == CFX_WideStringC(wsName.c_str(), wsPre.GetLength()))
      wsName.Delete(0, wsPre.GetLength());
  }
  return true;
}

CXFA_Node* CXFA_WidgetAcc::GetDatasets() {
  return m_pNode->GetBindData();
}

bool CXFA_WidgetAcc::ProcessValueChanged() {
  m_pDocView->AddValidateWidget(this);
  m_pDocView->AddCalculateWidgetAcc(this);
  m_pDocView->RunCalculateWidgets();
  m_pDocView->RunValidate();
  return true;
}

void CXFA_WidgetAcc::ResetData() {
  CFX_WideString wsValue;
  XFA_Element eUIType = GetUIType();
  switch (eUIType) {
    case XFA_Element::ImageEdit: {
      CXFA_Value imageValue = GetDefaultValue();
      CXFA_Image image = imageValue.GetImage();
      CFX_WideString wsContentType, wsHref;
      if (image) {
        image.GetContent(wsValue);
        image.GetContentType(wsContentType);
        image.GetHref(wsHref);
      }
      SetImageEdit(wsContentType, wsHref, wsValue);
      break;
    }
    case XFA_Element::ExclGroup: {
      CXFA_Node* pNextChild = m_pNode->GetNodeItem(
          XFA_NODEITEM_FirstChild, XFA_ObjectType::ContainerNode);
      while (pNextChild) {
        CXFA_Node* pChild = pNextChild;
        CXFA_WidgetAcc* pAcc =
            static_cast<CXFA_WidgetAcc*>(pChild->GetWidgetData());
        if (!pAcc)
          continue;

        CXFA_Value defValue(nullptr);
        if (wsValue.IsEmpty() && (defValue = pAcc->GetDefaultValue())) {
          defValue.GetChildValueContent(wsValue);
          SetValue(wsValue, XFA_VALUEPICTURE_Raw);
          pAcc->SetValue(wsValue, XFA_VALUEPICTURE_Raw);
        } else {
          CXFA_Node* pItems = pChild->GetChild(0, XFA_Element::Items);
          if (!pItems)
            continue;

          CFX_WideString itemText;
          if (pItems->CountChildren(XFA_Element::Unknown) > 1)
            itemText = pItems->GetChild(1, XFA_Element::Unknown)->GetContent();

          pAcc->SetValue(itemText, XFA_VALUEPICTURE_Raw);
        }
        pNextChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling,
                                         XFA_ObjectType::ContainerNode);
      }
      break;
    }
    case XFA_Element::ChoiceList:
      ClearAllSelections();
    default:
      if (CXFA_Value defValue = GetDefaultValue())
        defValue.GetChildValueContent(wsValue);

      SetValue(wsValue, XFA_VALUEPICTURE_Raw);
      break;
  }
}

void CXFA_WidgetAcc::SetImageEdit(const CFX_WideString& wsContentType,
                                  const CFX_WideString& wsHref,
                                  const CFX_WideString& wsData) {
  CXFA_Image image = GetFormValue().GetImage();
  if (image) {
    image.SetContentType(CFX_WideString(wsContentType));
    image.SetHref(wsHref);
  }
  CFX_WideString wsFormatValue(wsData);
  GetFormatDataValue(wsData, wsFormatValue);
  m_pNode->SetContent(wsData, wsFormatValue, true);
  CXFA_Node* pBind = GetDatasets();
  if (!pBind) {
    image.SetTransferEncoding(XFA_ATTRIBUTEENUM_Base64);
    return;
  }
  pBind->SetCData(XFA_ATTRIBUTE_ContentType, wsContentType);
  CXFA_Node* pHrefNode = pBind->GetNodeItem(XFA_NODEITEM_FirstChild);
  if (pHrefNode) {
    pHrefNode->SetCData(XFA_ATTRIBUTE_Value, wsHref);
  } else {
    CFX_XMLNode* pXMLNode = pBind->GetXMLMappingNode();
    ASSERT(pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element);
    static_cast<CFX_XMLElement*>(pXMLNode)->SetString(L"href", wsHref);
  }
}

CXFA_WidgetAcc* CXFA_WidgetAcc::GetExclGroup() {
  CXFA_Node* pExcl = m_pNode->GetNodeItem(XFA_NODEITEM_Parent);
  if (!pExcl || pExcl->GetElementType() != XFA_Element::ExclGroup)
    return nullptr;
  return static_cast<CXFA_WidgetAcc*>(pExcl->GetWidgetData());
}

CXFA_FFDocView* CXFA_WidgetAcc::GetDocView() {
  return m_pDocView;
}

CXFA_FFDoc* CXFA_WidgetAcc::GetDoc() {
  return m_pDocView->GetDoc();
}

CXFA_FFApp* CXFA_WidgetAcc::GetApp() {
  return GetDoc()->GetApp();
}

IXFA_AppProvider* CXFA_WidgetAcc::GetAppProvider() {
  return GetApp()->GetAppProvider();
}

int32_t CXFA_WidgetAcc::ProcessEvent(int32_t iActivity,
                                     CXFA_EventParam* pEventParam) {
  if (GetElementType() == XFA_Element::Draw)
    return XFA_EVENTERROR_NotExist;

  std::vector<CXFA_Node*> eventArray =
      GetEventByActivity(iActivity, pEventParam->m_bIsFormReady);
  bool first = true;
  int32_t iRet = XFA_EVENTERROR_NotExist;
  for (CXFA_Node* pNode : eventArray) {
    int32_t result = ProcessEvent(CXFA_Event(pNode), pEventParam);
    if (first || result == XFA_EVENTERROR_Success)
      iRet = result;
    first = false;
  }
  return iRet;
}

int32_t CXFA_WidgetAcc::ProcessEvent(const CXFA_Event& event,
                                     CXFA_EventParam* pEventParam) {
  if (!event)
    return XFA_EVENTERROR_NotExist;

  switch (event.GetEventType()) {
    case XFA_Element::Execute:
      break;
    case XFA_Element::Script:
      return ExecuteScript(event.GetScript(), pEventParam);
    case XFA_Element::SignData:
      break;
    case XFA_Element::Submit:
      return GetDoc()->GetDocEnvironment()->SubmitData(GetDoc(),
                                                       event.GetSubmit());
    default:
      break;
  }
  return XFA_EVENTERROR_NotExist;
}

int32_t CXFA_WidgetAcc::ProcessCalculate() {
  if (GetElementType() == XFA_Element::Draw)
    return XFA_EVENTERROR_NotExist;

  CXFA_Calculate calc = GetCalculate();
  if (!calc)
    return XFA_EVENTERROR_NotExist;
  if (GetNode()->IsUserInteractive())
    return XFA_EVENTERROR_Disabled;

  CXFA_EventParam EventParam;
  EventParam.m_eType = XFA_EVENT_Calculate;
  CXFA_Script script = calc.GetScript();
  int32_t iRet = ExecuteScript(script, &EventParam);
  if (iRet != XFA_EVENTERROR_Success)
    return iRet;

  if (GetRawValue() != EventParam.m_wsResult) {
    SetValue(EventParam.m_wsResult, XFA_VALUEPICTURE_Raw);
    UpdateUIDisplay();
  }
  return XFA_EVENTERROR_Success;
}

void CXFA_WidgetAcc::ProcessScriptTestValidate(CXFA_Validate validate,
                                               int32_t iRet,
                                               CFXJSE_Value* pRetValue,
                                               bool bVersionFlag) {
  if (iRet == XFA_EVENTERROR_Success && pRetValue) {
    if (pRetValue->IsBoolean() && !pRetValue->ToBoolean()) {
      IXFA_AppProvider* pAppProvider = GetAppProvider();
      if (!pAppProvider) {
        return;
      }
      CFX_WideString wsTitle = pAppProvider->GetAppTitle();
      CFX_WideString wsScriptMsg;
      validate.GetScriptMessageText(wsScriptMsg);
      int32_t eScriptTest = validate.GetScriptTest();
      if (eScriptTest == XFA_ATTRIBUTEENUM_Warning) {
        if (GetNode()->IsUserInteractive())
          return;
        if (wsScriptMsg.IsEmpty())
          wsScriptMsg = GetValidateMessage(false, bVersionFlag);

        if (bVersionFlag) {
          pAppProvider->MsgBox(wsScriptMsg, wsTitle, XFA_MBICON_Warning,
                               XFA_MB_OK);
          return;
        }
        if (pAppProvider->MsgBox(wsScriptMsg, wsTitle, XFA_MBICON_Warning,
                                 XFA_MB_YesNo) == XFA_IDYes) {
          GetNode()->SetFlag(XFA_NodeFlag_UserInteractive, false);
        }
      } else {
        if (wsScriptMsg.IsEmpty())
          wsScriptMsg = GetValidateMessage(true, bVersionFlag);
        pAppProvider->MsgBox(wsScriptMsg, wsTitle, XFA_MBICON_Error, XFA_MB_OK);
      }
    }
  }
}

int32_t CXFA_WidgetAcc::ProcessFormatTestValidate(CXFA_Validate validate,
                                                  bool bVersionFlag) {
  CFX_WideString wsRawValue = GetRawValue();
  if (!wsRawValue.IsEmpty()) {
    CFX_WideString wsPicture;
    validate.GetPicture(wsPicture);
    if (wsPicture.IsEmpty())
      return XFA_EVENTERROR_NotExist;

    IFX_Locale* pLocale = GetLocal();
    if (!pLocale)
      return XFA_EVENTERROR_NotExist;

    CXFA_LocaleValue lcValue = XFA_GetLocaleValue(this);
    if (!lcValue.ValidateValue(lcValue.GetValue(), wsPicture, pLocale,
                               nullptr)) {
      IXFA_AppProvider* pAppProvider = GetAppProvider();
      if (!pAppProvider)
        return XFA_EVENTERROR_NotExist;

      CFX_WideString wsFormatMsg;
      validate.GetFormatMessageText(wsFormatMsg);
      CFX_WideString wsTitle = pAppProvider->GetAppTitle();
      int32_t eFormatTest = validate.GetFormatTest();
      if (eFormatTest == XFA_ATTRIBUTEENUM_Error) {
        if (wsFormatMsg.IsEmpty())
          wsFormatMsg = GetValidateMessage(true, bVersionFlag);
        pAppProvider->MsgBox(wsFormatMsg, wsTitle, XFA_MBICON_Error, XFA_MB_OK);
        return XFA_EVENTERROR_Success;
      }
      if (GetNode()->IsUserInteractive())
        return XFA_EVENTERROR_NotExist;
      if (wsFormatMsg.IsEmpty())
        wsFormatMsg = GetValidateMessage(false, bVersionFlag);

      if (bVersionFlag) {
        pAppProvider->MsgBox(wsFormatMsg, wsTitle, XFA_MBICON_Warning,
                             XFA_MB_OK);
        return XFA_EVENTERROR_Success;
      }
      if (pAppProvider->MsgBox(wsFormatMsg, wsTitle, XFA_MBICON_Warning,
                               XFA_MB_YesNo) == XFA_IDYes) {
        GetNode()->SetFlag(XFA_NodeFlag_UserInteractive, false);
      }
      return XFA_EVENTERROR_Success;
    }
  }
  return XFA_EVENTERROR_NotExist;
}

int32_t CXFA_WidgetAcc::ProcessNullTestValidate(CXFA_Validate validate,
                                                int32_t iFlags,
                                                bool bVersionFlag) {
  CFX_WideString wsValue;
  GetValue(wsValue, XFA_VALUEPICTURE_Raw);
  if (!wsValue.IsEmpty())
    return XFA_EVENTERROR_Success;
  if (m_bIsNull && (m_bPreNull == m_bIsNull))
    return XFA_EVENTERROR_Success;

  int32_t eNullTest = validate.GetNullTest();
  CFX_WideString wsNullMsg;
  validate.GetNullMessageText(wsNullMsg);
  if (iFlags & 0x01) {
    int32_t iRet = XFA_EVENTERROR_Success;
    if (eNullTest != XFA_ATTRIBUTEENUM_Disabled)
      iRet = XFA_EVENTERROR_Error;

    if (!wsNullMsg.IsEmpty()) {
      if (eNullTest != XFA_ATTRIBUTEENUM_Disabled) {
        m_pDocView->m_arrNullTestMsg.push_back(wsNullMsg);
        return XFA_EVENTERROR_Error;
      }
      return XFA_EVENTERROR_Success;
    }
    return iRet;
  }
  if (wsNullMsg.IsEmpty() && bVersionFlag &&
      eNullTest != XFA_ATTRIBUTEENUM_Disabled) {
    return XFA_EVENTERROR_Error;
  }
  IXFA_AppProvider* pAppProvider = GetAppProvider();
  if (!pAppProvider)
    return XFA_EVENTERROR_NotExist;

  CFX_WideString wsCaptionName;
  CFX_WideString wsTitle = pAppProvider->GetAppTitle();
  switch (eNullTest) {
    case XFA_ATTRIBUTEENUM_Error: {
      if (wsNullMsg.IsEmpty()) {
        wsCaptionName = GetValidateCaptionName(bVersionFlag);
        wsNullMsg.Format(L"%s cannot be blank.", wsCaptionName.c_str());
      }
      pAppProvider->MsgBox(wsNullMsg, wsTitle, XFA_MBICON_Status, XFA_MB_OK);
      return XFA_EVENTERROR_Error;
    }
    case XFA_ATTRIBUTEENUM_Warning: {
      if (GetNode()->IsUserInteractive())
        return true;

      if (wsNullMsg.IsEmpty()) {
        wsCaptionName = GetValidateCaptionName(bVersionFlag);
        wsNullMsg.Format(
            L"%s cannot be blank. To ignore validations for %s, click Ignore.",
            wsCaptionName.c_str(), wsCaptionName.c_str());
      }
      if (pAppProvider->MsgBox(wsNullMsg, wsTitle, XFA_MBICON_Warning,
                               XFA_MB_YesNo) == XFA_IDYes) {
        GetNode()->SetFlag(XFA_NodeFlag_UserInteractive, false);
      }
      return XFA_EVENTERROR_Error;
    }
    case XFA_ATTRIBUTEENUM_Disabled:
    default:
      break;
  }
  return XFA_EVENTERROR_Success;
}

CFX_WideString CXFA_WidgetAcc::GetValidateCaptionName(bool bVersionFlag) {
  CFX_WideString wsCaptionName;

  if (!bVersionFlag) {
    if (CXFA_Caption caption = GetCaption()) {
      if (CXFA_Value capValue = caption.GetValue()) {
        if (CXFA_Text capText = capValue.GetText())
          capText.GetContent(wsCaptionName);
      }
    }
  }
  if (wsCaptionName.IsEmpty())
    GetName(wsCaptionName);

  return wsCaptionName;
}

CFX_WideString CXFA_WidgetAcc::GetValidateMessage(bool bError,
                                                  bool bVersionFlag) {
  CFX_WideString wsCaptionName = GetValidateCaptionName(bVersionFlag);
  CFX_WideString wsMessage;
  if (bVersionFlag) {
    wsMessage.Format(L"%s validation failed", wsCaptionName.c_str());
    return wsMessage;
  }
  if (bError) {
    wsMessage.Format(L"The value you entered for %s is invalid.",
                     wsCaptionName.c_str());
    return wsMessage;
  }
  wsMessage.Format(
      L"The value you entered for %s is invalid. To ignore "
      L"validations for %s, click Ignore.",
      wsCaptionName.c_str(), wsCaptionName.c_str());
  return wsMessage;
}

int32_t CXFA_WidgetAcc::ProcessValidate(int32_t iFlags) {
  if (GetElementType() == XFA_Element::Draw)
    return XFA_EVENTERROR_NotExist;

  CXFA_Validate validate = GetValidate(false);
  if (!validate)
    return XFA_EVENTERROR_NotExist;

  bool bInitDoc = validate.GetNode()->NeedsInitApp();
  bool bStatus = m_pDocView->GetLayoutStatus() < XFA_DOCVIEW_LAYOUTSTATUS_End;
  int32_t iFormat = 0;
  CFXJSE_Value* pRetValue = nullptr;
  int32_t iRet = XFA_EVENTERROR_NotExist;
  CXFA_Script script = validate.GetScript();
  if (script) {
    CXFA_EventParam eParam;
    eParam.m_eType = XFA_EVENT_Validate;
    eParam.m_pTarget = this;
    iRet = ExecuteScript(script, &eParam,
                         ((bInitDoc || bStatus) && GetRawValue().IsEmpty())
                             ? nullptr
                             : &pRetValue);
  }
  XFA_VERSION version = GetDoc()->GetXFADoc()->GetCurVersionMode();
  bool bVersionFlag = false;
  if (version < XFA_VERSION_208)
    bVersionFlag = true;

  if (bInitDoc) {
    validate.GetNode()->ClearFlag(XFA_NodeFlag_NeedsInitApp);
  } else {
    iFormat = ProcessFormatTestValidate(validate, bVersionFlag);
    if (!bVersionFlag)
      bVersionFlag = GetDoc()->GetXFADoc()->HasFlag(XFA_DOCFLAG_Scripting);

    iRet |= ProcessNullTestValidate(validate, iFlags, bVersionFlag);
  }
  if (iFormat != XFA_EVENTERROR_Success)
    ProcessScriptTestValidate(validate, iRet, pRetValue, bVersionFlag);

  delete pRetValue;

  return iRet | iFormat;
}

int32_t CXFA_WidgetAcc::ExecuteScript(CXFA_Script script,
                                      CXFA_EventParam* pEventParam,
                                      CFXJSE_Value** pRetValue) {
  static const uint32_t MAX_RECURSION_DEPTH = 2;
  if (m_nRecursionDepth > MAX_RECURSION_DEPTH)
    return XFA_EVENTERROR_Success;

  ASSERT(pEventParam);
  if (!script)
    return XFA_EVENTERROR_NotExist;
  if (script.GetRunAt() == XFA_ATTRIBUTEENUM_Server)
    return XFA_EVENTERROR_Disabled;

  CFX_WideString wsExpression;
  script.GetExpression(wsExpression);
  if (wsExpression.IsEmpty())
    return XFA_EVENTERROR_NotExist;

  XFA_SCRIPTTYPE eScriptType = script.GetContentType();
  if (eScriptType == XFA_SCRIPTTYPE_Unkown)
    return XFA_EVENTERROR_Success;

  CXFA_FFDoc* pDoc = GetDoc();
  CXFA_ScriptContext* pContext = pDoc->GetXFADoc()->GetScriptContext();
  pContext->SetEventParam(*pEventParam);
  pContext->SetRunAtType((XFA_ATTRIBUTEENUM)script.GetRunAt());
  std::vector<CXFA_Node*> refNodes;
  if (pEventParam->m_eType == XFA_EVENT_InitCalculate ||
      pEventParam->m_eType == XFA_EVENT_Calculate) {
    pContext->SetNodesOfRunScript(&refNodes);
  }
  auto pTmpRetValue = pdfium::MakeUnique<CFXJSE_Value>(pContext->GetRuntime());
  ++m_nRecursionDepth;
  bool bRet = pContext->RunScript((XFA_SCRIPTLANGTYPE)eScriptType,
                                  wsExpression.AsStringC(), pTmpRetValue.get(),
                                  m_pNode);
  --m_nRecursionDepth;
  int32_t iRet = XFA_EVENTERROR_Error;
  if (bRet) {
    iRet = XFA_EVENTERROR_Success;
    if (pEventParam->m_eType == XFA_EVENT_Calculate ||
        pEventParam->m_eType == XFA_EVENT_InitCalculate) {
      if (!pTmpRetValue->IsUndefined()) {
        if (!pTmpRetValue->IsNull())
          pEventParam->m_wsResult = pTmpRetValue->ToWideString();

        iRet = XFA_EVENTERROR_Success;
      } else {
        iRet = XFA_EVENTERROR_Error;
      }
      if (pEventParam->m_eType == XFA_EVENT_InitCalculate) {
        if ((iRet == XFA_EVENTERROR_Success) &&
            (GetRawValue() != pEventParam->m_wsResult)) {
          SetValue(pEventParam->m_wsResult, XFA_VALUEPICTURE_Raw);
          m_pDocView->AddValidateWidget(this);
        }
      }
      for (CXFA_Node* pRefNode : refNodes) {
        if (static_cast<CXFA_WidgetAcc*>(pRefNode->GetWidgetData()) == this)
          continue;

        auto* pGlobalData =
            static_cast<CXFA_CalcData*>(pRefNode->GetUserData(XFA_CalcData));
        if (!pGlobalData) {
          pGlobalData = new CXFA_CalcData;
          pRefNode->SetUserData(XFA_CalcData, pGlobalData,
                                &gs_XFADeleteCalcData);
        }
        if (!pdfium::ContainsValue(pGlobalData->m_Globals, this))
          pGlobalData->m_Globals.push_back(this);
      }
    }
  }
  if (pRetValue)
    *pRetValue = pTmpRetValue.release();

  pContext->SetNodesOfRunScript(nullptr);
  return iRet;
}

CXFA_FFWidget* CXFA_WidgetAcc::GetNextWidget(CXFA_FFWidget* pWidget) {
  CXFA_LayoutItem* pLayout = nullptr;
  if (pWidget)
    pLayout = pWidget->GetNext();
  else
    pLayout = m_pDocView->GetXFALayout()->GetLayoutItem(m_pNode);

  return static_cast<CXFA_FFWidget*>(pLayout);
}

void CXFA_WidgetAcc::UpdateUIDisplay(CXFA_FFWidget* pExcept) {
  CXFA_FFWidget* pWidget = nullptr;
  while ((pWidget = GetNextWidget(pWidget)) != nullptr) {
    if (pWidget == pExcept || !pWidget->IsLoaded() ||
        (GetUIType() != XFA_Element::CheckButton && pWidget->IsFocused())) {
      continue;
    }
    pWidget->UpdateFWLData();
    pWidget->AddInvalidateRect();
  }
}

void CXFA_WidgetAcc::CalcCaptionSize(CFX_SizeF& szCap) {
  CXFA_Caption caption = GetCaption();
  if (!caption || caption.GetPresence() != XFA_ATTRIBUTEENUM_Visible)
    return;

  LoadCaption();
  XFA_Element eUIType = GetUIType();
  int32_t iCapPlacement = caption.GetPlacementType();
  float fCapReserve = caption.GetReserve();
  const bool bVert = iCapPlacement == XFA_ATTRIBUTEENUM_Top ||
                     iCapPlacement == XFA_ATTRIBUTEENUM_Bottom;
  const bool bReserveExit = fCapReserve > 0.01;
  CXFA_TextLayout* pCapTextLayout =
      static_cast<CXFA_FieldLayoutData*>(m_pLayoutData.get())
          ->m_pCapTextLayout.get();
  if (pCapTextLayout) {
    if (!bVert && eUIType != XFA_Element::Button)
      szCap.width = fCapReserve;

    CFX_SizeF minSize;
    pCapTextLayout->CalcSize(minSize, szCap, szCap);
    if (bReserveExit)
      bVert ? szCap.height = fCapReserve : szCap.width = fCapReserve;
  } else {
    float fFontSize = 10.0f;
    if (CXFA_Font font = caption.GetFont())
      fFontSize = font.GetFontSize();
    else if (CXFA_Font widgetfont = GetFont(false))
      fFontSize = widgetfont.GetFontSize();

    if (bVert) {
      szCap.height = fCapReserve > 0 ? fCapReserve : fFontSize;
    } else {
      szCap.width = fCapReserve > 0 ? fCapReserve : 0;
      szCap.height = fFontSize;
    }
  }
  if (CXFA_Margin mgCap = caption.GetMargin()) {
    float fLeftInset, fTopInset, fRightInset, fBottomInset;
    mgCap.GetLeftInset(fLeftInset);
    mgCap.GetTopInset(fTopInset);
    mgCap.GetRightInset(fRightInset);
    mgCap.GetBottomInset(fBottomInset);
    if (bReserveExit) {
      bVert ? (szCap.width += fLeftInset + fRightInset)
            : (szCap.height += fTopInset + fBottomInset);
    } else {
      szCap.width += fLeftInset + fRightInset;
      szCap.height += fTopInset + fBottomInset;
    }
  }
}

bool CXFA_WidgetAcc::CalculateFieldAutoSize(CFX_SizeF& size) {
  CFX_SizeF szCap;
  CalcCaptionSize(szCap);
  CFX_RectF rtUIMargin = GetUIMargin();
  size.width += rtUIMargin.left + rtUIMargin.width;
  size.height += rtUIMargin.top + rtUIMargin.height;
  if (szCap.width > 0 && szCap.height > 0) {
    int32_t iCapPlacement = GetCaption().GetPlacementType();
    switch (iCapPlacement) {
      case XFA_ATTRIBUTEENUM_Left:
      case XFA_ATTRIBUTEENUM_Right:
      case XFA_ATTRIBUTEENUM_Inline: {
        size.width += szCap.width;
        size.height = std::max(size.height, szCap.height);
      } break;
      case XFA_ATTRIBUTEENUM_Top:
      case XFA_ATTRIBUTEENUM_Bottom: {
        size.height += szCap.height;
        size.width = std::max(size.width, szCap.width);
      }
      default:
        break;
    }
  }
  return CalculateWidgetAutoSize(size);
}

bool CXFA_WidgetAcc::CalculateWidgetAutoSize(CFX_SizeF& size) {
  CXFA_Margin mgWidget = GetMargin();
  if (mgWidget) {
    float fLeftInset, fTopInset, fRightInset, fBottomInset;
    mgWidget.GetLeftInset(fLeftInset);
    mgWidget.GetTopInset(fTopInset);
    mgWidget.GetRightInset(fRightInset);
    mgWidget.GetBottomInset(fBottomInset);
    size.width += fLeftInset + fRightInset;
    size.height += fTopInset + fBottomInset;
  }
  CXFA_Para para = GetPara();
  if (para)
    size.width += para.GetMarginLeft() + para.GetTextIndent();

  float fVal = 0;
  float fMin = 0;
  float fMax = 0;
  if (GetWidth(fVal)) {
    size.width = fVal;
  } else {
    if (GetMinWidth(fMin))
      size.width = std::max(size.width, fMin);
    if (GetMaxWidth(fMax) && fMax > 0)
      size.width = std::min(size.width, fMax);
  }
  fVal = 0;
  fMin = 0;
  fMax = 0;
  if (GetHeight(fVal)) {
    size.height = fVal;
  } else {
    if (GetMinHeight(fMin))
      size.height = std::max(size.height, fMin);
    if (GetMaxHeight(fMax) && fMax > 0)
      size.height = std::min(size.height, fMax);
  }
  return true;
}

void CXFA_WidgetAcc::CalculateTextContentSize(CFX_SizeF& size) {
  float fFontSize = GetFontSize();
  CFX_WideString wsText;
  GetValue(wsText, XFA_VALUEPICTURE_Display);
  if (wsText.IsEmpty()) {
    size.height += fFontSize;
    return;
  }

  wchar_t wcEnter = '\n';
  wchar_t wsLast = wsText[wsText.GetLength() - 1];
  if (wsLast == wcEnter)
    wsText = wsText + wcEnter;

  CXFA_FieldLayoutData* layoutData =
      static_cast<CXFA_FieldLayoutData*>(m_pLayoutData.get());
  if (!layoutData->m_pTextOut) {
    layoutData->m_pTextOut = pdfium::MakeUnique<CFDE_TextOut>();
    CFDE_TextOut* pTextOut = layoutData->m_pTextOut.get();
    pTextOut->SetFont(GetFDEFont());
    pTextOut->SetFontSize(fFontSize);
    pTextOut->SetLineBreakTolerance(fFontSize * 0.2f);
    pTextOut->SetLineSpace(GetLineHeight());

    FDE_TextStyle dwStyles;
    dwStyles.last_line_height_ = true;
    if (GetUIType() == XFA_Element::TextEdit && IsMultiLine())
      dwStyles.line_wrap_ = true;

    pTextOut->SetStyles(dwStyles);
  }
  layoutData->m_pTextOut->CalcLogicSize(wsText, size);
}

bool CXFA_WidgetAcc::CalculateTextEditAutoSize(CFX_SizeF& size) {
  if (size.width > 0) {
    CFX_SizeF szOrz = size;
    CFX_SizeF szCap;
    CalcCaptionSize(szCap);
    bool bCapExit = szCap.width > 0.01 && szCap.height > 0.01;
    int32_t iCapPlacement = XFA_ATTRIBUTEENUM_Unknown;
    if (bCapExit) {
      iCapPlacement = GetCaption().GetPlacementType();
      switch (iCapPlacement) {
        case XFA_ATTRIBUTEENUM_Left:
        case XFA_ATTRIBUTEENUM_Right:
        case XFA_ATTRIBUTEENUM_Inline: {
          size.width -= szCap.width;
        }
        default:
          break;
      }
    }
    CFX_RectF rtUIMargin = GetUIMargin();
    size.width -= rtUIMargin.left + rtUIMargin.width;
    CXFA_Margin mgWidget = GetMargin();
    if (mgWidget) {
      float fLeftInset, fRightInset;
      mgWidget.GetLeftInset(fLeftInset);
      mgWidget.GetRightInset(fRightInset);
      size.width -= fLeftInset + fRightInset;
    }
    CalculateTextContentSize(size);
    size.height += rtUIMargin.top + rtUIMargin.height;
    if (bCapExit) {
      switch (iCapPlacement) {
        case XFA_ATTRIBUTEENUM_Left:
        case XFA_ATTRIBUTEENUM_Right:
        case XFA_ATTRIBUTEENUM_Inline: {
          size.height = std::max(size.height, szCap.height);
        } break;
        case XFA_ATTRIBUTEENUM_Top:
        case XFA_ATTRIBUTEENUM_Bottom: {
          size.height += szCap.height;
        }
        default:
          break;
      }
    }
    size.width = szOrz.width;
    return CalculateWidgetAutoSize(size);
  }
  CalculateTextContentSize(size);
  return CalculateFieldAutoSize(size);
}

bool CXFA_WidgetAcc::CalculateCheckButtonAutoSize(CFX_SizeF& size) {
  float fCheckSize = GetCheckButtonSize();
  size = CFX_SizeF(fCheckSize, fCheckSize);
  return CalculateFieldAutoSize(size);
}

bool CXFA_WidgetAcc::CalculatePushButtonAutoSize(CFX_SizeF& size) {
  CalcCaptionSize(size);
  return CalculateWidgetAutoSize(size);
}

bool CXFA_WidgetAcc::CalculateImageAutoSize(CFX_SizeF& size) {
  if (!GetImageImage())
    LoadImageImage();

  size.clear();
  CFX_RetainPtr<CFX_DIBitmap> pBitmap = GetImageImage();
  if (pBitmap) {
    int32_t iImageXDpi = 0;
    int32_t iImageYDpi = 0;
    GetImageDpi(iImageXDpi, iImageYDpi);
    CFX_RectF rtImage(
        0, 0, XFA_UnitPx2Pt((float)pBitmap->GetWidth(), (float)iImageXDpi),
        XFA_UnitPx2Pt((float)pBitmap->GetHeight(), (float)iImageYDpi));

    CFX_RectF rtFit;
    if (GetWidth(rtFit.width))
      GetWidthWithoutMargin(rtFit.width);
    else
      rtFit.width = rtImage.width;

    if (GetHeight(rtFit.height))
      GetHeightWithoutMargin(rtFit.height);
    else
      rtFit.height = rtImage.height;

    size = rtFit.Size();
  }
  return CalculateWidgetAutoSize(size);
}

bool CXFA_WidgetAcc::CalculateImageEditAutoSize(CFX_SizeF& size) {
  if (!GetImageEditImage())
    LoadImageEditImage();

  size.clear();
  CFX_RetainPtr<CFX_DIBitmap> pBitmap = GetImageEditImage();
  if (pBitmap) {
    int32_t iImageXDpi = 0;
    int32_t iImageYDpi = 0;
    GetImageEditDpi(iImageXDpi, iImageYDpi);
    CFX_RectF rtImage(
        0, 0, XFA_UnitPx2Pt((float)pBitmap->GetWidth(), (float)iImageXDpi),
        XFA_UnitPx2Pt((float)pBitmap->GetHeight(), (float)iImageYDpi));

    CFX_RectF rtFit;
    if (GetWidth(rtFit.width))
      GetWidthWithoutMargin(rtFit.width);
    else
      rtFit.width = rtImage.width;

    if (GetHeight(rtFit.height))
      GetHeightWithoutMargin(rtFit.height);
    else
      rtFit.height = rtImage.height;

    size.width = rtFit.width;
    size.height = rtFit.height;
  }
  return CalculateFieldAutoSize(size);
}

bool CXFA_WidgetAcc::LoadImageImage() {
  InitLayoutData();
  return static_cast<CXFA_ImageLayoutData*>(m_pLayoutData.get())
      ->LoadImageData(this);
}

bool CXFA_WidgetAcc::LoadImageEditImage() {
  InitLayoutData();
  return static_cast<CXFA_ImageEditData*>(m_pLayoutData.get())
      ->LoadImageData(this);
}

void CXFA_WidgetAcc::GetImageDpi(int32_t& iImageXDpi, int32_t& iImageYDpi) {
  CXFA_ImageLayoutData* pData =
      static_cast<CXFA_ImageLayoutData*>(m_pLayoutData.get());
  iImageXDpi = pData->m_iImageXDpi;
  iImageYDpi = pData->m_iImageYDpi;
}

void CXFA_WidgetAcc::GetImageEditDpi(int32_t& iImageXDpi, int32_t& iImageYDpi) {
  CXFA_ImageEditData* pData =
      static_cast<CXFA_ImageEditData*>(m_pLayoutData.get());
  iImageXDpi = pData->m_iImageXDpi;
  iImageYDpi = pData->m_iImageYDpi;
}

bool CXFA_WidgetAcc::CalculateTextAutoSize(CFX_SizeF& size) {
  LoadText();
  CXFA_TextLayout* pTextLayout =
      static_cast<CXFA_TextLayoutData*>(m_pLayoutData.get())->GetTextLayout();
  if (pTextLayout) {
    size.width = pTextLayout->StartLayout(size.width);
    size.height = pTextLayout->GetLayoutHeight();
  }
  return CalculateWidgetAutoSize(size);
}

void CXFA_WidgetAcc::LoadText() {
  InitLayoutData();
  static_cast<CXFA_TextLayoutData*>(m_pLayoutData.get())->LoadText(this);
}

float CXFA_WidgetAcc::CalculateWidgetAutoWidth(float fWidthCalc) {
  CXFA_Margin mgWidget = GetMargin();
  if (mgWidget) {
    float fLeftInset, fRightInset;
    mgWidget.GetLeftInset(fLeftInset);
    mgWidget.GetRightInset(fRightInset);
    fWidthCalc += fLeftInset + fRightInset;
  }

  float fMin = 0, fMax = 0;
  if (GetMinWidth(fMin))
    fWidthCalc = std::max(fWidthCalc, fMin);
  if (GetMaxWidth(fMax) && fMax > 0)
    fWidthCalc = std::min(fWidthCalc, fMax);

  return fWidthCalc;
}

float CXFA_WidgetAcc::GetWidthWithoutMargin(float fWidthCalc) {
  CXFA_Margin mgWidget = GetMargin();
  if (mgWidget) {
    float fLeftInset, fRightInset;
    mgWidget.GetLeftInset(fLeftInset);
    mgWidget.GetRightInset(fRightInset);
    fWidthCalc -= fLeftInset + fRightInset;
  }
  return fWidthCalc;
}

float CXFA_WidgetAcc::CalculateWidgetAutoHeight(float fHeightCalc) {
  CXFA_Margin mgWidget = GetMargin();
  if (mgWidget) {
    float fTopInset, fBottomInset;
    mgWidget.GetTopInset(fTopInset);
    mgWidget.GetBottomInset(fBottomInset);
    fHeightCalc += fTopInset + fBottomInset;
  }

  float fMin = 0, fMax = 0;
  if (GetMinHeight(fMin))
    fHeightCalc = std::max(fHeightCalc, fMin);
  if (GetMaxHeight(fMax) && fMax > 0)
    fHeightCalc = std::min(fHeightCalc, fMax);

  return fHeightCalc;
}

float CXFA_WidgetAcc::GetHeightWithoutMargin(float fHeightCalc) {
  CXFA_Margin mgWidget = GetMargin();
  if (mgWidget) {
    float fTopInset, fBottomInset;
    mgWidget.GetTopInset(fTopInset);
    mgWidget.GetBottomInset(fBottomInset);
    fHeightCalc -= fTopInset + fBottomInset;
  }
  return fHeightCalc;
}

void CXFA_WidgetAcc::StartWidgetLayout(float& fCalcWidth, float& fCalcHeight) {
  InitLayoutData();
  XFA_Element eUIType = GetUIType();
  if (eUIType == XFA_Element::Text) {
    m_pLayoutData->m_fWidgetHeight = -1;
    GetHeight(m_pLayoutData->m_fWidgetHeight);
    StartTextLayout(fCalcWidth, fCalcHeight);
    return;
  }
  if (fCalcWidth > 0 && fCalcHeight > 0)
    return;

  m_pLayoutData->m_fWidgetHeight = -1;
  float fWidth = 0;
  if (fCalcWidth > 0 && fCalcHeight < 0) {
    if (!GetHeight(fCalcHeight))
      CalculateAccWidthAndHeight(eUIType, fCalcWidth, fCalcHeight);

    m_pLayoutData->m_fWidgetHeight = fCalcHeight;
    return;
  }
  if (fCalcWidth < 0 && fCalcHeight < 0) {
    if (!GetWidth(fWidth) || !GetHeight(fCalcHeight))
      CalculateAccWidthAndHeight(eUIType, fWidth, fCalcHeight);

    fCalcWidth = fWidth;
  }
  m_pLayoutData->m_fWidgetHeight = fCalcHeight;
}

void CXFA_WidgetAcc::CalculateAccWidthAndHeight(XFA_Element eUIType,
                                                float& fWidth,
                                                float& fCalcHeight) {
  CFX_SizeF sz(fWidth, m_pLayoutData->m_fWidgetHeight);
  switch (eUIType) {
    case XFA_Element::Barcode:
    case XFA_Element::ChoiceList:
    case XFA_Element::Signature:
      CalculateFieldAutoSize(sz);
      break;
    case XFA_Element::ImageEdit:
      CalculateImageEditAutoSize(sz);
      break;
    case XFA_Element::Button:
      CalculatePushButtonAutoSize(sz);
      break;
    case XFA_Element::CheckButton:
      CalculateCheckButtonAutoSize(sz);
      break;
    case XFA_Element::DateTimeEdit:
    case XFA_Element::NumericEdit:
    case XFA_Element::PasswordEdit:
    case XFA_Element::TextEdit:
      CalculateTextEditAutoSize(sz);
      break;
    case XFA_Element::Image:
      CalculateImageAutoSize(sz);
      break;
    case XFA_Element::Arc:
    case XFA_Element::Line:
    case XFA_Element::Rectangle:
    case XFA_Element::Subform:
    case XFA_Element::ExclGroup:
      CalculateWidgetAutoSize(sz);
      break;
    default:
      break;
  }
  fWidth = sz.width;
  m_pLayoutData->m_fWidgetHeight = sz.height;
  fCalcHeight = sz.height;
}

bool CXFA_WidgetAcc::FindSplitPos(int32_t iBlockIndex, float& fCalcHeight) {
  XFA_Element eUIType = GetUIType();
  if (eUIType == XFA_Element::Subform)
    return false;

  if (eUIType != XFA_Element::Text && eUIType != XFA_Element::TextEdit &&
      eUIType != XFA_Element::NumericEdit &&
      eUIType != XFA_Element::PasswordEdit) {
    fCalcHeight = 0;
    return true;
  }

  float fTopInset = 0;
  float fBottomInset = 0;
  if (iBlockIndex == 0) {
    CXFA_Margin mgWidget = GetMargin();
    if (mgWidget) {
      mgWidget.GetTopInset(fTopInset);
      mgWidget.GetBottomInset(fBottomInset);
    }
    CFX_RectF rtUIMargin = GetUIMargin();
    fTopInset += rtUIMargin.top;
    fBottomInset += rtUIMargin.width;
  }
  if (eUIType == XFA_Element::Text) {
    float fHeight = fCalcHeight;
    if (iBlockIndex == 0) {
      fCalcHeight = fCalcHeight - fTopInset;
      if (fCalcHeight < 0)
        fCalcHeight = 0;
    }

    CXFA_TextLayout* pTextLayout =
        static_cast<CXFA_TextLayoutData*>(m_pLayoutData.get())->GetTextLayout();
    pTextLayout->DoLayout(iBlockIndex, fCalcHeight, fCalcHeight,
                          m_pLayoutData->m_fWidgetHeight - fTopInset);
    if (fCalcHeight != 0) {
      if (iBlockIndex == 0)
        fCalcHeight = fCalcHeight + fTopInset;
      if (fabs(fHeight - fCalcHeight) < XFA_FLOAT_PERCISION)
        return false;
    }
    return true;
  }
  XFA_ATTRIBUTEENUM iCapPlacement = XFA_ATTRIBUTEENUM_Unknown;
  float fCapReserve = 0;
  if (iBlockIndex == 0) {
    CXFA_Caption caption = GetCaption();
    if (caption && caption.GetPresence() != XFA_ATTRIBUTEENUM_Hidden) {
      iCapPlacement = (XFA_ATTRIBUTEENUM)caption.GetPlacementType();
      fCapReserve = caption.GetReserve();
    }
    if (iCapPlacement == XFA_ATTRIBUTEENUM_Top &&
        fCalcHeight < fCapReserve + fTopInset) {
      fCalcHeight = 0;
      return true;
    }
    if (iCapPlacement == XFA_ATTRIBUTEENUM_Bottom &&
        m_pLayoutData->m_fWidgetHeight - fCapReserve - fBottomInset) {
      fCalcHeight = 0;
      return true;
    }
    if (iCapPlacement != XFA_ATTRIBUTEENUM_Top)
      fCapReserve = 0;
  }
  CXFA_FieldLayoutData* pFieldData =
      static_cast<CXFA_FieldLayoutData*>(m_pLayoutData.get());
  int32_t iLinesCount = 0;
  float fHeight = m_pLayoutData->m_fWidgetHeight;
  CFX_WideString wsText;
  GetValue(wsText, XFA_VALUEPICTURE_Display);
  if (wsText.IsEmpty()) {
    iLinesCount = 1;
  } else {
    if (!pFieldData->m_pTextOut) {
      float fWidth = 0;
      GetWidth(fWidth);
      CalculateAccWidthAndHeight(eUIType, fWidth, fHeight);
    }
    iLinesCount = pFieldData->m_pTextOut->GetTotalLines();
  }
  std::vector<float>* pFieldArray = &pFieldData->m_FieldSplitArray;
  int32_t iFieldSplitCount = pdfium::CollectionSize<int32_t>(*pFieldArray);
  for (int32_t i = 0; i < iBlockIndex * 3; i += 3) {
    iLinesCount -= (int32_t)(*pFieldArray)[i + 1];
    fHeight -= (*pFieldArray)[i + 2];
  }
  if (iLinesCount == 0)
    return false;

  float fLineHeight = GetLineHeight();
  float fFontSize = GetFontSize();
  float fTextHeight = iLinesCount * fLineHeight - fLineHeight + fFontSize;
  float fSpaceAbove = 0;
  float fStartOffset = 0;
  if (fHeight > 0.1f && iBlockIndex == 0) {
    fStartOffset = fTopInset;
    fHeight -= (fTopInset + fBottomInset);
    if (CXFA_Para para = GetPara()) {
      fSpaceAbove = para.GetSpaceAbove();
      float fSpaceBelow = para.GetSpaceBelow();
      fHeight -= (fSpaceAbove + fSpaceBelow);
      switch (para.GetVerticalAlign()) {
        case XFA_ATTRIBUTEENUM_Top:
          fStartOffset += fSpaceAbove;
          break;
        case XFA_ATTRIBUTEENUM_Middle:
          fStartOffset += ((fHeight - fTextHeight) / 2 + fSpaceAbove);
          break;
        case XFA_ATTRIBUTEENUM_Bottom:
          fStartOffset += (fHeight - fTextHeight + fSpaceAbove);
          break;
      }
    }
    if (fStartOffset < 0.1f)
      fStartOffset = 0;
  }
  for (int32_t i = iBlockIndex - 1; iBlockIndex > 0 && i < iBlockIndex; i++) {
    fStartOffset = (*pFieldArray)[i * 3] - (*pFieldArray)[i * 3 + 2];
    if (fStartOffset < 0.1f)
      fStartOffset = 0;
  }
  if (iFieldSplitCount / 3 == (iBlockIndex + 1))
    (*pFieldArray)[0] = fStartOffset;
  else
    pFieldArray->push_back(fStartOffset);

  XFA_VERSION version = GetDoc()->GetXFADoc()->GetCurVersionMode();
  bool bCanSplitNoContent = false;
  XFA_ATTRIBUTEENUM eLayoutMode;
  GetNode()
      ->GetNodeItem(XFA_NODEITEM_Parent)
      ->TryEnum(XFA_ATTRIBUTE_Layout, eLayoutMode, true);
  if ((eLayoutMode == XFA_ATTRIBUTEENUM_Position ||
       eLayoutMode == XFA_ATTRIBUTEENUM_Tb ||
       eLayoutMode == XFA_ATTRIBUTEENUM_Row ||
       eLayoutMode == XFA_ATTRIBUTEENUM_Table) &&
      version > XFA_VERSION_208) {
    bCanSplitNoContent = true;
  }
  if ((eLayoutMode == XFA_ATTRIBUTEENUM_Tb ||
       eLayoutMode == XFA_ATTRIBUTEENUM_Row ||
       eLayoutMode == XFA_ATTRIBUTEENUM_Table) &&
      version <= XFA_VERSION_208) {
    if (fStartOffset < fCalcHeight) {
      bCanSplitNoContent = true;
    } else {
      fCalcHeight = 0;
      return true;
    }
  }
  if (bCanSplitNoContent) {
    if ((fCalcHeight - fTopInset - fSpaceAbove < fLineHeight)) {
      fCalcHeight = 0;
      return true;
    }
    if (fStartOffset + XFA_FLOAT_PERCISION >= fCalcHeight) {
      if (iFieldSplitCount / 3 == (iBlockIndex + 1)) {
        (*pFieldArray)[iBlockIndex * 3 + 1] = 0;
        (*pFieldArray)[iBlockIndex * 3 + 2] = fCalcHeight;
      } else {
        pFieldArray->push_back(0);
        pFieldArray->push_back(fCalcHeight);
      }
      return false;
    }
    if (fCalcHeight - fStartOffset < fLineHeight) {
      fCalcHeight = fStartOffset;
      if (iFieldSplitCount / 3 == (iBlockIndex + 1)) {
        (*pFieldArray)[iBlockIndex * 3 + 1] = 0;
        (*pFieldArray)[iBlockIndex * 3 + 2] = fCalcHeight;
      } else {
        pFieldArray->push_back(0);
        pFieldArray->push_back(fCalcHeight);
      }
      return true;
    }
    float fTextNum =
        fCalcHeight + XFA_FLOAT_PERCISION - fCapReserve - fStartOffset;
    int32_t iLineNum =
        (int32_t)((fTextNum + (fLineHeight - fFontSize)) / fLineHeight);
    if (iLineNum >= iLinesCount) {
      if (fCalcHeight - fStartOffset - fTextHeight >= fFontSize) {
        if (iFieldSplitCount / 3 == (iBlockIndex + 1)) {
          (*pFieldArray)[iBlockIndex * 3 + 1] = (float)iLinesCount;
          (*pFieldArray)[iBlockIndex * 3 + 2] = fCalcHeight;
        } else {
          pFieldArray->push_back((float)iLinesCount);
          pFieldArray->push_back(fCalcHeight);
        }
        return false;
      }
      if (fHeight - fStartOffset - fTextHeight < fFontSize) {
        iLineNum -= 1;
        if (iLineNum == 0) {
          fCalcHeight = 0;
          return true;
        }
      } else {
        iLineNum = (int32_t)(fTextNum / fLineHeight);
      }
    }
    if (iLineNum > 0) {
      float fSplitHeight = iLineNum * fLineHeight + fCapReserve + fStartOffset;
      if (iFieldSplitCount / 3 == (iBlockIndex + 1)) {
        (*pFieldArray)[iBlockIndex * 3 + 1] = (float)iLineNum;
        (*pFieldArray)[iBlockIndex * 3 + 2] = fSplitHeight;
      } else {
        pFieldArray->push_back((float)iLineNum);
        pFieldArray->push_back(fSplitHeight);
      }
      if (fabs(fSplitHeight - fCalcHeight) < XFA_FLOAT_PERCISION)
        return false;

      fCalcHeight = fSplitHeight;
      return true;
    }
  }
  fCalcHeight = 0;
  return true;
}

void CXFA_WidgetAcc::InitLayoutData() {
  if (m_pLayoutData)
    return;

  switch (GetUIType()) {
    case XFA_Element::Text:
      m_pLayoutData = pdfium::MakeUnique<CXFA_TextLayoutData>();
      return;
    case XFA_Element::TextEdit:
      m_pLayoutData = pdfium::MakeUnique<CXFA_TextEditData>();
      return;
    case XFA_Element::Image:
      m_pLayoutData = pdfium::MakeUnique<CXFA_ImageLayoutData>();
      return;
    case XFA_Element::ImageEdit:
      m_pLayoutData = pdfium::MakeUnique<CXFA_ImageEditData>();
      return;
    default:
      break;
  }
  if (GetElementType() == XFA_Element::Field) {
    m_pLayoutData = pdfium::MakeUnique<CXFA_FieldLayoutData>();
    return;
  }
  m_pLayoutData = pdfium::MakeUnique<CXFA_WidgetLayoutData>();
}

void CXFA_WidgetAcc::StartTextLayout(float& fCalcWidth, float& fCalcHeight) {
  LoadText();
  CXFA_TextLayout* pTextLayout =
      static_cast<CXFA_TextLayoutData*>(m_pLayoutData.get())->GetTextLayout();
  float fTextHeight = 0;
  if (fCalcWidth > 0 && fCalcHeight > 0) {
    float fWidth = GetWidthWithoutMargin(fCalcWidth);
    pTextLayout->StartLayout(fWidth);
    fTextHeight = fCalcHeight;
    fTextHeight = GetHeightWithoutMargin(fTextHeight);
    pTextLayout->DoLayout(0, fTextHeight, -1, fTextHeight);
    return;
  }
  if (fCalcWidth > 0 && fCalcHeight < 0) {
    float fWidth = GetWidthWithoutMargin(fCalcWidth);
    pTextLayout->StartLayout(fWidth);
  }
  if (fCalcWidth < 0 && fCalcHeight < 0) {
    float fMaxWidth = -1;
    bool bRet = GetWidth(fMaxWidth);
    if (bRet) {
      float fWidth = GetWidthWithoutMargin(fMaxWidth);
      pTextLayout->StartLayout(fWidth);
    } else {
      float fWidth = pTextLayout->StartLayout(fMaxWidth);
      fMaxWidth = CalculateWidgetAutoWidth(fWidth);
      fWidth = GetWidthWithoutMargin(fMaxWidth);
      pTextLayout->StartLayout(fWidth);
    }
    fCalcWidth = fMaxWidth;
  }
  if (m_pLayoutData->m_fWidgetHeight < 0) {
    m_pLayoutData->m_fWidgetHeight = pTextLayout->GetLayoutHeight();
    m_pLayoutData->m_fWidgetHeight =
        CalculateWidgetAutoHeight(m_pLayoutData->m_fWidgetHeight);
  }
  fTextHeight = m_pLayoutData->m_fWidgetHeight;
  fTextHeight = GetHeightWithoutMargin(fTextHeight);
  pTextLayout->DoLayout(0, fTextHeight, -1, fTextHeight);
  fCalcHeight = m_pLayoutData->m_fWidgetHeight;
}

bool CXFA_WidgetAcc::LoadCaption() {
  InitLayoutData();
  return static_cast<CXFA_FieldLayoutData*>(m_pLayoutData.get())
      ->LoadCaption(this);
}

CXFA_TextLayout* CXFA_WidgetAcc::GetCaptionTextLayout() {
  return m_pLayoutData
             ? static_cast<CXFA_FieldLayoutData*>(m_pLayoutData.get())
                   ->m_pCapTextLayout.get()
             : nullptr;
}

CXFA_TextLayout* CXFA_WidgetAcc::GetTextLayout() {
  return m_pLayoutData
             ? static_cast<CXFA_TextLayoutData*>(m_pLayoutData.get())
                   ->GetTextLayout()
             : nullptr;
}

CFX_RetainPtr<CFX_DIBitmap> CXFA_WidgetAcc::GetImageImage() {
  return m_pLayoutData
             ? static_cast<CXFA_ImageLayoutData*>(m_pLayoutData.get())
                   ->m_pDIBitmap
             : nullptr;
}

CFX_RetainPtr<CFX_DIBitmap> CXFA_WidgetAcc::GetImageEditImage() {
  return m_pLayoutData
             ? static_cast<CXFA_ImageEditData*>(m_pLayoutData.get())
                   ->m_pDIBitmap
             : nullptr;
}

void CXFA_WidgetAcc::SetImageImage(
    const CFX_RetainPtr<CFX_DIBitmap>& newImage) {
  CXFA_ImageLayoutData* pData =
      static_cast<CXFA_ImageLayoutData*>(m_pLayoutData.get());
  if (pData->m_pDIBitmap != newImage)
    pData->m_pDIBitmap = newImage;
}

void CXFA_WidgetAcc::SetImageEditImage(
    const CFX_RetainPtr<CFX_DIBitmap>& newImage) {
  CXFA_ImageEditData* pData =
      static_cast<CXFA_ImageEditData*>(m_pLayoutData.get());
  if (pData->m_pDIBitmap != newImage)
    pData->m_pDIBitmap = newImage;
}

CXFA_WidgetLayoutData* CXFA_WidgetAcc::GetWidgetLayoutData() {
  return m_pLayoutData.get();
}

CFX_RetainPtr<CFGAS_GEFont> CXFA_WidgetAcc::GetFDEFont() {
  CFX_WideStringC wsFontName = L"Courier";
  uint32_t dwFontStyle = 0;
  if (CXFA_Font font = GetFont(false)) {
    if (font.IsBold())
      dwFontStyle |= FX_FONTSTYLE_Bold;
    if (font.IsItalic())
      dwFontStyle |= FX_FONTSTYLE_Italic;
    font.GetTypeface(wsFontName);
  }

  auto* pDoc = GetDoc();
  return pDoc->GetApp()->GetXFAFontMgr()->GetFont(pDoc, wsFontName,
                                                  dwFontStyle);
}

float CXFA_WidgetAcc::GetFontSize() {
  float fFontSize = 10.0f;
  if (CXFA_Font font = GetFont(false))
    fFontSize = font.GetFontSize();
  return fFontSize < 0.1f ? 10.0f : fFontSize;
}

float CXFA_WidgetAcc::GetLineHeight() {
  float fLineHeight = 0;
  if (CXFA_Para para = GetPara())
    fLineHeight = para.GetLineHeight();
  if (fLineHeight < 1)
    fLineHeight = GetFontSize() * 1.2f;
  return fLineHeight;
}

FX_ARGB CXFA_WidgetAcc::GetTextColor() {
  if (CXFA_Font font = GetFont(false))
    return font.GetColor();
  return 0xFF000000;
}

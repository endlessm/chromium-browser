// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FXFA_FXFA_H_
#define XFA_FXFA_FXFA_H_

#include <vector>

#include "core/fxcrt/cfx_retain_ptr.h"
#include "xfa/fxfa/cxfa_widgetacc.h"
#include "xfa/fxfa/fxfa_basic.h"

class CFGAS_GEFont;
class CXFA_Graphics;
class CPDF_Document;
class CXFA_FFPageView;
class CXFA_Node;
class CXFA_NodeList;
class CXFA_WidgetAcc;
class IFWL_AdapterTimerMgr;
class IFX_SeekableReadStream;
class IXFA_AppProvider;
class IXFA_DocEnvironment;
class IXFA_WidgetAccIterator;
class IXFA_WidgetIterator;

#define XFA_MBICON_Error 0
#define XFA_MBICON_Warning 1
#define XFA_MBICON_Question 2
#define XFA_MBICON_Status 3
#define XFA_MB_OK 0
#define XFA_MB_OKCancel 1
#define XFA_MB_YesNo 2
#define XFA_MB_YesNoCancel 3
#define XFA_IDOK 1
#define XFA_IDCancel 2
#define XFA_IDNo 3
#define XFA_IDYes 4

// Note, values match fpdf_formfill.h DOCTYPE_* flags.
enum class XFA_DocType { PDF = 0, Dynamic = 1, Static = 2 };

#define XFA_PARSESTATUS_StatusErr -3
#define XFA_PARSESTATUS_StreamErr -2
#define XFA_PARSESTATUS_SyntaxErr -1
#define XFA_PARSESTATUS_Ready 0
#define XFA_PARSESTATUS_Done 100

#define XFA_PRINTOPT_ShowDialog 0x00000001
#define XFA_PRINTOPT_CanCancel 0x00000002
#define XFA_PRINTOPT_ShrinkPage 0x00000004
#define XFA_PRINTOPT_AsImage 0x00000008
#define XFA_PRINTOPT_ReverseOrder 0x00000010
#define XFA_PRINTOPT_PrintAnnot 0x00000020
#define XFA_PAGEVIEWEVENT_PostAdded 1
#define XFA_PAGEVIEWEVENT_PostRemoved 3
#define XFA_PAGEVIEWEVENT_StopLayout 4

#define XFA_EVENTERROR_Success 1
#define XFA_EVENTERROR_Error -1
#define XFA_EVENTERROR_NotExist 0
#define XFA_EVENTERROR_Disabled 2

#define XFA_TRAVERSEWAY_Tranvalse 0x0001
#define XFA_TRAVERSEWAY_Form 0x0002

enum XFA_WidgetStatus {
  XFA_WidgetStatus_None = 0,

  XFA_WidgetStatus_Access = 1 << 0,
  XFA_WidgetStatus_ButtonDown = 1 << 1,
  XFA_WidgetStatus_Disabled = 1 << 2,
  XFA_WidgetStatus_Focused = 1 << 3,
  XFA_WidgetStatus_Highlight = 1 << 4,
  XFA_WidgetStatus_Printable = 1 << 5,
  XFA_WidgetStatus_RectCached = 1 << 6,
  XFA_WidgetStatus_TextEditValueChanged = 1 << 7,
  XFA_WidgetStatus_Viewable = 1 << 8,
  XFA_WidgetStatus_Visible = 1 << 9
};

enum XFA_WIDGETTYPE {
  XFA_WIDGETTYPE_Barcode,
  XFA_WIDGETTYPE_PushButton,
  XFA_WIDGETTYPE_CheckButton,
  XFA_WIDGETTYPE_RadioButton,
  XFA_WIDGETTYPE_DatetimeEdit,
  XFA_WIDGETTYPE_DecimalField,
  XFA_WIDGETTYPE_NumericField,
  XFA_WIDGETTYPE_Signature,
  XFA_WIDGETTYPE_TextEdit,
  XFA_WIDGETTYPE_DropdownList,
  XFA_WIDGETTYPE_ListBox,
  XFA_WIDGETTYPE_ImageField,
  XFA_WIDGETTYPE_PasswordEdit,
  XFA_WIDGETTYPE_Arc,
  XFA_WIDGETTYPE_Rectangle,
  XFA_WIDGETTYPE_Image,
  XFA_WIDGETTYPE_Line,
  XFA_WIDGETTYPE_Text,
  XFA_WIDGETTYPE_ExcludeGroup,
  XFA_WIDGETTYPE_Subform,
  XFA_WIDGETTYPE_Unknown,
};

// Probably should be called IXFA_AppDelegate.
class IXFA_AppProvider {
 public:
  virtual ~IXFA_AppProvider() {}

  /**
   * Returns the language of the running host application. Such as zh_CN
   */
  virtual CFX_WideString GetLanguage() = 0;

  /**
   * Returns the platform of the machine running the script. Such as WIN
   */
  virtual CFX_WideString GetPlatform() = 0;

  /**
   * Get application name, such as Phantom.
   */
  virtual CFX_WideString GetAppName() = 0;

  /**
   * Get application message box title.
   */
  virtual CFX_WideString GetAppTitle() const = 0;

  /**
   * Causes the system to play a sound.
   * @param[in] dwType The system code for the appropriate sound.0 (Error)1
   * (Warning)2 (Question)3 (Status)4 (Default)
   */
  virtual void Beep(uint32_t dwType) = 0;

  /**
   * Displays a message box.
   * @param[in] wsMessage    - Message string to display in box.
   * @param[in] wsTitle      - Title string for box.
   * @param[in] dwIconType   - Icon type, refer to XFA_MBICON.
   * @param[in] dwButtonType - Button type, refer to XFA_MESSAGEBUTTON.
   * @return A valid integer representing the value of the button pressed by the
   * user, refer to XFA_ID.
   */
  virtual int32_t MsgBox(const CFX_WideString& wsMessage,
                         const CFX_WideString& wsTitle = L"",
                         uint32_t dwIconType = 0,
                         uint32_t dwButtonType = 0) = 0;

  /**
   * Get a response from the user.
   * @param[in] wsQuestion      - Message string to display in box.
   * @param[in] wsTitle         - Title string for box.
   * @param[in] wsDefaultAnswer - Initial contents for answer.
   * @param[in] bMask           - Mask the user input with asterisks when true,
   * @return A string containing the user's response.
   */
  virtual CFX_WideString Response(const CFX_WideString& wsQuestion,
                                  const CFX_WideString& wsTitle = L"",
                                  const CFX_WideString& wsDefaultAnswer = L"",
                                  bool bMask = true) = 0;

  /**
   * Download something from somewhere.
   * @param[in] wsURL - http, ftp, such as
   * "http://www.w3.org/TR/REC-xml-names/".
   */
  virtual CFX_RetainPtr<IFX_SeekableReadStream> DownloadURL(
      const CFX_WideString& wsURL) = 0;

  /**
   * POST data to the given url.
   * @param[in] wsURL         the URL being uploaded.
   * @param[in] wsData        the data being uploaded.
   * @param[in] wsContentType the content type of data including text/html,
   * text/xml, text/plain, multipart/form-data,
   *                          application/x-www-form-urlencoded,
   * application/octet-stream, any valid MIME type.
   * @param[in] wsEncode      the encode of data including UTF-8, UTF-16,
   * ISO8859-1, any recognized [IANA]character encoding
   * @param[in] wsHeader      any additional HTTP headers to be included in the
   * post.
   * @param[out] wsResponse   decoded response from server.
   * @return true Server permitted the post request, false otherwise.
   */
  virtual bool PostRequestURL(const CFX_WideString& wsURL,
                              const CFX_WideString& wsData,
                              const CFX_WideString& wsContentType,
                              const CFX_WideString& wsEncode,
                              const CFX_WideString& wsHeader,
                              CFX_WideString& wsResponse) = 0;

  /**
   * PUT data to the given url.
   * @param[in] wsURL         the URL being uploaded.
   * @param[in] wsData            the data being uploaded.
   * @param[in] wsEncode      the encode of data including UTF-8, UTF-16,
   * ISO8859-1, any recognized [IANA]character encoding
   * @return true Server permitted the post request, false otherwise.
   */
  virtual bool PutRequestURL(const CFX_WideString& wsURL,
                             const CFX_WideString& wsData,
                             const CFX_WideString& wsEncode) = 0;

  virtual IFWL_AdapterTimerMgr* GetTimerMgr() = 0;
};

class IXFA_DocEnvironment {
 public:
  virtual ~IXFA_DocEnvironment() {}

  virtual void SetChangeMark(CXFA_FFDoc* hDoc) = 0;
  virtual void InvalidateRect(CXFA_FFPageView* pPageView,
                              const CFX_RectF& rt) = 0;
  virtual void DisplayCaret(CXFA_FFWidget* hWidget,
                            bool bVisible,
                            const CFX_RectF* pRtAnchor) = 0;
  virtual bool GetPopupPos(CXFA_FFWidget* hWidget,
                           float fMinPopup,
                           float fMaxPopup,
                           const CFX_RectF& rtAnchor,
                           CFX_RectF& rtPopup) = 0;
  virtual bool PopupMenu(CXFA_FFWidget* hWidget, CFX_PointF ptPopup) = 0;
  virtual void PageViewEvent(CXFA_FFPageView* pPageView, uint32_t dwFlags) = 0;
  virtual void WidgetPostAdd(CXFA_FFWidget* hWidget,
                             CXFA_WidgetAcc* pWidgetData) = 0;
  virtual void WidgetPreRemove(CXFA_FFWidget* hWidget,
                               CXFA_WidgetAcc* pWidgetData) = 0;

  virtual int32_t CountPages(CXFA_FFDoc* hDoc) = 0;
  virtual int32_t GetCurrentPage(CXFA_FFDoc* hDoc) = 0;
  virtual void SetCurrentPage(CXFA_FFDoc* hDoc, int32_t iCurPage) = 0;
  virtual bool IsCalculationsEnabled(CXFA_FFDoc* hDoc) = 0;
  virtual void SetCalculationsEnabled(CXFA_FFDoc* hDoc, bool bEnabled) = 0;
  virtual void GetTitle(CXFA_FFDoc* hDoc, CFX_WideString& wsTitle) = 0;
  virtual void SetTitle(CXFA_FFDoc* hDoc, const CFX_WideString& wsTitle) = 0;
  virtual void ExportData(CXFA_FFDoc* hDoc,
                          const CFX_WideString& wsFilePath,
                          bool bXDP) = 0;
  virtual void GotoURL(CXFA_FFDoc* hDoc, const CFX_WideString& bsURL) = 0;
  virtual bool IsValidationsEnabled(CXFA_FFDoc* hDoc) = 0;
  virtual void SetValidationsEnabled(CXFA_FFDoc* hDoc, bool bEnabled) = 0;
  virtual void SetFocusWidget(CXFA_FFDoc* hDoc, CXFA_FFWidget* hWidget) = 0;
  virtual void Print(CXFA_FFDoc* hDoc,
                     int32_t nStartPage,
                     int32_t nEndPage,
                     uint32_t dwOptions) = 0;
  virtual FX_ARGB GetHighlightColor(CXFA_FFDoc* hDoc) = 0;

  virtual bool SubmitData(CXFA_FFDoc* hDoc, CXFA_Submit submit) = 0;
  virtual bool GetGlobalProperty(CXFA_FFDoc* hDoc,
                                 const CFX_ByteStringC& szPropName,
                                 CFXJSE_Value* pValue) = 0;
  virtual bool SetGlobalProperty(CXFA_FFDoc* hDoc,
                                 const CFX_ByteStringC& szPropName,
                                 CFXJSE_Value* pValue) = 0;
  virtual CFX_RetainPtr<IFX_SeekableReadStream> OpenLinkedFile(
      CXFA_FFDoc* hDoc,
      const CFX_WideString& wsLink) = 0;
};

class IXFA_WidgetIterator {
 public:
  virtual ~IXFA_WidgetIterator() {}

  virtual void Reset() = 0;
  virtual CXFA_FFWidget* MoveToFirst() = 0;
  virtual CXFA_FFWidget* MoveToLast() = 0;
  virtual CXFA_FFWidget* MoveToNext() = 0;
  virtual CXFA_FFWidget* MoveToPrevious() = 0;
  virtual CXFA_FFWidget* GetCurrentWidget() = 0;
  virtual bool SetCurrentWidget(CXFA_FFWidget* hWidget) = 0;
};

#endif  // XFA_FXFA_FXFA_H_

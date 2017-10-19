// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "public/fpdfview.h"

#include <memory>
#include <utility>
#include <vector>

#include "core/fpdfapi/cpdf_modulemgr.h"
#include "core/fpdfapi/cpdf_pagerendercontext.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/fpdf_parser_decode.h"
#include "core/fpdfapi/render/cpdf_progressiverenderer.h"
#include "core/fpdfapi/render/cpdf_renderoptions.h"
#include "core/fpdfdoc/cpdf_annotlist.h"
#include "core/fpdfdoc/cpdf_nametree.h"
#include "core/fpdfdoc/cpdf_occontext.h"
#include "core/fpdfdoc/cpdf_viewerpreferences.h"
#include "core/fxcodec/fx_codec.h"
#include "core/fxcrt/fx_memory.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/fx_stream.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "core/fxge/cfx_gemodule.h"
#include "fpdfsdk/cpdfsdk_formfillenvironment.h"
#include "fpdfsdk/cpdfsdk_pageview.h"
#include "fpdfsdk/fsdk_define.h"
#include "fpdfsdk/fsdk_pauseadapter.h"
#include "fpdfsdk/javascript/ijs_runtime.h"
#include "public/fpdf_edit.h"
#include "public/fpdf_ext.h"
#include "public/fpdf_progressive.h"
#include "third_party/base/allocator/partition_allocator/partition_alloc.h"
#include "third_party/base/numerics/safe_conversions_impl.h"
#include "third_party/base/ptr_util.h"

#ifdef PDF_ENABLE_XFA
#include "fpdfsdk/fpdfxfa/cpdfxfa_context.h"
#include "fpdfsdk/fpdfxfa/cpdfxfa_page.h"
#include "fpdfsdk/fpdfxfa/cxfa_fwladaptertimermgr.h"
#include "fxbarcode/BC_Library.h"
#include "public/fpdf_formfill.h"
#endif  // PDF_ENABLE_XFA

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
#include "core/fxge/cfx_windowsrenderdevice.h"

// These checks are here because core/ and public/ cannot depend on each other.
static_assert(WindowsPrintMode::kModeEmf == FPDF_PRINTMODE_EMF,
              "WindowsPrintMode::kModeEmf value mismatch");
static_assert(WindowsPrintMode::kModeTextOnly == FPDF_PRINTMODE_TEXTONLY,
              "WindowsPrintMode::kModeTextOnly value mismatch");
static_assert(WindowsPrintMode::kModePostScript2 == FPDF_PRINTMODE_POSTSCRIPT2,
              "WindowsPrintMode::kModePostScript2 value mismatch");
static_assert(WindowsPrintMode::kModePostScript3 == FPDF_PRINTMODE_POSTSCRIPT3,
              "WindowsPrintMode::kModePostScript3 value mismatch");
#endif

namespace {

bool g_bLibraryInitialized = false;

void RenderPageImpl(CPDF_PageRenderContext* pContext,
                    CPDF_Page* pPage,
                    const CFX_Matrix& matrix,
                    const FX_RECT& clipping_rect,
                    int flags,
                    bool bNeedToRestore,
                    IFSDK_PAUSE_Adapter* pause) {
  if (!pContext->m_pOptions)
    pContext->m_pOptions = pdfium::MakeUnique<CPDF_RenderOptions>();

  if (flags & FPDF_LCD_TEXT)
    pContext->m_pOptions->m_Flags |= RENDER_CLEARTYPE;
  else
    pContext->m_pOptions->m_Flags &= ~RENDER_CLEARTYPE;

  if (flags & FPDF_NO_NATIVETEXT)
    pContext->m_pOptions->m_Flags |= RENDER_NO_NATIVETEXT;
  if (flags & FPDF_RENDER_LIMITEDIMAGECACHE)
    pContext->m_pOptions->m_Flags |= RENDER_LIMITEDIMAGECACHE;
  if (flags & FPDF_RENDER_FORCEHALFTONE)
    pContext->m_pOptions->m_Flags |= RENDER_FORCE_HALFTONE;
#ifndef PDF_ENABLE_XFA
  if (flags & FPDF_RENDER_NO_SMOOTHTEXT)
    pContext->m_pOptions->m_Flags |= RENDER_NOTEXTSMOOTH;
  if (flags & FPDF_RENDER_NO_SMOOTHIMAGE)
    pContext->m_pOptions->m_Flags |= RENDER_NOIMAGESMOOTH;
  if (flags & FPDF_RENDER_NO_SMOOTHPATH)
    pContext->m_pOptions->m_Flags |= RENDER_NOPATHSMOOTH;
#endif  // PDF_ENABLE_XFA

  // Grayscale output
  if (flags & FPDF_GRAYSCALE)
    pContext->m_pOptions->m_ColorMode = CPDF_RenderOptions::kGray;

  const CPDF_OCContext::UsageType usage =
      (flags & FPDF_PRINTING) ? CPDF_OCContext::Print : CPDF_OCContext::View;
  pContext->m_pOptions->m_pOCContext =
      pdfium::MakeRetain<CPDF_OCContext>(pPage->m_pDocument.Get(), usage);

  pContext->m_pDevice->SaveState();
  pContext->m_pDevice->SetClip_Rect(clipping_rect);
  pContext->m_pContext = pdfium::MakeUnique<CPDF_RenderContext>(pPage);
  pContext->m_pContext->AppendLayer(pPage, &matrix);

  if (flags & FPDF_ANNOT) {
    pContext->m_pAnnots = pdfium::MakeUnique<CPDF_AnnotList>(pPage);
    bool bPrinting = pContext->m_pDevice->GetDeviceClass() != FXDC_DISPLAY;
    pContext->m_pAnnots->DisplayAnnots(pPage, pContext->m_pContext.get(),
                                       bPrinting, &matrix, false, nullptr);
  }

  pContext->m_pRenderer = pdfium::MakeUnique<CPDF_ProgressiveRenderer>(
      pContext->m_pContext.get(), pContext->m_pDevice.get(),
      pContext->m_pOptions.get());
  pContext->m_pRenderer->Start(pause);
  if (bNeedToRestore)
    pContext->m_pDevice->RestoreState(false);
}

class CPDF_CustomAccess final : public IFX_SeekableReadStream {
 public:
  template <typename T, typename... Args>
  friend CFX_RetainPtr<T> pdfium::MakeRetain(Args&&... args);

  // IFX_SeekableReadStream
  FX_FILESIZE GetSize() override;
  bool ReadBlock(void* buffer, FX_FILESIZE offset, size_t size) override;

 private:
  explicit CPDF_CustomAccess(FPDF_FILEACCESS* pFileAccess);

  FPDF_FILEACCESS m_FileAccess;
};

CPDF_CustomAccess::CPDF_CustomAccess(FPDF_FILEACCESS* pFileAccess)
    : m_FileAccess(*pFileAccess) {}

FX_FILESIZE CPDF_CustomAccess::GetSize() {
  return m_FileAccess.m_FileLen;
}

bool CPDF_CustomAccess::ReadBlock(void* buffer,
                                  FX_FILESIZE offset,
                                  size_t size) {
  if (offset < 0)
    return false;

  FX_SAFE_FILESIZE newPos = pdfium::base::checked_cast<FX_FILESIZE>(size);
  newPos += offset;
  if (!newPos.IsValid() ||
      newPos.ValueOrDie() > static_cast<FX_FILESIZE>(m_FileAccess.m_FileLen)) {
    return false;
  }
  return !!m_FileAccess.m_GetBlock(m_FileAccess.m_Param, offset,
                                   static_cast<uint8_t*>(buffer), size);
}

#ifdef PDF_ENABLE_XFA
class CFPDF_FileStream : public IFX_SeekableStream {
 public:
  template <typename T, typename... Args>
  friend CFX_RetainPtr<T> pdfium::MakeRetain(Args&&... args);

  ~CFPDF_FileStream() override;

  // IFX_SeekableStream:
  FX_FILESIZE GetSize() override;
  bool IsEOF() override;
  FX_FILESIZE GetPosition() override;
  bool ReadBlock(void* buffer, FX_FILESIZE offset, size_t size) override;
  size_t ReadBlock(void* buffer, size_t size) override;
  bool WriteBlock(const void* buffer, FX_FILESIZE offset, size_t size) override;
  bool Flush() override;

  void SetPosition(FX_FILESIZE pos) { m_nCurPos = pos; }

 protected:
  explicit CFPDF_FileStream(FPDF_FILEHANDLER* pFS);

  FPDF_FILEHANDLER* m_pFS;
  FX_FILESIZE m_nCurPos;
};

CFPDF_FileStream::CFPDF_FileStream(FPDF_FILEHANDLER* pFS) {
  m_pFS = pFS;
  m_nCurPos = 0;
}

CFPDF_FileStream::~CFPDF_FileStream() {
  if (m_pFS && m_pFS->Release)
    m_pFS->Release(m_pFS->clientData);
}

FX_FILESIZE CFPDF_FileStream::GetSize() {
  if (m_pFS && m_pFS->GetSize)
    return (FX_FILESIZE)m_pFS->GetSize(m_pFS->clientData);
  return 0;
}

bool CFPDF_FileStream::IsEOF() {
  return m_nCurPos >= GetSize();
}

FX_FILESIZE CFPDF_FileStream::GetPosition() {
  return m_nCurPos;
}

bool CFPDF_FileStream::ReadBlock(void* buffer,
                                 FX_FILESIZE offset,
                                 size_t size) {
  if (!buffer || !size || !m_pFS->ReadBlock)
    return false;

  if (m_pFS->ReadBlock(m_pFS->clientData, (FPDF_DWORD)offset, buffer,
                       (FPDF_DWORD)size) == 0) {
    m_nCurPos = offset + size;
    return true;
  }
  return false;
}

size_t CFPDF_FileStream::ReadBlock(void* buffer, size_t size) {
  if (!buffer || !size || !m_pFS->ReadBlock)
    return 0;

  FX_FILESIZE nSize = GetSize();
  if (m_nCurPos >= nSize)
    return 0;
  FX_FILESIZE dwAvail = nSize - m_nCurPos;
  if (dwAvail < (FX_FILESIZE)size)
    size = (size_t)dwAvail;
  if (m_pFS->ReadBlock(m_pFS->clientData, (FPDF_DWORD)m_nCurPos, buffer,
                       (FPDF_DWORD)size) == 0) {
    m_nCurPos += size;
    return size;
  }

  return 0;
}

bool CFPDF_FileStream::WriteBlock(const void* buffer,
                                  FX_FILESIZE offset,
                                  size_t size) {
  if (!m_pFS || !m_pFS->WriteBlock)
    return false;

  if (m_pFS->WriteBlock(m_pFS->clientData, (FPDF_DWORD)offset, buffer,
                        (FPDF_DWORD)size) == 0) {
    m_nCurPos = offset + size;
    return true;
  }
  return false;
}

bool CFPDF_FileStream::Flush() {
  if (!m_pFS || !m_pFS->Flush)
    return true;

  return m_pFS->Flush(m_pFS->clientData) == 0;
}
#endif  // PDF_ENABLE_XFA

FPDF_DOCUMENT LoadDocumentImpl(
    const CFX_RetainPtr<IFX_SeekableReadStream>& pFileAccess,
    FPDF_BYTESTRING password) {
  if (!pFileAccess) {
    ProcessParseError(CPDF_Parser::FILE_ERROR);
    return nullptr;
  }

  auto pParser = pdfium::MakeUnique<CPDF_Parser>();
  pParser->SetPassword(password);

  auto pDocument = pdfium::MakeUnique<CPDF_Document>(std::move(pParser));
  CPDF_Parser::Error error =
      pDocument->GetParser()->StartParse(pFileAccess, pDocument.get());
  if (error != CPDF_Parser::SUCCESS) {
    ProcessParseError(error);
    return nullptr;
  }
  CheckUnSupportError(pDocument.get(), error);
  return FPDFDocumentFromCPDFDocument(pDocument.release());
}

}  // namespace

UnderlyingDocumentType* UnderlyingFromFPDFDocument(FPDF_DOCUMENT doc) {
  return static_cast<UnderlyingDocumentType*>(doc);
}

FPDF_DOCUMENT FPDFDocumentFromUnderlying(UnderlyingDocumentType* doc) {
  return static_cast<FPDF_DOCUMENT>(doc);
}

UnderlyingPageType* UnderlyingFromFPDFPage(FPDF_PAGE page) {
  return static_cast<UnderlyingPageType*>(page);
}

CPDF_Document* CPDFDocumentFromFPDFDocument(FPDF_DOCUMENT doc) {
#ifdef PDF_ENABLE_XFA
  return doc ? UnderlyingFromFPDFDocument(doc)->GetPDFDoc() : nullptr;
#else   // PDF_ENABLE_XFA
  return UnderlyingFromFPDFDocument(doc);
#endif  // PDF_ENABLE_XFA
}

FPDF_DOCUMENT FPDFDocumentFromCPDFDocument(CPDF_Document* doc) {
#ifdef PDF_ENABLE_XFA
  return doc ? FPDFDocumentFromUnderlying(
                   new CPDFXFA_Context(pdfium::WrapUnique(doc)))
             : nullptr;
#else   // PDF_ENABLE_XFA
  return FPDFDocumentFromUnderlying(doc);
#endif  // PDF_ENABLE_XFA
}

CPDF_Page* CPDFPageFromFPDFPage(FPDF_PAGE page) {
#ifdef PDF_ENABLE_XFA
  return page ? UnderlyingFromFPDFPage(page)->GetPDFPage() : nullptr;
#else   // PDF_ENABLE_XFA
  return UnderlyingFromFPDFPage(page);
#endif  // PDF_ENABLE_XFA
}

CPDF_PathObject* CPDFPathObjectFromFPDFPageObject(FPDF_PAGEOBJECT page_object) {
  return static_cast<CPDF_PathObject*>(page_object);
}

CPDF_PageObject* CPDFPageObjectFromFPDFPageObject(FPDF_PAGEOBJECT page_object) {
  return static_cast<CPDF_PageObject*>(page_object);
}

CPDF_Object* CPDFObjectFromFPDFAttachment(FPDF_ATTACHMENT attachment) {
  return static_cast<CPDF_Object*>(attachment);
}

CFX_ByteString CFXByteStringFromFPDFWideString(FPDF_WIDESTRING wide_string) {
  return CFX_WideString::FromUTF16LE(wide_string,
                                     CFX_WideString::WStringLength(wide_string))
      .UTF8Encode();
}

CFX_DIBitmap* CFXBitmapFromFPDFBitmap(FPDF_BITMAP bitmap) {
  return static_cast<CFX_DIBitmap*>(bitmap);
}

unsigned long Utf16EncodeMaybeCopyAndReturnLength(const CFX_WideString& text,
                                                  void* buffer,
                                                  unsigned long buflen) {
  CFX_ByteString encoded_text = text.UTF16LE_Encode();
  unsigned long len = encoded_text.GetLength();
  if (buffer && len <= buflen)
    memcpy(buffer, encoded_text.c_str(), len);
  return len;
}

unsigned long DecodeStreamMaybeCopyAndReturnLength(const CPDF_Stream* stream,
                                                   void* buffer,
                                                   unsigned long buflen) {
  ASSERT(stream);
  uint8_t* data = stream->GetRawData();
  uint32_t len = stream->GetRawSize();
  CPDF_Dictionary* dict = stream->GetDict();
  CPDF_Object* decoder = dict ? dict->GetDirectObjectFor("Filter") : nullptr;
  if (decoder && (decoder->IsArray() || decoder->IsName())) {
    // Decode the stream if one or more stream filters are specified.
    uint8_t* decoded_data = nullptr;
    uint32_t decoded_len = 0;
    CFX_ByteString dummy_last_decoder;
    CPDF_Dictionary* dummy_last_param;
    if (PDF_DataDecode(data, len, dict, dict->GetIntegerFor("DL"), false,
                       &decoded_data, &decoded_len, &dummy_last_decoder,
                       &dummy_last_param)) {
      if (buffer && buflen >= decoded_len)
        memcpy(buffer, decoded_data, decoded_len);

      // Free the buffer for the decoded data if it was allocated by
      // PDF_DataDecode(). Note that for images with a single image-specific
      // filter, |decoded_data| is directly assigned to be |data|, so
      // |decoded_data| does not need to be freed.
      if (decoded_data != data)
        FX_Free(decoded_data);

      return decoded_len;
    }
  }
  // Copy the raw data and return its length if there is no valid filter
  // specified or if decoding failed.
  if (buffer && buflen >= len)
    memcpy(buffer, data, len);

  return len;
}

CFX_RetainPtr<IFX_SeekableReadStream> MakeSeekableReadStream(
    FPDF_FILEACCESS* pFileAccess) {
  return pdfium::MakeRetain<CPDF_CustomAccess>(pFileAccess);
}

#ifdef PDF_ENABLE_XFA
CFX_RetainPtr<IFX_SeekableStream> MakeSeekableStream(
    FPDF_FILEHANDLER* pFilehandler) {
  return pdfium::MakeRetain<CFPDF_FileStream>(pFilehandler);
}
#endif  // PDF_ENABLE_XFA

// 0 bit: FPDF_POLICY_MACHINETIME_ACCESS
static uint32_t foxit_sandbox_policy = 0xFFFFFFFF;

void FSDK_SetSandBoxPolicy(FPDF_DWORD policy, FPDF_BOOL enable) {
  switch (policy) {
    case FPDF_POLICY_MACHINETIME_ACCESS: {
      if (enable)
        foxit_sandbox_policy |= 0x01;
      else
        foxit_sandbox_policy &= 0xFFFFFFFE;
    } break;
    default:
      break;
  }
}

FPDF_BOOL FSDK_IsSandBoxPolicyEnabled(FPDF_DWORD policy) {
  switch (policy) {
    case FPDF_POLICY_MACHINETIME_ACCESS:
      return !!(foxit_sandbox_policy & 0x01);
    default:
      return false;
  }
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_InitLibrary() {
  FPDF_InitLibraryWithConfig(nullptr);
}

FPDF_EXPORT void FPDF_CALLCONV
FPDF_InitLibraryWithConfig(const FPDF_LIBRARY_CONFIG* cfg) {
  if (g_bLibraryInitialized)
    return;

  FXMEM_InitializePartitionAlloc();

  CFX_GEModule* pModule = CFX_GEModule::Get();
  pModule->Init(cfg ? cfg->m_pUserFontPaths : nullptr);

  CPDF_ModuleMgr* pModuleMgr = CPDF_ModuleMgr::Get();
  pModuleMgr->Init();

#ifdef PDF_ENABLE_XFA
  FXJSE_Initialize();
  BC_Library_Init();
#endif  // PDF_ENABLE_XFA
  if (cfg && cfg->version >= 2)
    IJS_Runtime::Initialize(cfg->m_v8EmbedderSlot, cfg->m_pIsolate);

  g_bLibraryInitialized = true;
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_DestroyLibrary() {
  if (!g_bLibraryInitialized)
    return;

#ifdef PDF_ENABLE_XFA
  BC_Library_Destroy();
  FXJSE_Finalize();
#endif  // PDF_ENABLE_XFA

  CPDF_ModuleMgr::Destroy();
  CFX_GEModule::Destroy();

  IJS_Runtime::Destroy();

  g_bLibraryInitialized = false;
}

#ifndef _WIN32
int g_LastError;
void SetLastError(int err) {
  g_LastError = err;
}

int GetLastError() {
  return g_LastError;
}
#endif  // _WIN32

void ProcessParseError(CPDF_Parser::Error err) {
  uint32_t err_code = FPDF_ERR_SUCCESS;
  // Translate FPDFAPI error code to FPDFVIEW error code
  switch (err) {
    case CPDF_Parser::SUCCESS:
      err_code = FPDF_ERR_SUCCESS;
      break;
    case CPDF_Parser::FILE_ERROR:
      err_code = FPDF_ERR_FILE;
      break;
    case CPDF_Parser::FORMAT_ERROR:
      err_code = FPDF_ERR_FORMAT;
      break;
    case CPDF_Parser::PASSWORD_ERROR:
      err_code = FPDF_ERR_PASSWORD;
      break;
    case CPDF_Parser::HANDLER_ERROR:
      err_code = FPDF_ERR_SECURITY;
      break;
  }
  SetLastError(err_code);
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_SetSandBoxPolicy(FPDF_DWORD policy,
                                                     FPDF_BOOL enable) {
  return FSDK_SetSandBoxPolicy(policy, enable);
}

#if defined(_WIN32)
#if defined(PDFIUM_PRINT_TEXT_WITH_GDI)
FPDF_EXPORT void FPDF_CALLCONV
FPDF_SetTypefaceAccessibleFunc(PDFiumEnsureTypefaceCharactersAccessible func) {
  g_pdfium_typeface_accessible_func = func;
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_SetPrintTextWithGDI(FPDF_BOOL use_gdi) {
  g_pdfium_print_text_with_gdi = !!use_gdi;
}
#endif  // PDFIUM_PRINT_TEXT_WITH_GDI

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_SetPrintPostscriptLevel(int postscript_level) {
  return postscript_level != 1 && FPDF_SetPrintMode(postscript_level);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDF_SetPrintMode(int mode) {
  if (mode < FPDF_PRINTMODE_EMF || mode > FPDF_PRINTMODE_POSTSCRIPT3)
    return FALSE;
  g_pdfium_print_mode = mode;
  return TRUE;
}
#endif  // defined(_WIN32)

FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
FPDF_LoadDocument(FPDF_STRING file_path, FPDF_BYTESTRING password) {
  // NOTE: the creation of the file needs to be by the embedder on the
  // other side of this API.
  return LoadDocumentImpl(
      IFX_SeekableReadStream::CreateFromFilename((const char*)file_path),
      password);
}

#ifdef PDF_ENABLE_XFA
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDF_HasXFAField(FPDF_DOCUMENT document,
                                                     int* docType) {
  if (!document)
    return false;

  const CPDF_Document* pDoc =
      static_cast<CPDFXFA_Context*>(document)->GetPDFDoc();
  if (!pDoc)
    return false;

  const CPDF_Dictionary* pRoot = pDoc->GetRoot();
  if (!pRoot)
    return false;

  CPDF_Dictionary* pAcroForm = pRoot->GetDictFor("AcroForm");
  if (!pAcroForm)
    return false;

  CPDF_Object* pXFA = pAcroForm->GetObjectFor("XFA");
  if (!pXFA)
    return false;

  bool bDynamicXFA = pRoot->GetBooleanFor("NeedsRendering", false);
  *docType = bDynamicXFA ? DOCTYPE_DYNAMIC_XFA : DOCTYPE_STATIC_XFA;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDF_LoadXFA(FPDF_DOCUMENT document) {
  return document && static_cast<CPDFXFA_Context*>(document)->LoadXFADoc();
}
#endif  // PDF_ENABLE_XFA

class CMemFile final : public IFX_SeekableReadStream {
 public:
  static CFX_RetainPtr<CMemFile> Create(uint8_t* pBuf, FX_FILESIZE size) {
    return CFX_RetainPtr<CMemFile>(new CMemFile(pBuf, size));
  }

  FX_FILESIZE GetSize() override { return m_size; }
  bool ReadBlock(void* buffer, FX_FILESIZE offset, size_t size) override {
    if (offset < 0)
      return false;

    FX_SAFE_FILESIZE newPos = pdfium::base::checked_cast<FX_FILESIZE>(size);
    newPos += offset;
    if (!newPos.IsValid() || newPos.ValueOrDie() > m_size)
      return false;

    memcpy(buffer, m_pBuf + offset, size);
    return true;
  }

 private:
  CMemFile(uint8_t* pBuf, FX_FILESIZE size) : m_pBuf(pBuf), m_size(size) {}

  uint8_t* const m_pBuf;
  const FX_FILESIZE m_size;
};

FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
FPDF_LoadMemDocument(const void* data_buf, int size, FPDF_BYTESTRING password) {
  return LoadDocumentImpl(CMemFile::Create((uint8_t*)data_buf, size), password);
}

FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
FPDF_LoadCustomDocument(FPDF_FILEACCESS* pFileAccess,
                        FPDF_BYTESTRING password) {
  return LoadDocumentImpl(pdfium::MakeRetain<CPDF_CustomAccess>(pFileAccess),
                          password);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDF_GetFileVersion(FPDF_DOCUMENT doc,
                                                        int* fileVersion) {
  if (!fileVersion)
    return false;

  *fileVersion = 0;
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  if (!pDoc)
    return false;

  const CPDF_Parser* pParser = pDoc->GetParser();
  if (!pParser)
    return false;

  *fileVersion = pParser->GetFileVersion();
  return true;
}

// jabdelmalek: changed return type from uint32_t to build on Linux (and match
// header).
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDF_GetDocPermissions(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  // https://bugs.chromium.org/p/pdfium/issues/detail?id=499
  if (!pDoc) {
#ifndef PDF_ENABLE_XFA
    return 0;
#else   // PDF_ENABLE_XFA
    return 0xFFFFFFFF;
#endif  // PDF_ENABLE_XFA
  }

  return pDoc->GetUserPermissions();
}

FPDF_EXPORT int FPDF_CALLCONV
FPDF_GetSecurityHandlerRevision(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc || !pDoc->GetParser())
    return -1;

  CPDF_Dictionary* pDict = pDoc->GetParser()->GetEncryptDict();
  return pDict ? pDict->GetIntegerFor("R") : -1;
}

FPDF_EXPORT int FPDF_CALLCONV FPDF_GetPageCount(FPDF_DOCUMENT document) {
  UnderlyingDocumentType* pDoc = UnderlyingFromFPDFDocument(document);
  return pDoc ? pDoc->GetPageCount() : 0;
}

FPDF_EXPORT FPDF_PAGE FPDF_CALLCONV FPDF_LoadPage(FPDF_DOCUMENT document,
                                                  int page_index) {
  UnderlyingDocumentType* pDoc = UnderlyingFromFPDFDocument(document);
  if (!pDoc)
    return nullptr;

  if (page_index < 0 || page_index >= pDoc->GetPageCount())
    return nullptr;

#ifdef PDF_ENABLE_XFA
  return pDoc->GetXFAPage(page_index).Leak();
#else   // PDF_ENABLE_XFA
  CPDF_Dictionary* pDict = pDoc->GetPage(page_index);
  if (!pDict)
    return nullptr;

  CPDF_Page* pPage = new CPDF_Page(pDoc, pDict, true);
  pPage->ParseContent();
  return pPage;
#endif  // PDF_ENABLE_XFA
}

FPDF_EXPORT double FPDF_CALLCONV FPDF_GetPageWidth(FPDF_PAGE page) {
  UnderlyingPageType* pPage = UnderlyingFromFPDFPage(page);
  return pPage ? pPage->GetPageWidth() : 0.0;
}

FPDF_EXPORT double FPDF_CALLCONV FPDF_GetPageHeight(FPDF_PAGE page) {
  UnderlyingPageType* pPage = UnderlyingFromFPDFPage(page);
  return pPage ? pPage->GetPageHeight() : 0.0;
}

#if defined(_WIN32)
namespace {

const double kEpsilonSize = 0.01f;

void GetScaling(CPDF_Page* pPage,
                int size_x,
                int size_y,
                int rotate,
                double* scale_x,
                double* scale_y) {
  ASSERT(pPage);
  ASSERT(scale_x);
  ASSERT(scale_y);
  double page_width = pPage->GetPageWidth();
  double page_height = pPage->GetPageHeight();
  if (page_width < kEpsilonSize || page_height < kEpsilonSize)
    return;

  if (rotate % 2 == 0) {
    *scale_x = size_x / page_width;
    *scale_y = size_y / page_height;
  } else {
    *scale_x = size_y / page_width;
    *scale_y = size_x / page_height;
  }
}

FX_RECT GetMaskDimensionsAndOffsets(CPDF_Page* pPage,
                                    int start_x,
                                    int start_y,
                                    int size_x,
                                    int size_y,
                                    int rotate,
                                    const CFX_FloatRect& mask_box) {
  double scale_x = 0.0f;
  double scale_y = 0.0f;
  GetScaling(pPage, size_x, size_y, rotate, &scale_x, &scale_y);
  if (scale_x < kEpsilonSize || scale_y < kEpsilonSize)
    return FX_RECT();

  // Compute sizes in page points. Round down to catch the entire bitmap.
  int start_x_bm = static_cast<int>(mask_box.left * scale_x);
  int start_y_bm = static_cast<int>(mask_box.bottom * scale_y);
  int size_x_bm = static_cast<int>(mask_box.right * scale_x + 1.0f) -
                  static_cast<int>(mask_box.left * scale_x);
  int size_y_bm = static_cast<int>(mask_box.top * scale_y + 1.0f) -
                  static_cast<int>(mask_box.bottom * scale_y);

  // Get page rotation
  int page_rotation = pPage->GetPageRotation();

  // Compute offsets
  int offset_x = 0;
  int offset_y = 0;
  if (size_x > size_y)
    std::swap(size_x_bm, size_y_bm);

  switch ((rotate + page_rotation) % 4) {
    case 0:
      offset_x = start_x_bm + start_x;
      offset_y = start_y + size_y - size_y_bm - start_y_bm;
      break;
    case 1:
      offset_x = start_y_bm + start_x;
      offset_y = start_x_bm + start_y;
      break;
    case 2:
      offset_x = start_x + size_x - size_x_bm - start_x_bm;
      offset_y = start_y_bm + start_y;
      break;
    case 3:
      offset_x = start_x + size_x - size_x_bm - start_y_bm;
      offset_y = start_y + size_y - size_y_bm - start_x_bm;
      break;
  }
  return FX_RECT(offset_x, offset_y, offset_x + size_x_bm,
                 offset_y + size_y_bm);
}

// Get a bitmap of just the mask section defined by |mask_box| from a full page
// bitmap |pBitmap|.
CFX_RetainPtr<CFX_DIBitmap> GetMaskBitmap(CPDF_Page* pPage,
                                          int start_x,
                                          int start_y,
                                          int size_x,
                                          int size_y,
                                          int rotate,
                                          CFX_RetainPtr<CFX_DIBitmap>& pSrc,
                                          const CFX_FloatRect& mask_box,
                                          FX_RECT* bitmap_area) {
  ASSERT(bitmap_area);
  *bitmap_area = GetMaskDimensionsAndOffsets(pPage, start_x, start_y, size_x,
                                             size_y, rotate, mask_box);
  if (bitmap_area->IsEmpty())
    return nullptr;

  // Create a new bitmap to transfer part of the page bitmap to.
  CFX_RetainPtr<CFX_DIBitmap> pDst = pdfium::MakeRetain<CFX_DIBitmap>();
  pDst->Create(bitmap_area->Width(), bitmap_area->Height(), FXDIB_Argb);
  pDst->Clear(0x00ffffff);
  pDst->TransferBitmap(0, 0, bitmap_area->Width(), bitmap_area->Height(), pSrc,
                       bitmap_area->left, bitmap_area->top);
  return pDst;
}

void RenderBitmap(CFX_RenderDevice* device,
                  const CFX_RetainPtr<CFX_DIBitmap>& pSrc,
                  const FX_RECT& mask_area) {
  int size_x_bm = mask_area.Width();
  int size_y_bm = mask_area.Height();
  if (size_x_bm == 0 || size_y_bm == 0)
    return;

  // Create a new bitmap from the old one
  CFX_RetainPtr<CFX_DIBitmap> pDst = pdfium::MakeRetain<CFX_DIBitmap>();
  pDst->Create(size_x_bm, size_y_bm, FXDIB_Rgb32);
  pDst->Clear(0xffffffff);
  pDst->CompositeBitmap(0, 0, size_x_bm, size_y_bm, pSrc, 0, 0,
                        FXDIB_BLEND_NORMAL, nullptr, false);

  if (device->GetDeviceCaps(FXDC_DEVICE_CLASS) == FXDC_PRINTER) {
    device->StretchDIBits(pDst, mask_area.left, mask_area.top, size_x_bm,
                          size_y_bm);
  } else {
    device->SetDIBits(pDst, mask_area.left, mask_area.top);
  }
}

}  // namespace

FPDF_EXPORT void FPDF_CALLCONV FPDF_RenderPage(HDC dc,
                                               FPDF_PAGE page,
                                               int start_x,
                                               int start_y,
                                               int size_x,
                                               int size_y,
                                               int rotate,
                                               int flags) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return;
  pPage->SetRenderContext(pdfium::MakeUnique<CPDF_PageRenderContext>());
  CPDF_PageRenderContext* pContext = pPage->GetRenderContext();

  CFX_RetainPtr<CFX_DIBitmap> pBitmap;
  // Don't render the full page to bitmap for a mask unless there are a lot
  // of masks. Full page bitmaps result in large spool sizes, so they should
  // only be used when necessary. For large numbers of masks, rendering each
  // individually is inefficient and unlikely to significantly improve spool
  // size.
  const bool bNewBitmap =
      pPage->BackgroundAlphaNeeded() ||
      (pPage->HasImageMask() && pPage->GetMaskBoundingBoxes().size() > 100);
  const bool bHasMask = pPage->HasImageMask() && !bNewBitmap;
  if (bNewBitmap || bHasMask) {
    pBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
    pBitmap->Create(size_x, size_y, FXDIB_Argb);
    pBitmap->Clear(0x00ffffff);
    CFX_DefaultRenderDevice* pDevice = new CFX_DefaultRenderDevice;
    pContext->m_pDevice = pdfium::WrapUnique(pDevice);
    pDevice->Attach(pBitmap, false, nullptr, false);
    if (bHasMask) {
      pContext->m_pOptions = pdfium::MakeUnique<CPDF_RenderOptions>();
      pContext->m_pOptions->m_Flags |= RENDER_BREAKFORMASKS;
    }
  } else {
    pContext->m_pDevice = pdfium::MakeUnique<CFX_WindowsRenderDevice>(dc);
  }

  FPDF_RenderPage_Retail(pContext, page, start_x, start_y, size_x, size_y,
                         rotate, flags, true, nullptr);

  if (bHasMask) {
    // Finish rendering the page to bitmap and copy the correct segments
    // of the page to individual image mask bitmaps.
    const std::vector<CFX_FloatRect>& mask_boxes =
        pPage->GetMaskBoundingBoxes();
    std::vector<FX_RECT> bitmap_areas(mask_boxes.size());
    std::vector<CFX_RetainPtr<CFX_DIBitmap>> bitmaps(mask_boxes.size());
    for (size_t i = 0; i < mask_boxes.size(); i++) {
      bitmaps[i] =
          GetMaskBitmap(pPage, start_x, start_y, size_x, size_y, rotate,
                        pBitmap, mask_boxes[i], &bitmap_areas[i]);
      pContext->m_pRenderer->Continue(nullptr);
    }

    // Reset rendering context
    pPage->SetRenderContext(nullptr);

    // Begin rendering to the printer. Add flag to indicate the renderer should
    // pause after each image mask.
    pPage->SetRenderContext(pdfium::MakeUnique<CPDF_PageRenderContext>());
    pContext = pPage->GetRenderContext();
    pContext->m_pDevice = pdfium::MakeUnique<CFX_WindowsRenderDevice>(dc);
    pContext->m_pOptions = pdfium::MakeUnique<CPDF_RenderOptions>();
    pContext->m_pOptions->m_Flags |= RENDER_BREAKFORMASKS;
    FPDF_RenderPage_Retail(pContext, page, start_x, start_y, size_x, size_y,
                           rotate, flags, true, nullptr);

    // Render masks
    for (size_t i = 0; i < mask_boxes.size(); i++) {
      // Render the bitmap for the mask and free the bitmap.
      if (bitmaps[i]) {  // will be null if mask has zero area
        RenderBitmap(pContext->m_pDevice.get(), bitmaps[i], bitmap_areas[i]);
      }
      // Render the next portion of page.
      pContext->m_pRenderer->Continue(nullptr);
    }
  } else if (bNewBitmap) {
    CFX_WindowsRenderDevice WinDC(dc);
    if (WinDC.GetDeviceCaps(FXDC_DEVICE_CLASS) == FXDC_PRINTER) {
      auto pDst = pdfium::MakeRetain<CFX_DIBitmap>();
      int pitch = pBitmap->GetPitch();
      pDst->Create(size_x, size_y, FXDIB_Rgb32);
      memset(pDst->GetBuffer(), -1, pitch * size_y);
      pDst->CompositeBitmap(0, 0, size_x, size_y, pBitmap, 0, 0,
                            FXDIB_BLEND_NORMAL, nullptr, false);
      WinDC.StretchDIBits(pDst, 0, 0, size_x, size_y);
    } else {
      WinDC.SetDIBits(pBitmap, 0, 0);
    }
  }

  pPage->SetRenderContext(nullptr);
}
#endif  // defined(_WIN32)

FPDF_EXPORT void FPDF_CALLCONV FPDF_RenderPageBitmap(FPDF_BITMAP bitmap,
                                                     FPDF_PAGE page,
                                                     int start_x,
                                                     int start_y,
                                                     int size_x,
                                                     int size_y,
                                                     int rotate,
                                                     int flags) {
  if (!bitmap)
    return;

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return;

  CPDF_PageRenderContext* pContext = new CPDF_PageRenderContext;
  pPage->SetRenderContext(pdfium::WrapUnique(pContext));

  CFX_DefaultRenderDevice* pDevice = new CFX_DefaultRenderDevice;
  pContext->m_pDevice.reset(pDevice);

  CFX_RetainPtr<CFX_DIBitmap> pBitmap(CFXBitmapFromFPDFBitmap(bitmap));
  pDevice->Attach(pBitmap, !!(flags & FPDF_REVERSE_BYTE_ORDER), nullptr, false);
  FPDF_RenderPage_Retail(pContext, page, start_x, start_y, size_x, size_y,
                         rotate, flags, true, nullptr);

#ifdef _SKIA_SUPPORT_PATHS_
  pDevice->Flush(true);
  pBitmap->UnPreMultiply();
#endif
  pPage->SetRenderContext(nullptr);
}

FPDF_EXPORT void FPDF_CALLCONV
FPDF_RenderPageBitmapWithMatrix(FPDF_BITMAP bitmap,
                                FPDF_PAGE page,
                                const FS_MATRIX* matrix,
                                const FS_RECTF* clipping,
                                int flags) {
  if (!bitmap)
    return;

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return;

  CPDF_PageRenderContext* pContext = new CPDF_PageRenderContext;
  pPage->SetRenderContext(pdfium::WrapUnique(pContext));

  CFX_DefaultRenderDevice* pDevice = new CFX_DefaultRenderDevice;
  pContext->m_pDevice.reset(pDevice);

  CFX_RetainPtr<CFX_DIBitmap> pBitmap(CFXBitmapFromFPDFBitmap(bitmap));
  pDevice->Attach(pBitmap, !!(flags & FPDF_REVERSE_BYTE_ORDER), nullptr, false);

  CFX_FloatRect clipping_rect;
  if (clipping) {
    clipping_rect.left = clipping->left;
    clipping_rect.bottom = clipping->bottom;
    clipping_rect.right = clipping->right;
    clipping_rect.top = clipping->top;
  }
  FX_RECT clip_rect = clipping_rect.ToFxRect();

  CFX_Matrix transform_matrix = pPage->GetDisplayMatrix(
      clip_rect.left, clip_rect.top, clip_rect.Width(), clip_rect.Height(), 0);
  if (matrix) {
    transform_matrix.Concat(CFX_Matrix(matrix->a, matrix->b, matrix->c,
                                       matrix->d, matrix->e, matrix->f));
  }
  RenderPageImpl(pContext, pPage, transform_matrix, clip_rect, flags, true,
                 nullptr);

  pPage->SetRenderContext(nullptr);
}

#ifdef _SKIA_SUPPORT_
FPDF_EXPORT FPDF_RECORDER FPDF_CALLCONV FPDF_RenderPageSkp(FPDF_PAGE page,
                                                           int size_x,
                                                           int size_y) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return nullptr;

  CPDF_PageRenderContext* pContext = new CPDF_PageRenderContext;
  pPage->SetRenderContext(pdfium::WrapUnique(pContext));
  CFX_DefaultRenderDevice* skDevice = new CFX_DefaultRenderDevice;
  FPDF_RECORDER recorder = skDevice->CreateRecorder(size_x, size_y);
  pContext->m_pDevice.reset(skDevice);
  FPDF_RenderPage_Retail(pContext, page, 0, 0, size_x, size_y, 0, 0, true,
                         nullptr);
  pPage->SetRenderContext(nullptr);
  return recorder;
}
#endif

FPDF_EXPORT void FPDF_CALLCONV FPDF_ClosePage(FPDF_PAGE page) {
  UnderlyingPageType* pPage = UnderlyingFromFPDFPage(page);
  if (!page)
    return;
#ifdef PDF_ENABLE_XFA
  // Take it back across the API and throw it away.
  CFX_RetainPtr<CPDFXFA_Page>().Unleak(pPage);
#else   // PDF_ENABLE_XFA
  CPDFSDK_PageView* pPageView =
      static_cast<CPDFSDK_PageView*>(pPage->GetView());
  if (pPageView) {
    // We're already destroying the pageview, so bail early.
    if (pPageView->IsBeingDestroyed())
      return;

    if (pPageView->IsLocked()) {
      pPageView->TakePageOwnership();
      return;
    }

    bool owned = pPageView->OwnsPage();
    // This will delete the |pPageView| object. We must cleanup the PageView
    // first because it will attempt to reset the View on the |pPage| during
    // destruction.
    pPageView->GetFormFillEnv()->RemovePageView(pPage);
    // If the page was owned then the pageview will have deleted the page.
    if (owned)
      return;
  }
  delete pPage;
#endif  // PDF_ENABLE_XFA
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_CloseDocument(FPDF_DOCUMENT document) {
  delete UnderlyingFromFPDFDocument(document);
}

FPDF_EXPORT unsigned long FPDF_CALLCONV FPDF_GetLastError() {
  return GetLastError();
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_DeviceToPage(FPDF_PAGE page,
                                                 int start_x,
                                                 int start_y,
                                                 int size_x,
                                                 int size_y,
                                                 int rotate,
                                                 int device_x,
                                                 int device_y,
                                                 double* page_x,
                                                 double* page_y) {
  if (!page || !page_x || !page_y)
    return;
  UnderlyingPageType* pPage = UnderlyingFromFPDFPage(page);
#ifdef PDF_ENABLE_XFA
  pPage->DeviceToPage(start_x, start_y, size_x, size_y, rotate, device_x,
                      device_y, page_x, page_y);
#else   // PDF_ENABLE_XFA
  CFX_Matrix page2device =
      pPage->GetDisplayMatrix(start_x, start_y, size_x, size_y, rotate);

  CFX_PointF pos = page2device.GetInverse().Transform(
      CFX_PointF(static_cast<float>(device_x), static_cast<float>(device_y)));

  *page_x = pos.x;
  *page_y = pos.y;
#endif  // PDF_ENABLE_XFA
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_PageToDevice(FPDF_PAGE page,
                                                 int start_x,
                                                 int start_y,
                                                 int size_x,
                                                 int size_y,
                                                 int rotate,
                                                 double page_x,
                                                 double page_y,
                                                 int* device_x,
                                                 int* device_y) {
  if (!device_x || !device_y)
    return;
  UnderlyingPageType* pPage = UnderlyingFromFPDFPage(page);
  if (!pPage)
    return;
#ifdef PDF_ENABLE_XFA
  pPage->PageToDevice(start_x, start_y, size_x, size_y, rotate, page_x, page_y,
                      device_x, device_y);
#else   // PDF_ENABLE_XFA
  CFX_Matrix page2device =
      pPage->GetDisplayMatrix(start_x, start_y, size_x, size_y, rotate);
  CFX_PointF pos = page2device.Transform(
      CFX_PointF(static_cast<float>(page_x), static_cast<float>(page_y)));

  *device_x = FXSYS_round(pos.x);
  *device_y = FXSYS_round(pos.y);
#endif  // PDF_ENABLE_XFA
}

FPDF_EXPORT FPDF_BITMAP FPDF_CALLCONV FPDFBitmap_Create(int width,
                                                        int height,
                                                        int alpha) {
  auto pBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!pBitmap->Create(width, height, alpha ? FXDIB_Argb : FXDIB_Rgb32))
    return nullptr;

  return pBitmap.Leak();
}

FPDF_EXPORT FPDF_BITMAP FPDF_CALLCONV FPDFBitmap_CreateEx(int width,
                                                          int height,
                                                          int format,
                                                          void* first_scan,
                                                          int stride) {
  FXDIB_Format fx_format;
  switch (format) {
    case FPDFBitmap_Gray:
      fx_format = FXDIB_8bppRgb;
      break;
    case FPDFBitmap_BGR:
      fx_format = FXDIB_Rgb;
      break;
    case FPDFBitmap_BGRx:
      fx_format = FXDIB_Rgb32;
      break;
    case FPDFBitmap_BGRA:
      fx_format = FXDIB_Argb;
      break;
    default:
      return nullptr;
  }
  auto pBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  pBitmap->Create(width, height, fx_format, (uint8_t*)first_scan, stride);
  return pBitmap.Leak();
}

FPDF_EXPORT int FPDF_CALLCONV FPDFBitmap_GetFormat(FPDF_BITMAP bitmap) {
  if (!bitmap)
    return FPDFBitmap_Unknown;

  FXDIB_Format format = CFXBitmapFromFPDFBitmap(bitmap)->GetFormat();
  switch (format) {
    case FXDIB_8bppRgb:
    case FXDIB_8bppMask:
      return FPDFBitmap_Gray;
    case FXDIB_Rgb:
      return FPDFBitmap_BGR;
    case FXDIB_Rgb32:
      return FPDFBitmap_BGRx;
    case FXDIB_Argb:
      return FPDFBitmap_BGRA;
    default:
      return FPDFBitmap_Unknown;
  }
}

FPDF_EXPORT void FPDF_CALLCONV FPDFBitmap_FillRect(FPDF_BITMAP bitmap,
                                                   int left,
                                                   int top,
                                                   int width,
                                                   int height,
                                                   FPDF_DWORD color) {
  if (!bitmap)
    return;

  CFX_DefaultRenderDevice device;
  CFX_RetainPtr<CFX_DIBitmap> pBitmap(CFXBitmapFromFPDFBitmap(bitmap));
  device.Attach(pBitmap, false, nullptr, false);
  if (!pBitmap->HasAlpha())
    color |= 0xFF000000;
  FX_RECT rect(left, top, left + width, top + height);
  device.FillRect(&rect, color);
}

FPDF_EXPORT void* FPDF_CALLCONV FPDFBitmap_GetBuffer(FPDF_BITMAP bitmap) {
  return bitmap ? CFXBitmapFromFPDFBitmap(bitmap)->GetBuffer() : nullptr;
}

FPDF_EXPORT int FPDF_CALLCONV FPDFBitmap_GetWidth(FPDF_BITMAP bitmap) {
  return bitmap ? CFXBitmapFromFPDFBitmap(bitmap)->GetWidth() : 0;
}

FPDF_EXPORT int FPDF_CALLCONV FPDFBitmap_GetHeight(FPDF_BITMAP bitmap) {
  return bitmap ? CFXBitmapFromFPDFBitmap(bitmap)->GetHeight() : 0;
}

FPDF_EXPORT int FPDF_CALLCONV FPDFBitmap_GetStride(FPDF_BITMAP bitmap) {
  return bitmap ? CFXBitmapFromFPDFBitmap(bitmap)->GetPitch() : 0;
}

FPDF_EXPORT void FPDF_CALLCONV FPDFBitmap_Destroy(FPDF_BITMAP bitmap) {
  CFX_RetainPtr<CFX_DIBitmap> destroyer;
  destroyer.Unleak(CFXBitmapFromFPDFBitmap(bitmap));
}

void FPDF_RenderPage_Retail(CPDF_PageRenderContext* pContext,
                            FPDF_PAGE page,
                            int start_x,
                            int start_y,
                            int size_x,
                            int size_y,
                            int rotate,
                            int flags,
                            bool bNeedToRestore,
                            IFSDK_PAUSE_Adapter* pause) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return;

  RenderPageImpl(pContext, pPage, pPage->GetDisplayMatrix(
                                      start_x, start_y, size_x, size_y, rotate),
                 FX_RECT(start_x, start_y, start_x + size_x, start_y + size_y),
                 flags, bNeedToRestore, pause);
}

FPDF_EXPORT int FPDF_CALLCONV FPDF_GetPageSizeByIndex(FPDF_DOCUMENT document,
                                                      int page_index,
                                                      double* width,
                                                      double* height) {
  UnderlyingDocumentType* pDoc = UnderlyingFromFPDFDocument(document);
  if (!pDoc)
    return false;

#ifdef PDF_ENABLE_XFA
  int count = pDoc->GetPageCount();
  if (page_index < 0 || page_index >= count)
    return false;
  CFX_RetainPtr<CPDFXFA_Page> pPage = pDoc->GetXFAPage(page_index);
  if (!pPage)
    return false;
  *width = pPage->GetPageWidth();
  *height = pPage->GetPageHeight();
#else   // PDF_ENABLE_XFA
  CPDF_Dictionary* pDict = pDoc->GetPage(page_index);
  if (!pDict)
    return false;

  CPDF_Page page(pDoc, pDict, true);
  *width = page.GetPageWidth();
  *height = page.GetPageHeight();
#endif  // PDF_ENABLE_XFA

  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_VIEWERREF_GetPrintScaling(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return true;
  CPDF_ViewerPreferences viewRef(pDoc);
  return viewRef.PrintScaling();
}

FPDF_EXPORT int FPDF_CALLCONV
FPDF_VIEWERREF_GetNumCopies(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return 1;
  CPDF_ViewerPreferences viewRef(pDoc);
  return viewRef.NumCopies();
}

FPDF_EXPORT FPDF_PAGERANGE FPDF_CALLCONV
FPDF_VIEWERREF_GetPrintPageRange(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return nullptr;
  CPDF_ViewerPreferences viewRef(pDoc);
  return viewRef.PrintPageRange();
}

FPDF_EXPORT FPDF_DUPLEXTYPE FPDF_CALLCONV
FPDF_VIEWERREF_GetDuplex(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return DuplexUndefined;
  CPDF_ViewerPreferences viewRef(pDoc);
  CFX_ByteString duplex = viewRef.Duplex();
  if ("Simplex" == duplex)
    return Simplex;
  if ("DuplexFlipShortEdge" == duplex)
    return DuplexFlipShortEdge;
  if ("DuplexFlipLongEdge" == duplex)
    return DuplexFlipLongEdge;
  return DuplexUndefined;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDF_VIEWERREF_GetName(FPDF_DOCUMENT document,
                       FPDF_BYTESTRING key,
                       char* buffer,
                       unsigned long length) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return 0;

  CPDF_ViewerPreferences viewRef(pDoc);
  CFX_ByteString bsVal;
  if (!viewRef.GenericName(key, &bsVal))
    return 0;

  unsigned long dwStringLen = bsVal.GetLength() + 1;
  if (buffer && length >= dwStringLen)
    memcpy(buffer, bsVal.c_str(), dwStringLen);
  return dwStringLen;
}

FPDF_EXPORT FPDF_DWORD FPDF_CALLCONV
FPDF_CountNamedDests(FPDF_DOCUMENT document) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return 0;

  const CPDF_Dictionary* pRoot = pDoc->GetRoot();
  if (!pRoot)
    return 0;

  CPDF_NameTree nameTree(pDoc, "Dests");
  pdfium::base::CheckedNumeric<FPDF_DWORD> count = nameTree.GetCount();
  CPDF_Dictionary* pDest = pRoot->GetDictFor("Dests");
  if (pDest)
    count += pDest->GetCount();

  if (!count.IsValid())
    return 0;

  return count.ValueOrDie();
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
FPDF_GetNamedDestByName(FPDF_DOCUMENT document, FPDF_BYTESTRING name) {
  if (!name || name[0] == 0)
    return nullptr;

  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return nullptr;

  CPDF_NameTree name_tree(pDoc, "Dests");
  return name_tree.LookupNamedDest(pDoc, PDF_DecodeText(CFX_ByteString(name)));
}

#ifdef PDF_ENABLE_XFA
FPDF_EXPORT FPDF_RESULT FPDF_CALLCONV FPDF_BStr_Init(FPDF_BSTR* str) {
  if (!str)
    return -1;

  memset(str, 0, sizeof(FPDF_BSTR));
  return 0;
}

FPDF_EXPORT FPDF_RESULT FPDF_CALLCONV FPDF_BStr_Set(FPDF_BSTR* str,
                                                    FPDF_LPCSTR bstr,
                                                    int length) {
  if (!str)
    return -1;
  if (!bstr || !length)
    return -1;
  if (length == -1)
    length = FXSYS_strlen(bstr);

  if (length == 0) {
    if (str->str) {
      FX_Free(str->str);
      str->str = nullptr;
    }
    str->len = 0;
    return 0;
  }

  if (str->str && str->len < length)
    str->str = FX_Realloc(char, str->str, length + 1);
  else if (!str->str)
    str->str = FX_Alloc(char, length + 1);

  str->str[length] = 0;
  memcpy(str->str, bstr, length);
  str->len = length;

  return 0;
}

FPDF_EXPORT FPDF_RESULT FPDF_CALLCONV FPDF_BStr_Clear(FPDF_BSTR* str) {
  if (!str)
    return -1;

  if (str->str) {
    FX_Free(str->str);
    str->str = nullptr;
  }
  str->len = 0;
  return 0;
}
#endif  // PDF_ENABLE_XFA

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV FPDF_GetNamedDest(FPDF_DOCUMENT document,
                                                      int index,
                                                      void* buffer,
                                                      long* buflen) {
  if (!buffer)
    *buflen = 0;

  if (index < 0)
    return nullptr;

  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return nullptr;

  const CPDF_Dictionary* pRoot = pDoc->GetRoot();
  if (!pRoot)
    return nullptr;

  CPDF_Object* pDestObj = nullptr;
  CFX_WideString wsName;
  CPDF_NameTree nameTree(pDoc, "Dests");
  int count = nameTree.GetCount();
  if (index >= count) {
    CPDF_Dictionary* pDest = pRoot->GetDictFor("Dests");
    if (!pDest)
      return nullptr;

    pdfium::base::CheckedNumeric<int> checked_count = count;
    checked_count += pDest->GetCount();
    if (!checked_count.IsValid() || index >= checked_count.ValueOrDie())
      return nullptr;

    index -= count;
    int i = 0;
    CFX_ByteString bsName;
    for (const auto& it : *pDest) {
      bsName = it.first;
      pDestObj = it.second.get();
      if (!pDestObj)
        continue;
      if (i == index)
        break;
      i++;
    }
    wsName = PDF_DecodeText(bsName);
  } else {
    pDestObj = nameTree.LookupValueAndName(index, &wsName);
  }
  if (!pDestObj)
    return nullptr;
  if (CPDF_Dictionary* pDict = pDestObj->AsDictionary()) {
    pDestObj = pDict->GetArrayFor("D");
    if (!pDestObj)
      return nullptr;
  }
  if (!pDestObj->IsArray())
    return nullptr;

  CFX_ByteString utf16Name = wsName.UTF16LE_Encode();
  int len = utf16Name.GetLength();
  if (!buffer) {
    *buflen = len;
  } else if (len <= *buflen) {
    memcpy(buffer, utf16Name.c_str(), len);
    *buflen = len;
  } else {
    *buflen = -1;
  }
  return (FPDF_DEST)pDestObj;
}

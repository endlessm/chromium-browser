// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_structtree.h"

#include <memory>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfdoc/cpdf_structelement.h"
#include "core/fpdfdoc/cpdf_structtree.h"
#include "fpdfsdk/fsdk_define.h"

namespace {

CPDF_StructTree* ToStructTree(FPDF_STRUCTTREE struct_tree) {
  return static_cast<CPDF_StructTree*>(struct_tree);
}

CPDF_StructElement* ToStructTreeElement(FPDF_STRUCTELEMENT struct_element) {
  return static_cast<CPDF_StructElement*>(struct_element);
}

unsigned long WideStringToBuffer(const CFX_WideString& str,
                                 void* buffer,
                                 unsigned long buflen) {
  if (str.IsEmpty())
    return 0;

  CFX_ByteString encodedStr = str.UTF16LE_Encode();
  const unsigned long len = encodedStr.GetLength();
  if (buffer && len <= buflen)
    memcpy(buffer, encodedStr.c_str(), len);
  return len;
}

}  // namespace

FPDF_EXPORT FPDF_STRUCTTREE FPDF_CALLCONV
FPDF_StructTree_GetForPage(FPDF_PAGE page) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage)
    return nullptr;
  return CPDF_StructTree::LoadPage(pPage->m_pDocument.Get(),
                                   pPage->m_pFormDict.Get())
      .release();
}

FPDF_EXPORT void FPDF_CALLCONV
FPDF_StructTree_Close(FPDF_STRUCTTREE struct_tree) {
  std::unique_ptr<CPDF_StructTree>(ToStructTree(struct_tree));
}

FPDF_EXPORT int FPDF_CALLCONV
FPDF_StructTree_CountChildren(FPDF_STRUCTTREE struct_tree) {
  CPDF_StructTree* tree = ToStructTree(struct_tree);
  return tree ? tree->CountTopElements() : -1;
}

FPDF_EXPORT FPDF_STRUCTELEMENT FPDF_CALLCONV
FPDF_StructTree_GetChildAtIndex(FPDF_STRUCTTREE struct_tree, int index) {
  CPDF_StructTree* tree = ToStructTree(struct_tree);
  if (!tree || index < 0 || index >= tree->CountTopElements())
    return nullptr;
  return tree->GetTopElement(index);
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDF_StructElement_GetAltText(FPDF_STRUCTELEMENT struct_element,
                              void* buffer,
                              unsigned long buflen) {
  CPDF_StructElement* elem = ToStructTreeElement(struct_element);
  return (elem && elem->GetDict())
             ? WideStringToBuffer(elem->GetDict()->GetUnicodeTextFor("Alt"),
                                  buffer, buflen)
             : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDF_StructElement_GetType(FPDF_STRUCTELEMENT struct_element,
                           void* buffer,
                           unsigned long buflen) {
  CPDF_StructElement* elem = ToStructTreeElement(struct_element);
  return elem ? WideStringToBuffer(elem->GetType().UTF8Decode(), buffer, buflen)
              : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDF_StructElement_GetTitle(FPDF_STRUCTELEMENT struct_element,
                            void* buffer,
                            unsigned long buflen) {
  CPDF_StructElement* elem = ToStructTreeElement(struct_element);
  return elem
             ? WideStringToBuffer(elem->GetTitle().UTF8Decode(), buffer, buflen)
             : 0;
}

FPDF_EXPORT int FPDF_CALLCONV
FPDF_StructElement_CountChildren(FPDF_STRUCTELEMENT struct_element) {
  CPDF_StructElement* elem = ToStructTreeElement(struct_element);
  return elem ? elem->CountKids() : -1;
}

FPDF_EXPORT FPDF_STRUCTELEMENT FPDF_CALLCONV
FPDF_StructElement_GetChildAtIndex(FPDF_STRUCTELEMENT struct_element,
                                   int index) {
  CPDF_StructElement* elem = ToStructTreeElement(struct_element);
  if (!elem || index < 0 || index >= elem->CountKids())
    return nullptr;

  return elem->GetKidIfElement(index);
}

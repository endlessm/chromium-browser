// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/page/cpdf_colorspace.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "core/fpdfapi/cpdf_modulemgr.h"
#include "core/fpdfapi/page/cpdf_devicecs.h"
#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/page/cpdf_function.h"
#include "core/fpdfapi/page/cpdf_iccprofile.h"
#include "core/fpdfapi/page/cpdf_pagemodule.h"
#include "core/fpdfapi/page/cpdf_pattern.h"
#include "core/fpdfapi/page/cpdf_patterncs.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fpdfdoc/cpdf_action.h"
#include "core/fxcodec/fx_codec.h"
#include "core/fxcrt/cfx_fixedbufgrow.h"
#include "core/fxcrt/cfx_maybe_owned.h"
#include "core/fxcrt/fx_memory.h"
#include "third_party/base/stl_util.h"

namespace {

const uint8_t g_sRGBSamples1[] = {
    0,   3,   6,   10,  13,  15,  18,  20,  22,  23,  25,  27,  28,  30,  31,
    32,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
    48,  49,  49,  50,  51,  52,  53,  53,  54,  55,  56,  56,  57,  58,  58,
    59,  60,  61,  61,  62,  62,  63,  64,  64,  65,  66,  66,  67,  67,  68,
    68,  69,  70,  70,  71,  71,  72,  72,  73,  73,  74,  74,  75,  76,  76,
    77,  77,  78,  78,  79,  79,  79,  80,  80,  81,  81,  82,  82,  83,  83,
    84,  84,  85,  85,  85,  86,  86,  87,  87,  88,  88,  88,  89,  89,  90,
    90,  91,  91,  91,  92,  92,  93,  93,  93,  94,  94,  95,  95,  95,  96,
    96,  97,  97,  97,  98,  98,  98,  99,  99,  99,  100, 100, 101, 101, 101,
    102, 102, 102, 103, 103, 103, 104, 104, 104, 105, 105, 106, 106, 106, 107,
    107, 107, 108, 108, 108, 109, 109, 109, 110, 110, 110, 110, 111, 111, 111,
    112, 112, 112, 113, 113, 113, 114, 114, 114, 115, 115, 115, 115, 116, 116,
    116, 117, 117, 117, 118, 118, 118, 118, 119, 119, 119, 120,
};

const uint8_t g_sRGBSamples2[] = {
    120, 121, 122, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 148, 149,
    150, 151, 152, 153, 154, 155, 155, 156, 157, 158, 159, 159, 160, 161, 162,
    163, 163, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173, 173,
    174, 175, 175, 176, 177, 178, 178, 179, 180, 180, 181, 182, 182, 183, 184,
    185, 185, 186, 187, 187, 188, 189, 189, 190, 190, 191, 192, 192, 193, 194,
    194, 195, 196, 196, 197, 197, 198, 199, 199, 200, 200, 201, 202, 202, 203,
    203, 204, 205, 205, 206, 206, 207, 208, 208, 209, 209, 210, 210, 211, 212,
    212, 213, 213, 214, 214, 215, 215, 216, 216, 217, 218, 218, 219, 219, 220,
    220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 226, 226, 227, 227, 228,
    228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235,
    236, 236, 237, 237, 238, 238, 238, 239, 239, 240, 240, 241, 241, 242, 242,
    243, 243, 244, 244, 245, 245, 246, 246, 246, 247, 247, 248, 248, 249, 249,
    250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 255, 255,
};

class CPDF_CalGray : public CPDF_ColorSpace {
 public:
  explicit CPDF_CalGray(CPDF_Document* pDoc);
  ~CPDF_CalGray() override {}

  // CPDF_ColorSpace:
  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;
  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;
  void TranslateImageLine(uint8_t* pDestBuf,
                          const uint8_t* pSrcBuf,
                          int pixels,
                          int image_width,
                          int image_height,
                          bool bTransMask) const override;

 private:
  float m_WhitePoint[3];
  float m_BlackPoint[3];
  float m_Gamma;
};

class CPDF_CalRGB : public CPDF_ColorSpace {
 public:
  explicit CPDF_CalRGB(CPDF_Document* pDoc);
  ~CPDF_CalRGB() override {}

  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;

  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;

  void TranslateImageLine(uint8_t* pDestBuf,
                          const uint8_t* pSrcBuf,
                          int pixels,
                          int image_width,
                          int image_height,
                          bool bTransMask) const override;

  float m_WhitePoint[3];
  float m_BlackPoint[3];
  float m_Gamma[3];
  float m_Matrix[9];
  bool m_bGamma;
  bool m_bMatrix;
};

class CPDF_LabCS : public CPDF_ColorSpace {
 public:
  explicit CPDF_LabCS(CPDF_Document* pDoc);
  ~CPDF_LabCS() override {}

  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;

  void GetDefaultValue(int iComponent,
                       float* value,
                       float* min,
                       float* max) const override;
  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;

  void TranslateImageLine(uint8_t* pDestBuf,
                          const uint8_t* pSrcBuf,
                          int pixels,
                          int image_width,
                          int image_height,
                          bool bTransMask) const override;

  float m_WhitePoint[3];
  float m_BlackPoint[3];
  float m_Ranges[4];
};

class CPDF_ICCBasedCS : public CPDF_ColorSpace {
 public:
  explicit CPDF_ICCBasedCS(CPDF_Document* pDoc);
  ~CPDF_ICCBasedCS() override;

  // CPDF_ColorSpace:
  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;
  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;
  void EnableStdConversion(bool bEnabled) override;
  void TranslateImageLine(uint8_t* pDestBuf,
                          const uint8_t* pSrcBuf,
                          int pixels,
                          int image_width,
                          int image_height,
                          bool bTransMask) const override;

  bool IsSRGB() const { return m_pProfile->IsSRGB(); }

 private:
  // If no valid ICC profile or using sRGB, try looking for an alternate.
  bool FindAlternateProfile(CPDF_Document* pDoc, CPDF_Dictionary* pDict);

  void UseStockAlternateProfile();
  bool IsValidComponents(int32_t nComps) const;
  void PopulateRanges(CPDF_Dictionary* pDict);

  CFX_MaybeOwned<CPDF_ColorSpace> m_pAlterCS;
  CFX_RetainPtr<CPDF_IccProfile> m_pProfile;
  uint8_t* m_pCache;
  float* m_pRanges;
};

class CPDF_IndexedCS : public CPDF_ColorSpace {
 public:
  explicit CPDF_IndexedCS(CPDF_Document* pDoc);
  ~CPDF_IndexedCS() override;

  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;

  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;

  void EnableStdConversion(bool bEnabled) override;

  CPDF_ColorSpace* m_pBaseCS;
  CFX_UnownedPtr<CPDF_CountedColorSpace> m_pCountedBaseCS;
  int m_nBaseComponents;
  int m_MaxIndex;
  CFX_ByteString m_Table;
  float* m_pCompMinMax;
};

class CPDF_SeparationCS : public CPDF_ColorSpace {
 public:
  explicit CPDF_SeparationCS(CPDF_Document* pDoc);
  ~CPDF_SeparationCS() override;

  // CPDF_ColorSpace:
  void GetDefaultValue(int iComponent,
                       float* value,
                       float* min,
                       float* max) const override;
  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;
  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;
  void EnableStdConversion(bool bEnabled) override;

  std::unique_ptr<CPDF_ColorSpace> m_pAltCS;
  std::unique_ptr<CPDF_Function> m_pFunc;
  enum { None, All, Colorant } m_Type;
};

class CPDF_DeviceNCS : public CPDF_ColorSpace {
 public:
  explicit CPDF_DeviceNCS(CPDF_Document* pDoc);
  ~CPDF_DeviceNCS() override;

  // CPDF_ColorSpace:
  void GetDefaultValue(int iComponent,
                       float* value,
                       float* min,
                       float* max) const override;
  bool v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) override;
  bool GetRGB(float* pBuf, float* R, float* G, float* B) const override;
  void EnableStdConversion(bool bEnabled) override;

  std::unique_ptr<CPDF_ColorSpace> m_pAltCS;
  std::unique_ptr<CPDF_Function> m_pFunc;
};

class Vector_3by1 {
 public:
  Vector_3by1() : a(0.0f), b(0.0f), c(0.0f) {}

  Vector_3by1(float a1, float b1, float c1) : a(a1), b(b1), c(c1) {}

  float a;
  float b;
  float c;
};

class Matrix_3by3 {
 public:
  Matrix_3by3()
      : a(0.0f),
        b(0.0f),
        c(0.0f),
        d(0.0f),
        e(0.0f),
        f(0.0f),
        g(0.0f),
        h(0.0f),
        i(0.0f) {}

  Matrix_3by3(float a1,
              float b1,
              float c1,
              float d1,
              float e1,
              float f1,
              float g1,
              float h1,
              float i1)
      : a(a1), b(b1), c(c1), d(d1), e(e1), f(f1), g(g1), h(h1), i(i1) {}

  Matrix_3by3 Inverse() {
    float det = a * (e * i - f * h) - b * (i * d - f * g) + c * (d * h - e * g);
    if (fabs(det) < std::numeric_limits<float>::epsilon())
      return Matrix_3by3();

    return Matrix_3by3(
        (e * i - f * h) / det, -(b * i - c * h) / det, (b * f - c * e) / det,
        -(d * i - f * g) / det, (a * i - c * g) / det, -(a * f - c * d) / det,
        (d * h - e * g) / det, -(a * h - b * g) / det, (a * e - b * d) / det);
  }

  Matrix_3by3 Multiply(const Matrix_3by3& m) {
    return Matrix_3by3(a * m.a + b * m.d + c * m.g, a * m.b + b * m.e + c * m.h,
                       a * m.c + b * m.f + c * m.i, d * m.a + e * m.d + f * m.g,
                       d * m.b + e * m.e + f * m.h, d * m.c + e * m.f + f * m.i,
                       g * m.a + h * m.d + i * m.g, g * m.b + h * m.e + i * m.h,
                       g * m.c + h * m.f + i * m.i);
  }

  Vector_3by1 TransformVector(const Vector_3by1& v) {
    return Vector_3by1(a * v.a + b * v.b + c * v.c, d * v.a + e * v.b + f * v.c,
                       g * v.a + h * v.b + i * v.c);
  }

  float a;
  float b;
  float c;
  float d;
  float e;
  float f;
  float g;
  float h;
  float i;
};

float RGB_Conversion(float colorComponent) {
  colorComponent = pdfium::clamp(colorComponent, 0.0f, 1.0f);
  int scale = std::max(static_cast<int>(colorComponent * 1023), 0);
  if (scale < 192)
    return g_sRGBSamples1[scale] / 255.0f;
  return g_sRGBSamples2[scale / 4 - 48] / 255.0f;
}

void XYZ_to_sRGB(float X, float Y, float Z, float* R, float* G, float* B) {
  float R1 = 3.2410f * X - 1.5374f * Y - 0.4986f * Z;
  float G1 = -0.9692f * X + 1.8760f * Y + 0.0416f * Z;
  float B1 = 0.0556f * X - 0.2040f * Y + 1.0570f * Z;

  *R = RGB_Conversion(R1);
  *G = RGB_Conversion(G1);
  *B = RGB_Conversion(B1);
}

void XYZ_to_sRGB_WhitePoint(float X,
                            float Y,
                            float Z,
                            float Xw,
                            float Yw,
                            float Zw,
                            float* R,
                            float* G,
                            float* B) {
  // The following RGB_xyz is based on
  // sRGB value {Rx,Ry}={0.64, 0.33}, {Gx,Gy}={0.30, 0.60}, {Bx,By}={0.15, 0.06}

  float Rx = 0.64f, Ry = 0.33f;
  float Gx = 0.30f, Gy = 0.60f;
  float Bx = 0.15f, By = 0.06f;
  Matrix_3by3 RGB_xyz(Rx, Gx, Bx, Ry, Gy, By, 1 - Rx - Ry, 1 - Gx - Gy,
                      1 - Bx - By);
  Vector_3by1 whitePoint(Xw, Yw, Zw);
  Vector_3by1 XYZ(X, Y, Z);

  Vector_3by1 RGB_Sum_XYZ = RGB_xyz.Inverse().TransformVector(whitePoint);
  Matrix_3by3 RGB_SUM_XYZ_DIAG(RGB_Sum_XYZ.a, 0, 0, 0, RGB_Sum_XYZ.b, 0, 0, 0,
                               RGB_Sum_XYZ.c);
  Matrix_3by3 M = RGB_xyz.Multiply(RGB_SUM_XYZ_DIAG);
  Vector_3by1 RGB = M.Inverse().TransformVector(XYZ);

  *R = RGB_Conversion(RGB.a);
  *G = RGB_Conversion(RGB.b);
  *B = RGB_Conversion(RGB.c);
}

}  // namespace

CPDF_ColorSpace* CPDF_ColorSpace::ColorspaceFromName(
    const CFX_ByteString& name) {
  if (name == "DeviceRGB" || name == "RGB")
    return CPDF_ColorSpace::GetStockCS(PDFCS_DEVICERGB);
  if (name == "DeviceGray" || name == "G")
    return CPDF_ColorSpace::GetStockCS(PDFCS_DEVICEGRAY);
  if (name == "DeviceCMYK" || name == "CMYK")
    return CPDF_ColorSpace::GetStockCS(PDFCS_DEVICECMYK);
  if (name == "Pattern")
    return CPDF_ColorSpace::GetStockCS(PDFCS_PATTERN);
  return nullptr;
}

CPDF_ColorSpace* CPDF_ColorSpace::GetStockCS(int family) {
  return CPDF_ModuleMgr::Get()->GetPageModule()->GetStockCS(family);
}

std::unique_ptr<CPDF_ColorSpace> CPDF_ColorSpace::Load(CPDF_Document* pDoc,
                                                       CPDF_Object* pObj) {
  if (!pObj)
    return nullptr;

  if (pObj->IsName()) {
    return std::unique_ptr<CPDF_ColorSpace>(
        ColorspaceFromName(pObj->GetString()));
  }
  if (CPDF_Stream* pStream = pObj->AsStream()) {
    CPDF_Dictionary* pDict = pStream->GetDict();
    if (!pDict)
      return nullptr;

    for (const auto& it : *pDict) {
      std::unique_ptr<CPDF_ColorSpace> pRet;
      CPDF_Object* pValue = it.second.get();
      if (ToName(pValue))
        pRet.reset(ColorspaceFromName(pValue->GetString()));
      if (pRet)
        return pRet;
    }
    return nullptr;
  }

  CPDF_Array* pArray = pObj->AsArray();
  if (!pArray || pArray->IsEmpty())
    return nullptr;

  CPDF_Object* pFamilyObj = pArray->GetDirectObjectAt(0);
  if (!pFamilyObj)
    return nullptr;

  CFX_ByteString familyname = pFamilyObj->GetString();
  if (pArray->GetCount() == 1)
    return std::unique_ptr<CPDF_ColorSpace>(ColorspaceFromName(familyname));

  std::unique_ptr<CPDF_ColorSpace> pCS;
  switch (familyname.GetID()) {
    case FXBSTR_ID('C', 'a', 'l', 'G'):
      pCS.reset(new CPDF_CalGray(pDoc));
      break;
    case FXBSTR_ID('C', 'a', 'l', 'R'):
      pCS.reset(new CPDF_CalRGB(pDoc));
      break;
    case FXBSTR_ID('L', 'a', 'b', 0):
      pCS.reset(new CPDF_LabCS(pDoc));
      break;
    case FXBSTR_ID('I', 'C', 'C', 'B'):
      pCS.reset(new CPDF_ICCBasedCS(pDoc));
      break;
    case FXBSTR_ID('I', 'n', 'd', 'e'):
    case FXBSTR_ID('I', 0, 0, 0):
      pCS.reset(new CPDF_IndexedCS(pDoc));
      break;
    case FXBSTR_ID('S', 'e', 'p', 'a'):
      pCS.reset(new CPDF_SeparationCS(pDoc));
      break;
    case FXBSTR_ID('D', 'e', 'v', 'i'):
      pCS.reset(new CPDF_DeviceNCS(pDoc));
      break;
    case FXBSTR_ID('P', 'a', 't', 't'):
      pCS.reset(new CPDF_PatternCS(pDoc));
      break;
    default:
      return nullptr;
  }
  pCS->m_pArray = pArray;
  if (!pCS->v_Load(pDoc, pArray))
    return nullptr;

  return pCS;
}

void CPDF_ColorSpace::Release() {
  if (this == GetStockCS(PDFCS_DEVICERGB) ||
      this == GetStockCS(PDFCS_DEVICEGRAY) ||
      this == GetStockCS(PDFCS_DEVICECMYK) ||
      this == GetStockCS(PDFCS_PATTERN)) {
    return;
  }
  delete this;
}

int CPDF_ColorSpace::GetBufSize() const {
  if (m_Family == PDFCS_PATTERN) {
    return sizeof(PatternValue);
  }
  return m_nComponents * sizeof(float);
}

float* CPDF_ColorSpace::CreateBuf() {
  int size = GetBufSize();
  uint8_t* pBuf = FX_Alloc(uint8_t, size);
  return (float*)pBuf;
}

void CPDF_ColorSpace::GetDefaultColor(float* buf) const {
  if (!buf || m_Family == PDFCS_PATTERN)
    return;

  float min;
  float max;
  for (uint32_t i = 0; i < m_nComponents; i++)
    GetDefaultValue(i, &buf[i], &min, &max);
}

uint32_t CPDF_ColorSpace::CountComponents() const {
  return m_nComponents;
}

void CPDF_ColorSpace::GetDefaultValue(int iComponent,
                                      float* value,
                                      float* min,
                                      float* max) const {
  *value = 0.0f;
  *min = 0.0f;
  *max = 1.0f;
}

void CPDF_ColorSpace::TranslateImageLine(uint8_t* dest_buf,
                                         const uint8_t* src_buf,
                                         int pixels,
                                         int image_width,
                                         int image_height,
                                         bool bTransMask) const {
  CFX_FixedBufGrow<float, 16> srcbuf(m_nComponents);
  float* src = srcbuf;
  float R;
  float G;
  float B;
  const int divisor = m_Family != PDFCS_INDEXED ? 255 : 1;
  for (int i = 0; i < pixels; i++) {
    for (uint32_t j = 0; j < m_nComponents; j++)
      src[j] = static_cast<float>(*src_buf++) / divisor;
    GetRGB(src, &R, &G, &B);
    *dest_buf++ = static_cast<int32_t>(B * 255);
    *dest_buf++ = static_cast<int32_t>(G * 255);
    *dest_buf++ = static_cast<int32_t>(R * 255);
  }
}

void CPDF_ColorSpace::EnableStdConversion(bool bEnabled) {
  if (bEnabled)
    m_dwStdConversion++;
  else if (m_dwStdConversion)
    m_dwStdConversion--;
}

CPDF_ColorSpace::CPDF_ColorSpace(CPDF_Document* pDoc,
                                 int family,
                                 uint32_t nComponents)
    : m_pDocument(pDoc),
      m_Family(family),
      m_nComponents(nComponents),
      m_pArray(nullptr),
      m_dwStdConversion(0) {}

CPDF_ColorSpace::~CPDF_ColorSpace() {}

bool CPDF_ColorSpace::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  return true;
}

CPDF_CalGray::CPDF_CalGray(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_CALGRAY, 1) {}

bool CPDF_CalGray::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CPDF_Dictionary* pDict = pArray->GetDictAt(1);
  if (!pDict)
    return false;

  CPDF_Array* pParam = pDict->GetArrayFor("WhitePoint");
  int i;
  for (i = 0; i < 3; i++)
    m_WhitePoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  pParam = pDict->GetArrayFor("BlackPoint");
  for (i = 0; i < 3; i++)
    m_BlackPoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  m_Gamma = pDict->GetNumberFor("Gamma");
  if (m_Gamma == 0)
    m_Gamma = 1.0f;
  return true;
}

bool CPDF_CalGray::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  *R = *pBuf;
  *G = *pBuf;
  *B = *pBuf;
  return true;
}

void CPDF_CalGray::TranslateImageLine(uint8_t* pDestBuf,
                                      const uint8_t* pSrcBuf,
                                      int pixels,
                                      int image_width,
                                      int image_height,
                                      bool bTransMask) const {
  for (int i = 0; i < pixels; i++) {
    *pDestBuf++ = pSrcBuf[i];
    *pDestBuf++ = pSrcBuf[i];
    *pDestBuf++ = pSrcBuf[i];
  }
}

CPDF_CalRGB::CPDF_CalRGB(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_CALRGB, 3) {}

bool CPDF_CalRGB::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CPDF_Dictionary* pDict = pArray->GetDictAt(1);
  if (!pDict)
    return false;

  CPDF_Array* pParam = pDict->GetArrayFor("WhitePoint");
  int i;
  for (i = 0; i < 3; i++)
    m_WhitePoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  pParam = pDict->GetArrayFor("BlackPoint");
  for (i = 0; i < 3; i++)
    m_BlackPoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  pParam = pDict->GetArrayFor("Gamma");
  if (pParam) {
    m_bGamma = true;
    for (i = 0; i < 3; i++)
      m_Gamma[i] = pParam->GetNumberAt(i);
  } else {
    m_bGamma = false;
  }

  pParam = pDict->GetArrayFor("Matrix");
  if (pParam) {
    m_bMatrix = true;
    for (i = 0; i < 9; i++)
      m_Matrix[i] = pParam->GetNumberAt(i);
  } else {
    m_bMatrix = false;
  }
  return true;
}

bool CPDF_CalRGB::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  float A_ = pBuf[0];
  float B_ = pBuf[1];
  float C_ = pBuf[2];
  if (m_bGamma) {
    A_ = (float)FXSYS_pow(A_, m_Gamma[0]);
    B_ = (float)FXSYS_pow(B_, m_Gamma[1]);
    C_ = (float)FXSYS_pow(C_, m_Gamma[2]);
  }

  float X;
  float Y;
  float Z;
  if (m_bMatrix) {
    X = m_Matrix[0] * A_ + m_Matrix[3] * B_ + m_Matrix[6] * C_;
    Y = m_Matrix[1] * A_ + m_Matrix[4] * B_ + m_Matrix[7] * C_;
    Z = m_Matrix[2] * A_ + m_Matrix[5] * B_ + m_Matrix[8] * C_;
  } else {
    X = A_;
    Y = B_;
    Z = C_;
  }
  XYZ_to_sRGB_WhitePoint(X, Y, Z, m_WhitePoint[0], m_WhitePoint[1],
                         m_WhitePoint[2], R, G, B);
  return true;
}

void CPDF_CalRGB::TranslateImageLine(uint8_t* pDestBuf,
                                     const uint8_t* pSrcBuf,
                                     int pixels,
                                     int image_width,
                                     int image_height,
                                     bool bTransMask) const {
  if (bTransMask) {
    float Cal[3];
    float R;
    float G;
    float B;
    for (int i = 0; i < pixels; i++) {
      Cal[0] = static_cast<float>(pSrcBuf[2]) / 255;
      Cal[1] = static_cast<float>(pSrcBuf[1]) / 255;
      Cal[2] = static_cast<float>(pSrcBuf[0]) / 255;
      GetRGB(Cal, &R, &G, &B);
      pDestBuf[0] = FXSYS_round(B * 255);
      pDestBuf[1] = FXSYS_round(G * 255);
      pDestBuf[2] = FXSYS_round(R * 255);
      pSrcBuf += 3;
      pDestBuf += 3;
    }
  }
  ReverseRGB(pDestBuf, pSrcBuf, pixels);
}

CPDF_LabCS::CPDF_LabCS(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_LAB, 3) {}

void CPDF_LabCS::GetDefaultValue(int iComponent,
                                 float* value,
                                 float* min,
                                 float* max) const {
  ASSERT(iComponent < 3);
  if (iComponent == 0) {
    *min = 0.0f;
    *max = 100 * 1.0f;
    *value = 0.0f;
    return;
  }

  *min = m_Ranges[iComponent * 2 - 2];
  *max = m_Ranges[iComponent * 2 - 1];
  *value = pdfium::clamp(0.0f, *min, *max);
}

bool CPDF_LabCS::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CPDF_Dictionary* pDict = pArray->GetDictAt(1);
  if (!pDict)
    return false;

  CPDF_Array* pParam = pDict->GetArrayFor("WhitePoint");
  int i;
  for (i = 0; i < 3; i++)
    m_WhitePoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  pParam = pDict->GetArrayFor("BlackPoint");
  for (i = 0; i < 3; i++)
    m_BlackPoint[i] = pParam ? pParam->GetNumberAt(i) : 0;

  pParam = pDict->GetArrayFor("Range");
  const float def_ranges[4] = {-100 * 1.0f, 100 * 1.0f, -100 * 1.0f,
                               100 * 1.0f};
  for (i = 0; i < 4; i++)
    m_Ranges[i] = pParam ? pParam->GetNumberAt(i) : def_ranges[i];
  return true;
}

bool CPDF_LabCS::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  float Lstar = pBuf[0];
  float astar = pBuf[1];
  float bstar = pBuf[2];
  float M = (Lstar + 16.0f) / 116.0f;
  float L = M + astar / 500.0f;
  float N = M - bstar / 200.0f;
  float X;
  float Y;
  float Z;
  if (L < 0.2069f)
    X = 0.957f * 0.12842f * (L - 0.1379f);
  else
    X = 0.957f * L * L * L;

  if (M < 0.2069f)
    Y = 0.12842f * (M - 0.1379f);
  else
    Y = M * M * M;

  if (N < 0.2069f)
    Z = 1.0889f * 0.12842f * (N - 0.1379f);
  else
    Z = 1.0889f * N * N * N;

  XYZ_to_sRGB(X, Y, Z, R, G, B);
  return true;
}

void CPDF_LabCS::TranslateImageLine(uint8_t* pDestBuf,
                                    const uint8_t* pSrcBuf,
                                    int pixels,
                                    int image_width,
                                    int image_height,
                                    bool bTransMask) const {
  for (int i = 0; i < pixels; i++) {
    float lab[3];
    float R;
    float G;
    float B;
    lab[0] = (pSrcBuf[0] * 100 / 255.0f);
    lab[1] = (float)(pSrcBuf[1] - 128);
    lab[2] = (float)(pSrcBuf[2] - 128);
    GetRGB(lab, &R, &G, &B);
    pDestBuf[0] = static_cast<int32_t>(B * 255);
    pDestBuf[1] = static_cast<int32_t>(G * 255);
    pDestBuf[2] = static_cast<int32_t>(R * 255);
    pDestBuf += 3;
    pSrcBuf += 3;
  }
}

CPDF_ICCBasedCS::CPDF_ICCBasedCS(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_ICCBASED, 0),
      m_pProfile(nullptr),
      m_pCache(nullptr),
      m_pRanges(nullptr) {}

CPDF_ICCBasedCS::~CPDF_ICCBasedCS() {
  FX_Free(m_pCache);
  FX_Free(m_pRanges);
  if (m_pProfile && m_pDocument) {
    CPDF_Stream* pStream = m_pProfile->GetStream();
    m_pProfile.Reset();  // Give up our reference first.
    auto* pPageData = m_pDocument->GetPageData();
    if (pPageData)
      pPageData->MaybePurgeIccProfile(pStream);
  }
}

bool CPDF_ICCBasedCS::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CPDF_Stream* pStream = pArray->GetStreamAt(1);
  if (!pStream)
    return false;

  // The PDF 1.7 spec says the number of components must be valid. While some
  // PDF viewers tolerate invalid values, Acrobat does not, so be consistent
  // with Acrobat and reject bad values.
  CPDF_Dictionary* pDict = pStream->GetDict();
  int32_t nDictComponents = pDict ? pDict->GetIntegerFor("N") : 0;
  if (!IsValidComponents(nDictComponents))
    return false;

  m_nComponents = nDictComponents;
  m_pProfile = pDoc->LoadIccProfile(pStream);
  if (!m_pProfile)
    return false;

  // The PDF 1.7 spec also says the number of components in the ICC profile
  // must match the N value. However, that assumes the viewer actually
  // understands the ICC profile.
  // If the valid ICC profile has a mismatch, fail.
  if (m_pProfile->IsValid() && m_pProfile->GetComponents() != m_nComponents)
    return false;

  // If PDFium does not understand the ICC profile format at all, or if it's
  // SRGB, a profile PDFium recognizes but does not support well, then try the
  // alternate profile.
  if (!m_pProfile->IsSupported() && !FindAlternateProfile(pDoc, pDict)) {
    // If there is no alternate profile, use a stock profile as mentioned in
    // the PDF 1.7 spec in table 4.16 in the "Alternate" key description.
    UseStockAlternateProfile();
  }

  PopulateRanges(pDict);
  return true;
}

bool CPDF_ICCBasedCS::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  ASSERT(m_pProfile);
  if (IsSRGB()) {
    *R = pBuf[0];
    *G = pBuf[1];
    *B = pBuf[2];
    return true;
  }
  if (m_pProfile->transform()) {
    float rgb[3];
    CCodec_IccModule* pIccModule = CPDF_ModuleMgr::Get()->GetIccModule();
    pIccModule->SetComponents(m_nComponents);
    pIccModule->Translate(m_pProfile->transform(), pBuf, rgb);
    *R = rgb[0];
    *G = rgb[1];
    *B = rgb[2];
    return true;
  }

  if (m_pAlterCS)
    return m_pAlterCS->GetRGB(pBuf, R, G, B);

  *R = 0.0f;
  *G = 0.0f;
  *B = 0.0f;
  return true;
}

void CPDF_ICCBasedCS::EnableStdConversion(bool bEnabled) {
  CPDF_ColorSpace::EnableStdConversion(bEnabled);
  if (m_pAlterCS)
    m_pAlterCS->EnableStdConversion(bEnabled);
}

void CPDF_ICCBasedCS::TranslateImageLine(uint8_t* pDestBuf,
                                         const uint8_t* pSrcBuf,
                                         int pixels,
                                         int image_width,
                                         int image_height,
                                         bool bTransMask) const {
  if (IsSRGB()) {
    ReverseRGB(pDestBuf, pSrcBuf, pixels);
  } else if (m_pProfile->transform()) {
    int nMaxColors = 1;
    for (uint32_t i = 0; i < m_nComponents; i++) {
      nMaxColors *= 52;
    }
    if (m_nComponents > 3 || image_width * image_height < nMaxColors * 3 / 2) {
      CPDF_ModuleMgr::Get()->GetIccModule()->TranslateScanline(
          m_pProfile->transform(), pDestBuf, pSrcBuf, pixels);
    } else {
      if (!m_pCache) {
        ((CPDF_ICCBasedCS*)this)->m_pCache = FX_Alloc2D(uint8_t, nMaxColors, 3);
        uint8_t* temp_src = FX_Alloc2D(uint8_t, nMaxColors, m_nComponents);
        uint8_t* pSrc = temp_src;
        for (int i = 0; i < nMaxColors; i++) {
          uint32_t color = i;
          uint32_t order = nMaxColors / 52;
          for (uint32_t c = 0; c < m_nComponents; c++) {
            *pSrc++ = (uint8_t)(color / order * 5);
            color %= order;
            order /= 52;
          }
        }
        CPDF_ModuleMgr::Get()->GetIccModule()->TranslateScanline(
            m_pProfile->transform(), m_pCache, temp_src, nMaxColors);
        FX_Free(temp_src);
      }
      for (int i = 0; i < pixels; i++) {
        int index = 0;
        for (uint32_t c = 0; c < m_nComponents; c++) {
          index = index * 52 + (*pSrcBuf) / 5;
          pSrcBuf++;
        }
        index *= 3;
        *pDestBuf++ = m_pCache[index];
        *pDestBuf++ = m_pCache[index + 1];
        *pDestBuf++ = m_pCache[index + 2];
      }
    }
  } else if (m_pAlterCS) {
    m_pAlterCS->TranslateImageLine(pDestBuf, pSrcBuf, pixels, image_width,
                                   image_height, false);
  }
}

bool CPDF_ICCBasedCS::FindAlternateProfile(CPDF_Document* pDoc,
                                           CPDF_Dictionary* pDict) {
  CPDF_Object* pAlterCSObj = pDict->GetDirectObjectFor("Alternate");
  if (!pAlterCSObj)
    return false;

  auto pAlterCS = CPDF_ColorSpace::Load(pDoc, pAlterCSObj);
  if (!pAlterCS)
    return false;

  if (pAlterCS->CountComponents() != m_nComponents)
    return false;

  m_pAlterCS = std::move(pAlterCS);
  return true;
}

void CPDF_ICCBasedCS::UseStockAlternateProfile() {
  ASSERT(!m_pAlterCS);
  if (m_nComponents == 1)
    m_pAlterCS = GetStockCS(PDFCS_DEVICEGRAY);
  else if (m_nComponents == 3)
    m_pAlterCS = GetStockCS(PDFCS_DEVICERGB);
  else if (m_nComponents == 4)
    m_pAlterCS = GetStockCS(PDFCS_DEVICECMYK);
}

bool CPDF_ICCBasedCS::IsValidComponents(int32_t nComps) const {
  return nComps == 1 || nComps == 3 || nComps == 4;
}

void CPDF_ICCBasedCS::PopulateRanges(CPDF_Dictionary* pDict) {
  CPDF_Array* pRanges = pDict->GetArrayFor("Range");
  m_pRanges = FX_Alloc2D(float, m_nComponents, 2);
  for (uint32_t i = 0; i < m_nComponents * 2; i++) {
    if (pRanges)
      m_pRanges[i] = pRanges->GetNumberAt(i);
    else if (i % 2)
      m_pRanges[i] = 1.0f;
    else
      m_pRanges[i] = 0.0f;
  }
}

CPDF_IndexedCS::CPDF_IndexedCS(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_INDEXED, 1),
      m_pBaseCS(nullptr),
      m_pCountedBaseCS(nullptr),
      m_pCompMinMax(nullptr) {}

CPDF_IndexedCS::~CPDF_IndexedCS() {
  FX_Free(m_pCompMinMax);
  CPDF_ColorSpace* pCS = m_pCountedBaseCS ? m_pCountedBaseCS->get() : nullptr;
  if (pCS && m_pDocument) {
    auto* pPageData = m_pDocument->GetPageData();
    if (pPageData)
      pPageData->ReleaseColorSpace(pCS->GetArray());
  }
}

bool CPDF_IndexedCS::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  if (pArray->GetCount() < 4)
    return false;

  CPDF_Object* pBaseObj = pArray->GetDirectObjectAt(1);
  if (pBaseObj == m_pArray)
    return false;

  CPDF_DocPageData* pDocPageData = pDoc->GetPageData();
  m_pBaseCS = pDocPageData->GetColorSpace(pBaseObj, nullptr);
  if (!m_pBaseCS)
    return false;

  m_pCountedBaseCS = pDocPageData->FindColorSpacePtr(m_pBaseCS->GetArray());
  m_nBaseComponents = m_pBaseCS->CountComponents();
  m_pCompMinMax = FX_Alloc2D(float, m_nBaseComponents, 2);
  float defvalue;
  for (int i = 0; i < m_nBaseComponents; i++) {
    m_pBaseCS->GetDefaultValue(i, &defvalue, &m_pCompMinMax[i * 2],
                               &m_pCompMinMax[i * 2 + 1]);
    m_pCompMinMax[i * 2 + 1] -= m_pCompMinMax[i * 2];
  }
  m_MaxIndex = pArray->GetIntegerAt(2);

  CPDF_Object* pTableObj = pArray->GetDirectObjectAt(3);
  if (!pTableObj)
    return false;

  if (CPDF_String* pString = pTableObj->AsString()) {
    m_Table = pString->GetString();
  } else if (CPDF_Stream* pStream = pTableObj->AsStream()) {
    auto pAcc = pdfium::MakeRetain<CPDF_StreamAcc>(pStream);
    pAcc->LoadAllData(false);
    m_Table = CFX_ByteStringC(pAcc->GetData(), pAcc->GetSize());
  }
  return true;
}

bool CPDF_IndexedCS::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  int index = static_cast<int32_t>(*pBuf);
  if (index < 0 || index > m_MaxIndex)
    return false;

  if (m_nBaseComponents) {
    if (index == INT_MAX || (index + 1) > INT_MAX / m_nBaseComponents ||
        (index + 1) * m_nBaseComponents > (int)m_Table.GetLength()) {
      R = G = B = 0;
      return false;
    }
  }
  CFX_FixedBufGrow<float, 16> Comps(m_nBaseComponents);
  float* comps = Comps;
  const uint8_t* pTable = m_Table.raw_str();
  for (int i = 0; i < m_nBaseComponents; i++) {
    comps[i] =
        m_pCompMinMax[i * 2] +
        m_pCompMinMax[i * 2 + 1] * pTable[index * m_nBaseComponents + i] / 255;
  }
  return m_pBaseCS->GetRGB(comps, R, G, B);
}

void CPDF_IndexedCS::EnableStdConversion(bool bEnabled) {
  CPDF_ColorSpace::EnableStdConversion(bEnabled);
  if (m_pBaseCS)
    m_pBaseCS->EnableStdConversion(bEnabled);
}

CPDF_SeparationCS::CPDF_SeparationCS(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_SEPARATION, 1) {}

CPDF_SeparationCS::~CPDF_SeparationCS() {}

void CPDF_SeparationCS::GetDefaultValue(int iComponent,
                                        float* value,
                                        float* min,
                                        float* max) const {
  *value = 1.0f;
  *min = 0;
  *max = 1.0f;
}

bool CPDF_SeparationCS::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CFX_ByteString name = pArray->GetStringAt(1);
  if (name == "None") {
    m_Type = None;
    return true;
  }

  m_Type = Colorant;
  CPDF_Object* pAltCS = pArray->GetDirectObjectAt(2);
  if (pAltCS == m_pArray)
    return false;

  m_pAltCS = Load(pDoc, pAltCS);
  if (!m_pAltCS)
    return false;

  CPDF_Object* pFuncObj = pArray->GetDirectObjectAt(3);
  if (pFuncObj && !pFuncObj->IsName())
    m_pFunc = CPDF_Function::Load(pFuncObj);

  if (m_pFunc && m_pFunc->CountOutputs() < m_pAltCS->CountComponents())
    m_pFunc.reset();
  return true;
}

bool CPDF_SeparationCS::GetRGB(float* pBuf,
                               float* R,
                               float* G,
                               float* B) const {
  if (m_Type == None)
    return false;

  if (!m_pFunc) {
    if (!m_pAltCS)
      return false;

    int nComps = m_pAltCS->CountComponents();
    CFX_FixedBufGrow<float, 16> results(nComps);
    for (int i = 0; i < nComps; i++)
      results[i] = *pBuf;
    return m_pAltCS->GetRGB(results, R, G, B);
  }

  CFX_FixedBufGrow<float, 16> results(m_pFunc->CountOutputs());
  int nresults = 0;
  m_pFunc->Call(pBuf, 1, results, &nresults);
  if (nresults == 0)
    return false;

  if (m_pAltCS)
    return m_pAltCS->GetRGB(results, R, G, B);

  R = 0;
  G = 0;
  B = 0;
  return false;
}

void CPDF_SeparationCS::EnableStdConversion(bool bEnabled) {
  CPDF_ColorSpace::EnableStdConversion(bEnabled);
  if (m_pAltCS)
    m_pAltCS->EnableStdConversion(bEnabled);
}

CPDF_DeviceNCS::CPDF_DeviceNCS(CPDF_Document* pDoc)
    : CPDF_ColorSpace(pDoc, PDFCS_DEVICEN, 0) {}

CPDF_DeviceNCS::~CPDF_DeviceNCS() {}

void CPDF_DeviceNCS::GetDefaultValue(int iComponent,
                                     float* value,
                                     float* min,
                                     float* max) const {
  *value = 1.0f;
  *min = 0;
  *max = 1.0f;
}

bool CPDF_DeviceNCS::v_Load(CPDF_Document* pDoc, CPDF_Array* pArray) {
  CPDF_Array* pObj = ToArray(pArray->GetDirectObjectAt(1));
  if (!pObj)
    return false;

  m_nComponents = pObj->GetCount();
  CPDF_Object* pAltCS = pArray->GetDirectObjectAt(2);
  if (!pAltCS || pAltCS == m_pArray)
    return false;

  m_pAltCS = Load(pDoc, pAltCS);
  m_pFunc = CPDF_Function::Load(pArray->GetDirectObjectAt(3));
  if (!m_pAltCS || !m_pFunc)
    return false;

  return m_pFunc->CountOutputs() >= m_pAltCS->CountComponents();
}

bool CPDF_DeviceNCS::GetRGB(float* pBuf, float* R, float* G, float* B) const {
  if (!m_pFunc)
    return false;

  CFX_FixedBufGrow<float, 16> results(m_pFunc->CountOutputs());
  int nresults = 0;
  m_pFunc->Call(pBuf, m_nComponents, results, &nresults);
  if (nresults == 0)
    return false;

  return m_pAltCS->GetRGB(results, R, G, B);
}

void CPDF_DeviceNCS::EnableStdConversion(bool bEnabled) {
  CPDF_ColorSpace::EnableStdConversion(bEnabled);
  if (m_pAltCS) {
    m_pAltCS->EnableStdConversion(bEnabled);
  }
}

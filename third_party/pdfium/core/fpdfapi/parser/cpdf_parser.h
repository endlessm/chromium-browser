// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_PARSER_CPDF_PARSER_H_
#define CORE_FPDFAPI_PARSER_CPDF_PARSER_H_

#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/fxcrt/cfx_retain_ptr.h"
#include "core/fxcrt/cfx_unowned_ptr.h"
#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/fx_system.h"

class CPDF_Array;
class CPDF_CryptoHandler;
class CPDF_Dictionary;
class CPDF_Document;
class CPDF_IndirectObjectHolder;
class CPDF_LinearizedHeader;
class CPDF_Object;
class CPDF_SecurityHandler;
class CPDF_StreamAcc;
class CPDF_SyntaxParser;
class IFX_SeekableReadStream;

class CPDF_Parser {
 public:
  enum Error {
    SUCCESS = 0,
    FILE_ERROR,
    FORMAT_ERROR,
    PASSWORD_ERROR,
    HANDLER_ERROR
  };

  enum class ObjectType : uint8_t {
    kFree = 0x00,
    kNotCompressed = 0x01,
    kCompressed = 0x02,
    kNull = 0xFF,
  };

  // A limit on the maximum object number in the xref table. Theoretical limits
  // are higher, but this may be large enough in practice.
  static const uint32_t kMaxObjectNumber = 1048576;

  static const size_t kInvalidPos = std::numeric_limits<size_t>::max();

  CPDF_Parser();
  ~CPDF_Parser();

  Error StartParse(const CFX_RetainPtr<IFX_SeekableReadStream>& pFile,
                   CPDF_Document* pDocument);
  Error StartLinearizedParse(const CFX_RetainPtr<IFX_SeekableReadStream>& pFile,
                             CPDF_Document* pDocument);

  void SetPassword(const char* password) { m_Password = password; }
  CFX_ByteString GetPassword() { return m_Password; }
  CPDF_Dictionary* GetTrailer() const;
  FX_FILESIZE GetLastXRefOffset() const { return m_LastXRefOffset; }

  uint32_t GetPermissions() const;
  uint32_t GetRootObjNum();
  uint32_t GetInfoObjNum();
  CPDF_Array* GetIDArray();

  CPDF_Dictionary* GetEncryptDict() const { return m_pEncryptDict.Get(); }

  std::unique_ptr<CPDF_Object> ParseIndirectObject(
      CPDF_IndirectObjectHolder* pObjList,
      uint32_t objnum);

  uint32_t GetLastObjNum() const;
  bool IsValidObjectNumber(uint32_t objnum) const;
  FX_FILESIZE GetObjectPositionOrZero(uint32_t objnum) const;
  ObjectType GetObjectType(uint32_t objnum) const;
  uint16_t GetObjectGenNum(uint32_t objnum) const;
  bool IsObjectFreeOrNull(uint32_t objnum) const;
  CFX_RetainPtr<CPDF_CryptoHandler> GetCryptoHandler() const;
  CFX_RetainPtr<IFX_SeekableReadStream> GetFileAccess() const;

  FX_FILESIZE GetObjectOffset(uint32_t objnum) const;

  int GetFileVersion() const { return m_FileVersion; }
  bool IsXRefStream() const { return m_bXRefStream; }

  std::unique_ptr<CPDF_Object> ParseIndirectObjectAt(
      CPDF_IndirectObjectHolder* pObjList,
      FX_FILESIZE pos,
      uint32_t objnum);

  std::unique_ptr<CPDF_Object> ParseIndirectObjectAtByStrict(
      CPDF_IndirectObjectHolder* pObjList,
      FX_FILESIZE pos,
      uint32_t objnum,
      FX_FILESIZE* pResultPos);

  uint32_t GetFirstPageNo() const;

 protected:
  struct ObjectInfo {
    ObjectInfo() : pos(0), type(ObjectType::kFree), gennum(0) {}
    // if type is ObjectType::kCompressed the archive_obj_num should be used.
    // if type is ObjectType::kNotCompressed the pos should be used.
    // In other cases its are unused.
    union {
      FX_FILESIZE pos;
      FX_FILESIZE archive_obj_num;
    };
    ObjectType type;
    uint16_t gennum;
  };

  std::unique_ptr<CPDF_SyntaxParser> m_pSyntax;
  std::map<uint32_t, ObjectInfo> m_ObjectInfo;

  bool LoadCrossRefV4(FX_FILESIZE pos, FX_FILESIZE streampos, bool bSkip);
  bool RebuildCrossRef();

 private:
  friend class CPDF_DataAvail;

  class TrailerData;

  enum class ParserState {
    kDefault,
    kComment,
    kWhitespace,
    kString,
    kHexString,
    kEscapedString,
    kXref,
    kObjNum,
    kPostObjNum,
    kGenNum,
    kPostGenNum,
    kTrailer,
    kBeginObj,
    kEndObj
  };

  struct CrossRefObjData {
    uint32_t obj_num = 0;
    ObjectInfo info;
  };

  Error StartParseInternal(CPDF_Document* pDocument);
  CPDF_Object* ParseDirect(CPDF_Object* pObj);
  bool LoadAllCrossRefV4(FX_FILESIZE pos);
  bool LoadAllCrossRefV5(FX_FILESIZE pos);
  bool LoadCrossRefV5(FX_FILESIZE* pos, bool bMainXRef);
  std::unique_ptr<CPDF_Dictionary> LoadTrailerV4();
  Error SetEncryptHandler();
  void ReleaseEncryptHandler();
  bool LoadLinearizedAllCrossRefV4(FX_FILESIZE pos, uint32_t dwObjCount);
  bool LoadLinearizedCrossRefV4(FX_FILESIZE pos, uint32_t dwObjCount);
  bool LoadLinearizedAllCrossRefV5(FX_FILESIZE pos);
  Error LoadLinearizedMainXRefTable();
  CFX_RetainPtr<CPDF_StreamAcc> GetObjectStream(uint32_t number);
  bool ParseLinearizedHeader();
  void SetEncryptDictionary(CPDF_Dictionary* pDict);
  void ShrinkObjectMap(uint32_t size);
  // A simple check whether the cross reference table matches with
  // the objects.
  bool VerifyCrossRefV4();

  // If out_objects is null, the parser position will be moved to end subsection
  // without additional validation.
  bool ParseAndAppendCrossRefSubsectionData(
      uint32_t start_objnum,
      uint32_t count,
      std::vector<CrossRefObjData>* out_objects);
  bool ParseCrossRefV4(std::vector<CrossRefObjData>* out_objects);
  void MergeCrossRefObjectsData(const std::vector<CrossRefObjData>& objects);

  std::unique_ptr<CPDF_Object> ParseIndirectObjectAtInternal(
      CPDF_IndirectObjectHolder* pObjList,
      FX_FILESIZE pos,
      uint32_t objnum,
      bool strict_parse,
      FX_FILESIZE* pResultPos);

  bool InitSyntaxParser(
      const CFX_RetainPtr<IFX_SeekableReadStream>& file_access);
  bool ParseFileVersion();

  CFX_UnownedPtr<CPDF_Document> m_pDocument;
  bool m_bHasParsed;
  bool m_bXRefStream;
  int m_FileVersion;
  CFX_UnownedPtr<CPDF_Dictionary> m_pEncryptDict;
  FX_FILESIZE m_LastXRefOffset;
  std::unique_ptr<CPDF_SecurityHandler> m_pSecurityHandler;
  CFX_ByteString m_Password;
  std::unique_ptr<TrailerData> m_TrailerData;
  std::unique_ptr<CPDF_LinearizedHeader> m_pLinearized;
  uint32_t m_dwLinearizedFirstPageXRefStartObjNum;

  // A map of object numbers to indirect streams.
  std::map<uint32_t, CFX_RetainPtr<CPDF_StreamAcc>> m_ObjectStreamMap;

  // Mapping of object numbers to offsets. The offsets are relative to the first
  // object in the stream.
  using StreamObjectCache = std::map<uint32_t, uint32_t>;

  // Mapping of streams to their object caches. This is valid as long as the
  // streams in |m_ObjectStreamMap| are valid.
  std::map<CFX_RetainPtr<CPDF_StreamAcc>, StreamObjectCache> m_ObjCache;

  // All indirect object numbers that are being parsed.
  std::set<uint32_t> m_ParsingObjNums;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_PARSER_H_

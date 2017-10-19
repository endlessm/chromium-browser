// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fxcrt/cfx_widestring.h"
#include "core/fxcrt/fx_string.h"

#include <algorithm>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

TEST(fxcrt, WideStringElementAccess) {
  const CFX_WideString abc(L"abc");
#ifndef NDEBUG
  EXPECT_DEATH({ abc[-1]; }, ".*");
#endif
  EXPECT_EQ(L'a', abc[0]);
  EXPECT_EQ(L'b', abc[1]);
  EXPECT_EQ(L'c', abc[2]);
#ifndef NDEBUG
  EXPECT_DEATH({ abc[4]; }, ".*");
#endif

  CFX_WideString mutable_abc = abc;
  EXPECT_EQ(abc.c_str(), mutable_abc.c_str());
  EXPECT_EQ(L'a', mutable_abc[0]);
  EXPECT_EQ(L'b', mutable_abc[1]);
  EXPECT_EQ(L'c', mutable_abc[2]);
  EXPECT_EQ(abc.c_str(), mutable_abc.c_str());
#ifndef NDEBUG
  EXPECT_DEATH({ mutable_abc.SetAt(-1, L'd'); }, ".*");
  EXPECT_EQ(L"abc", abc);
#endif
  const wchar_t* c_str = abc.c_str();
  mutable_abc.SetAt(0, L'd');
  EXPECT_EQ(c_str, abc.c_str());
  EXPECT_NE(c_str, mutable_abc.c_str());
  EXPECT_EQ(L"abc", abc);
  EXPECT_EQ(L"dbc", mutable_abc);

  mutable_abc.SetAt(1, L'e');
  EXPECT_EQ(L"abc", abc);
  EXPECT_EQ(L"dec", mutable_abc);

  mutable_abc.SetAt(2, L'f');
  EXPECT_EQ(L"abc", abc);
  EXPECT_EQ(L"def", mutable_abc);
#ifndef NDEBUG
  EXPECT_DEATH({ mutable_abc.SetAt(3, L'g'); }, ".*");
  EXPECT_EQ(L"abc", abc);
#endif
}

TEST(fxcrt, WideStringOperatorLT) {
  CFX_WideString empty;
  CFX_WideString a(L"a");
  CFX_WideString abc(L"\x0110qq");  // Comes before despite endianness.
  CFX_WideString def(L"\x1001qq");  // Comes after despite endianness.

  EXPECT_FALSE(empty < empty);
  EXPECT_FALSE(a < a);
  EXPECT_FALSE(abc < abc);
  EXPECT_FALSE(def < def);

  EXPECT_TRUE(empty < a);
  EXPECT_FALSE(a < empty);

  EXPECT_TRUE(empty < abc);
  EXPECT_FALSE(abc < empty);

  EXPECT_TRUE(empty < def);
  EXPECT_FALSE(def < empty);

  EXPECT_TRUE(a < abc);
  EXPECT_FALSE(abc < a);

  EXPECT_TRUE(a < def);
  EXPECT_FALSE(def < a);

  EXPECT_TRUE(abc < def);
  EXPECT_FALSE(def < abc);
}

TEST(fxcrt, WideStringOperatorEQ) {
  CFX_WideString null_string;
  EXPECT_TRUE(null_string == null_string);

  CFX_WideString empty_string(L"");
  EXPECT_TRUE(empty_string == empty_string);
  EXPECT_TRUE(empty_string == null_string);
  EXPECT_TRUE(null_string == empty_string);

  CFX_WideString deleted_string(L"hello");
  deleted_string.Delete(0, 5);
  EXPECT_TRUE(deleted_string == deleted_string);
  EXPECT_TRUE(deleted_string == null_string);
  EXPECT_TRUE(deleted_string == empty_string);
  EXPECT_TRUE(null_string == deleted_string);
  EXPECT_TRUE(null_string == empty_string);

  CFX_WideString wide_string(L"hello");
  EXPECT_TRUE(wide_string == wide_string);
  EXPECT_FALSE(wide_string == null_string);
  EXPECT_FALSE(wide_string == empty_string);
  EXPECT_FALSE(wide_string == deleted_string);
  EXPECT_FALSE(null_string == wide_string);
  EXPECT_FALSE(empty_string == wide_string);
  EXPECT_FALSE(deleted_string == wide_string);

  CFX_WideString wide_string_same1(L"hello");
  EXPECT_TRUE(wide_string == wide_string_same1);
  EXPECT_TRUE(wide_string_same1 == wide_string);

  CFX_WideString wide_string_same2(wide_string);
  EXPECT_TRUE(wide_string == wide_string_same2);
  EXPECT_TRUE(wide_string_same2 == wide_string);

  CFX_WideString wide_string1(L"he");
  CFX_WideString wide_string2(L"hellp");
  CFX_WideString wide_string3(L"hellod");
  EXPECT_FALSE(wide_string == wide_string1);
  EXPECT_FALSE(wide_string == wide_string2);
  EXPECT_FALSE(wide_string == wide_string3);
  EXPECT_FALSE(wide_string1 == wide_string);
  EXPECT_FALSE(wide_string2 == wide_string);
  EXPECT_FALSE(wide_string3 == wide_string);

  CFX_WideStringC null_string_c;
  CFX_WideStringC empty_string_c(L"");
  EXPECT_TRUE(null_string == null_string_c);
  EXPECT_TRUE(null_string == empty_string_c);
  EXPECT_TRUE(empty_string == null_string_c);
  EXPECT_TRUE(empty_string == empty_string_c);
  EXPECT_TRUE(deleted_string == null_string_c);
  EXPECT_TRUE(deleted_string == empty_string_c);
  EXPECT_TRUE(null_string_c == null_string);
  EXPECT_TRUE(empty_string_c == null_string);
  EXPECT_TRUE(null_string_c == empty_string);
  EXPECT_TRUE(empty_string_c == empty_string);
  EXPECT_TRUE(null_string_c == deleted_string);
  EXPECT_TRUE(empty_string_c == deleted_string);

  CFX_WideStringC wide_string_c_same1(L"hello");
  EXPECT_TRUE(wide_string == wide_string_c_same1);
  EXPECT_TRUE(wide_string_c_same1 == wide_string);

  CFX_WideStringC wide_string_c1(L"he");
  CFX_WideStringC wide_string_c2(L"hellp");
  CFX_WideStringC wide_string_c3(L"hellod");
  EXPECT_FALSE(wide_string == wide_string_c1);
  EXPECT_FALSE(wide_string == wide_string_c2);
  EXPECT_FALSE(wide_string == wide_string_c3);
  EXPECT_FALSE(wide_string_c1 == wide_string);
  EXPECT_FALSE(wide_string_c2 == wide_string);
  EXPECT_FALSE(wide_string_c3 == wide_string);

  const wchar_t* c_null_string = nullptr;
  const wchar_t* c_empty_string = L"";
  EXPECT_TRUE(null_string == c_null_string);
  EXPECT_TRUE(null_string == c_empty_string);
  EXPECT_TRUE(empty_string == c_null_string);
  EXPECT_TRUE(empty_string == c_empty_string);
  EXPECT_TRUE(deleted_string == c_null_string);
  EXPECT_TRUE(deleted_string == c_empty_string);
  EXPECT_TRUE(c_null_string == null_string);
  EXPECT_TRUE(c_empty_string == null_string);
  EXPECT_TRUE(c_null_string == empty_string);
  EXPECT_TRUE(c_empty_string == empty_string);
  EXPECT_TRUE(c_null_string == deleted_string);
  EXPECT_TRUE(c_empty_string == deleted_string);

  const wchar_t* c_string_same1 = L"hello";
  EXPECT_TRUE(wide_string == c_string_same1);
  EXPECT_TRUE(c_string_same1 == wide_string);

  const wchar_t* c_string1 = L"he";
  const wchar_t* c_string2 = L"hellp";
  const wchar_t* c_string3 = L"hellod";
  EXPECT_FALSE(wide_string == c_string1);
  EXPECT_FALSE(wide_string == c_string2);
  EXPECT_FALSE(wide_string == c_string3);
  EXPECT_FALSE(c_string1 == wide_string);
  EXPECT_FALSE(c_string2 == wide_string);
  EXPECT_FALSE(c_string3 == wide_string);
}

TEST(fxcrt, WideStringOperatorNE) {
  CFX_WideString null_string;
  EXPECT_FALSE(null_string != null_string);

  CFX_WideString empty_string(L"");
  EXPECT_FALSE(empty_string != empty_string);
  EXPECT_FALSE(empty_string != null_string);
  EXPECT_FALSE(null_string != empty_string);

  CFX_WideString deleted_string(L"hello");
  deleted_string.Delete(0, 5);
  EXPECT_FALSE(deleted_string != deleted_string);
  EXPECT_FALSE(deleted_string != null_string);
  EXPECT_FALSE(deleted_string != empty_string);
  EXPECT_FALSE(null_string != deleted_string);
  EXPECT_FALSE(null_string != empty_string);

  CFX_WideString wide_string(L"hello");
  EXPECT_FALSE(wide_string != wide_string);
  EXPECT_TRUE(wide_string != null_string);
  EXPECT_TRUE(wide_string != empty_string);
  EXPECT_TRUE(wide_string != deleted_string);
  EXPECT_TRUE(null_string != wide_string);
  EXPECT_TRUE(empty_string != wide_string);
  EXPECT_TRUE(deleted_string != wide_string);

  CFX_WideString wide_string_same1(L"hello");
  EXPECT_FALSE(wide_string != wide_string_same1);
  EXPECT_FALSE(wide_string_same1 != wide_string);

  CFX_WideString wide_string_same2(wide_string);
  EXPECT_FALSE(wide_string != wide_string_same2);
  EXPECT_FALSE(wide_string_same2 != wide_string);

  CFX_WideString wide_string1(L"he");
  CFX_WideString wide_string2(L"hellp");
  CFX_WideString wide_string3(L"hellod");
  EXPECT_TRUE(wide_string != wide_string1);
  EXPECT_TRUE(wide_string != wide_string2);
  EXPECT_TRUE(wide_string != wide_string3);
  EXPECT_TRUE(wide_string1 != wide_string);
  EXPECT_TRUE(wide_string2 != wide_string);
  EXPECT_TRUE(wide_string3 != wide_string);

  CFX_WideStringC null_string_c;
  CFX_WideStringC empty_string_c(L"");
  EXPECT_FALSE(null_string != null_string_c);
  EXPECT_FALSE(null_string != empty_string_c);
  EXPECT_FALSE(empty_string != null_string_c);
  EXPECT_FALSE(empty_string != empty_string_c);
  EXPECT_FALSE(deleted_string != null_string_c);
  EXPECT_FALSE(deleted_string != empty_string_c);
  EXPECT_FALSE(null_string_c != null_string);
  EXPECT_FALSE(empty_string_c != null_string);
  EXPECT_FALSE(null_string_c != empty_string);
  EXPECT_FALSE(empty_string_c != empty_string);

  CFX_WideStringC wide_string_c_same1(L"hello");
  EXPECT_FALSE(wide_string != wide_string_c_same1);
  EXPECT_FALSE(wide_string_c_same1 != wide_string);

  CFX_WideStringC wide_string_c1(L"he");
  CFX_WideStringC wide_string_c2(L"hellp");
  CFX_WideStringC wide_string_c3(L"hellod");
  EXPECT_TRUE(wide_string != wide_string_c1);
  EXPECT_TRUE(wide_string != wide_string_c2);
  EXPECT_TRUE(wide_string != wide_string_c3);
  EXPECT_TRUE(wide_string_c1 != wide_string);
  EXPECT_TRUE(wide_string_c2 != wide_string);
  EXPECT_TRUE(wide_string_c3 != wide_string);

  const wchar_t* c_null_string = nullptr;
  const wchar_t* c_empty_string = L"";
  EXPECT_FALSE(null_string != c_null_string);
  EXPECT_FALSE(null_string != c_empty_string);
  EXPECT_FALSE(empty_string != c_null_string);
  EXPECT_FALSE(empty_string != c_empty_string);
  EXPECT_FALSE(deleted_string != c_null_string);
  EXPECT_FALSE(deleted_string != c_empty_string);
  EXPECT_FALSE(c_null_string != null_string);
  EXPECT_FALSE(c_empty_string != null_string);
  EXPECT_FALSE(c_null_string != empty_string);
  EXPECT_FALSE(c_empty_string != empty_string);
  EXPECT_FALSE(c_null_string != deleted_string);
  EXPECT_FALSE(c_empty_string != deleted_string);

  const wchar_t* c_string_same1 = L"hello";
  EXPECT_FALSE(wide_string != c_string_same1);
  EXPECT_FALSE(c_string_same1 != wide_string);

  const wchar_t* c_string1 = L"he";
  const wchar_t* c_string2 = L"hellp";
  const wchar_t* c_string3 = L"hellod";
  EXPECT_TRUE(wide_string != c_string1);
  EXPECT_TRUE(wide_string != c_string2);
  EXPECT_TRUE(wide_string != c_string3);
  EXPECT_TRUE(c_string1 != wide_string);
  EXPECT_TRUE(c_string2 != wide_string);
  EXPECT_TRUE(c_string3 != wide_string);
}

TEST(fxcrt, WideStringConcatInPlace) {
  CFX_WideString fred;
  fred.Concat(L"FRED", 4);
  EXPECT_EQ(L"FRED", fred);

  fred.Concat(L"DY", 2);
  EXPECT_EQ(L"FREDDY", fred);

  fred.Delete(3, 3);
  EXPECT_EQ(L"FRE", fred);

  fred.Concat(L"D", 1);
  EXPECT_EQ(L"FRED", fred);

  CFX_WideString copy = fred;
  fred.Concat(L"DY", 2);
  EXPECT_EQ(L"FREDDY", fred);
  EXPECT_EQ(L"FRED", copy);
}

TEST(fxcrt, WideStringRemove) {
  CFX_WideString freed(L"FREED");
  freed.Remove(L'E');
  EXPECT_EQ(L"FRD", freed);
  freed.Remove(L'F');
  EXPECT_EQ(L"RD", freed);
  freed.Remove(L'D');
  EXPECT_EQ(L"R", freed);
  freed.Remove(L'X');
  EXPECT_EQ(L"R", freed);
  freed.Remove(L'R');
  EXPECT_EQ(L"", freed);

  CFX_WideString empty;
  empty.Remove(L'X');
  EXPECT_EQ(L"", empty);
}

TEST(fxcrt, WideStringRemoveCopies) {
  CFX_WideString freed(L"FREED");
  const wchar_t* old_buffer = freed.c_str();

  // No change with single reference - no copy.
  freed.Remove(L'Q');
  EXPECT_EQ(L"FREED", freed);
  EXPECT_EQ(old_buffer, freed.c_str());

  // Change with single reference - no copy.
  freed.Remove(L'E');
  EXPECT_EQ(L"FRD", freed);
  EXPECT_EQ(old_buffer, freed.c_str());

  // No change with multiple references - no copy.
  CFX_WideString shared(freed);
  freed.Remove(L'Q');
  EXPECT_EQ(L"FRD", freed);
  EXPECT_EQ(old_buffer, freed.c_str());
  EXPECT_EQ(old_buffer, shared.c_str());

  // Change with multiple references -- must copy.
  freed.Remove(L'D');
  EXPECT_EQ(L"FR", freed);
  EXPECT_NE(old_buffer, freed.c_str());
  EXPECT_EQ(L"FRD", shared);
  EXPECT_EQ(old_buffer, shared.c_str());
}

TEST(fxcrt, WideStringReplace) {
  CFX_WideString fred(L"FRED");
  fred.Replace(L"FR", L"BL");
  EXPECT_EQ(L"BLED", fred);
  fred.Replace(L"D", L"DDY");
  EXPECT_EQ(L"BLEDDY", fred);
  fred.Replace(L"LEDD", L"");
  EXPECT_EQ(L"BY", fred);
  fred.Replace(L"X", L"CLAMS");
  EXPECT_EQ(L"BY", fred);
  fred.Replace(L"BY", L"HI");
  EXPECT_EQ(L"HI", fred);
  fred.Replace(L"", L"CLAMS");
  EXPECT_EQ(L"HI", fred);
  fred.Replace(L"HI", L"");
  EXPECT_EQ(L"", fred);
}

TEST(fxcrt, WideStringInsert) {
  CFX_WideString fred(L"FRED");
  EXPECT_EQ(4, fred.Insert(-1, 'X'));
  EXPECT_EQ(L"FRED", fred);
  EXPECT_EQ(5, fred.Insert(0, 'S'));
  EXPECT_EQ(L"SFRED", fred);
  EXPECT_EQ(6, fred.Insert(1, 'T'));
  EXPECT_EQ(L"STFRED", fred);
  EXPECT_EQ(7, fred.Insert(4, 'U'));
  EXPECT_EQ(L"STFRUED", fred);
  EXPECT_EQ(8, fred.Insert(7, 'V'));
  EXPECT_EQ(L"STFRUEDV", fred);
  EXPECT_EQ(8, fred.Insert(12, 'P'));
  EXPECT_EQ(L"STFRUEDV", fred);
  {
    CFX_WideString empty;
    EXPECT_EQ(0, empty.Insert(-1, 'X'));
    EXPECT_NE(L"X", empty);
  }
  {
    CFX_WideString empty;
    EXPECT_EQ(1, empty.Insert(0, 'X'));
    EXPECT_EQ(L"X", empty);
  }
  {
    CFX_WideString empty;
    EXPECT_EQ(0, empty.Insert(5, 'X'));
    EXPECT_NE(L"X", empty);
  }
}

TEST(fxcrt, WideStringInsertAtFrontAndInsertAtBack) {
  {
    CFX_WideString empty;
    EXPECT_EQ(1, empty.InsertAtFront('D'));
    EXPECT_EQ(L"D", empty);
    EXPECT_EQ(2, empty.InsertAtFront('E'));
    EXPECT_EQ(L"ED", empty);
    EXPECT_EQ(3, empty.InsertAtFront('R'));
    EXPECT_EQ(L"RED", empty);
    EXPECT_EQ(4, empty.InsertAtFront('F'));
    EXPECT_EQ(L"FRED", empty);
  }
  {
    CFX_WideString empty;
    EXPECT_EQ(1, empty.InsertAtBack('F'));
    EXPECT_EQ(L"F", empty);
    EXPECT_EQ(2, empty.InsertAtBack('R'));
    EXPECT_EQ(L"FR", empty);
    EXPECT_EQ(3, empty.InsertAtBack('E'));
    EXPECT_EQ(L"FRE", empty);
    EXPECT_EQ(4, empty.InsertAtBack('D'));
    EXPECT_EQ(L"FRED", empty);
  }
  {
    CFX_WideString empty;
    EXPECT_EQ(1, empty.InsertAtBack('E'));
    EXPECT_EQ(L"E", empty);
    EXPECT_EQ(2, empty.InsertAtFront('R'));
    EXPECT_EQ(L"RE", empty);
    EXPECT_EQ(3, empty.InsertAtBack('D'));
    EXPECT_EQ(L"RED", empty);
    EXPECT_EQ(4, empty.InsertAtFront('F'));
    EXPECT_EQ(L"FRED", empty);
  }
}

TEST(fxcrt, WideStringDelete) {
  CFX_WideString fred(L"FRED");
  EXPECT_EQ(4, fred.Delete(0, 0));
  EXPECT_EQ(L"FRED", fred);
  EXPECT_EQ(2, fred.Delete(0, 2));
  EXPECT_EQ(L"ED", fred);
  EXPECT_EQ(1, fred.Delete(1));
  EXPECT_EQ(L"E", fred);
  EXPECT_EQ(1, fred.Delete(-1));
  EXPECT_EQ(L"E", fred);
  EXPECT_EQ(0, fred.Delete(0));
  EXPECT_EQ(L"", fred);
  EXPECT_EQ(0, fred.Delete(0));
  EXPECT_EQ(L"", fred);

  CFX_WideString empty;
  EXPECT_EQ(0, empty.Delete(0));
  EXPECT_EQ(L"", empty);
  EXPECT_EQ(0, empty.Delete(-1));
  EXPECT_EQ(L"", empty);
  EXPECT_EQ(0, empty.Delete(1));
  EXPECT_EQ(L"", empty);
}

TEST(fxcrt, WideStringMid) {
  CFX_WideString fred(L"FRED");
  EXPECT_EQ(L"", fred.Mid(0, 0));
  EXPECT_EQ(L"", fred.Mid(3, 0));
  EXPECT_EQ(L"FRED", fred.Mid(0, 4));
  EXPECT_EQ(L"RED", fred.Mid(1, 3));
  EXPECT_EQ(L"ED", fred.Mid(2, 2));
  EXPECT_EQ(L"D", fred.Mid(3, 1));
  EXPECT_EQ(L"F", fred.Mid(0, 1));
  EXPECT_EQ(L"R", fred.Mid(1, 1));
  EXPECT_EQ(L"E", fred.Mid(2, 1));
  EXPECT_EQ(L"D", fred.Mid(3, 1));
  EXPECT_EQ(L"FR", fred.Mid(0, 2));
  EXPECT_EQ(L"FRED", fred.Mid(0, 4));
  EXPECT_EQ(L"", fred.Mid(0, 10));

  EXPECT_EQ(L"", fred.Mid(-1, 2));
  EXPECT_EQ(L"", fred.Mid(1, 4));
  EXPECT_EQ(L"", fred.Mid(4, 1));

  CFX_WideString empty;
  EXPECT_EQ(L"", empty.Mid(0, 0));
}

TEST(fxcrt, WideStringLeft) {
  CFX_WideString fred(L"FRED");
  EXPECT_EQ(L"", fred.Left(0));
  EXPECT_EQ(L"F", fred.Left(1));
  EXPECT_EQ(L"FR", fred.Left(2));
  EXPECT_EQ(L"FRE", fred.Left(3));
  EXPECT_EQ(L"FRED", fred.Left(4));

  EXPECT_EQ(L"", fred.Left(5));
  EXPECT_EQ(L"", fred.Left(-1));

  CFX_WideString empty;
  EXPECT_EQ(L"", empty.Left(0));
  EXPECT_EQ(L"", empty.Left(1));
  EXPECT_EQ(L"", empty.Left(-1));
}

TEST(fxcrt, WideStringRight) {
  CFX_WideString fred(L"FRED");
  EXPECT_EQ(L"", fred.Right(0));
  EXPECT_EQ(L"D", fred.Right(1));
  EXPECT_EQ(L"ED", fred.Right(2));
  EXPECT_EQ(L"RED", fred.Right(3));
  EXPECT_EQ(L"FRED", fred.Right(4));

  EXPECT_EQ(L"", fred.Right(5));
  EXPECT_EQ(L"", fred.Right(-1));

  CFX_WideString empty;
  EXPECT_EQ(L"", empty.Right(0));
  EXPECT_EQ(L"", empty.Right(1));
  EXPECT_EQ(L"", empty.Right(-1));
}

TEST(fxcrt, WideStringFind) {
  CFX_WideString null_string;
  EXPECT_FALSE(null_string.Find(L'a').has_value());
  EXPECT_FALSE(null_string.Find(L'\0').has_value());

  CFX_WideString empty_string(L"");
  EXPECT_FALSE(empty_string.Find(L'a').has_value());
  EXPECT_FALSE(empty_string.Find(L'\0').has_value());

  pdfium::Optional<FX_STRSIZE> result;
  CFX_WideString single_string(L"a");
  result = single_string.Find(L'a');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(0, result.value());
  EXPECT_FALSE(single_string.Find(L'b').has_value());
  EXPECT_FALSE(single_string.Find(L'\0').has_value());

  CFX_WideString longer_string(L"abccc");
  result = longer_string.Find(L'a');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(0, result.value());
  result = longer_string.Find(L'c');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result.value());
  result = longer_string.Find(L'c', 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(3, result.value());
  EXPECT_FALSE(longer_string.Find(L'\0').has_value());

  result = longer_string.Find(L"ab");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(0, result.value());
  result = longer_string.Find(L"ccc");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result.value());
  result = longer_string.Find(L"cc", 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(3, result.value());
  EXPECT_FALSE(longer_string.Find(L"d").has_value());

  CFX_WideString hibyte_string(
      L"ab\xff8c"
      L"def");
  result = hibyte_string.Find(L'\xff8c');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result.value());
}

TEST(fxcrt, WideStringUpperLower) {
  CFX_WideString fred(L"F-Re.42D");
  fred.MakeLower();
  EXPECT_EQ(L"f-re.42d", fred);
  fred.MakeUpper();
  EXPECT_EQ(L"F-RE.42D", fred);

  CFX_WideString empty;
  empty.MakeLower();
  EXPECT_EQ(L"", empty);
  empty.MakeUpper();
  EXPECT_EQ(L"", empty);
}

TEST(fxcrt, WideStringTrimRight) {
  CFX_WideString fred(L"  FRED  ");
  fred.TrimRight();
  EXPECT_EQ(L"  FRED", fred);
  fred.TrimRight(L'E');
  EXPECT_EQ(L"  FRED", fred);
  fred.TrimRight(L'D');
  EXPECT_EQ(L"  FRE", fred);
  fred.TrimRight(L"ERP");
  EXPECT_EQ(L"  F", fred);

  CFX_WideString blank(L"   ");
  blank.TrimRight(L"ERP");
  EXPECT_EQ(L"   ", blank);
  blank.TrimRight(L'E');
  EXPECT_EQ(L"   ", blank);
  blank.TrimRight();
  EXPECT_EQ(L"", blank);

  CFX_WideString empty;
  empty.TrimRight(L"ERP");
  EXPECT_EQ(L"", empty);
  empty.TrimRight(L'E');
  EXPECT_EQ(L"", empty);
  empty.TrimRight();
  EXPECT_EQ(L"", empty);
}

TEST(fxcrt, WideStringTrimRightCopies) {
  {
    // With a single reference, no copy takes place.
    CFX_WideString fred(L"  FRED  ");
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimRight();
    EXPECT_EQ(L"  FRED", fred);
    EXPECT_EQ(old_buffer, fred.c_str());
  }
  {
    // With multiple references, we must copy.
    CFX_WideString fred(L"  FRED  ");
    CFX_WideString other_fred = fred;
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimRight();
    EXPECT_EQ(L"  FRED", fred);
    EXPECT_EQ(L"  FRED  ", other_fred);
    EXPECT_NE(old_buffer, fred.c_str());
  }
  {
    // With multiple references, but no modifications, no copy.
    CFX_WideString fred(L"FRED");
    CFX_WideString other_fred = fred;
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimRight();
    EXPECT_EQ(L"FRED", fred);
    EXPECT_EQ(L"FRED", other_fred);
    EXPECT_EQ(old_buffer, fred.c_str());
  }
}

TEST(fxcrt, WideStringTrimLeft) {
  CFX_WideString fred(L"  FRED  ");
  fred.TrimLeft();
  EXPECT_EQ(L"FRED  ", fred);
  fred.TrimLeft(L'E');
  EXPECT_EQ(L"FRED  ", fred);
  fred.TrimLeft(L'F');
  EXPECT_EQ(L"RED  ", fred);
  fred.TrimLeft(L"ERP");
  EXPECT_EQ(L"D  ", fred);

  CFX_WideString blank(L"   ");
  blank.TrimLeft(L"ERP");
  EXPECT_EQ(L"   ", blank);
  blank.TrimLeft(L'E');
  EXPECT_EQ(L"   ", blank);
  blank.TrimLeft();
  EXPECT_EQ(L"", blank);

  CFX_WideString empty;
  empty.TrimLeft(L"ERP");
  EXPECT_EQ(L"", empty);
  empty.TrimLeft(L'E');
  EXPECT_EQ(L"", empty);
  empty.TrimLeft();
  EXPECT_EQ(L"", empty);
}

TEST(fxcrt, WideStringTrimLeftCopies) {
  {
    // With a single reference, no copy takes place.
    CFX_WideString fred(L"  FRED  ");
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimLeft();
    EXPECT_EQ(L"FRED  ", fred);
    EXPECT_EQ(old_buffer, fred.c_str());
  }
  {
    // With multiple references, we must copy.
    CFX_WideString fred(L"  FRED  ");
    CFX_WideString other_fred = fred;
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimLeft();
    EXPECT_EQ(L"FRED  ", fred);
    EXPECT_EQ(L"  FRED  ", other_fred);
    EXPECT_NE(old_buffer, fred.c_str());
  }
  {
    // With multiple references, but no modifications, no copy.
    CFX_WideString fred(L"FRED");
    CFX_WideString other_fred = fred;
    const wchar_t* old_buffer = fred.c_str();
    fred.TrimLeft();
    EXPECT_EQ(L"FRED", fred);
    EXPECT_EQ(L"FRED", other_fred);
    EXPECT_EQ(old_buffer, fred.c_str());
  }
}

TEST(fxcrt, WideStringReserve) {
  {
    CFX_WideString str;
    str.Reserve(6);
    const wchar_t* old_buffer = str.c_str();
    str += L"ABCDEF";
    EXPECT_EQ(old_buffer, str.c_str());
    str += L"Blah Blah Blah Blah Blah Blah";
    EXPECT_NE(old_buffer, str.c_str());
  }
  {
    CFX_WideString str(L"A");
    str.Reserve(6);
    const wchar_t* old_buffer = str.c_str();
    str += L"BCDEF";
    EXPECT_EQ(old_buffer, str.c_str());
    str += L"Blah Blah Blah Blah Blah Blah";
    EXPECT_NE(old_buffer, str.c_str());
  }
}

TEST(fxcrt, WideStringGetBuffer) {
  {
    CFX_WideString str;
    wchar_t* buffer = str.GetBuffer(12);
    wcscpy(buffer, L"clams");
    str.ReleaseBuffer(str.GetStringLength());
    EXPECT_EQ(L"clams", str);
  }
  {
    CFX_WideString str(L"cl");
    wchar_t* buffer = str.GetBuffer(12);
    wcscpy(buffer + 2, L"ams");
    str.ReleaseBuffer(str.GetStringLength());
    EXPECT_EQ(L"clams", str);
  }
}

TEST(fxcrt, WideStringReleaseBuffer) {
  {
    CFX_WideString str;
    str.Reserve(12);
    str += L"clams";
    const wchar_t* old_buffer = str.c_str();
    str.ReleaseBuffer(4);
    EXPECT_EQ(old_buffer, str.c_str());
    EXPECT_EQ(L"clam", str);
  }
  {
    CFX_WideString str(L"c");
    str.Reserve(12);
    str += L"lams";
    const wchar_t* old_buffer = str.c_str();
    str.ReleaseBuffer(4);
    EXPECT_EQ(old_buffer, str.c_str());
    EXPECT_EQ(L"clam", str);
  }
  {
    CFX_WideString str;
    str.Reserve(200);
    str += L"clams";
    const wchar_t* old_buffer = str.c_str();
    str.ReleaseBuffer(4);
    EXPECT_NE(old_buffer, str.c_str());
    EXPECT_EQ(L"clam", str);
  }
  {
    CFX_WideString str(L"c");
    str.Reserve(200);
    str += L"lams";
    const wchar_t* old_buffer = str.c_str();
    str.ReleaseBuffer(4);
    EXPECT_NE(old_buffer, str.c_str());
    EXPECT_EQ(L"clam", str);
  }
}

TEST(fxcrt, WideStringUTF16LE_Encode) {
  struct UTF16LEEncodeCase {
    CFX_WideString ws;
    CFX_ByteString bs;
  } utf16le_encode_cases[] = {
      {L"", CFX_ByteString("\0\0", 2)},
      {L"abc", CFX_ByteString("a\0b\0c\0\0\0", 8)},
      {L"abcdef", CFX_ByteString("a\0b\0c\0d\0e\0f\0\0\0", 14)},
      {L"abc\0def", CFX_ByteString("a\0b\0c\0\0\0", 8)},
      {L"\xaabb\xccdd", CFX_ByteString("\xbb\xaa\xdd\xcc\0\0", 6)},
      {L"\x3132\x6162", CFX_ByteString("\x32\x31\x62\x61\0\0", 6)},
  };

  for (size_t i = 0; i < FX_ArraySize(utf16le_encode_cases); ++i) {
    EXPECT_EQ(utf16le_encode_cases[i].bs,
              utf16le_encode_cases[i].ws.UTF16LE_Encode())
        << " for case number " << i;
  }
}

TEST(fxcrt, WideStringCFromVector) {
  std::vector<CFX_WideStringC::UnsignedType> null_vec;
  CFX_WideStringC null_string(null_vec);
  EXPECT_EQ(0, null_string.GetLength());

  std::vector<CFX_WideStringC::UnsignedType> lower_a_vec(
      10, static_cast<CFX_WideStringC::UnsignedType>(L'a'));
  CFX_WideStringC lower_a_string(lower_a_vec);
  EXPECT_EQ(10, lower_a_string.GetLength());
  EXPECT_EQ(L"aaaaaaaaaa", lower_a_string);

  std::vector<CFX_WideStringC::UnsignedType> cleared_vec;
  cleared_vec.push_back(42);
  cleared_vec.pop_back();
  CFX_WideStringC cleared_string(cleared_vec);
  EXPECT_EQ(0, cleared_string.GetLength());
  EXPECT_EQ(nullptr, cleared_string.raw_str());
}

TEST(fxcrt, WideStringCElementAccess) {
  CFX_WideStringC abc(L"abc");
#ifndef NDEBUG
  EXPECT_DEATH({ abc[-1]; }, ".*");
#endif
  EXPECT_EQ(L'a', static_cast<wchar_t>(abc[0]));
  EXPECT_EQ(L'b', static_cast<wchar_t>(abc[1]));
  EXPECT_EQ(L'c', static_cast<wchar_t>(abc[2]));
#ifndef NDEBUG
  EXPECT_DEATH({ abc[4]; }, ".*");
#endif
}

TEST(fxcrt, WideStringCOperatorLT) {
  CFX_WideStringC empty;
  CFX_WideStringC a(L"a");
  CFX_WideStringC abc(L"\x0110qq");  // Comes InsertAtFront despite endianness.
  CFX_WideStringC def(L"\x1001qq");  // Comes InsertAtBack despite endianness.

  EXPECT_FALSE(empty < empty);
  EXPECT_FALSE(a < a);
  EXPECT_FALSE(abc < abc);
  EXPECT_FALSE(def < def);

  EXPECT_TRUE(empty < a);
  EXPECT_FALSE(a < empty);

  EXPECT_TRUE(empty < abc);
  EXPECT_FALSE(abc < empty);

  EXPECT_TRUE(empty < def);
  EXPECT_FALSE(def < empty);

  EXPECT_TRUE(a < abc);
  EXPECT_FALSE(abc < a);

  EXPECT_TRUE(a < def);
  EXPECT_FALSE(def < a);

  EXPECT_TRUE(abc < def);
  EXPECT_FALSE(def < abc);
}

TEST(fxcrt, WideStringCOperatorEQ) {
  CFX_WideStringC wide_string_c(L"hello");
  EXPECT_TRUE(wide_string_c == wide_string_c);

  CFX_WideStringC wide_string_c_same1(L"hello");
  EXPECT_TRUE(wide_string_c == wide_string_c_same1);
  EXPECT_TRUE(wide_string_c_same1 == wide_string_c);

  CFX_WideStringC wide_string_c_same2(wide_string_c);
  EXPECT_TRUE(wide_string_c == wide_string_c_same2);
  EXPECT_TRUE(wide_string_c_same2 == wide_string_c);

  CFX_WideStringC wide_string_c1(L"he");
  CFX_WideStringC wide_string_c2(L"hellp");
  CFX_WideStringC wide_string_c3(L"hellod");
  EXPECT_FALSE(wide_string_c == wide_string_c1);
  EXPECT_FALSE(wide_string_c == wide_string_c2);
  EXPECT_FALSE(wide_string_c == wide_string_c3);
  EXPECT_FALSE(wide_string_c1 == wide_string_c);
  EXPECT_FALSE(wide_string_c2 == wide_string_c);
  EXPECT_FALSE(wide_string_c3 == wide_string_c);

  CFX_WideString wide_string_same1(L"hello");
  EXPECT_TRUE(wide_string_c == wide_string_same1);
  EXPECT_TRUE(wide_string_same1 == wide_string_c);

  CFX_WideString wide_string1(L"he");
  CFX_WideString wide_string2(L"hellp");
  CFX_WideString wide_string3(L"hellod");
  EXPECT_FALSE(wide_string_c == wide_string1);
  EXPECT_FALSE(wide_string_c == wide_string2);
  EXPECT_FALSE(wide_string_c == wide_string3);
  EXPECT_FALSE(wide_string1 == wide_string_c);
  EXPECT_FALSE(wide_string2 == wide_string_c);
  EXPECT_FALSE(wide_string3 == wide_string_c);

  const wchar_t* c_string_same1 = L"hello";
  EXPECT_TRUE(wide_string_c == c_string_same1);
  EXPECT_TRUE(c_string_same1 == wide_string_c);

  const wchar_t* c_string1 = L"he";
  const wchar_t* c_string2 = L"hellp";
  const wchar_t* c_string3 = L"hellod";
  EXPECT_FALSE(wide_string_c == c_string1);
  EXPECT_FALSE(wide_string_c == c_string2);
  EXPECT_FALSE(wide_string_c == c_string3);

  EXPECT_FALSE(c_string1 == wide_string_c);
  EXPECT_FALSE(c_string2 == wide_string_c);
  EXPECT_FALSE(c_string3 == wide_string_c);
}

TEST(fxcrt, WideStringCOperatorNE) {
  CFX_WideStringC wide_string_c(L"hello");
  EXPECT_FALSE(wide_string_c != wide_string_c);

  CFX_WideStringC wide_string_c_same1(L"hello");
  EXPECT_FALSE(wide_string_c != wide_string_c_same1);
  EXPECT_FALSE(wide_string_c_same1 != wide_string_c);

  CFX_WideStringC wide_string_c_same2(wide_string_c);
  EXPECT_FALSE(wide_string_c != wide_string_c_same2);
  EXPECT_FALSE(wide_string_c_same2 != wide_string_c);

  CFX_WideStringC wide_string_c1(L"he");
  CFX_WideStringC wide_string_c2(L"hellp");
  CFX_WideStringC wide_string_c3(L"hellod");
  EXPECT_TRUE(wide_string_c != wide_string_c1);
  EXPECT_TRUE(wide_string_c != wide_string_c2);
  EXPECT_TRUE(wide_string_c != wide_string_c3);
  EXPECT_TRUE(wide_string_c1 != wide_string_c);
  EXPECT_TRUE(wide_string_c2 != wide_string_c);
  EXPECT_TRUE(wide_string_c3 != wide_string_c);

  CFX_WideString wide_string_same1(L"hello");
  EXPECT_FALSE(wide_string_c != wide_string_same1);
  EXPECT_FALSE(wide_string_same1 != wide_string_c);

  CFX_WideString wide_string1(L"he");
  CFX_WideString wide_string2(L"hellp");
  CFX_WideString wide_string3(L"hellod");
  EXPECT_TRUE(wide_string_c != wide_string1);
  EXPECT_TRUE(wide_string_c != wide_string2);
  EXPECT_TRUE(wide_string_c != wide_string3);
  EXPECT_TRUE(wide_string1 != wide_string_c);
  EXPECT_TRUE(wide_string2 != wide_string_c);
  EXPECT_TRUE(wide_string3 != wide_string_c);

  const wchar_t* c_string_same1 = L"hello";
  EXPECT_FALSE(wide_string_c != c_string_same1);
  EXPECT_FALSE(c_string_same1 != wide_string_c);

  const wchar_t* c_string1 = L"he";
  const wchar_t* c_string2 = L"hellp";
  const wchar_t* c_string3 = L"hellod";
  EXPECT_TRUE(wide_string_c != c_string1);
  EXPECT_TRUE(wide_string_c != c_string2);
  EXPECT_TRUE(wide_string_c != c_string3);

  EXPECT_TRUE(c_string1 != wide_string_c);
  EXPECT_TRUE(c_string2 != wide_string_c);
  EXPECT_TRUE(c_string3 != wide_string_c);
}

TEST(fxcrt, WideStringCFind) {
  CFX_WideStringC null_string;
  EXPECT_FALSE(null_string.Find(L'a').has_value());
  EXPECT_FALSE(null_string.Find(L'\0').has_value());

  CFX_WideStringC empty_string(L"");
  EXPECT_FALSE(empty_string.Find(L'a').has_value());
  EXPECT_FALSE(empty_string.Find(L'\0').has_value());

  pdfium::Optional<FX_STRSIZE> result;
  CFX_WideStringC single_string(L"a");
  result = single_string.Find(L'a');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(0, result.value());
  EXPECT_FALSE(single_string.Find(L'b').has_value());
  EXPECT_FALSE(single_string.Find(L'\0').has_value());

  CFX_WideStringC longer_string(L"abccc");
  result = longer_string.Find(L'a');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(0, result.value());
  result = longer_string.Find(L'c');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result.value());
  EXPECT_FALSE(longer_string.Find(L'd').has_value());
  EXPECT_FALSE(longer_string.Find(L'\0').has_value());

  CFX_WideStringC hibyte_string(
      L"ab\xFF8c"
      L"def");
  result = hibyte_string.Find(L'\xFF8c');
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result.value());
}

TEST(fxcrt, WideStringCNullIterator) {
  CFX_WideStringC null_str;
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : null_str) {
    sum += c;  // Avoid unused arg warnings.
    any_present = true;
  }
  EXPECT_FALSE(any_present);
  EXPECT_EQ(0, sum);
}

TEST(fxcrt, WideStringCEmptyIterator) {
  CFX_WideStringC empty_str(L"");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : empty_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_FALSE(any_present);
  EXPECT_EQ(0, sum);
}

TEST(fxcrt, WideStringCOneCharIterator) {
  CFX_WideStringC one_str(L"a");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : one_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_TRUE(any_present);
  EXPECT_EQ(static_cast<int32_t>(L'a'), sum);
}

TEST(fxcrt, WideStringCMultiCharIterator) {
  CFX_WideStringC one_str(L"abc");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : one_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_TRUE(any_present);
  EXPECT_EQ(static_cast<int32_t>(L'a' + L'b' + L'c'), sum);
}

TEST(fxcrt, WideStringCAnyAllNoneOf) {
  CFX_WideStringC str(L"aaaaaaaaaaaaaaaaab");
  EXPECT_FALSE(std::all_of(str.begin(), str.end(),
                           [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_FALSE(std::none_of(str.begin(), str.end(),
                            [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_TRUE(std::any_of(str.begin(), str.end(),
                          [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_TRUE(pdfium::ContainsValue(str, L'a'));
  EXPECT_TRUE(pdfium::ContainsValue(str, L'b'));
  EXPECT_FALSE(pdfium::ContainsValue(str, L'z'));
}

TEST(fxcrt, WideStringFormatWidth) {
  {
    CFX_WideString str;
    str.Format(L"%5d", 1);
    EXPECT_EQ(L"    1", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%d", 1);
    EXPECT_EQ(L"1", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%*d", 5, 1);
    EXPECT_EQ(L"    1", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%-1d", 1);
    EXPECT_EQ(L"1", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%0d", 1);
    EXPECT_EQ(L"1", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%1048576d", 1);
    EXPECT_EQ(L"", str);
  }
}

TEST(fxcrt, WideStringFormatPrecision) {
  {
    CFX_WideString str;
    str.Format(L"%.2f", 1.12345);
    EXPECT_EQ(L"1.12", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%.*f", 3, 1.12345);
    EXPECT_EQ(L"1.123", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%f", 1.12345);
    EXPECT_EQ(L"1.123450", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%-1f", 1.12345);
    EXPECT_EQ(L"1.123450", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%0f", 1.12345);
    EXPECT_EQ(L"1.123450", str);
  }

  {
    CFX_WideString str;
    str.Format(L"%.1048576f", 1.2);
    EXPECT_EQ(L"", str);
  }
}

TEST(fxcrt, WideStringFormatOutOfRangeChar) {
  CFX_WideString str;
  str.Format(L"unsupported char '%c'", 0x00FF00FF);
}

TEST(fxcrt, EmptyWideString) {
  CFX_WideString empty_str;
  EXPECT_TRUE(empty_str.IsEmpty());
  EXPECT_EQ(0, empty_str.GetLength());
  const wchar_t* cstr = empty_str.c_str();
  EXPECT_EQ(0, FXSYS_wcslen(cstr));
}

TEST(fxcrt, WidStringInitializerList) {
  CFX_WideString many_str({L"clams", L" and ", L"oysters"});
  EXPECT_EQ(L"clams and oysters", many_str);
  many_str = {L"fish", L" and ", L"chips", L" and ", L"soda"};
  EXPECT_EQ(L"fish and chips and soda", many_str);
}

TEST(fxcrt, WideStringNullIterator) {
  CFX_WideString null_str;
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : null_str) {
    sum += c;  // Avoid unused arg warnings.
    any_present = true;
  }
  EXPECT_FALSE(any_present);
  EXPECT_EQ(0, sum);
}

TEST(fxcrt, WideStringEmptyIterator) {
  CFX_WideString empty_str(L"");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : empty_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_FALSE(any_present);
  EXPECT_EQ(0, sum);
}

TEST(fxcrt, WideStringOneCharIterator) {
  CFX_WideString one_str(L"a");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : one_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_TRUE(any_present);
  EXPECT_EQ(static_cast<int32_t>(L'a'), sum);
}

TEST(fxcrt, WideStringMultiCharIterator) {
  CFX_WideString one_str(L"abc");
  int32_t sum = 0;
  bool any_present = false;
  for (const auto& c : one_str) {
    any_present = true;
    sum += c;  // Avoid unused arg warnings.
  }
  EXPECT_TRUE(any_present);
  EXPECT_EQ(static_cast<int32_t>(L'a' + L'b' + L'c'), sum);
}

TEST(fxcrt, WideStringAnyAllNoneOf) {
  CFX_WideString str(L"aaaaaaaaaaaaaaaaab");
  EXPECT_FALSE(std::all_of(str.begin(), str.end(),
                           [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_FALSE(std::none_of(str.begin(), str.end(),
                            [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_TRUE(std::any_of(str.begin(), str.end(),
                          [](const wchar_t& c) { return c == L'a'; }));

  EXPECT_TRUE(pdfium::ContainsValue(str, L'a'));
  EXPECT_TRUE(pdfium::ContainsValue(str, L'b'));
  EXPECT_FALSE(pdfium::ContainsValue(str, L'z'));
}

TEST(fxcrt, OStreamWideStringOverload) {
  std::ostringstream stream;

  // Basic case, empty string
  CFX_WideString str;
  stream << str;
  EXPECT_EQ("", stream.str());

  // Basic case, wide character
  str = L"\u20AC";
  stream << str;
  EXPECT_EQ("\u20AC", stream.str());

  // Basic case, non-empty string
  str = L"def";
  stream.str("");
  stream << "abc" << str << "ghi";
  EXPECT_EQ("abcdefghi", stream.str());

  // Changing the CFX_WideString does not change the stream it was written to.
  str = L"123";
  EXPECT_EQ("abcdefghi", stream.str());

  // Writing it again to the stream will use the latest value.
  stream.str("");
  stream << "abc" << str << "ghi";
  EXPECT_EQ("abc123ghi", stream.str());

  wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};

  // Writing a CFX_WideString with nulls and no specified length treats it as
  // a C-style null-terminated string.
  str = CFX_WideString(stringWithNulls);
  EXPECT_EQ(2, str.GetLength());
  stream.str("");
  stream << str;
  EXPECT_EQ(2u, stream.tellp());

  // Writing a CFX_WideString with nulls but specifying its length treats it as
  // a C++-style string.
  str = CFX_WideString(stringWithNulls, 4);
  EXPECT_EQ(4, str.GetLength());
  stream.str("");
  stream << str;
  EXPECT_EQ(4u, stream.tellp());

  // << operators can be chained.
  CFX_WideString str1(L"abc");
  CFX_WideString str2(L"def");
  stream.str("");
  stream << str1 << str2;
  EXPECT_EQ("abcdef", stream.str());
}

TEST(fxcrt, WideOStreamWideStringOverload) {
  std::wostringstream stream;

  // Basic case, empty string
  CFX_WideString str;
  stream << str;
  EXPECT_EQ(L"", stream.str());

  // Basic case, wide character
  str = L"\u20AC";
  stream << str;
  EXPECT_EQ(L"\u20AC", stream.str());

  // Basic case, non-empty string
  str = L"def";
  stream.str(L"");
  stream << L"abc" << str << L"ghi";
  EXPECT_EQ(L"abcdefghi", stream.str());

  // Changing the CFX_WideString does not change the stream it was written to.
  str = L"123";
  EXPECT_EQ(L"abcdefghi", stream.str());

  // Writing it again to the stream will use the latest value.
  stream.str(L"");
  stream << L"abc" << str << L"ghi";
  EXPECT_EQ(L"abc123ghi", stream.str());

  wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};

  // Writing a CFX_WideString with nulls and no specified length treats it as
  // a C-style null-terminated string.
  str = CFX_WideString(stringWithNulls);
  EXPECT_EQ(2, str.GetLength());
  stream.str(L"");
  stream << str;
  EXPECT_EQ(2u, stream.tellp());

  // Writing a CFX_WideString with nulls but specifying its length treats it as
  // a C++-style string.
  str = CFX_WideString(stringWithNulls, 4);
  EXPECT_EQ(4, str.GetLength());
  stream.str(L"");
  stream << str;
  EXPECT_EQ(4u, stream.tellp());

  // << operators can be chained.
  CFX_WideString str1(L"abc");
  CFX_WideString str2(L"def");
  stream.str(L"");
  stream << str1 << str2;
  EXPECT_EQ(L"abcdef", stream.str());
}

TEST(fxcrt, OStreamWideStringCOverload) {
  // Basic case, empty string
  {
    std::ostringstream stream;
    CFX_WideStringC str;
    stream << str;
    EXPECT_EQ("", stream.str());
  }

  // Basic case, non-empty string
  {
    std::ostringstream stream;
    CFX_WideStringC str(L"def");
    stream << "abc" << str << "ghi";
    EXPECT_EQ("abcdefghi", stream.str());
  }

  // Basic case, wide character
  {
    std::ostringstream stream;
    CFX_WideStringC str(L"\u20AC");
    stream << str;
    EXPECT_EQ("\u20AC", stream.str());
  }

  // Changing the CFX_WideStringC does not change the stream it was written to.
  {
    std::ostringstream stream;
    CFX_WideStringC str(L"abc");
    stream << str;
    str = L"123";
    EXPECT_EQ("abc", stream.str());
  }

  // Writing it again to the stream will use the latest value.
  {
    std::ostringstream stream;
    CFX_WideStringC str(L"abc");
    stream << str;
    stream.str("");
    str = L"123";
    stream << str;
    EXPECT_EQ("123", stream.str());
  }

  // Writing a CFX_WideStringC with nulls and no specified length treats it as
  // a C-style null-terminated string.
  {
    wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};
    std::ostringstream stream;
    CFX_WideStringC str(stringWithNulls);
    EXPECT_EQ(2, str.GetLength());
    stream << str;
    EXPECT_EQ(2u, stream.tellp());
    str = L"";
  }

  // Writing a CFX_WideStringC with nulls but specifying its length treats it as
  // a C++-style string.
  {
    wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};
    std::ostringstream stream;
    CFX_WideStringC str(stringWithNulls, 4);
    EXPECT_EQ(4, str.GetLength());
    stream << str;
    EXPECT_EQ(4u, stream.tellp());
    str = L"";
  }

  // << operators can be chained.
  {
    std::ostringstream stream;
    CFX_WideStringC str1(L"abc");
    CFX_WideStringC str2(L"def");
    stream << str1 << str2;
    EXPECT_EQ("abcdef", stream.str());
  }
}

TEST(fxcrt, WideOStreamWideStringCOverload) {
  // Basic case, empty string
  {
    std::wostringstream stream;
    CFX_WideStringC str;
    stream << str;
    EXPECT_EQ(L"", stream.str());
  }

  // Basic case, non-empty string
  {
    std::wostringstream stream;
    CFX_WideStringC str(L"def");
    stream << "abc" << str << "ghi";
    EXPECT_EQ(L"abcdefghi", stream.str());
  }

  // Basic case, wide character
  {
    std::wostringstream stream;
    CFX_WideStringC str(L"\u20AC");
    stream << str;
    EXPECT_EQ(L"\u20AC", stream.str());
  }

  // Changing the CFX_WideStringC does not change the stream it was written to.
  {
    std::wostringstream stream;
    CFX_WideStringC str(L"abc");
    stream << str;
    str = L"123";
    EXPECT_EQ(L"abc", stream.str());
  }

  // Writing it again to the stream will use the latest value.
  {
    std::wostringstream stream;
    CFX_WideStringC str(L"abc");
    stream << str;
    stream.str(L"");
    str = L"123";
    stream << str;
    EXPECT_EQ(L"123", stream.str());
  }

  // Writing a CFX_WideStringC with nulls and no specified length treats it as
  // a C-style null-terminated string.
  {
    wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};
    std::wostringstream stream;
    CFX_WideStringC str(stringWithNulls);
    EXPECT_EQ(2, str.GetLength());
    stream << str;
    EXPECT_EQ(2u, stream.tellp());
  }

  // Writing a CFX_WideStringC with nulls but specifying its length treats it as
  // a C++-style string.
  {
    wchar_t stringWithNulls[]{'x', 'y', '\0', 'z'};
    std::wostringstream stream;
    CFX_WideStringC str(stringWithNulls, 4);
    EXPECT_EQ(4, str.GetLength());
    stream << str;
    EXPECT_EQ(4u, stream.tellp());
  }

  // << operators can be chained.
  {
    std::wostringstream stream;
    CFX_WideStringC str1(L"abc");
    CFX_WideStringC str2(L"def");
    stream << str1 << str2;
    EXPECT_EQ(L"abcdef", stream.str());
  }
}

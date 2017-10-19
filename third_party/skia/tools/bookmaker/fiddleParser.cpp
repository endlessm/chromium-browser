/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bookmaker.h"

static Definition* find_fiddle(Definition* def, const string& name) {
    if (MarkType::kExample == def->fMarkType && name == def->fFiddle) {
        return def;
    }
    for (auto& child : def->fChildren) {
        Definition* result = find_fiddle(child, name);
        if (result) {
            return result;
        }
    }
    return nullptr;
}

Definition* FiddleParser::findExample(const string& name) const {
    for (const auto& topic : fBmhParser->fTopicMap) {
        if (topic.second->fParent) {
            continue;
        }
        Definition* def = find_fiddle(topic.second, name);
        if (def) {
            return def;
        }
    }
    return nullptr;
}

bool FiddleParser::parseFiddles() {
    if (!this->skipExact("{\n")) {
        return false;
    }
    while (!this->eof()) {
        if (!this->skipExact("  \"")) {
            return false;
        }
        const char* nameLoc = fChar;
        if (!this->skipToEndBracket("\"")) {
            return false;
        }
        string name(nameLoc, fChar - nameLoc);
        if (!this->skipExact("\": {\n")) {
            return false;
        }
        if (!this->skipExact("    \"compile_errors\": [")) {
            return false;
        }
        if (']' != this->peek()) {
            // report compiler errors
            int brackets = 1;
            const char* errorStart = fChar;
            do {
                if ('[' == this->peek()) {
                    ++brackets;
                } else if (']' == this->peek()) {
                    --brackets;
                }
            } while (!this->eof() && this->next() && brackets > 0);
            SkDebugf("fiddle compile error in %s: %.*s\n", name.c_str(), (int) (fChar - errorStart),
                    errorStart);
        }
        if (!this->skipExact("],\n")) {
            return false;
        }
        if (!this->skipExact("    \"runtime_error\": \"")) {
            return false;
        }
        if ('"' != this->peek()) {
            const char* errorStart = fChar;
            if (!this->skipToEndBracket('"')) {
                return false;
            }
            SkDebugf("fiddle runtime error in %s: %.*s\n", name.c_str(), (int) (fChar - errorStart),
                    errorStart);
        }
        if (!this->skipExact("\",\n")) {
            return false;
        }
        if (!this->skipExact("    \"fiddleHash\": \"")) {
            return false;
        }
        const char* hashStart = fChar;
        if (!this->skipToEndBracket('"')) {
            return false;
        }
        Definition* example = this->findExample(name);
        if (!example) {
            SkDebugf("missing example %s\n", name.c_str());
        }
        string hash(hashStart, fChar - hashStart);
        if (example) {
            example->fHash = hash;
        }
        if (!this->skipExact("\",\n")) {
            return false;
        }
        if (!this->skipExact("    \"text\": \"")) {
            return false;
        }
        if ('"' != this->peek()) {
            const char* stdOutStart = fChar;
            do {
                if ('\\' == this->peek()) {
                    this->next();
                } else if ('"' == this->peek()) {
                    break;
                }
            } while (!this->eof() && this->next());
            const char* stdOutEnd = fChar;
            if (example) {
                bool foundStdOut = false;
                for (auto& textOut : example->fChildren) {
                    if (MarkType::kStdOut != textOut->fMarkType) {
                        continue;
                    }
                    foundStdOut = true;
                    bool foundVolatile = false;
                    for (auto& stdOutChild : textOut->fChildren) {
                         if (MarkType::kVolatile == stdOutChild->fMarkType) {
                             foundVolatile = true;
                             break;
                         }
                    }
                    TextParser bmh(textOut);
                    EscapeParser fiddle(stdOutStart, stdOutEnd);
                    do {
                        bmh.skipWhiteSpace();
                        fiddle.skipWhiteSpace();
                        const char* bmhEnd = bmh.trimmedLineEnd();
                        const char* fiddleEnd = fiddle.trimmedLineEnd();
                        ptrdiff_t bmhLen = bmhEnd - bmh.fChar;
                        SkASSERT(bmhLen > 0);
                        ptrdiff_t fiddleLen = fiddleEnd - fiddle.fChar;
                        SkASSERT(fiddleLen > 0);
                        if (bmhLen != fiddleLen) {
                            if (!foundVolatile) {
                                SkDebugf("mismatched stdout len in %s\n", name.c_str());
                            }
                        } else  if (strncmp(bmh.fChar, fiddle.fChar, fiddleLen)) {
                            if (!foundVolatile) {
                                SkDebugf("mismatched stdout text in %s\n", name.c_str());
                            }
                        }
                        bmh.skipToLineStart();
                        fiddle.skipToLineStart();
                    } while (!bmh.eof() && !fiddle.eof());
                    if (!foundStdOut) {
                        SkDebugf("bmh %s missing stdout\n", name.c_str());
                    } else if (!bmh.eof() || !fiddle.eof()) {
                        if (!foundVolatile) {
                            SkDebugf("%s mismatched stdout eof\n", name.c_str());
                        }
                    }
                }
            }
        }
        if (!this->skipExact("\"\n")) {
            return false;
        }
        if (!this->skipExact("  }")) {
            return false;
        }
        if ('\n' == this->peek()) {
            break;
        }
        if (!this->skipExact(",\n")) {
            return false;
        }
    }
#if 0
            // compare the text output with the expected output in the markup tree
            this->skipToSpace();
            SkASSERT(' ' == fChar[0]);
            this->next();
            const char* nameLoc = fChar;
            this->skipToNonAlphaNum();
            const char* nameEnd = fChar;
            string name(nameLoc, nameEnd - nameLoc);
            const Definition* example = this->findExample(name);
            if (!example) {
                return this->reportError<bool>("missing stdout name");
            }
            SkASSERT(':' == fChar[0]);
            this->next();
            this->skipSpace();
            const char* stdOutLoc = fChar;
            do {
                this->skipToLineStart();
            } while (!this->eof() && !this->startsWith("fiddles.htm:"));
            const char* stdOutEnd = fChar;
            for (auto& textOut : example->fChildren) {
                if (MarkType::kStdOut != textOut->fMarkType) {
                    continue;
                }
                TextParser bmh(textOut);
                TextParser fiddle(fFileName, stdOutLoc, stdOutEnd, fLineCount);
                do {
                    bmh.skipWhiteSpace();
                    fiddle.skipWhiteSpace();
                    const char* bmhEnd = bmh.trimmedLineEnd();
                    const char* fiddleEnd = fiddle.trimmedLineEnd();
                    ptrdiff_t bmhLen = bmhEnd - bmh.fChar;
                    SkASSERT(bmhLen > 0);
                    ptrdiff_t fiddleLen = fiddleEnd - fiddle.fChar;
                    SkASSERT(fiddleLen > 0);
                    if (bmhLen != fiddleLen) {
                        return this->reportError<bool>("mismatched stdout len");
                    }
                    if (strncmp(bmh.fChar, fiddle.fChar, fiddleLen)) {
                        return this->reportError<bool>("mismatched stdout text");
                    }
                    bmh.skipToLineStart();
                    fiddle.skipToLineStart();
                } while (!bmh.eof() && !fiddle.eof());
                if (!bmh.eof() || (!fiddle.eof() && !fiddle.startsWith("</pre>"))) {
                    return this->reportError<bool>("mismatched stdout eof");
                }
                break;
            }
        }
    }
#endif
    return true;
}

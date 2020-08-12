// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSER_H_
#define V8_PARSING_PARSER_H_

#include "src/parsing/abstract-parser.h"

namespace v8 {
namespace internal {

template <>
struct ParserTypes<Parser> : AbstractParserTypes<Parser> {
};

class V8_EXPORT_PRIVATE Parser
    : public NON_EXPORTED_BASE(AbstractParser<Parser>) {
 public:
  explicit Parser(ParseInfo* info) : AbstractParser<Parser>(info) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSER_H_

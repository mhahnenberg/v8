// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSER_H_
#define V8_PARSING_PARSER_H_

#include "src/parsing/abstract-parser.h"

namespace v8 {
namespace internal {

template <>
struct ParserTypes<Parser> : AbstractParserTypes<Parser> {};

class V8_EXPORT_PRIVATE Parser
    : public NON_EXPORTED_BASE(AbstractParser<Parser>) {
 public:
  Parser(ParseInfo* info) : AbstractParser<Parser>(info) {}

  SpeculativeParseFailureReason speculative_parse_failure_reason() const { return info()->speculative_parse_failure_reason(); }

 private:
  friend class AbstractParser<Parser>;
  
  FunctionLiteral* DoParseFunction(Isolate* isolate, ParseInfo* info,
                                   int start_position, int end_position,
                                   int function_literal_id,
                                   const AstRawString* raw_name) {
    DCHECK_EQ(impl()->parsing_on_main_thread_, isolate != nullptr);

    return AbstractParser<Parser>::DoParseFunction(
        isolate, info, start_position, end_position, function_literal_id,
        raw_name);
  }

  void PostProcessParseResult(Isolate* isolate, ParseInfo* info,
                              FunctionLiteral* literal) {
    DCHECK_EQ(isolate != nullptr, impl()->parsing_on_main_thread_);
    AbstractParser<Parser>::PostProcessParseResult(isolate, info, literal);
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSER_H_

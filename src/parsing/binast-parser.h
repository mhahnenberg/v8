// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_PARSER_H_
#define V8_PARSING_BINAST_PARSER_H_

#include "src/strings/string-hasher-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser-base.h"
#include "src/ast/ast.h"
#include "src/parsing/parser.h"
#include "src/base/overflowing-math.h"
#include "src/base/ieee754.h"

namespace v8 {
namespace internal {

class ParseInfo;
    

class BinAstParser;

template <>
struct ParserTypes<BinAstParser> : AbstractParserTypes<BinAstParser> {
};

class BinAstParser : public AbstractParser<BinAstParser> {
 public:
  explicit BinAstParser(ParseInfo* info);

  void ParseProgram(ParseInfo* info);

 private:
  friend class AbstractParser<BinAstParser>;
  friend class ParserBase<BinAstParser>;
  friend class i::ExpressionScope<ParserTypes<BinAstParser>>;
  friend class i::VariableDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ParameterDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ArrowHeadParsingScope<ParserTypes<BinAstParser>>;

  FunctionLiteral* DoParseProgram(Isolate* isolate, ParseInfo* info);
  void PostProcessParseResult(Isolate* isolate, ParseInfo* info,
                              FunctionLiteral* literal);
  FunctionLiteral* ParseFunctionLiteral(
    const AstRawString* name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_position, FunctionSyntaxKind type,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_PARSER_H_

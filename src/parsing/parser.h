// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSER_H_
#define V8_PARSING_PARSER_H_

#include "src/parsing/abstract-parser.h"

namespace v8 {
namespace internal {

template <>
struct ParserTypes<Parser> {
  using Base = ParserBase<Parser>;
  using Impl = Parser;

  // Return types for traversing functions.
  using Block = v8::internal::Block*;
  using BreakableStatement = v8::internal::BreakableStatement*;
  using ClassLiteralProperty = ClassLiteral::Property*;
  using ClassLiteralStaticElement = ClassLiteral::StaticElement*;
  using ClassPropertyList = ZonePtrList<ClassLiteral::Property>*;
  using ClassStaticElementList = ZonePtrList<ClassLiteral::StaticElement>*;
  using Expression = v8::internal::Expression*;
  using ExpressionList = ScopedPtrList<v8::internal::Expression>;
  using FormalParameters = ParserFormalParameters;
  using ForStatement = v8::internal::ForStatement*;
  using FunctionLiteral = v8::internal::FunctionLiteral*;
  using Identifier = const AstRawString*;
  using IterationStatement = v8::internal::IterationStatement*;
  using ObjectLiteralProperty = ObjectLiteral::Property*;
  using ObjectPropertyList = ScopedPtrList<v8::internal::ObjectLiteralProperty>;
  using Statement = v8::internal::Statement*;
  using StatementList = ScopedPtrList<v8::internal::Statement>;
  using Suspend = v8::internal::Suspend*;

  // For constructing objects returned by the traversing functions.
  using Factory = AstNodeFactory;

  // Other implementation-specific functions.
  using FuncNameInferrer = v8::internal::FuncNameInferrer<ParserTypes<Parser>>;
  using SourceRange = v8::internal::SourceRange;
  using SourceRangeScope = v8::internal::SourceRangeScope;
  using AstValueFactory = v8::internal::AstValueFactory;
  using AstRawString = AstRawString;
};

class V8_EXPORT_PRIVATE Parser
    : public NON_EXPORTED_BASE(AbstractParser<Parser>) {
 public:
  explicit Parser(ParseInfo* info) : AbstractParser<Parser>(info) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSER_H_

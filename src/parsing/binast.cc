// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast.h"
#include "src/ast/scopes.h"
#include "src/strings/string-hasher.h"
#include "src/strings/string-hasher-inl.h"
#include "src/ast/ast-value-factory.h"

namespace v8 {
namespace internal {

BinAstParseInfo::BinAstParseInfo(Zone* zone, const UnoptimizedCompileFlags flags, const AstStringConstants* ast_string_constants)
  : flags_(flags),
    zone_(zone),
    script_scope_(nullptr),
    stack_limit_(0),
    parameters_end_pos_(kNoSourcePosition),
    max_function_literal_id_(kFunctionLiteralIdInvalid),
    character_stream_(nullptr),
    ast_value_factory_(nullptr),
    function_name_(nullptr),
    runtime_call_stats_(nullptr),
    source_range_map_(nullptr),
    ast_string_constants_(ast_string_constants),
    literal_(nullptr),
    allow_eval_cache_(false),
    contains_asm_module_(false),
    language_mode_(flags.outer_language_mode())
{
}

BinAstParseInfo::BinAstParseInfo(ParseInfo* other_parse_info, const UnoptimizedCompileFlags flags)
  : flags_(flags),
    zone_(std::make_unique<Zone>(other_parse_info->state()->allocator(), ZONE_NAME)),
    script_scope_(other_parse_info->script_scope()),
    stack_limit_(other_parse_info->stack_limit()),
    parameters_end_pos_(other_parse_info->parameters_end_pos()),
    max_function_literal_id_(other_parse_info->max_function_literal_id()),
    character_stream_(nullptr),
    ast_value_factory_(nullptr),
    function_name_(other_parse_info->function_name()),
    runtime_call_stats_(nullptr),
    source_range_map_(nullptr),
    ast_string_constants_(other_parse_info->ast_string_constants()),
    literal_(nullptr),
    allow_eval_cache_(other_parse_info->allow_eval_cache()),
    contains_asm_module_(other_parse_info->contains_asm_module()),
    language_mode_(flags.outer_language_mode())
{
}

void BinAstParseInfo::set_character_stream(
    std::unique_ptr<Utf16CharacterStream> character_stream) {
  DCHECK_NULL(character_stream_);
  character_stream_.swap(character_stream);
}

void BinAstParseInfo::ResetCharacterStream() { character_stream_.reset(); }

BinAstValueFactory* BinAstParseInfo::GetOrCreateAstValueFactory() {
  if (!ast_value_factory_.get()) {
    ast_value_factory_.reset(new BinAstValueFactory(zone(), ast_string_constants()));
  }
  return ast_value_factory();
}

#define RETURN_NODE(Node) \
  case k##Node:           \
    return static_cast<BinAst##Node*>(this);

BinAstIterationStatement* BinAstNode::AsIterationStatement() {
  switch (node_type()) {
    BINAST_ITERATION_NODE_LIST(RETURN_NODE);
    default:
      return nullptr;
  }
}

bool BinAstExpression::IsPropertyName() const {
  return IsLiteral() && AsLiteral()->IsPropertyName();
}


bool BinAstExpression::IsConciseMethodDefinition() const {
  return IsFunctionLiteral() && IsConciseMethod(AsFunctionLiteral()->kind());
}

bool BinAstExpression::IsAnonymousFunctionDefinition() const {
  if (IsClassLiteral()) {
    // TODO(binast): Add support for classes
    DCHECK(false);
  }
  return (IsFunctionLiteral() &&
          AsFunctionLiteral()->IsAnonymousFunctionDefinition()); /* TODO(binast): Add support for classes ||
          (IsClassLiteral() &&
          AsClassLiteral()->IsAnonymousFunctionDefinition()); */
}

bool BinAstExpression::IsAccessorFunctionDefinition() const {
  return IsFunctionLiteral() && IsAccessorFunction(AsFunctionLiteral()->kind());
}

bool BinAstExpression::IsNumberLiteral() const {
  return IsLiteral() && AsLiteral()->IsNumber();
}

bool BinAstExpression::IsTheHoleLiteral() const {
  return IsLiteral() && AsLiteral()->type() == BinAstLiteral::kTheHole;
}

int BinAstFunctionLiteral::start_position() const {
  return scope()->start_position();
}

int BinAstFunctionLiteral::end_position() const {
  return scope()->end_position();
}

LanguageMode BinAstFunctionLiteral::language_mode() const {
  return scope()->language_mode();
}

FunctionKind BinAstFunctionLiteral::kind() const { return scope()->function_kind(); }

bool BinAstFunctionLiteral::ShouldEagerCompile() const {
  return scope()->ShouldEagerCompile();
}

void BinAstFunctionLiteral::SetShouldEagerCompile() {
  scope()->set_should_eager_compile();
}

void BinAstFunctionLiteral::set_raw_inferred_name(AstConsString* raw_inferred_name) {
  DCHECK_NOT_NULL(raw_inferred_name);
  raw_inferred_name_ = raw_inferred_name;
  scope()->set_has_inferred_function_name(true);
}

}  // namespace internal
}  // namespace v8

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

AstValueFactory* BinAstParseInfo::GetOrCreateAstValueFactory() {
  if (!ast_value_factory_.get()) {
    ast_value_factory_.reset(new AstValueFactory(
        zone(), ast_string_constants(), ast_string_constants()->hash_seed()));
  }
  return ast_value_factory();
}

}  // namespace internal
}  // namespace v8

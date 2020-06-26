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

BinAstParseInfo::BinAstParseInfo(Zone* zone, const UnoptimizedCompileFlags flags, const BinAstStringConstants* ast_string_constants)
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

void BinAstParseInfo::set_character_stream(
    std::unique_ptr<Utf16CharacterStream> character_stream) {
  DCHECK_NULL(character_stream_);
  character_stream_.swap(character_stream);
}

BinAstValueFactory* BinAstParseInfo::GetOrCreateAstValueFactory() {
  if (!ast_value_factory_.get()) {
    ast_value_factory_.reset(new BinAstValueFactory(zone(), ast_string_constants()));
  }
  return ast_value_factory();
}

// bool AstRawString::Compare(void* a, void* b) {
//   const AstRawString* lhs = static_cast<AstRawString*>(a);
//   const AstRawString* rhs = static_cast<AstRawString*>(b);
//   DCHECK_EQ(lhs->Hash(), rhs->Hash());

//   if (lhs->length() != rhs->length()) return false;
//   if (lhs->length() == 0) return true;
//   const unsigned char* l = lhs->raw_data();
//   const unsigned char* r = rhs->raw_data();
//   size_t length = rhs->length();
//   if (lhs->is_one_byte()) {
//     if (rhs->is_one_byte()) {
//       return CompareCharsUnsigned(reinterpret_cast<const uint8_t*>(l),
//                                   reinterpret_cast<const uint8_t*>(r),
//                                   length) == 0;
//     } else {
//       return CompareCharsUnsigned(reinterpret_cast<const uint8_t*>(l),
//                                   reinterpret_cast<const uint16_t*>(r),
//                                   length) == 0;
//     }
//   } else {
//     if (rhs->is_one_byte()) {
//       return CompareCharsUnsigned(reinterpret_cast<const uint16_t*>(l),
//                                   reinterpret_cast<const uint8_t*>(r),
//                                   length) == 0;
//     } else {
//       return CompareCharsUnsigned(reinterpret_cast<const uint16_t*>(l),
//                                   reinterpret_cast<const uint16_t*>(r),
//                                   length) == 0;
//     }
//   }
// }

BinAstStringConstants::BinAstStringConstants(AccountingAllocator* allocator/* , uint64_t hash_seed */)
  : zone_(allocator, ZONE_NAME),
    string_table_(),
    hash_seed_(0)  // TODO(binast): figure out how to thread this through
{
#define F(name, str)                                                       \
  {                                                                        \
    const char* data = str;                                                  \
    Vector<const uint8_t> literal(reinterpret_cast<const uint8_t*>(data),    \
                                  static_cast<int>(strlen(data)));           \
    uint32_t raw_hash_field = StringHasher::HashSequentialString<uint8_t>(   \
        literal.begin(), literal.length(), hash_seed_);                      \
    name##_string_ = zone_.New<AstRawString>(true, literal, raw_hash_field); \
    /* TODO(binast): I don't think we care about this since the binAST will be independent of any isolate */ \
    /* The Handle returned by the factory is located on the roots */       \
    /* array, not on the temporary HandleScope, so this is safe.  */       \
    /* name##_string_->set_string(isolate->factory()->name##_string());  */     \
    string_table_.InsertNew(name##_string_, name##_string_->Hash());   \
  }
  BINAST_AST_STRING_CONSTANTS(F)
#undef F
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

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-deserializer.h"
#include "src/parsing/binast-deserializer-inl.h"
#include "src/ast/ast.h"
#include "src/parsing/binast-serialize-visitor.h"
#include "src/parsing/parser.h"
#include "src/objects/fixed-array-inl.h"
#include "src/zone/zone-list-inl.h"
#include "third_party/zlib/zlib.h"

namespace v8 {
namespace internal {

BinAstDeserializer::BinAstDeserializer(Isolate* isolate, Parser* parser,
                                       Handle<ByteArray> parse_data,
                                       MaybeHandle<PreparseData> preparse_data)
    : isolate_(isolate),
      parser_(parser),
      parse_data_(parse_data),
      preparse_data_(preparse_data),
      serialized_ast_(nullptr),
      current_offset_(0),
      string_table_base_offset_(0),
      proxy_variable_table_base_offset_(0),
      global_variable_table_base_offset_(0) {
}

AstNode* BinAstDeserializer::DeserializeAst(
    base::Optional<uint32_t> start_offset, base::Optional<uint32_t> length) {
  DCHECK(UseCompression() == BinAstSerializeVisitor::UseCompression());
  if (UseCompression()) {
    return DeserializeCompressedAst(start_offset, length);
  } else {
    return DeserializeUncompressedAst(start_offset, length, parse_data_->GetDataStartAddress(), parse_data_->length());
  }
}

AstNode* BinAstDeserializer::DeserializeCompressedAst(
    base::Optional<uint32_t> start_offset, base::Optional<uint32_t> length) {

  DCHECK(UseCompression() == BinAstSerializeVisitor::UseCompression());
  DCHECK(UseCompression());
  ByteArray serialized_ast = *parse_data_;
  std::unique_ptr<uint8_t[]> compressed_byte_array_with_size_header = std::make_unique<uint8_t[]>(serialized_ast.length());
  serialized_ast.copy_out(0, compressed_byte_array_with_size_header.get(), serialized_ast.length());

  size_t uncompressed_size = *reinterpret_cast<size_t*>(compressed_byte_array_with_size_header.get());
  uint8_t* compressed_data = compressed_byte_array_with_size_header.get() + sizeof(size_t);

  std::unique_ptr<uint8_t[]> uncompressed_byte_array = std::make_unique<uint8_t[]>(uncompressed_size);
  int uncompress_result = uncompress(uncompressed_byte_array.get(), &uncompressed_size, compressed_data, serialized_ast.length() - sizeof(size_t));
  if (uncompress_result != Z_OK) {
    printf("Error decompressing serialized AST: %s\n", zError(uncompress_result));
    UNREACHABLE();
  }

  return DeserializeUncompressedAst(start_offset, length, uncompressed_byte_array.get(), uncompressed_size);
}

AstNode* BinAstDeserializer::DeserializeUncompressedAst(
    base::Optional<uint32_t> start_offset, base::Optional<uint32_t> length, uint8_t* uncompressed_ast, size_t uncompressed_size) {
  bool is_toplevel = true;
  serialized_ast_ = uncompressed_ast;

  DeserializeStringTable();

  uint32_t variable_table_base_offset = DeserializeUint32();
  global_variable_table_base_offset_ = variable_table_base_offset;

  // Note: we don't deserialize the string table here because we rely on the
  // proxy string table within the FunctionLiteral, which references offsets in
  // the actual string table, even if we're deserializing a top-level function.
  if (start_offset.has_value()) {
    DCHECK(start_offset.value() >= static_cast<uint32_t>(current_offset_));
    is_toplevel = false;
    current_offset_ = start_offset.value();
  }

  AstNode* result = DeserializeAstNode(is_toplevel);

  DCHECK(start_offset ? 
      (static_cast<size_t>(current_offset_) == start_offset.value() + length.value()) 
    : (static_cast<size_t>(current_offset_) == global_variable_table_base_offset_));

  // Skip ahead to global variable table.
  current_offset_ = global_variable_table_base_offset_;
  uint32_t variable_table_length = DeserializeUint32();

  current_offset_ += GLOBAL_VARIABLE_TABLE_ENTRY_SIZE * variable_table_length;
  // Check that we consumed all the bytes that were serialized.
  DCHECK(static_cast<size_t>(current_offset_) == uncompressed_size);
  return result;
}

void BinAstDeserializer::DeserializeScopeVariableMap(Scope* scope) {
  uint32_t total_local_variables = DeserializeUint32();
  for (uint32_t i = 0; i < total_local_variables; ++i) {
    DeserializeLocalVariable(scope);
  }

  uint32_t total_nonlocal_variables = DeserializeUint32();
  for (uint32_t i = 0; i < total_nonlocal_variables; ++i) {
    DeserializeNonLocalVariable(scope);
  }
}

void BinAstDeserializer::DeserializeScopeUnresolvedList(Scope* scope) {
  uint32_t total_unresolved_variables = DeserializeUint32();

  bool add_unresolved = false;
  for (uint32_t i = 0; i < total_unresolved_variables; ++i) {
    VariableProxy* variable_proxy = DeserializeVariableProxy(add_unresolved);

    // Normally we add variable proxies to the unresolved_list_ when we encounter them inside
    // the body of the function, but if we're skipping the function we won't encounter
    // them so we need to add them here instead. Unfortunately, we don't know if
    // we're skipping the function yet, so we unconditionally add them here and then clear
    // the list later if we decide to deserialize the body of the function.
    scope->AddUnresolved(variable_proxy);
  }
}

Declaration* BinAstDeserializer::DeserializeDeclaration(Scope* scope) {
  int32_t start_pos = DeserializeInt32();
  uint8_t decl_type = DeserializeUint8();
  Variable* variable = DeserializeVariableReference(scope);

  Declaration* result;
  switch (decl_type) {
    case Declaration::DeclType::VariableDecl: {
      uint8_t is_nested = DeserializeUint8();

      // TODO(binast): Add support for nested var decls.
      DCHECK(!is_nested);
      result = zone()->New<VariableDeclaration>(start_pos, is_nested);
      break;
    }
    case Declaration::DeclType::FunctionDecl: {
      // We need to push the Scope being currently deserialized onto the Scope stack while deserializing
      // the FunctionLiterals inside FunctionDeclarations, otherwise their scope chain will end up skipping it.
      Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope->AsDeclarationScope());
      AstNode* func = DeserializeAstNode();
      result = zone()->New<FunctionDeclaration>((void*)func, start_pos);
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  result->set_var(variable);
  return result;
}

void BinAstDeserializer::DeserializeScopeDeclarations(Scope* scope) {
  uint32_t num_decls = DeserializeUint32();

  for (uint32_t i = 0; i < num_decls; ++i) {
    Declaration* decl_result = DeserializeDeclaration(scope);
    scope->decls_.Add(decl_result);
  }
}

void BinAstDeserializer::DeserializeScopeParameters(DeclarationScope* scope) {
  int32_t num_parameters_result = DeserializeInt32();
  scope->num_parameters_ = num_parameters_result;

  for (int i = 0; i < num_parameters_result; ++i) {
    Variable* param_result = DeserializeVariableReference(scope);
    scope->params_.Add(param_result, zone());
  }
}

void BinAstDeserializer::HandleFunctionSkipping(Scope* scope) {
  DCHECK(scope->scope_type() == FUNCTION_SCOPE);

  DeclarationScope* decl_scope = scope->AsDeclarationScope();

  // We skip FunctionLiterals before we even start deserializing the scope,
  // so we should never encounter this condition.
  DCHECK(!decl_scope->outer_scope()->GetClosureScope()->is_skipped_function());

  int end_position;
  int num_parameters;
  int preparse_function_length;
  int num_inner_functions;
  bool uses_super_property;
  LanguageMode language_mode;
  // If we're skipping this function we need to consume the inner function
  // data for it from the PreparseData.
  ProducedPreparseData* preparse_data = parser_->info()->consumed_preparse_data()->GetDataForSkippableFunction(
    zone(),
    decl_scope->start_position(),
    &end_position,
    &num_parameters,
    &preparse_function_length,
    &num_inner_functions,
    &uses_super_property,
    &language_mode);

  decl_scope->outer_scope()->SetMustUsePreparseData();
  decl_scope->set_is_skipped_function(true);
  produced_preparse_data_by_start_position_[decl_scope->start_position()] = preparse_data;
}

void BinAstDeserializer::DeserializeCommonScopeFields(Scope* scope) {
  DeserializeScopeVariableMap(scope);
  DeserializeScopeUnresolvedList(scope);

  // scope_info_ TODO(binast): do we need this?
#ifdef DEBUG
  uint8_t has_scope_name = DeserializeUint8();

  if (has_scope_name) {
    const AstRawString* scope_name_result = DeserializeRawStringReference();
    scope->SetScopeName(scope_name_result);
  }

  uint8_t already_resolved_result = DeserializeUint8();
  scope->already_resolved_ = already_resolved_result;

  uint8_t needs_migration_result = DeserializeUint8();
  scope->needs_migration_ = needs_migration_result;
#endif
  int32_t start_position_result = DeserializeInt32();
  scope->set_start_position(start_position_result);

  int32_t end_position_result = DeserializeInt32();
  scope->set_end_position(end_position_result);

  int32_t num_stack_slots_result = DeserializeInt32();
  scope->num_stack_slots_ = num_stack_slots_result;

  int32_t num_heap_slots_result = DeserializeInt32();
  scope->num_heap_slots_ = num_heap_slots_result;

  auto encoded_boolean_flags_result = DeserializeUint16Flags();

  scope->is_strict_ = encoded_boolean_flags_result[0];
  scope->calls_eval_ = encoded_boolean_flags_result[1];
  scope->sloppy_eval_can_extend_vars_ = encoded_boolean_flags_result[2];
  scope->scope_nonlinear_ = encoded_boolean_flags_result[3];
  scope->is_hidden_ = encoded_boolean_flags_result[4];
  scope->is_debug_evaluate_scope_ = encoded_boolean_flags_result[5];
  scope->inner_scope_calls_eval_ = encoded_boolean_flags_result[6];
  scope->force_context_allocation_for_parameters_ = encoded_boolean_flags_result[7];
  scope->is_declaration_scope_ = encoded_boolean_flags_result[8];
  scope->private_name_lookup_skips_outer_class_ = encoded_boolean_flags_result[9];
  // We determine if we can use preparse data ourselves.
  // scope->must_use_preparsed_scope_data_ = encoded_boolean_flags_result[10];
  scope->must_use_preparsed_scope_data_ = false;
  scope->is_repl_mode_scope_ = encoded_boolean_flags_result[11];
  scope->deserialized_scope_uses_external_cache_ = encoded_boolean_flags_result[12];
  scope->needs_home_object_ = encoded_boolean_flags_result[13];
  scope->is_block_scope_for_object_literal_ = encoded_boolean_flags_result[14];
  bool is_skipped_function = encoded_boolean_flags_result[15];

  // We now have the start_position of the scope so we can tell if we can skip the function.
  // We need to do this before we deserialize any FunctionDeclarations so that
  // we can end the recursion as early as possible and skip to the end of their
  // associated FunctionLiteral.
  if (is_skipped_function) {
    HandleFunctionSkipping(scope);
  }

  DeserializeScopeDeclarations(scope);
}

Scope* BinAstDeserializer::DeserializeScope() {
  Scope* scope = nullptr;
  uint8_t scope_type = DeserializeUint8();

  switch (scope_type) {
    case FUNCTION_SCOPE:
    case SCRIPT_SCOPE:
    case MODULE_SCOPE: {
      // Client should use the more specific version of this function.
      UNREACHABLE();
    }
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case WITH_SCOPE: {
      printf("scope_type = %d\n", scope_type);
      // TODO(binast): Implement
      DCHECK(false);
      break;
    }
    case CATCH_SCOPE: {
      scope = parser_->NewScope(CATCH_SCOPE);
      break;
    }
    case BLOCK_SCOPE: {
      scope = parser_->NewVarblockScope();
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  DCHECK(scope_type == scope->scope_type());

  DeserializeCommonScopeFields(scope);
  return scope;
}


DeclarationScope* BinAstDeserializer::DeserializeDeclarationScope() {
  DeclarationScope* scope = nullptr;
  uint8_t scope_type = DeserializeUint8();

  switch (scope_type) {
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case MODULE_SCOPE:
    case SCRIPT_SCOPE:
    case CATCH_SCOPE:
    case WITH_SCOPE: {
      printf("Invalid scope type: %d\n", scope_type);
      UNREACHABLE();
    }
    case FUNCTION_SCOPE: {
      uint8_t function_kind = DeserializeUint8();
      scope = parser_->NewFunctionScope(static_cast<FunctionKind>(function_kind));
      break;
    }
    case BLOCK_SCOPE: {
      scope = parser_->NewVarblockScope();
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  DCHECK(scope_type == scope->scope_type());

  DeserializeCommonScopeFields(scope);

  // DeclarationScope data:
  auto encoded_decl_scope_bool_flags_result = DeserializeUint16Flags();
  scope->has_simple_parameters_ = encoded_decl_scope_bool_flags_result[0];
  scope->is_asm_module_ = encoded_decl_scope_bool_flags_result[1];
  scope->force_eager_compilation_ = encoded_decl_scope_bool_flags_result[2];
  scope->has_rest_ = encoded_decl_scope_bool_flags_result[3];
  scope->has_arguments_parameter_ = encoded_decl_scope_bool_flags_result[4];
  scope->uses_super_property_ = encoded_decl_scope_bool_flags_result[5];
  scope->should_eager_compile_ = encoded_decl_scope_bool_flags_result[6];
  scope->was_lazily_parsed_ = encoded_decl_scope_bool_flags_result[7];
  // We compute this ourselves inside DeserializeCommonScopeFields.
  // scope->is_skipped_function_ = encoded_decl_scope_bool_flags_result[8];
  scope->has_inferred_function_name_ = encoded_decl_scope_bool_flags_result[9];
  scope->has_checked_syntax_ = encoded_decl_scope_bool_flags_result[10];
  scope->has_this_reference_ = encoded_decl_scope_bool_flags_result[11];
  scope->has_this_declaration_ = encoded_decl_scope_bool_flags_result[12];
  scope->needs_private_name_context_chain_recalc_ = encoded_decl_scope_bool_flags_result[13];

  DeserializeScopeParameters(scope);

  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)

  scope->receiver_ = DeserializeVariableReference(scope);
  scope->function_ = DeserializeVariableReference(scope);
  scope->new_target_ = DeserializeVariableReference(scope);
  scope->arguments_ = DeserializeVariableReference(scope);

  // TODO(binast): rare_data_ (needed for > ES5.1 features)
  return scope;
}

FunctionLiteral* BinAstDeserializer::DeserializeFunctionLiteral(uint32_t bit_field, int32_t position) {
  // Swap in a new map for string offsets.
  std::vector<const AstRawString*> temp_strings;
  strings_.swap(temp_strings);

  uint32_t proxy_string_table_offset = DeserializeUint32();
  uint32_t end_of_proxy_string_table_offset = 0;
  {
    OffsetRestorationScope restoration_scope(this);
    current_offset_ = proxy_string_table_offset;
    DeserializeProxyStringTable();
    // Note: We set the offset after processing the body of the function.
    end_of_proxy_string_table_offset = current_offset_;
  }

  std::vector<Variable*> temp_variables;
  variables_.swap(temp_variables);

  uint32_t proxy_variable_table_offset = DeserializeUint32();
  uint32_t end_of_proxy_variable_table_offset = 0;

  auto previous_proxy_variable_table_base_offset = proxy_variable_table_base_offset_;
  proxy_variable_table_base_offset_ = proxy_variable_table_offset;
  {
    OffsetRestorationScope restoration_scope(this);
    current_offset_ = proxy_variable_table_offset;
    DeserializeProxyVariableTable();
    // Note: We set the offset after processing the body of the function.
    end_of_proxy_variable_table_offset = current_offset_;
  }

  // TODO(binast): Kind of silly that we serialize a cons string only to deserialized into a raw string
  AstConsString* name = DeserializeConsString();

  const AstRawString* raw_name = nullptr;
  if (name == nullptr) {
    raw_name = nullptr;
  } else {
    for (const AstRawString* s : name->ToRawStrings()) {
      DCHECK(raw_name == nullptr);
      DCHECK(s != nullptr);
      raw_name = s;
    }
  }

  DeclarationScope* scope = DeserializeDeclarationScope();
  int32_t expected_property_count = DeserializeInt32();
  int32_t parameter_count = DeserializeInt32();
  int32_t function_length = DeserializeInt32();
  int32_t function_token_position = DeserializeInt32();
  int32_t suspend_count = DeserializeInt32();
  int32_t function_literal_id = DeserializeInt32();

  FunctionLiteral::ParameterFlag has_duplicate_parameters = FunctionLiteral::HasDuplicateParameters::decode(bit_field) ? FunctionLiteral::ParameterFlag::kHasDuplicateParameters : FunctionLiteral::ParameterFlag::kNoDuplicateParameters;
  FunctionSyntaxKind function_syntax_kind = FunctionLiteral::FunctionSyntaxKindBits::decode(bit_field);
  FunctionLiteral::EagerCompileHint eager_compile_hint = scope->ShouldEagerCompile() ? FunctionLiteral::kShouldEagerCompile : FunctionLiteral::kShouldLazyCompile;
  bool has_braces = FunctionLiteral::HasBracesField::decode(bit_field);

  int32_t num_statements = DeserializeInt32();
  ScopedPtrList<Statement> body(pointer_buffer());
  body.Reserve(num_statements);
  if (!scope->is_skipped_function()) {
    // Warning: leaky separation of concerns. We need to clear the Scope's unresolved_list_ so
    // that the VariableProxy nodes we encounter during the deserialization of the
    // body can/will be used and added to the unresolved_list_ instead.
    scope->unresolved_list_.Clear();
    Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope);

    for (int i = 0; i < num_statements; ++i) {
      AstNode* statement = DeserializeAstNode();
      DCHECK(statement != nullptr);
      DCHECK(statement->AsStatement() != nullptr);
      body.Add(static_cast<Statement*>(statement));
    }
  }

  // Setting the offset now to advance past the proxy variable table.
  DCHECK(scope->is_skipped_function() || static_cast<uint32_t>(current_offset_) == proxy_variable_table_offset);
  current_offset_ = end_of_proxy_variable_table_offset;

  // Setting the offset now to advance past the proxy string table.
  DCHECK(static_cast<uint32_t>(current_offset_) == proxy_string_table_offset);
  current_offset_ = end_of_proxy_string_table_offset;

  FunctionLiteral* result = parser_->factory()->NewFunctionLiteral(
    raw_name, scope, body, expected_property_count, parameter_count,
    function_length, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id, SpeculativeParseFailureReason::kSucceeded);

  result->function_token_position_ = function_token_position;
  result->suspend_count_ = suspend_count;
  result->bit_field_ = bit_field;

  auto preparse_data_result = produced_preparse_data_by_start_position_.find(scope->start_position());
  if (preparse_data_result != produced_preparse_data_by_start_position_.end()) {
    result->produced_preparse_data_ = preparse_data_result->second;
  }

  // Swap the variable table back for the previous function.
  proxy_variable_table_base_offset_ = previous_proxy_variable_table_base_offset;
  variables_.swap(temp_variables);

  // Swap the string table back for the previous function.
  strings_.swap(temp_strings);

  return result;
}

ObjectLiteral* BinAstDeserializer::DeserializeObjectLiteral(uint32_t bit_field, int32_t position) {
  int32_t properties_length = DeserializeInt32();
  int32_t boilerplate_properties = DeserializeInt32();

  ScopedPtrList<ObjectLiteral::Property> properties(pointer_buffer());
  properties.Reserve(properties_length);

  for (int i = 0; i < properties_length; i++) {
    AstNode* key = DeserializeAstNode();
    AstNode* value = DeserializeAstNode();
    uint8_t kind = DeserializeUint8();
    uint8_t is_computed_name = DeserializeUint8();

    auto property = parser_->factory()->NewObjectLiteralProperty(
        static_cast<Expression*>(key),
        static_cast<Expression*>(value),
        static_cast<ObjectLiteral::Property::Kind>(kind),
        is_computed_name);
    properties.Add(property);
  }

  Variable* home_object = DeserializeVariableReference();

  bool has_rest_property = false;
  ObjectLiteral* result = parser_->factory()->NewObjectLiteral(
      properties, boilerplate_properties, position,
      has_rest_property, home_object);
  result->bit_field_ = bit_field;
  return result;
}

ArrayLiteral* BinAstDeserializer::DeserializeArrayLiteral(uint32_t bit_field, int32_t position) {
  int32_t array_length = DeserializeInt32();
  int32_t first_spread_index = DeserializeInt32();

  DCHECK(first_spread_index == -1);

  ScopedPtrList<Expression> values(pointer_buffer());
  values.Reserve(array_length);

  for (int i = 0; i < array_length; i++) {
    AstNode* value = DeserializeAstNode();
    values.Add(static_cast<Expression*>(value));
  }

  ArrayLiteral* result = parser_->factory()->NewArrayLiteral(
      values, first_spread_index, position);
  result->bit_field_ = bit_field;
  return result;
}

NaryOperation* BinAstDeserializer::DeserializeNaryOperation(uint32_t bit_field, int32_t position) {
  Token::Value op = NaryOperation::OperatorField::decode(bit_field);

  AstNode* first = DeserializeAstNode();
  uint32_t subsequent_length = DeserializeUint32();

  NaryOperation* result = parser_->factory()->NewNaryOperation(
      op, static_cast<Expression*>(first), subsequent_length);

  for (uint32_t i = 0; i < subsequent_length; i++) {
    AstNode* expr = DeserializeAstNode();
    int32_t pos = DeserializeInt32();
    result->AddSubsequent(static_cast<Expression*>(expr), pos);
  }

  result->bit_field_ = bit_field;
  return result;
}

Conditional* BinAstDeserializer::DeserializeConditional(uint32_t bit_field, int32_t position) {
  AstNode* condition = DeserializeAstNode();
  AstNode*  then_expression = DeserializeAstNode();
  AstNode* else_expression = DeserializeAstNode();

  Conditional* result = parser_->factory()->NewConditional(
      static_cast<Expression*>(condition),
      static_cast<Expression*>(then_expression),
      static_cast<Expression*>(else_expression),
      position);
  result->bit_field_ = bit_field;
  return result;
}

TryCatchStatement* BinAstDeserializer::DeserializeTryCatchStatement(uint32_t bit_field, int32_t position) {
  AstNode* try_block = DeserializeAstNode();
  uint8_t has_scope = DeserializeUint8();

  Scope* scope = nullptr;
  Block* catch_block = nullptr;
  if (has_scope) {
    scope = DeserializeScope();
    DCHECK(scope != nullptr);

    Parser::BlockState catch_variable_block_state(&parser_->scope_, scope);
    AstNode* catch_block_result = DeserializeAstNode();
    catch_block = static_cast<Block*>(catch_block_result);
  } else {
    AstNode* catch_block_result = DeserializeAstNode();
    catch_block = static_cast<Block*>(catch_block_result);
  }

  TryCatchStatement* result = parser_->factory()->NewTryCatchStatement(
      static_cast<Block*>(try_block), scope,
      catch_block, position);
  result->bit_field_ = bit_field;
  return result;
}

RegExpLiteral* BinAstDeserializer::DeserializeRegExpLiteral(uint32_t bit_field, int32_t position) {
  const AstRawString* raw_pattern = DeserializeRawStringReference();
  int32_t flags = DeserializeInt32();

  RegExpLiteral* result = parser_->factory()->NewRegExpLiteral(
      raw_pattern, flags, position);
  result->bit_field_ = bit_field;
  return result;
}

SwitchStatement* BinAstDeserializer::DeserializeSwitchStatement(uint32_t bit_field, int32_t position) {
  AstNode* tag = DeserializeAstNode();

  SwitchStatement* result = parser_->factory()->NewSwitchStatement(
      static_cast<Expression*>(tag), position);

  int32_t num_cases = DeserializeInt32();
  for (int i = 0; i < num_cases; i++) {
    uint8_t is_default = DeserializeUint8();
    Expression* label;

    if (is_default == 0) {
      AstNode* deserialized_label = DeserializeAstNode();
      label = static_cast<Expression*>(deserialized_label);
    } else {
      label = nullptr;
    }

    int32_t statements_length = DeserializeInt32();
    ScopedPtrList<Statement> statements(pointer_buffer());
    statements.Reserve(statements_length);

    for (int i = 0; i < statements_length; i++) {
      AstNode* statement = DeserializeAstNode();
      statements.Add(static_cast<Statement*>(statement));
    }

    result->cases()->Add(parser_->factory()->NewCaseClause(label, statements),
                         zone());
  }

  result->bit_field_ = bit_field;
  return result;
}

BreakStatement* BinAstDeserializer::DeserializeBreakStatement(uint32_t bit_field, int32_t position) {
  BreakStatement* result = parser_->factory()->NewBreakStatement(nullptr, position);
  DeserializeNodeReference(reinterpret_cast<void**>(&result->target_));
  return result;
}

ContinueStatement* BinAstDeserializer::DeserializeContinueStatement(uint32_t bit_field, int32_t position) {
  ContinueStatement* result = parser_->factory()->NewContinueStatement(nullptr, position);
  DeserializeNodeReference(reinterpret_cast<void**>(&result->target_));
  return result;
}

Throw* BinAstDeserializer::DeserializeThrow(uint32_t bit_field, int32_t position) {
  AstNode* exception = DeserializeAstNode();
  Throw* result = parser_->factory()->NewThrow(
      static_cast<Expression*>(exception), position);
  result->bit_field_ = bit_field;
  return result;
}

}  // namespace internal
}  // namespace v8

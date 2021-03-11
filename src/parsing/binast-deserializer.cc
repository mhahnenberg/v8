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
      string_table_base_offset_(0),
      proxy_variable_table_base_offset_(0),
      global_variable_table_base_offset_(0),
      is_root_fn_(true) {
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
  int offset = 0;
  bool is_toplevel = true;

  auto string_table_result = DeserializeStringTable(uncompressed_ast, offset);
  offset = string_table_result.new_offset;

  auto variable_table_base_offset = DeserializeUint32(uncompressed_ast, offset);
  offset = variable_table_base_offset.new_offset;
  global_variable_table_base_offset_ = variable_table_base_offset.value;

  // Note: we don't deserialize the string table here because we rely on the
  // proxy string table within the FunctionLiteral, which references offsets in
  // the actual string table, even if we're deserializing a top-level function.
  if (start_offset.has_value()) {
    DCHECK(start_offset.value() >= static_cast<uint32_t>(offset));
    is_toplevel = false;
    offset = start_offset.value();
  }

  auto result = DeserializeAstNode(uncompressed_ast, offset, is_toplevel);
  offset = result.new_offset;

  DCHECK(start_offset ? 
      (static_cast<size_t>(offset) == start_offset.value() + length.value()) 
    : (static_cast<size_t>(offset) == global_variable_table_base_offset_));

  // Skip ahead to global variable table.
  offset = global_variable_table_base_offset_;
  auto variable_table_length = DeserializeUint32(uncompressed_ast, offset);
  offset = variable_table_length.new_offset;

  offset += GLOBAL_VARIABLE_TABLE_ENTRY_SIZE * variable_table_length.value;
  // Check that we consumed all the bytes that were serialized.
  DCHECK(static_cast<size_t>(offset) == uncompressed_size);
  return result.value;
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeVariableMap(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto total_local_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_local_variables.new_offset;

  for (uint32_t i = 0; i < total_local_variables.value; ++i) {
    auto new_variable =
        DeserializeLocalVariable(serialized_binast, offset, scope);
    offset = new_variable.new_offset;
  }

  auto total_nonlocal_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_nonlocal_variables.new_offset;

  for (uint32_t i = 0; i < total_nonlocal_variables.value; ++i) {
    auto new_variable =
        DeserializeNonLocalVariable(serialized_binast, offset, scope);
    offset = new_variable.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeUnresolvedList(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto total_unresolved_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_unresolved_variables.new_offset;

  bool add_unresolved = false;
  for (uint32_t i = 0; i < total_unresolved_variables.value; ++i) {
    auto variable_proxy = DeserializeVariableProxy(serialized_binast, offset, add_unresolved);
    offset = variable_proxy.new_offset;

    // Normally we add variable proxies to the unresolved_list_ when we encounter them inside
    // the body of the function, but if we're skipping the function we won't encounter
    // them so we need to add them here instead. Unfortunately, we don't know if
    // we're skipping the function yet, so we unconditionally add them here and then clear
    // the list later if we decide to deserialize the body of the function.
    scope->AddUnresolved(variable_proxy.value);
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<Declaration*> BinAstDeserializer::DeserializeDeclaration(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto start_pos = DeserializeInt32(serialized_binast, offset);
  offset = start_pos.new_offset;

  auto decl_type = DeserializeUint8(serialized_binast, offset);
  offset = decl_type.new_offset;

  auto variable = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = variable.new_offset;

  Declaration* result;
  switch (decl_type.value) {
    case Declaration::DeclType::VariableDecl: {
      auto is_nested = DeserializeUint8(serialized_binast, offset);
      offset = is_nested.new_offset;

      // TODO(binast): Add support for nested var decls.
      DCHECK(!is_nested.value);
      result = new (zone()) VariableDeclaration(start_pos.value, is_nested.value);
      break;
    }
    case Declaration::DeclType::FunctionDecl: {
      // We need to push the Scope being currently deserialized onto the Scope stack while deserializing
      // the FunctionLiterals inside FunctionDeclarations, otherwise their scope chain will end up skipping it.
      Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope->AsDeclarationScope());
      auto func = DeserializeAstNode(serialized_binast, offset);
      offset = func.new_offset;

      result = new (zone()) FunctionDeclaration((void*)func.value, start_pos.value);
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  result->set_var(variable.value);
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeDeclarations(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto num_decls = DeserializeUint32(serialized_binast, offset);
  offset = num_decls.new_offset;

  for (uint32_t i = 0; i < num_decls.value; ++i) {
    auto decl_result = DeserializeDeclaration(serialized_binast, offset, scope);
    scope->decls_.Add(decl_result.value);
    offset = decl_result.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeParameters(uint8_t* serialized_binast, int offset, DeclarationScope* scope) {
  auto num_parameters_result = DeserializeInt32(serialized_binast, offset);
  offset = num_parameters_result.new_offset;
  scope->num_parameters_ = num_parameters_result.value;

  for (int i = 0; i < num_parameters_result.value; ++i) {
    auto param_result = DeserializeVariableReference(serialized_binast, offset, scope);
    offset = param_result.new_offset;
    scope->params_.Add(param_result.value, zone());
  }

  return {nullptr, offset};
}

void BinAstDeserializer::HandleFunctionSkipping(Scope* scope, bool can_skip_function) {
  if (scope->scope_type() != FUNCTION_SCOPE) {
    return;
  }

  DeclarationScope* decl_scope = scope->AsDeclarationScope();

  if (!can_skip_function) {
    decl_scope->set_is_skipped_function(false);
    return;
  }

  // We skip FunctionLiterals before we even start deserializing the scope,
  // so we should never encounter this condition.
  DCHECK(!decl_scope->outer_scope()->GetClosureScope()->is_skipped_function());
  if (!parser_->info()->consumed_preparse_data()->IsFunctionOffsetNextSkippable(decl_scope->start_position())) {
    decl_scope->set_is_skipped_function(false);
    return;
  }

  decl_scope->outer_scope()->SetMustUsePreparseData();
  decl_scope->set_is_skipped_function(true);

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

  if (preparse_data != nullptr) {
    DCHECK(decl_scope->outer_scope()->must_use_preparsed_scope_data());
    produced_preparse_data_by_start_position_[decl_scope->start_position()] = preparse_data;
  }
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeCommonScopeFields(uint8_t* serialized_binast, int offset, Scope* scope, bool can_skip_function) {
  auto variable_map_result = DeserializeScopeVariableMap(serialized_binast, offset, scope);
  offset = variable_map_result.new_offset;

  auto unresolved_list_result = DeserializeScopeUnresolvedList(serialized_binast, offset, scope);
  offset = unresolved_list_result.new_offset;

  // scope_info_ TODO(binast): do we need this?
#ifdef DEBUG
  auto has_scope_name = DeserializeUint8(serialized_binast, offset);
  offset = has_scope_name.new_offset;

  if (has_scope_name.value) {
    auto scope_name_result = DeserializeRawStringReference(serialized_binast, offset);
    offset = scope_name_result.new_offset;
    scope->SetScopeName(scope_name_result.value);
  }

  auto already_resolved_result = DeserializeUint8(serialized_binast, offset);
  offset = already_resolved_result.new_offset;
  scope->already_resolved_ = already_resolved_result.value;

  auto needs_migration_result = DeserializeUint8(serialized_binast, offset);
  offset = needs_migration_result.new_offset;
  scope->needs_migration_ = needs_migration_result.value;
#endif
  auto start_position_result = DeserializeInt32(serialized_binast, offset);
  offset = start_position_result.new_offset;
  scope->set_start_position(start_position_result.value);

  auto end_position_result = DeserializeInt32(serialized_binast, offset);
  offset = end_position_result.new_offset;
  scope->set_end_position(end_position_result.value);

  auto num_stack_slots_result = DeserializeInt32(serialized_binast, offset);
  offset = num_stack_slots_result.new_offset;
  scope->num_stack_slots_ = num_stack_slots_result.value;

  auto num_heap_slots_result = DeserializeInt32(serialized_binast, offset);
  offset = num_heap_slots_result.new_offset;
  scope->num_heap_slots_ = num_heap_slots_result.value;

  auto encoded_boolean_flags_result = DeserializeUint16Flags(serialized_binast, offset);
  offset = encoded_boolean_flags_result.new_offset;

  scope->is_strict_ = encoded_boolean_flags_result.value[0];
  scope->calls_eval_ = encoded_boolean_flags_result.value[1];
  scope->sloppy_eval_can_extend_vars_ = encoded_boolean_flags_result.value[2];
  scope->scope_nonlinear_ = encoded_boolean_flags_result.value[3];
  scope->is_hidden_ = encoded_boolean_flags_result.value[4];
  scope->is_debug_evaluate_scope_ = encoded_boolean_flags_result.value[5];
  scope->inner_scope_calls_eval_ = encoded_boolean_flags_result.value[6];
  scope->force_context_allocation_for_parameters_ = encoded_boolean_flags_result.value[7];
  scope->is_declaration_scope_ = encoded_boolean_flags_result.value[8];
  scope->private_name_lookup_skips_outer_class_ = encoded_boolean_flags_result.value[9];
  // We determine if we can use preparse data ourselves.
  // scope->must_use_preparsed_scope_data_ = encoded_boolean_flags_result.value[10];
  scope->must_use_preparsed_scope_data_ = false;
  scope->is_repl_mode_scope_ = encoded_boolean_flags_result.value[11];
  scope->deserialized_scope_uses_external_cache_ = encoded_boolean_flags_result.value[12];

  // We now have the start_position of the scope so we can tell if we can skip the function.
  // We need to do this before we deserialize any FunctionDeclarations so that
  // we can end the recursion as early as possible and skip to the end of their
  // associated FunctionLiteral.
  HandleFunctionSkipping(scope, can_skip_function);

  auto declarations_result = DeserializeScopeDeclarations(serialized_binast, offset, scope);
  offset = declarations_result.new_offset;

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<Scope*> BinAstDeserializer::DeserializeScope(uint8_t* serialized_binast, int offset) {
  Scope* scope = nullptr;
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  switch (scope_type.value) {
    case FUNCTION_SCOPE:
    case SCRIPT_SCOPE:
    case MODULE_SCOPE: {
      // Client should use the more specific version of this function.
      UNREACHABLE();
    }
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case WITH_SCOPE: {
      printf("scope_type.value = %d\n", scope_type.value);
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
  DCHECK(scope_type.value == scope->scope_type());

  auto common_fields_result = DeserializeCommonScopeFields(serialized_binast, offset, scope);
  offset = common_fields_result.new_offset;

  return {scope, offset};
}


BinAstDeserializer::DeserializeResult<DeclarationScope*> BinAstDeserializer::DeserializeDeclarationScope(uint8_t* serialized_binast, int offset, bool can_skip_function) {
  DeclarationScope* scope = nullptr;
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  switch (scope_type.value) {
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case MODULE_SCOPE:
    case SCRIPT_SCOPE:
    case CATCH_SCOPE:
    case WITH_SCOPE: {
      printf("Invalid scope type: %d\n", scope_type.value);
      UNREACHABLE();
    }
    case FUNCTION_SCOPE: {
      auto function_kind = DeserializeUint8(serialized_binast, offset);
      offset = function_kind.new_offset;

      scope = parser_->NewFunctionScope(static_cast<FunctionKind>(function_kind.value));
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
  DCHECK(scope_type.value == scope->scope_type());

  auto common_fields_result = DeserializeCommonScopeFields(serialized_binast, offset, scope, can_skip_function);
  offset = common_fields_result.new_offset;

  // DeclarationScope data:
  auto encoded_decl_scope_bool_flags_result = DeserializeUint16Flags(serialized_binast, offset);
  offset = encoded_decl_scope_bool_flags_result.new_offset;

  scope->has_simple_parameters_ = encoded_decl_scope_bool_flags_result.value[0];
  scope->is_asm_module_ = encoded_decl_scope_bool_flags_result.value[1];
  scope->force_eager_compilation_ = encoded_decl_scope_bool_flags_result.value[2];
  scope->has_rest_ = encoded_decl_scope_bool_flags_result.value[3];
  scope->has_arguments_parameter_ = encoded_decl_scope_bool_flags_result.value[4];
  scope->scope_uses_super_property_ = encoded_decl_scope_bool_flags_result.value[5];
  scope->should_eager_compile_ = encoded_decl_scope_bool_flags_result.value[6];
  scope->was_lazily_parsed_ = encoded_decl_scope_bool_flags_result.value[7];
  // We compute this ourselves inside DeserializeCommonScopeFields.
  // scope->is_skipped_function_ = encoded_decl_scope_bool_flags_result.value[8];
  scope->has_inferred_function_name_ = encoded_decl_scope_bool_flags_result.value[9];
  scope->has_checked_syntax_ = encoded_decl_scope_bool_flags_result.value[10];
  scope->has_this_reference_ = encoded_decl_scope_bool_flags_result.value[11];
  scope->has_this_declaration_ = encoded_decl_scope_bool_flags_result.value[12];
  scope->needs_private_name_context_chain_recalc_ = encoded_decl_scope_bool_flags_result.value[13];

  auto params_result = DeserializeScopeParameters(serialized_binast, offset, scope);
  offset = params_result.new_offset;

  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)

  auto receiver_result = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = receiver_result.new_offset;
  scope->receiver_ = receiver_result.value;

  auto function_result = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = function_result.new_offset;
  scope->function_ = function_result.value;

  auto new_target_result = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = new_target_result.new_offset;
  scope->new_target_ = new_target_result.value;

  auto arguments_result = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = arguments_result.new_offset;
  scope->arguments_ = arguments_result.value;

  // TODO(binast): rare_data_ (needed for > ES5.1 features)
  return {scope, offset};
}

BinAstDeserializer::DeserializeResult<FunctionLiteral*> BinAstDeserializer::DeserializeFunctionLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  bool is_root_fn = is_root_fn_;
  is_root_fn_ = false;
  // If we have PreparseData and we're not deserializing the "root" function
  // (i.e. the function we're currently compiling) then we can skip deserializing
  // the current function.
  bool can_skip_function = !(is_root_fn || preparse_data_.is_null());

  // Swap in a new map for string offsets.
  std::vector<const AstRawString*> temp_strings;
  strings_.swap(temp_strings);

  auto proxy_string_table_offset = DeserializeUint32(serialized_binast, offset);
  offset = proxy_string_table_offset.new_offset;

  auto proxy_string_table = DeserializeProxyStringTable(serialized_binast, proxy_string_table_offset.value);
  // Note: We set the offset after processing the body of the function.

  std::vector<Variable*> temp_variables;
  variables_.swap(temp_variables);

  auto proxy_variable_table_offset = DeserializeUint32(serialized_binast, offset);
  offset = proxy_variable_table_offset.new_offset;

  auto previous_proxy_variable_table_base_offset = proxy_variable_table_base_offset_;
  proxy_variable_table_base_offset_ = proxy_variable_table_offset.value;
  auto proxy_variable_table = DeserializeProxyVariableTable(serialized_binast, proxy_variable_table_offset.value);
  // Note: We set the offset after processing the body of the function.

  // TODO(binast): Kind of silly that we serialize a cons string only to deserialized into a raw string
  auto name = DeserializeConsString(serialized_binast, offset);
  offset = name.new_offset;

  const AstRawString* raw_name = nullptr;
  if (name.value == nullptr) {
    raw_name = nullptr;
  } else {
    for (const AstRawString* s : name.value->ToRawStrings()) {
      DCHECK(raw_name == nullptr);
      DCHECK(s != nullptr);
      raw_name = s;
    }
  }

  auto scope = DeserializeDeclarationScope(serialized_binast, offset, can_skip_function);
  offset = scope.new_offset;
  
  auto expected_property_count = DeserializeInt32(serialized_binast, offset);
  offset = expected_property_count.new_offset;

  auto parameter_count = DeserializeInt32(serialized_binast, offset);
  offset = parameter_count.new_offset;

  auto function_length = DeserializeInt32(serialized_binast, offset);
  offset = function_length.new_offset;

  auto function_token_position = DeserializeInt32(serialized_binast, offset);
  offset = function_token_position.new_offset;

  auto suspend_count = DeserializeInt32(serialized_binast, offset);
  offset = suspend_count.new_offset;

  auto function_literal_id = DeserializeInt32(serialized_binast, offset);
  offset = function_literal_id.new_offset;

  FunctionLiteral::ParameterFlag has_duplicate_parameters = FunctionLiteral::HasDuplicateParameters::decode(bit_field) ? FunctionLiteral::ParameterFlag::kHasDuplicateParameters : FunctionLiteral::ParameterFlag::kNoDuplicateParameters;
  FunctionSyntaxKind function_syntax_kind = FunctionLiteral::FunctionSyntaxKindBits::decode(bit_field);
  FunctionLiteral::EagerCompileHint eager_compile_hint = scope.value->ShouldEagerCompile() ? FunctionLiteral::kShouldEagerCompile : FunctionLiteral::kShouldLazyCompile;
  bool has_braces = FunctionLiteral::HasBracesField::decode(bit_field);

  auto num_statements = DeserializeInt32(serialized_binast, offset);
  offset = num_statements.new_offset;

  std::vector<void*> pointer_buffer;
  pointer_buffer.reserve(num_statements.value);
  ScopedPtrList<Statement> body(&pointer_buffer);
  if (!scope.value->is_skipped_function()) {
    // Warning: leaky separation of concerns. We need to clear the Scope's unresolved_list_ so
    // that the VariableProxy nodes we encounter during the deserialization of the
    // body can/will be used and added to the unresolved_list_ instead.
    scope.value->unresolved_list_.Clear();
    Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope.value);

    for (int i = 0; i < num_statements.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;
      DCHECK(statement.value != nullptr);
      DCHECK(statement.value->AsStatement() != nullptr);
      body.Add(static_cast<Statement*>(statement.value));
    }
  }

  // Setting the offset now to advance past the proxy variable table.
  DCHECK(scope.value->is_skipped_function() || static_cast<uint32_t>(offset) == proxy_variable_table_offset.value);
  offset = proxy_variable_table.new_offset;

  // Setting the offset now to advance past the proxy string table.
  DCHECK(static_cast<uint32_t>(offset) == proxy_string_table_offset.value);
  offset = proxy_string_table.new_offset;

  FunctionLiteral* result = parser_->factory()->NewFunctionLiteral(
    raw_name, scope.value, body, expected_property_count.value, parameter_count.value,
    function_length.value, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id.value);

  result->function_token_position_ = function_token_position.value;
  result->suspend_count_ = suspend_count.value;
  result->bit_field_ = bit_field;

  auto preparse_data_result = produced_preparse_data_by_start_position_.find(scope.value->start_position());
  if (preparse_data_result != produced_preparse_data_by_start_position_.end()) {
    result->produced_preparse_data_ = preparse_data_result->second;
  }

  // Swap the variable table back for the previous function.
  proxy_variable_table_base_offset_ = previous_proxy_variable_table_base_offset;
  variables_.swap(temp_variables);

  // Swap the string table back for the previous function.
  strings_.swap(temp_strings);

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ObjectLiteral*> BinAstDeserializer::DeserializeObjectLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto properties_length = DeserializeInt32(serialized_binast, offset);
  offset = properties_length.new_offset;

  auto boilerplate_properties = DeserializeInt32(serialized_binast, offset);
  offset = boilerplate_properties.new_offset;

  std::vector<void*> pointer_buffer;
  ScopedPtrList<ObjectLiteral::Property> properties(&pointer_buffer);

  for (int i = 0; i < properties_length.value; i++) {
    auto key = DeserializeAstNode(serialized_binast, offset);
    offset = key.new_offset;

    auto value = DeserializeAstNode(serialized_binast, offset);
    offset = value.new_offset;

    auto kind = DeserializeUint8(serialized_binast, offset);
    offset = kind.new_offset;

    auto is_computed_name = DeserializeUint8(serialized_binast, offset);
    offset = is_computed_name.new_offset;

    auto property = parser_->factory()->NewObjectLiteralProperty(
        static_cast<Expression*>(key.value),
        static_cast<Expression*>(value.value),
        static_cast<ObjectLiteral::Property::Kind>(kind.value),
        is_computed_name.value);

    properties.Add(property);
  }

  bool has_rest_property = false;
  ObjectLiteral* result = parser_->factory()->NewObjectLiteral(
      properties, boilerplate_properties.value, position,
      has_rest_property);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ArrayLiteral*>
BinAstDeserializer::DeserializeArrayLiteral(uint8_t* serialized_binast,
                                            uint32_t bit_field,
                                            int32_t position, int offset) {
  auto array_length = DeserializeInt32(serialized_binast, offset);
  offset = array_length.new_offset;

  auto first_spread_index = DeserializeInt32(serialized_binast, offset);
  offset = first_spread_index.new_offset;

  DCHECK(first_spread_index.value == -1);

  std::vector<void*> pointer_buffer;
  pointer_buffer.reserve(array_length.value);
  ScopedPtrList<Expression> values(&pointer_buffer);

  for (int i = 0; i < array_length.value; i++) {
    auto value = DeserializeAstNode(serialized_binast, offset);
    offset = value.new_offset;

    values.Add(static_cast<Expression*>(value.value));
  }

  ArrayLiteral* result = parser_->factory()->NewArrayLiteral(
      values, first_spread_index.value, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<NaryOperation*>
BinAstDeserializer::DeserializeNaryOperation(uint8_t* serialized_binast,
                                             uint32_t bit_field,
                                             int32_t position, int offset) {
  Token::Value op = NaryOperation::OperatorField::decode(bit_field);

  auto first = DeserializeAstNode(serialized_binast, offset);
  offset = first.new_offset;

  auto subsequent_length = DeserializeUint32(serialized_binast, offset);
  offset = subsequent_length.new_offset;

  NaryOperation* result = parser_->factory()->NewNaryOperation(
      op, static_cast<Expression*>(first.value), subsequent_length.value);

  for (uint32_t i = 0; i < subsequent_length.value; i++) {
    auto expr = DeserializeAstNode(serialized_binast, offset);
    offset = expr.new_offset;

    auto pos = DeserializeInt32(serialized_binast, offset);
    offset = pos.new_offset;

    result->AddSubsequent(static_cast<Expression*>(expr.value), pos.value);
  }

  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Conditional*>
BinAstDeserializer::DeserializeConditional(uint8_t* serialized_binast,
                                           uint32_t bit_field, int32_t position,
                                           int offset) {
  auto condition = DeserializeAstNode(serialized_binast, offset);
  offset = condition.new_offset;

  auto then_expression = DeserializeAstNode(serialized_binast, offset);
  offset = then_expression.new_offset;

  auto else_expression = DeserializeAstNode(serialized_binast, offset);
  offset = else_expression.new_offset;

  Conditional* result = parser_->factory()->NewConditional(
      static_cast<Expression*>(condition.value),
      static_cast<Expression*>(then_expression.value),
      static_cast<Expression*>(else_expression.value),
      position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<TryCatchStatement*>
BinAstDeserializer::DeserializeTryCatchStatement(uint8_t* serialized_binast,
                                                 uint32_t bit_field,
                                                 int32_t position, int offset) {
  auto try_block = DeserializeAstNode(serialized_binast, offset);
  offset = try_block.new_offset;

  auto has_scope = DeserializeUint8(serialized_binast, offset);
  offset = has_scope.new_offset;

  Scope* scope_ptr = nullptr;
  Block* catch_block = nullptr;
  if (has_scope.value) {
    auto scope = DeserializeScope(serialized_binast, offset);
    offset = scope.new_offset;

    DCHECK(scope.value != nullptr);
    scope_ptr = scope.value;

    Parser::BlockState catch_variable_block_state(&parser_->scope_, scope_ptr);
    auto catch_block_result = DeserializeAstNode(serialized_binast, offset);
    catch_block = static_cast<Block*>(catch_block_result.value);
    offset = catch_block_result.new_offset;
  } else {
    auto catch_block_result = DeserializeAstNode(serialized_binast, offset);
    catch_block = static_cast<Block*>(catch_block_result.value);
    offset = catch_block_result.new_offset;
  }

  TryCatchStatement* result = parser_->factory()->NewTryCatchStatement(
      static_cast<Block*>(try_block.value), scope_ptr,
      catch_block, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<RegExpLiteral*>
BinAstDeserializer::DeserializeRegExpLiteral(uint8_t* serialized_binast,
                                                 uint32_t bit_field,
                                                 int32_t position, int offset) {
  auto raw_pattern = DeserializeRawStringReference(serialized_binast, offset);
  offset = raw_pattern.new_offset;

  auto flags = DeserializeInt32(serialized_binast, offset);
  offset = flags.new_offset;

  RegExpLiteral* result = parser_->factory()->NewRegExpLiteral(
      raw_pattern.value, flags.value, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<SwitchStatement*>
BinAstDeserializer::DeserializeSwitchStatement(uint8_t* serialized_binast,
                                               uint32_t bit_field,
                                               int32_t position, int offset) {
  auto tag = DeserializeAstNode(serialized_binast, offset);
  offset = tag.new_offset;

  SwitchStatement* result = parser_->factory()->NewSwitchStatement(
      static_cast<Expression*>(tag.value), position);

  auto num_cases = DeserializeInt32(serialized_binast, offset);
  offset = num_cases.new_offset;

  for (int i = 0; i < num_cases.value; i++) {
    auto is_default = DeserializeUint8(serialized_binast, offset);
    offset = is_default.new_offset;

    Expression* label;

    if (is_default.value == 0) {
      auto deserialized_label = DeserializeAstNode(serialized_binast, offset);
      offset = deserialized_label.new_offset;

      label = static_cast<Expression*>(deserialized_label.value);
    } else {
      label = nullptr;
    }

    auto statements_length = DeserializeInt32(serialized_binast, offset);
    offset = statements_length.new_offset;

    std::vector<void*> pointer_buffer;
    pointer_buffer.reserve(statements_length.value);
    ScopedPtrList<Statement> statements(&pointer_buffer);

    for (int i = 0; i < statements_length.value; i++) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;

      statements.Add(static_cast<Statement*>(statement.value));
    }

    result->cases()->Add(parser_->factory()->NewCaseClause(label, statements),
                         zone());
  }

  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<BreakStatement*>
BinAstDeserializer::DeserializeBreakStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  BreakStatement* result = parser_->factory()->NewBreakStatement(nullptr, position);

  auto target = DeserializeNodeReference(serialized_binast, offset, reinterpret_cast<void**>(&result->target_));
  offset = target.new_offset;

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ContinueStatement*>
BinAstDeserializer::DeserializeContinueStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  ContinueStatement* result = parser_->factory()->NewContinueStatement(nullptr, position);

  auto target = DeserializeNodeReference(serialized_binast, offset, reinterpret_cast<void**>(&result->target_));
  offset = target.new_offset;

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Throw*>
BinAstDeserializer::DeserializeThrow(uint8_t* serialized_binast,
                                     uint32_t bit_field, int32_t position,
                                     int offset) {
  auto exception = DeserializeAstNode(serialized_binast, offset);
  offset = exception.new_offset;

  Throw* result = parser_->factory()->NewThrow(
      static_cast<Expression*>(exception.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

// This is just a placeholder while we implement the various nodes that we'll support.
BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeNodeStub(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  UNREACHABLE();
  return {nullptr, offset};
}

}  // namespace internal
}  // namespace v8

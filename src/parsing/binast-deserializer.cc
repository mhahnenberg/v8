// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-deserializer.h"
#include "src/ast/ast.h"
#include "src/parsing/parser.h"
#include "src/objects/fixed-array-inl.h"

namespace v8 {
namespace internal {

// #define OBJECT_OFFSETOF(class, field) (reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<class*>(0x4000)->field)) - 0x4000)

BinAstDeserializer::BinAstDeserializer(Parser* parser)
  : parser_(parser) {

}

BinAstDeserializer::DeserializeResult<uint32_t> BinAstDeserializer::DeserializeUint32(ByteArray bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes.get(offset + i);
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(uint32_t)};
}

BinAstDeserializer::DeserializeResult<uint16_t> BinAstDeserializer::DeserializeUint16(ByteArray bytes, int offset) {
  uint16_t result = 0;
  for (int i = 0; i < 2; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes.get(offset + i);
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(uint16_t)};
}


BinAstDeserializer::DeserializeResult<uint8_t> BinAstDeserializer::DeserializeUint8(ByteArray bytes, int offset) {
  return {bytes.get(offset), offset + sizeof(uint8_t)};
}

BinAstDeserializer::DeserializeResult<int32_t> BinAstDeserializer::DeserializeInt32(ByteArray bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes.get(offset + i);
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(int32_t)};
}

BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawString(ByteArray serialized_ast, int offset) {
  auto is_one_byte = DeserializeUint8(serialized_ast, offset);
  offset = is_one_byte.new_offset;

  auto hash_field = DeserializeUint32(serialized_ast, offset);
  offset = hash_field.new_offset;

  auto length = DeserializeUint32(serialized_ast, offset);
  offset = length.new_offset;

  std::vector<uint8_t> raw_data;
  for (uint32_t i = 0; i < length.value; ++i) {
    auto next_byte = DeserializeUint8(serialized_ast, offset);
    offset = next_byte.new_offset;
    raw_data.push_back(next_byte.value);
  }
  const AstRawString* s = nullptr;
  if (raw_data.size() > 0) {
    Vector<const byte> literal_bytes(&raw_data[0], raw_data.size());
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  } else {
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  }
  string_table_.insert({string_table_.size() + 1, s});
  return {s, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeStringTable(ByteArray serialized_ast, int offset) {
  auto num_entries = DeserializeUint32(serialized_ast, offset);
  offset = num_entries.new_offset;
  for (uint32_t i = 0; i < num_entries.value; ++i) {
    auto string = DeserializeRawString(serialized_ast, offset);
    offset = string.new_offset;
  }
  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawStringReference(ByteArray serialized_ast, int offset) {
  auto string_table_index = DeserializeUint32(serialized_ast, offset);
  offset = string_table_index.new_offset;
  auto lookup_result = string_table_.find(string_table_index.value);
  DCHECK(lookup_result != string_table_.end());
  const AstRawString* result = lookup_result->second;
  DCHECK(result != nullptr);
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<AstConsString*> BinAstDeserializer::DeserializeConsString(ByteArray serialized_ast, int offset) {
  auto raw_string_count = DeserializeUint32(serialized_ast, offset);
  offset = raw_string_count.new_offset;

  if (raw_string_count.value == 0) {
    return {nullptr, offset};
  }

  AstConsString* cons_string = parser_->ast_value_factory()->NewConsString();

  for (uint32_t i = 0; i < raw_string_count.value; ++i) {
    auto string = DeserializeRawStringReference(serialized_ast, offset);
    DCHECK(parser_->zone() != nullptr);
    cons_string->AddString(parser_->zone(), string.value);
    offset = string.new_offset;
  }

  return {cons_string, offset};
}

AstNode* BinAstDeserializer::DeserializeAst(ByteArray serialized_ast) {
  int offset = 0;
  auto string_table_result = DeserializeStringTable(serialized_ast, offset);
  offset = string_table_result.new_offset;
  auto result = DeserializeAstNode(serialized_ast, offset);
  return result.value;
}

BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeAstNode(ByteArray serialized_binast, int offset) {
  auto bit_field = DeserializeUint32(serialized_binast, offset);
  offset = bit_field.new_offset;

  auto position = DeserializeInt32(serialized_binast, offset);
  offset = position.new_offset;

  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field.value);
  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    auto result = DeserializeFunctionLiteral(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  default: {
    UNREACHABLE();
  }
  }
}

BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeLocalVariable(ByteArray serialized_binast, int offset, Scope* scope) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_

  auto index = DeserializeInt32(serialized_binast, offset);
  offset = index.new_offset;

  auto initializer_position = DeserializeInt32(serialized_binast, offset);
  offset = initializer_position.new_offset;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->Declare(parser_->zone(), name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, &was_added);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}

BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonLocalVariable(ByteArray serialized_binast, int offset, Scope* scope) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_

  auto index = DeserializeInt32(serialized_binast, offset);
  offset = index.new_offset;

  auto initializer_position = DeserializeInt32(serialized_binast, offset);
  offset = initializer_position.new_offset;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->variables_.Declare(parser_->zone(), scope, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic, &was_added);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeVariableMap(ByteArray serialized_binast, int offset, Scope* scope) {
  auto total_local_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_local_variables.new_offset;

  for (uint32_t i = 0; i < total_local_variables.value; ++i) {
    auto new_variable = DeserializeLocalVariable(serialized_binast, offset, scope);
    offset = new_variable.new_offset;
  }

  auto total_nonlocal_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_nonlocal_variables.new_offset;

  for (uint32_t i = 0; i < total_nonlocal_variables.value; ++i) {
    auto new_variable = DeserializeNonLocalVariable(serialized_binast, offset, scope);
    offset = new_variable.new_offset;
  }

  return {nullptr, offset};
}


BinAstDeserializer::DeserializeResult<DeclarationScope*> BinAstDeserializer::DeserializeDeclarationScope(ByteArray serialized_binast, int offset) {
  Scope* scope = nullptr;
  // Scope data:
  // scope_type_
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  // function_kind_
  auto function_kind = DeserializeUint8(serialized_binast, offset);
  offset = function_kind.new_offset;

  switch (scope_type.value) {
    case CLASS_SCOPE: 
    case EVAL_SCOPE:
    case MODULE_SCOPE:
    case SCRIPT_SCOPE:
    case CATCH_SCOPE: {
      UNREACHABLE();
    }
    case FUNCTION_SCOPE: {
      scope = parser_->NewFunctionScope(static_cast<FunctionKind>(function_kind.value));
      break;
    }
    case BLOCK_SCOPE: {
      scope = parser_->NewVarblockScope();
      break;
    }
    case WITH_SCOPE:
    default: {
      UNREACHABLE();
    }
  }
  DCHECK(scope_type.value == scope->scope_type());
  // TODO(binast): How to represent/store these scope relationships?
  // Idea: We could store an ID with each scope when serializing it and then build a table of scopes as we deserialize and then link all the scopes in the tree together (similar to raw string references)
  // outer_scope_
  // inner_scope_
  // sibling_

  // variables_
  auto variable_map_result = DeserializeScopeVariableMap(serialized_binast, offset, scope);
  offset = variable_map_result.new_offset;
  // locals_
  // unresolved_list_
  // decls_
  // scope_info_ TODO(binast): do we need this?
#ifdef DEBUG
  // scope_name_
  // already_resolved_
  // needs_migration_
#endif
  // start_position_
  // end_position_
  // num_stack_slots_
  // num_heap_slots_
  // is_strict_
  // calls_eval_
  // sloppy_eval_can_extend_vars_
  // scope_nonlinear_
  // is_hidden_
  // is_debug_evaluate_scope_
  // inner_scope_calls_eval_
  // force_context_allocation_for_parameters_
  // is_declaration_scope_
  // private_name_lookup_skips_outer_class_
  // must_use_preparsed_scope_data_
  // is_repl_mode_scope_
  // deserialized_scope_uses_external_cache_

  // DeclarationScope data:
  // has_simple_parameters_
  // is_asm_module_
  // force_eager_compilation_
  // has_rest_
  // has_arguments_parameter_
  // scope_uses_super_property_
  // should_eager_compile_
  // was_lazily_parsed_
#ifdef DEBUG
  // is_being_lazy_parsed_
#endif
  // is_skipped_function_
  // has_inferred_function_name_
  // has_checked_syntax_
  // has_this_reference_
  // has_this_declaration_
  // needs_private_name_context_chain_recalc_
  // num_parameters_
  // params_
  // sloppy_block_functions_
  // receiver_
  // function_
  // new_target_
  // arguments_
  // rare_data_



  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<FunctionLiteral*> BinAstDeserializer::DeserializeFunctionLiteral(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  std::vector<void*> pointer_buffer;
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
    DCHECK(raw_name != nullptr);
  }

  auto scope = DeserializeDeclarationScope(serialized_binast, offset);
  offset = scope.new_offset;
  
  /* TODO */const ScopedPtrList<Statement> body(&pointer_buffer);
  /* TODO */int expected_property_count = 0;
  /* TODO */int parameter_count = 0;
  /* TODO */int function_length = 0;
  /* TODO */FunctionLiteral::ParameterFlag has_duplicate_parameters = FunctionLiteral::ParameterFlag::kNoDuplicateParameters;
  /* TODO */FunctionSyntaxKind function_syntax_kind = FunctionSyntaxKind::kDeclaration;
  /* TODO */FunctionLiteral::EagerCompileHint eager_compile_hint = FunctionLiteral::EagerCompileHint::kShouldLazyCompile;
  /* TODO */bool has_braces = false;
  /* TODO */int function_literal_id = 0;
  FunctionLiteral* result = parser_->factory()->NewFunctionLiteral(
    raw_name, scope.value, body, expected_property_count, parameter_count, 
    function_length, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id);
  return {result, offset};
}


}  // namespace internal
}  // namespace v8

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-deserializer.h"
#include "src/ast/ast.h"
#include "src/parsing/binast-serialize-visitor.h"
#include "src/parsing/parser.h"
#include "src/objects/fixed-array-inl.h"
#include "src/zone/zone-list-inl.h"

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

BinAstDeserializer::DeserializeResult<std::array<bool, 16>> BinAstDeserializer::DeserializeUint16Flags(ByteArray bytes, int offset) {
  std::array<bool, 16> flags;
  auto encoded_flags_result = DeserializeUint16(bytes, offset);
  offset = encoded_flags_result.new_offset;
  uint16_t encoded_flags = encoded_flags_result.value;
  for (size_t i = 0; i < flags.size(); ++i) {
    auto shift = flags.size() - i - 1;
    flags[i] = (encoded_flags >> shift) & 0x1;
  }
  return {flags, offset};
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
  if (string_table_index.value == 0) {
    return {nullptr, offset};
  }
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

void BinAstDeserializer::LinkUnresolvedVariableProxies() {
  for (auto it = variable_proxies_by_position_.begin(); it != variable_proxies_by_position_.end(); ++it) {
    VariableProxy* var_proxy = it->second;
    if (var_proxy->next_unresolved_ == nullptr) {
      continue;
    }

    int next_unresolved_position = (int)(std::intptr_t)var_proxy->next_unresolved_;
    auto lookup_result = variable_proxies_by_position_.find(next_unresolved_position);
    DCHECK(lookup_result != variable_proxies_by_position_.end());
    var_proxy->next_unresolved_ = lookup_result->second;
  }
}

AstNode* BinAstDeserializer::DeserializeAst(ByteArray serialized_ast) {
  int offset = 0;
  auto string_table_result = DeserializeStringTable(serialized_ast, offset);
  offset = string_table_result.new_offset;
  auto result = DeserializeAstNode(serialized_ast, offset);
  // Check that we consumed all the bytes that were serialized.
  DCHECK(result.new_offset == serialized_ast.length());
  LinkUnresolvedVariableProxies();
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
  case AstNode::kReturnStatement: {
    auto result = DeserializeReturnStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBinaryOperation: {
    auto result = DeserializeBinaryOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kProperty: {
    auto result = DeserializeProperty(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kExpressionStatement: {
    auto result = DeserializeExpressionStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kVariableProxyExpression: {
    auto result = DeserializeVariableProxyExpression(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBlock:
  case AstNode::kIfStatement:
  case AstNode::kLiteral:
  case AstNode::kEmptyStatement:
  case AstNode::kAssignment:
  case AstNode::kForStatement:
  case AstNode::kCompareOperation:
  case AstNode::kCountOperation:
  case AstNode::kCall:
  case AstNode::kObjectLiteral:
  case AstNode::kArrayLiteral: {
    auto result = DeserializeNodeStub(serialized_binast, bit_field.value, position.value, offset);
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

  if (name.value == nullptr) {
    return {nullptr, offset};
  }

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

BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeVariableReference(ByteArray serialized_binast, int offset) {
  auto variable_reference = DeserializeUint32(serialized_binast, offset);
  offset = variable_reference.new_offset;

  if (variable_reference.value == 0) {
    return {nullptr, offset};
  }

  auto variable_result = variables_by_id_.find(variable_reference.value);
  DCHECK(variable_result != variables_by_id_.end());
  Variable* variable = variable_result->second;

  return {variable, offset};
}

BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeScopeVariable(ByteArray serialized_binast, int offset, Scope* scope) {
  auto variable_result = DeserializeNonLocalVariable(serialized_binast, offset, scope);
  offset = variable_result.new_offset;
  
  Variable* variable = variable_result.value;
  if (variable == nullptr) {
    return {nullptr, offset};
  }
  variables_by_id_.insert({variables_by_id_.size() + 1, variable});
  return {variable, offset};
}

// This is for Variables that didn't belong to any particular Scope, i.e. their scope_ field was null.
BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonScopeVariable(ByteArray serialized_binast, int offset) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  if (name.value == nullptr) {
    return {nullptr, offset};
  }

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_

  auto index = DeserializeInt32(serialized_binast, offset);
  offset = index.new_offset;

  auto initializer_position = DeserializeInt32(serialized_binast, offset);
  offset = initializer_position.new_offset;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  Variable* variable = new (zone()) Variable(nullptr, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}


BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeScopeVariableOrReference(ByteArray serialized_binast, int offset, Scope* scope) {
  auto marker_result = DeserializeUint8(serialized_binast, offset);
  offset = marker_result.new_offset;

  switch (marker_result.value) {
    case ScopeVariableKind::Null: {
      return {nullptr, offset};
    }
    case ScopeVariableKind::Definition: {
      auto scope_result = DeserializeScopeVariable(serialized_binast, offset, scope);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    case ScopeVariableKind::Reference: {
      auto scope_result = DeserializeVariableReference(serialized_binast, offset);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    default: {
      UNREACHABLE();
    }
  }
}

BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonScopeVariableOrReference(ByteArray serialized_binast, int offset) {
  auto marker_result = DeserializeUint8(serialized_binast, offset);
  offset = marker_result.new_offset;

  switch (marker_result.value) {
    case ScopeVariableKind::Null: {
      return {nullptr, offset};
    }
    case ScopeVariableKind::Definition: {
      auto scope_result = DeserializeNonScopeVariable(serialized_binast, offset);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    case ScopeVariableKind::Reference: {
      auto scope_result = DeserializeVariableReference(serialized_binast, offset);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    default: {
      UNREACHABLE();
    }
  }
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeVariableMap(ByteArray serialized_binast, int offset, Scope* scope) {
  auto total_local_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_local_variables.new_offset;

  for (uint32_t i = 0; i < total_local_variables.value; ++i) {
    auto new_variable = DeserializeLocalVariable(serialized_binast, offset, scope);
    variables_by_id_.insert({variables_by_id_.size() + 1, new_variable.value});
    offset = new_variable.new_offset;
  }

  auto total_nonlocal_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_nonlocal_variables.new_offset;

  for (uint32_t i = 0; i < total_nonlocal_variables.value; ++i) {
    auto new_variable = DeserializeNonLocalVariable(serialized_binast, offset, scope);
    variables_by_id_.insert({variables_by_id_.size() + 1, new_variable.value});
    offset = new_variable.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<Declaration*> BinAstDeserializer::DeserializeDeclaration(ByteArray serialized_binast, int offset, Scope* scope) {
  auto start_pos = DeserializeInt32(serialized_binast, offset);
  offset = start_pos.new_offset;

  auto decl_type = DeserializeUint8(serialized_binast, offset);
  offset = decl_type.new_offset;

  auto variable = DeserializeVariableReference(serialized_binast, offset);
  offset = variable.new_offset;

  Declaration* decl = new (zone()) Declaration(start_pos.value, static_cast<Declaration::DeclType>(decl_type.value));
  decl->set_var(variable.value);
  return {decl, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeDeclarations(ByteArray serialized_binast, int offset, Scope* scope) {
  auto num_decls = DeserializeUint32(serialized_binast, offset);
  offset = num_decls.new_offset;

  for (uint32_t i = 0; i < num_decls.value; ++i) {
    auto decl_result = DeserializeDeclaration(serialized_binast, offset, scope);
    scope->decls_.Add(decl_result.value);
    offset = decl_result.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeParameters(ByteArray serialized_binast, int offset, DeclarationScope* scope) {
  auto num_parameters_result = DeserializeInt32(serialized_binast, offset);
  offset = num_parameters_result.new_offset;
  scope->num_parameters_ = num_parameters_result.value;

  for (int i = 0; i < num_parameters_result.value; ++i) {
    auto param_result = DeserializeVariableReference(serialized_binast, offset);
    offset = param_result.new_offset;
    scope->params_.Add(param_result.value, zone());
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<DeclarationScope*> BinAstDeserializer::DeserializeDeclarationScope(ByteArray serialized_binast, int offset) {
  DeclarationScope* scope = nullptr;
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  auto function_kind = DeserializeUint8(serialized_binast, offset);
  offset = function_kind.new_offset;

  switch (scope_type.value) {
    case CLASS_SCOPE: 
    case EVAL_SCOPE:
    case MODULE_SCOPE:
    case SCRIPT_SCOPE:
    case CATCH_SCOPE:
    case WITH_SCOPE: {
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
  auto declarations_result = DeserializeScopeDeclarations(serialized_binast, offset, scope);
  offset = declarations_result.new_offset;

  // scope_info_ TODO(binast): do we need this?
#ifdef DEBUG
  auto scope_name_result = DeserializeRawStringReference(serialized_binast, offset);
  offset = scope_name_result.new_offset;
  scope->SetScopeName(scope_name_result.value);

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
  scope->must_use_preparsed_scope_data_ = encoded_boolean_flags_result.value[10];
  scope->is_repl_mode_scope_ = encoded_boolean_flags_result.value[11];
  scope->deserialized_scope_uses_external_cache_ = encoded_boolean_flags_result.value[12];

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
  scope->is_skipped_function_ = encoded_decl_scope_bool_flags_result.value[9];
  scope->has_inferred_function_name_ = encoded_decl_scope_bool_flags_result.value[10];
  scope->has_checked_syntax_ = encoded_decl_scope_bool_flags_result.value[11];
  scope->has_this_reference_ = encoded_decl_scope_bool_flags_result.value[12];
  scope->has_this_declaration_ = encoded_decl_scope_bool_flags_result.value[13];
  scope->needs_private_name_context_chain_recalc_ = encoded_decl_scope_bool_flags_result.value[14];

  auto params_result = DeserializeScopeParameters(serialized_binast, offset, scope);
  offset = params_result.new_offset;

  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)

  auto receiver_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = receiver_result.new_offset;
  scope->receiver_ = receiver_result.value;

  auto function_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = function_result.new_offset;
  scope->function_ = function_result.value;

  auto new_target_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = new_target_result.new_offset;
  scope->new_target_ = new_target_result.value;

  auto arguments_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = arguments_result.new_offset;
  scope->arguments_ = arguments_result.value;

  // TODO(binast): rare_data_ (needed for > ES5.1 features)
  return {scope, offset};
}

BinAstDeserializer::DeserializeResult<FunctionLiteral*> BinAstDeserializer::DeserializeFunctionLiteral(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
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

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Statement> body(&pointer_buffer);
  auto num_statements = DeserializeInt32(serialized_binast, offset);
  offset = num_statements.new_offset;

  for (int i = 0; i < num_statements.value; ++i) {
    auto statement = DeserializeAstNode(serialized_binast, offset);
    offset = statement.new_offset;
    DCHECK(statement.value == nullptr || statement.value->AsStatement() != nullptr);
    body.Add(static_cast<Statement*>(statement.value));
  }

  FunctionLiteral* result = parser_->factory()->NewFunctionLiteral(
    raw_name, scope.value, body, expected_property_count.value, parameter_count.value,
    function_length.value, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id.value);

  result->function_token_position_ = function_token_position.value;
  result->suspend_count_ = suspend_count.value;

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ReturnStatement*> BinAstDeserializer::DeserializeReturnStatement(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto end_position = DeserializeInt32(serialized_binast, offset);
  offset = end_position.new_offset;

  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ReturnStatement* result = parser_->factory()->NewReturnStatement(static_cast<Expression*>(expression.value), position, end_position.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<BinaryOperation*> BinAstDeserializer::DeserializeBinaryOperation(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(serialized_binast, offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(serialized_binast, offset);
  offset = right.new_offset;

  Token::Value op = BinaryOperation::OperatorField::decode(bit_field);

  BinaryOperation* result = parser_->factory()->NewBinaryOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Property*> BinAstDeserializer::DeserializeProperty(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto obj = DeserializeAstNode(serialized_binast, offset);
  offset = obj.new_offset;

  auto key = DeserializeAstNode(serialized_binast, offset);
  offset = key.new_offset;

  Property* result = parser_->factory()->NewProperty(static_cast<Expression*>(obj.value), static_cast<Expression*>(key.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ExpressionStatement*> BinAstDeserializer::DeserializeExpressionStatement(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ExpressionStatement* result = parser_->factory()->NewExpressionStatement(static_cast<Expression*>(expression.value), offset);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<VariableProxy*> BinAstDeserializer::DeserializeVariableProxy(ByteArray serialized_binast, int offset) {
  auto position = DeserializeInt32(serialized_binast, offset);
  offset = position.new_offset;

  auto bit_field = DeserializeUint32(serialized_binast, offset);
  offset = bit_field.new_offset;

  bool is_resolved = VariableProxy::IsResolvedField::decode(bit_field.value);

  VariableProxy* result;
  if (is_resolved) {
    // The resolved Variable should either be a reference (i.e. currently visible in scope) or should be a 
    // NonScope Variable definition (i.e. it's a Variable that is outside the current Scope boundaries, 
    // e.g. inside an eval).
    auto variable = DeserializeNonScopeVariableOrReference(serialized_binast, offset);
    offset = variable.new_offset;
    result = parser_->factory()->NewVariableProxy(variable.value, position.value);
  } else {
    auto raw_name = DeserializeRawStringReference(serialized_binast, offset);
    offset = raw_name.new_offset;
    // We use NORMAL_VARIABLE as a placeholder here.
    result = parser_->factory()->NewVariableProxy(raw_name.value, VariableKind::NORMAL_VARIABLE, position.value);
  }
  result->bit_field_ = bit_field.value;

  auto next_unresolved_position = DeserializeInt32(serialized_binast, offset);
  offset = next_unresolved_position.new_offset;

  if (next_unresolved_position.value == -1) {
    result->next_unresolved_ = nullptr;
  } else {
    result->next_unresolved_ = static_cast<VariableProxy*>(reinterpret_cast<void*>(next_unresolved_position.value));
  }

  DCHECK(variable_proxies_by_position_.count(position.value) == 0);
  variable_proxies_by_position_[position.value] = result;

  printf("\n  Deserialized a VariableProxy for Variable '%.*s'\n", result->raw_name()->byte_length(), result->raw_name()->raw_data());

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<VariableProxyExpression*> BinAstDeserializer::DeserializeVariableProxyExpression(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto variable_proxy = DeserializeVariableProxy(serialized_binast, offset);
  offset = variable_proxy.new_offset;

  VariableProxyExpression* result = parser_->factory()->NewVariableProxyExpression(variable_proxy.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

// This is just a placeholder while we implement the various nodes that we'll support.
BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeNodeStub(ByteArray serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  return {nullptr, offset};
}

}  // namespace internal
}  // namespace v8

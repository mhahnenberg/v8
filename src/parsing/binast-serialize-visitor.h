// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
#define V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

#include "src/parsing/binast-visitor.h"
#include "src/ast/scopes.h"

namespace v8 {
namespace internal {

enum ScopeVariableKind : uint8_t {
  Null = 0,
  Reference = 1,
  Definition = 2,
};

// TODO(binast)
// Serializes binAST format into a linear sequence of bytes.
class BinAstSerializeVisitor final : public BinAstVisitor {
 public:
  BinAstSerializeVisitor(AstValueFactory* ast_value_factory)
    : BinAstVisitor(),
      ast_value_factory_(ast_value_factory) {
  }
  std::vector<uint8_t>& serialized_bytes() { return byte_data_; }

  void SerializeAst(AstNode* root);

  virtual void VisitFunctionLiteral(FunctionLiteral* function_literal) override;
  virtual void VisitBlock(Block* block) override;
  virtual void VisitIfStatement(IfStatement* if_statement) override;
  virtual void VisitExpressionStatement(ExpressionStatement* statement) override;
  virtual void VisitLiteral(Literal* literal) override;
  virtual void VisitEmptyStatement(EmptyStatement* empty_statement) override;
  virtual void VisitAssignment(Assignment* assignment) override;
  virtual void VisitVariableProxyExpression(VariableProxyExpression* var_proxy) override;
  virtual void VisitForStatement(ForStatement* for_statement) override;
  virtual void VisitCompareOperation(CompareOperation* compare) override;
  virtual void VisitCountOperation(CountOperation* operation) override;
  virtual void VisitCall(Call* call) override;
  virtual void VisitProperty(Property* property) override;
  virtual void VisitReturnStatement(ReturnStatement* return_statement) override;
  virtual void VisitBinaryOperation(BinaryOperation* binary_op) override;
  virtual void VisitObjectLiteral(ObjectLiteral* binary_op) override;
  virtual void VisitArrayLiteral(ArrayLiteral *array_literal) override;

 private:
  void SerializeUint32(uint32_t value);
  void SerializeUint16(uint16_t value);
  void SerializeUint16Flags(const std::list<bool>& flags);
  void SerializeInt32(int32_t value);
  void SerializeUint8(uint8_t value);
  void SerializeRawString(const AstRawString* s);
  void SerializeConsString(const AstConsString* cons_string);
  void SerializeRawStringReference(const AstRawString* s);
  void SerializeStringTable(const AstConsString* function_name);
  void SerializeVariable(Variable* variable);
  void SerializeScopeVariable(Scope* scope, Variable* variable);
  void SerializeScopeVariableReference(Scope* scope, Variable* variable);
  void SerializeScopeVariableOrReference(Scope* scope, Variable* variable);
  void SerializeScopeVariableMap(Scope* scope);
  void SerializeDeclaration(Scope* scope, Declaration* decl);
  void SerializeScopeDeclarations(Scope* scope);
  void SerializeScopeParameters(DeclarationScope* scope);
  void SerializeDeclarationScope(DeclarationScope* scope);


  AstValueFactory* ast_value_factory_;
  std::unordered_map<const AstRawString*, uint32_t> string_table_indices_;
  std::vector<uint8_t> byte_data_;
  std::unordered_map<Scope*, std::unordered_map<Variable*, uint32_t>> vars_by_scope_;
};

inline void BinAstSerializeVisitor::SerializeUint32(uint32_t value) {
  for (size_t i = 0; i < sizeof(uint32_t) / sizeof(uint8_t); ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t mask = 0xff << shift;
    uint32_t masked_value = value & mask;
    uint32_t final_value = masked_value >> shift;
    DCHECK(final_value <= 0xff);
    uint8_t truncated_final_value = final_value;
    byte_data_.push_back(truncated_final_value);
  }
}

inline void BinAstSerializeVisitor::SerializeUint16(uint16_t value) {
  for (size_t i = 0; i < sizeof(uint16_t) / sizeof(uint8_t); ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t mask = 0xff << shift;
    uint32_t masked_value = value & mask;
    uint32_t final_value = masked_value >> shift;
    DCHECK(final_value <= 0xff);
    uint8_t truncated_final_value = final_value;
    byte_data_.push_back(truncated_final_value);
  }
}

inline void BinAstSerializeVisitor::SerializeUint16Flags(const std::list<bool>& flags) {
  DCHECK(flags.size() <= 16);
  uint16_t encoded_flags = 0;
  for (bool flag : flags) {
    uint16_t encoded_flag = static_cast<uint16_t>(flag);
    encoded_flags = (encoded_flags << 1) | encoded_flag;
  }

  // For any unused flags, shift the encoding over so that the first flag is in the most significant bit.
  // This makes it so that we don't need to know how many flags were serialized while deserializing (i.e.
  // whatever is unused can be ignored);
  for (size_t i = 16; i > flags.size(); i--) {
    encoded_flags <<= 1;
  }
  SerializeUint16(encoded_flags);
}


inline void BinAstSerializeVisitor::SerializeUint8(uint8_t value) {
  byte_data_.push_back(value);
}

inline void BinAstSerializeVisitor::SerializeInt32(int32_t value) {
  for (size_t i = 0; i < sizeof(uint32_t) / sizeof(uint8_t); ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t mask = 0xff << shift;
    uint32_t masked_value = value & mask;
    uint32_t final_value = masked_value >> shift;
    DCHECK(final_value <= 0xff);
    uint8_t truncated_final_value = final_value;
    byte_data_.push_back(truncated_final_value);
  }
}

inline void BinAstSerializeVisitor::SerializeRawString(const AstRawString* s) {
  DCHECK(s != nullptr);
  DCHECK(string_table_indices_.count(s) == 0);
  uint32_t length = s->byte_length();
  bool is_one_byte = s->is_one_byte();
  uint32_t hash_field = s->hash_field();

  SerializeUint8(is_one_byte);
  SerializeUint32(hash_field);
  SerializeUint32(length);
  if (length > 0) {
    const uint8_t* bytes = s->raw_data();
    for (uint32_t i = 0; i < length; ++i) {
      SerializeUint8(bytes[i]);
    }
  }
}

inline void BinAstSerializeVisitor::SerializeRawStringReference(const AstRawString* s) {
  auto lookup_result = string_table_indices_.find(s);
  DCHECK(lookup_result != string_table_indices_.end());
  uint32_t string_table_index = lookup_result->second;
  SerializeUint32(string_table_index);
}

inline void BinAstSerializeVisitor::SerializeConsString(const AstConsString* cons_string) {
  if (cons_string == nullptr) {
    // TODO(binast): This makes it impossible to distinguish between a nullptr and an empty AstConsString. Not sure if it will matter...
    SerializeUint32(0);
    return;
  }
  std::forward_list<const AstRawString*> strings = cons_string->ToRawStrings();
  uint32_t length = 0;
  for (const AstRawString* string : strings) {
    DCHECK(string != nullptr);
    DCHECK(string_table_indices_.count(string) == 1);
    length += 1;
  }

  SerializeUint32(length);
  for (const AstRawString* string : strings) {
    DCHECK(string != nullptr);
    SerializeRawStringReference(string);
  }
}

inline void BinAstSerializeVisitor::SerializeStringTable(const AstConsString* function_name) {
  uint32_t num_entries = ast_value_factory_->string_table_.occupancy();
  // We serialize the outer function raw_name too.
  // TODO(binast): Do we need to?
  if (function_name != nullptr) {
    for (const AstRawString* s : function_name->ToRawStrings()) {
      void* key = const_cast<AstRawString*>(s);
      if (ast_value_factory_->string_table_.Lookup(key, s->Hash()) == nullptr) {
        num_entries += 1;
      }
    }
  }
  SerializeUint32(num_entries);
  uint32_t current_index = 1;
  for (base::HashMap::Entry* entry = ast_value_factory_->string_table_.Start(); entry != nullptr; entry = ast_value_factory_->string_table_.Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    SerializeRawString(s);
    string_table_indices_.insert({s, current_index});
    current_index += 1;
  }

  if (function_name != nullptr) {
    for (const AstRawString* s : function_name->ToRawStrings()) {
      void* key = const_cast<AstRawString*>(s);
      if (ast_value_factory_->string_table_.Lookup(key, s->Hash()) == nullptr) {
        SerializeRawString(s);
        string_table_indices_.insert({s, current_index});
        current_index += 1;
      }
    }
  }

  DCHECK(current_index == num_entries + 1);
}

inline void BinAstSerializeVisitor::SerializeAst(AstNode* root) {
  auto start = std::chrono::high_resolution_clock::now();
  FunctionLiteral* literal = root->AsFunctionLiteral();
  DCHECK(literal != nullptr);
  SerializeStringTable(literal->raw_name());
  VisitNode(root);

  auto elapsed = std::chrono::high_resolution_clock::now() - start;
  long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  printf("Serialized function: '");
  for (const AstRawString* s : literal->raw_name()->ToRawStrings()) {
    printf("%.*s", s->byte_length(), s->raw_data());
  }
  printf("' in %lld us\n", microseconds);
}

inline void BinAstSerializeVisitor::SerializeVariable(Variable* variable) {
  SerializeRawStringReference(variable->raw_name());

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?

  SerializeInt32(variable->index());
  SerializeInt32(variable->initializer_position());
  SerializeUint16(variable->bit_field_);
}

inline void BinAstSerializeVisitor::SerializeScopeVariableReference(Scope* scope, Variable* variable) {
  if (variable == nullptr) {
    SerializeUint32(0);
    return;
  }
  auto scope_var_ids_result = vars_by_scope_.find(scope);
  DCHECK(scope_var_ids_result != vars_by_scope_.end());
  std::unordered_map<Variable*, uint32_t>& scope_var_ids = scope_var_ids_result->second;

  auto var_id_result = scope_var_ids.find(variable);
  DCHECK(var_id_result != scope_var_ids.end());
  uint32_t var_id = var_id_result->second;
  SerializeUint32(var_id);
}

inline void BinAstSerializeVisitor::SerializeScopeVariableMap(Scope* scope) {
  // Serialize locals first.
  std::unordered_set<const AstRawString*> locals;
  for (Variable* variable : scope->locals_) {
    locals.insert(variable->raw_name());
  }

  DCHECK(vars_by_scope_.count(scope) == 0);
  vars_by_scope_.insert({scope, std::unordered_map<Variable*, uint32_t>()});
  std::unordered_map<Variable*, uint32_t>& var_ids = vars_by_scope_[scope];

  DCHECK(locals.size() < UINT32_MAX);
  uint32_t total_local_vars = static_cast<uint32_t>(locals.size());
  SerializeUint32(total_local_vars);
  for (Variable* variable : scope->locals_) {
    SerializeVariable(variable);
    var_ids.insert({variable, var_ids.size() + 1});
  }

  // Now serialize any remaining variables we missed
  uint32_t total_nonlocal_vars = scope->num_var() - total_local_vars;
  uint32_t serialized_nonlocal_vars = 0;
  SerializeUint32(total_nonlocal_vars);
  for (VariableMap::Entry* entry = scope->variables_.Start(); entry != nullptr; entry = scope->variables_.Next(entry)) {
    Variable* variable = reinterpret_cast<Variable*>(entry->value);
    if (locals.count(variable->raw_name()) == 0) {
      SerializeVariable(variable);
      var_ids.insert({variable, var_ids.size() + 1});
      serialized_nonlocal_vars += 1;
    }
  }
  DCHECK(total_nonlocal_vars == serialized_nonlocal_vars);
}

inline void BinAstSerializeVisitor::SerializeDeclaration(Scope* scope, Declaration* decl) {
  SerializeInt32(decl->position());
  SerializeUint8(decl->type());
  SerializeScopeVariableReference(scope, decl->var());
}

inline void BinAstSerializeVisitor::SerializeScopeDeclarations(Scope* scope) {
  uint32_t num_decls = 0;
  for (Declaration* decl : *scope->declarations()) {
    (void)decl;
    num_decls += 1;
  }

  SerializeUint32(num_decls);

  for (Declaration* decl : *scope->declarations()) {
    SerializeDeclaration(scope, decl);
  }
}

inline void BinAstSerializeVisitor::SerializeScopeParameters(DeclarationScope* scope) {
  SerializeInt32(scope->num_parameters());

  for (Variable* variable : scope->params_) {
    SerializeScopeVariableReference(scope, variable);
  }
}

inline void BinAstSerializeVisitor::SerializeScopeVariable(Scope* scope, Variable* variable) {
  SerializeVariable(variable);
  auto& vars_in_scope = vars_by_scope_[scope];
  DCHECK(vars_in_scope.count(variable) == 0);
  vars_in_scope.insert({variable, vars_in_scope.size() + 1});
}

inline void BinAstSerializeVisitor::SerializeScopeVariableOrReference(Scope* scope, Variable* variable) {
  if (variable == nullptr) {
    SerializeUint8(ScopeVariableKind::Null);
    return;
  }

  auto& vars_in_scope = vars_by_scope_[scope];
  if (vars_in_scope.count(variable) == 0) {
    SerializeUint8(ScopeVariableKind::Definition);
    SerializeScopeVariable(scope, variable);
  } else {
    SerializeUint8(ScopeVariableKind::Reference);
    SerializeScopeVariableReference(scope, variable);
  }
}

inline void BinAstSerializeVisitor::SerializeDeclarationScope(DeclarationScope* scope) {
  ScopeType scope_type = scope->scope_type();
  SerializeUint8(scope_type);
  SerializeUint8(scope->function_kind());
  SerializeScopeVariableMap(scope);
  SerializeScopeDeclarations(scope);
#ifdef DEBUG
  SerializeRawStringReference(scope->scope_name_);
  SerializeUint8(scope->already_resolved_);
  SerializeUint8(scope->needs_migration_);
#endif
  SerializeInt32(scope->start_position());
  SerializeInt32(scope->end_position());
  SerializeInt32(scope->num_stack_slots());
  SerializeInt32(scope->num_heap_slots());
  // Standard Scope flags
  SerializeUint16Flags({
    scope->is_strict_,
    scope->calls_eval_,
    scope->sloppy_eval_can_extend_vars_,
    scope->scope_nonlinear_,
    scope->is_hidden_,
    scope->is_debug_evaluate_scope_,
    scope->inner_scope_calls_eval_,
    scope->force_context_allocation_for_parameters_,
    scope->is_declaration_scope_,
    scope->private_name_lookup_skips_outer_class_,
    scope->must_use_preparsed_scope_data_,
    scope->is_repl_mode_scope_,
    scope->deserialized_scope_uses_external_cache_,
  });
  // DeclarationScope-specific flags
  SerializeUint16Flags({
    scope->has_simple_parameters_,
    scope->is_asm_module_,
    scope->force_eager_compilation_,
    scope->has_rest_,
    scope->has_arguments_parameter_,
    scope->scope_uses_super_property_,
    scope->should_eager_compile_,
    scope->was_lazily_parsed_,
    scope->is_skipped_function_,
    scope->has_inferred_function_name_,
    scope->has_checked_syntax_,
    scope->has_this_reference_,
    scope->has_this_declaration_,
    scope->needs_private_name_context_chain_recalc_,
  });

  SerializeScopeParameters(scope);
  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)
  DCHECK(scope->sloppy_block_functions_.is_empty());

  SerializeScopeVariableOrReference(scope, scope->receiver_);
  SerializeScopeVariableOrReference(scope, scope->function_);
  SerializeScopeVariableOrReference(scope, scope->new_target_);
  SerializeScopeVariableOrReference(scope, scope->arguments_);

  // TODO(binast): rare_data_ (needed for > ES5.1 feature support)
  DCHECK(scope->rare_data_ == nullptr);
}

inline void BinAstSerializeVisitor::VisitFunctionLiteral(FunctionLiteral* function_literal) {
  SerializeUint32(function_literal->bit_field_);
  SerializeInt32(function_literal->position_);
  const AstConsString* name = function_literal->raw_name();
  SerializeConsString(name);
  SerializeDeclarationScope(function_literal->scope());
}

inline void BinAstSerializeVisitor::VisitBlock(Block* block) {

}

inline void BinAstSerializeVisitor::VisitIfStatement(IfStatement* if_statement) {

}

inline void BinAstSerializeVisitor::VisitExpressionStatement(ExpressionStatement* statement) {

}

inline void BinAstSerializeVisitor::VisitLiteral(Literal* literal) {

}

inline void BinAstSerializeVisitor::VisitEmptyStatement(EmptyStatement* empty_statement) {

}

inline void BinAstSerializeVisitor::VisitAssignment(Assignment* assignment) {

}

inline void BinAstSerializeVisitor::VisitVariableProxyExpression(VariableProxyExpression* var_proxy) {

}

inline void BinAstSerializeVisitor::VisitForStatement(ForStatement* for_statement) {

}

inline void BinAstSerializeVisitor::VisitCompareOperation(CompareOperation* compare) {

}

inline void BinAstSerializeVisitor::VisitCountOperation(CountOperation* operation) {

}

inline void BinAstSerializeVisitor::VisitCall(Call* call) {

}

inline void BinAstSerializeVisitor::VisitProperty(Property* property) {

}

inline void BinAstSerializeVisitor::VisitReturnStatement(ReturnStatement* return_statement) {

}

inline void BinAstSerializeVisitor::VisitBinaryOperation(BinaryOperation* binary_op) {

}

inline void BinAstSerializeVisitor::VisitObjectLiteral(ObjectLiteral* object_literal) {
  
}

inline void BinAstSerializeVisitor::VisitArrayLiteral(ArrayLiteral* array_literal) {
  
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
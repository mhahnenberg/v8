// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
#define V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

#include "src/parsing/binast-visitor.h"
#include "src/ast/scopes.h"
#include "third_party/zlib/zlib.h"

namespace v8 {
namespace internal {

enum ScopeVariableKind : uint8_t {
  Null = 0,
  Reference = 1,
  Definition = 2,
};

// Serializes binAST format into a linear sequence of bytes.
class BinAstSerializeVisitor final : public BinAstVisitor {
 public:
  BinAstSerializeVisitor(AstValueFactory* ast_value_factory)
    : BinAstVisitor(),
      ast_value_factory_(ast_value_factory),
      encountered_unhandled_nodes_(0),
      skipped_functions_(0),
      serialized_functions_(0) {
  }

  uint8_t* serialized_bytes() const {
    if (UseCompression()) {
      return compressed_byte_data_.get();
    } else {
      return const_cast<uint8_t*>(&byte_data_[0]);
    }
  }
  size_t serialized_bytes_length() const {
    if (UseCompression()) {
      return compressed_byte_data_length_;
    } else {
      return byte_data_.size();
    }
  }

  bool SerializeAst(AstNode* root);

  virtual void VisitFunctionLiteral(FunctionLiteral* function_literal) override;
  virtual void VisitBlock(Block* block) override;
  virtual void VisitIfStatement(IfStatement* if_statement) override;
  virtual void VisitExpressionStatement(ExpressionStatement* statement) override;
  virtual void VisitLiteral(Literal* literal) override;
  virtual void VisitEmptyStatement(EmptyStatement* empty_statement) override;
  virtual void VisitAssignment(Assignment* assignment) override;
  virtual void VisitVariableProxyExpression(VariableProxyExpression* var_proxy) override;
  virtual void VisitForStatement(ForStatement* for_statement) override;
  virtual void VisitForInStatement(ForInStatement* for_in_statement) override;
  virtual void VisitWhileStatement(WhileStatement* while_statement) override;
  virtual void VisitDoWhileStatement(DoWhileStatement* do_while_statement) override;
  virtual void VisitCompareOperation(CompareOperation* compare) override;
  virtual void VisitCountOperation(CountOperation* operation) override;
  virtual void VisitCall(Call* call) override;
  virtual void VisitCallNew(CallNew* call) override;
  virtual void VisitProperty(Property* property) override;
  virtual void VisitReturnStatement(ReturnStatement* return_statement) override;
  virtual void VisitUnaryOperation(UnaryOperation* unary_op) override;
  virtual void VisitBinaryOperation(BinaryOperation* binary_op) override;
  virtual void VisitNaryOperation(NaryOperation* nary_op) override;

  virtual void VisitObjectLiteral(ObjectLiteral* object_literal) override;
  virtual void VisitArrayLiteral(ArrayLiteral* array_literal) override;
  virtual void VisitCompoundAssignment(
      CompoundAssignment* compound_assignment) override;
  virtual void VisitConditional(Conditional* conditional) override;
  virtual void VisitTryCatchStatement(
      TryCatchStatement* try_catch_statement) override;
  virtual void VisitThrow(Throw* throw_statement) override;
  virtual void VisitThisExpression(ThisExpression* this_expression) override;
  virtual void VisitRegExpLiteral(RegExpLiteral* reg_exp_literal) override;
  virtual void VisitSwitchStatement(SwitchStatement* switch_statement) override;
  virtual void VisitUnhandledNodeType(AstNode* node) override;

 private:
  friend class BinAstDeserializer;
  static bool UseCompression() { return false; }

  void SerializeUint8(uint8_t value);
  void SerializeUint16(uint16_t value);
  void SerializeUint16Flags(const std::list<bool>& flags);
  void SerializeUint32(uint32_t value, const base::Optional<size_t> index);
  void SerializeVarUint32(uint32_t value);
  void SerializeUint64(uint64_t value);
  void SerializeInt32(int32_t value);
  void SerializeDouble(double value);
  void SerializeCString(const char* str);
  void SerializeRawString(const AstRawString* s);
  void SerializeConsString(const AstConsString* cons_string);
  void SerializeRawStringReference(const AstRawString* s);
  void SerializeStringTable(const AstConsString* function_name);
  void SerializeVariable(Variable* variable);
  void SerializeVariableReference(Variable* variable);
  void SerializeVariableOrReference(Variable* variable);
  void SerializeScopeVariableMap(Scope* scope);
  void SerializeDeclaration(Scope* scope, Declaration* decl);
  void SerializeScopeDeclarations(Scope* scope);
  void SerializeScopeParameters(DeclarationScope* scope);
  void SerializeCommonScopeFields(Scope* scope);
  void SerializeScope(Scope* scope);
  void SerializeDeclarationScope(DeclarationScope* scope);
  void SerializeAstNodeHeader(AstNode* node);
  void SerializeVariableProxy(VariableProxy* proxy);
  void SerializeCaseClause(CaseClause* case_clause);

  void VisitMaybeNode(AstNode* maybe_node);
  void ToDoBinAst(AstNode* node);

  AstValueFactory* ast_value_factory_;
  std::unordered_map<const AstRawString*, uint32_t> string_table_indices_;
  std::vector<uint8_t> byte_data_;
  std::unordered_map<Variable*, uint32_t> variable_ids_;
  std::unordered_map<VariableProxy*, int> var_proxy_ids;
  int encountered_unhandled_nodes_;
  int skipped_functions_;
  int serialized_functions_;

  std::unique_ptr<uint8_t[]> compressed_byte_data_;
  size_t compressed_byte_data_length_;
};

// TODO(binast): Maybe templatize these to reduce duplication?
inline void BinAstSerializeVisitor::SerializeUint64(uint64_t value) {
  for (size_t i = 0; i < sizeof(uint64_t) / sizeof(uint8_t); ++i) {
    size_t shift = 8 * i;
    uint64_t mask = 0xff;
    mask <<= shift;
    uint64_t masked_value = value & mask;
    uint64_t final_value = masked_value >> shift;
    if (final_value > 0xff) {
      printf("ERROR: value not in range: %zu\n", final_value);
      DCHECK(final_value <= 0xff);
    }
    uint8_t truncated_final_value = final_value;
    byte_data_.push_back(truncated_final_value);
  }
}

inline void BinAstSerializeVisitor::SerializeUint32(
    uint32_t value, const base::Optional<size_t> index = base::nullopt) {
  DCHECK(!index.has_value() ||
         (index.value() >= 0 && index <= byte_data_.size()));
  for (size_t i = 0; i < sizeof(uint32_t) / sizeof(uint8_t); ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t mask = 0xff << shift;
    uint32_t masked_value = value & mask;
    uint32_t final_value = masked_value >> shift;
    DCHECK(final_value <= 0xff);
    uint8_t truncated_final_value = final_value;

    if (!index.has_value()) {
      byte_data_.push_back(truncated_final_value);
    } else {
      byte_data_[index.value() + i] = truncated_final_value;
    }
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

inline void BinAstSerializeVisitor::SerializeDouble(double value) {
  union {
    double d;
    uint64_t ui;
  } converter;
  converter.d = value;
  SerializeUint64(converter.ui);
}

// Note: Assumes the provided char string is null-terminated
inline void BinAstSerializeVisitor::SerializeCString(const char* str) {
  for (int i = 0;; i++) {
    char c = str[i];
    if (c == 0) {
      break;
    }
    SerializeUint8(c);
  }
}

inline void BinAstSerializeVisitor::SerializeVarUint32(uint32_t value) {
  while (true) {
    uint8_t mask = 0x7f;
    uint8_t current_byte = value & mask;
    value >>= 7;
    if (value == 0) {
      SerializeUint8(current_byte);
      break;
    }
    current_byte |= 0x80;
    SerializeUint8(current_byte);
  }
}

inline void BinAstSerializeVisitor::SerializeRawString(const AstRawString* s) {
  DCHECK(s != nullptr);
  DCHECK(string_table_indices_.count(s) == 0);
  DCHECK(s->byte_length() >= 0);
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
  if (lookup_result == string_table_indices_.end()) {
    printf("ERROR: Failed to find raw string reference for '%.*s'\n", s->byte_length(), s->raw_data());
    DCHECK(false);
  }
  // DCHECK(lookup_result != string_table_indices_.end());
  uint32_t string_table_index = lookup_result->second;
  SerializeVarUint32(string_table_index);
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
    (void)string;
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
  uint32_t num_constant_entries = ast_value_factory_->string_constants_->string_table()->occupancy();
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
  uint32_t num_non_constant_entries = num_entries - num_constant_entries;
  DCHECK(num_entries >= num_constant_entries);
  SerializeUint32(num_non_constant_entries);
  uint32_t current_index = 1;
  // Insert non-constant strings
  for (base::HashMap::Entry* entry = ast_value_factory_->string_table_.Start(); entry != nullptr; entry = ast_value_factory_->string_table_.Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    // We don't want to serialize string constants that are shared between Isolates
    base::HashMap::Entry* string_const_entry = ast_value_factory_->string_constants_->string_table()->Lookup(const_cast<AstRawString*>(s), s->Hash());
    if (string_const_entry != nullptr) {
      continue;
    }
    SerializeRawString(s);
    string_table_indices_.insert({s, current_index});
    current_index += 1;
  }
  DCHECK(current_index - 1 == num_non_constant_entries);

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

  // Only insert constant strings into the table (i.e. don't serialize their contents)
  for (base::HashMap::Entry* entry = ast_value_factory_->string_constants_->string_table()->Start(); entry != nullptr; entry = ast_value_factory_->string_constants_->string_table()->Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    string_table_indices_.insert({s, current_index});
    current_index += 1;
  }

  DCHECK(current_index == num_entries + 1);
}

inline bool BinAstSerializeVisitor::SerializeAst(AstNode* root) {
  auto start = std::chrono::high_resolution_clock::now();
  FunctionLiteral* literal = root->AsFunctionLiteral();
  DCHECK(literal != nullptr);
  SerializeStringTable(literal->raw_name());
  VisitNode(root);

  if (encountered_unhandled_nodes_ > 0) {
    printf("Failed to serialize function: '");
    for (const AstRawString* s : literal->raw_name()->ToRawStrings()) {
      printf("%.*s", s->byte_length(), s->raw_data());
    }
    printf(
        "'. Successfully serialized %d functions, and skipped serialization due to %d "
        "functions containing a total of %d unsupported nodes \n",
        serialized_functions_, skipped_functions_,
        encountered_unhandled_nodes_);
    return false;
  }

  if (UseCompression()) {
    compressed_byte_data_ = std::make_unique<byte[]>(byte_data_.size() + sizeof(size_t));
    size_t compressed_data_size = byte_data_.size();
    int compress_result = compress2(compressed_byte_data_.get() + sizeof(size_t), &compressed_data_size, &byte_data_[0], byte_data_.size(), /* level */6);
    if (compress_result == Z_OK) {
      compressed_byte_data_length_ = compressed_data_size + sizeof(size_t);
      *reinterpret_cast<size_t*>(compressed_byte_data_.get()) = byte_data_.size();
    } else {
      printf("\nError compressing serialized AST: %s\n", zError(compress_result));
      return false;
    }
  }

  auto elapsed = std::chrono::high_resolution_clock::now() - start;
  long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  printf("Serialized function: '");
  for (const AstRawString* s : literal->raw_name()->ToRawStrings()) {
    printf("%.*s", s->byte_length(), s->raw_data());
  }
  printf("' in %lld us\n", microseconds);
  return true;
}

inline void BinAstSerializeVisitor::SerializeVariable(Variable* variable) {
  auto variable_offset = byte_data_.size();
  SerializeRawStringReference(variable->raw_name());

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?

  SerializeInt32(variable->index());
  SerializeInt32(variable->initializer_position());
  SerializeUint16(variable->bit_field_);

  DCHECK(variable_ids_.count(variable) == 0);
  variable_ids_.insert({variable, variable_offset});
}

inline void BinAstSerializeVisitor::SerializeVariableReference(Variable* variable) {
  if (variable == nullptr) {
    SerializeVarUint32(0);
    return;
  }
  auto var_id_result = variable_ids_.find(variable);
  DCHECK(var_id_result != variable_ids_.end());
  uint32_t var_id = var_id_result->second;
  SerializeVarUint32(var_id);
}

inline void BinAstSerializeVisitor::SerializeScopeVariableMap(Scope* scope) {
  // Serialize locals first.
  std::unordered_set<const AstRawString*> locals;
  std::unordered_set<const AstRawString*> temporaries;
  for (Variable* variable : scope->locals_) {
    locals.insert(variable->raw_name());
    if (variable->mode() == VariableMode::kTemporary) {
      temporaries.insert(variable->raw_name());
    }
  }

  DCHECK(locals.size() < UINT32_MAX);
  uint32_t total_local_vars = static_cast<uint32_t>(locals.size());
  SerializeUint32(total_local_vars);
  for (Variable* variable : scope->locals_) {
    SerializeVariable(variable);
  }

  // Now serialize any remaining variables we missed
  DCHECK(temporaries.size() < UINT32_MAX);
  uint32_t total_non_temporary_locals = total_local_vars - static_cast<uint32_t>(temporaries.size());
  DCHECK(static_cast<uint32_t>(scope->num_var()) >= total_non_temporary_locals);
  // Temporaries are only stored in locals_ (and not variables_), so don't include them in the non-local vars count.
  uint32_t total_nonlocal_vars = scope->num_var() - total_non_temporary_locals;
  uint32_t serialized_nonlocal_vars = 0;
  SerializeUint32(total_nonlocal_vars);
  for (VariableMap::Entry* entry = scope->variables_.Start(); entry != nullptr; entry = scope->variables_.Next(entry)) {
    Variable* variable = reinterpret_cast<Variable*>(entry->value);
    if (locals.count(variable->raw_name()) == 0) {
      SerializeVariable(variable);
      serialized_nonlocal_vars += 1;
    }
  }
  DCHECK(total_nonlocal_vars == serialized_nonlocal_vars);
}

inline void BinAstSerializeVisitor::SerializeDeclaration(Scope* scope, Declaration* decl) {
  SerializeInt32(decl->position());
  SerializeUint8(decl->type());
  SerializeVariableReference(decl->var());
  switch(decl->type()) {
    case Declaration::DeclType::VariableDecl: {
      // TODO(binast): Add support for nested variable declarations.
      if (decl->AsVariableDeclaration()->is_nested()) {
        encountered_unhandled_nodes_++;
        printf("BinAstSerializeVisitor encountered unhandled nested variable declaration, skipping function\n");
      }
      SerializeUint8(decl->AsVariableDeclaration()->is_nested());
      break;
    }
    case Declaration::DeclType::FunctionDecl: {
      VisitNode(decl->AsFunctionDeclaration()->fun<FunctionLiteral>());
      break;
    }
  }
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
    SerializeVariableReference(variable);
  }
}

// Note: This could be deserialized as a Scoped or Non-scoped Variable depending on the context.
inline void BinAstSerializeVisitor::SerializeVariableOrReference(Variable* variable) {
  if (variable == nullptr) {
    SerializeUint8(ScopeVariableKind::Null);
    return;
  }

  if (variable_ids_.count(variable) == 0) {
    SerializeUint8(ScopeVariableKind::Definition);
    SerializeVariable(variable);
  } else {
    DCHECK(variable->scope() != nullptr);
    SerializeUint8(ScopeVariableKind::Reference);
    SerializeVariableReference(variable);
  }
}

inline void BinAstSerializeVisitor::SerializeCommonScopeFields(Scope* scope) {
  SerializeScopeVariableMap(scope);
  SerializeScopeDeclarations(scope);
#ifdef DEBUG
  if (scope->scope_name_ == nullptr) {
    SerializeUint8(0);
  } else {
    SerializeUint8(1);
    SerializeRawStringReference(scope->scope_name_);
  }
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
}

inline void BinAstSerializeVisitor::SerializeDeclarationScope(DeclarationScope* scope) {
  SerializeUint8(scope->scope_type());
  DCHECK(scope->scope_type() == FUNCTION_SCOPE);
  SerializeUint8(scope->function_kind());
  SerializeCommonScopeFields(scope);
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

  if (scope->has_rest_) {
    printf(
        "BinAstSerializeVisitor encountered unsupported rest parameter on "
        "DeclarationScope\n");
    encountered_unhandled_nodes_++;
  }

  SerializeScopeParameters(scope);
  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)
  if (!scope->sloppy_block_functions_.is_empty()) {
    printf("BinAstSerializeVisitor encountered unsupported sloppy block functions on DeclarationScope\n");
    encountered_unhandled_nodes_++;
  }

  SerializeVariableOrReference(scope->receiver_);
  SerializeVariableOrReference(scope->function_);
  SerializeVariableOrReference(scope->new_target_);
  SerializeVariableOrReference(scope->arguments_);

  // TODO(binast): rare_data_ (needed for > ES5.1 feature support)
  if (scope->rare_data_ != nullptr) {
    printf("BinAstSerializeVisitor encountered unsupported rare data on DeclarationScope\n");
    encountered_unhandled_nodes_++;
  }
}

inline void BinAstSerializeVisitor::SerializeScope(Scope* scope) {
  ScopeType scope_type = scope->scope_type();
  SerializeUint8(scope_type);
  DCHECK(scope_type != FUNCTION_SCOPE);
  DCHECK(scope_type != SCRIPT_SCOPE);
  DCHECK(scope_type != MODULE_SCOPE);
  SerializeCommonScopeFields(scope);
}

inline void BinAstSerializeVisitor::SerializeAstNodeHeader(AstNode* node) {
  SerializeUint32(node->bit_field_);
  SerializeInt32(node->position_);
}

inline void BinAstSerializeVisitor::SerializeVariableProxy(VariableProxy* proxy) {
  SerializeInt32(proxy->position());
  SerializeUint32(proxy->bit_field_);
  if (proxy->is_resolved()) {
    SerializeVariableOrReference(proxy->var());
  } else {
    SerializeRawStringReference(proxy->raw_name());
  }
}

inline void BinAstSerializeVisitor::SerializeCaseClause(
    CaseClause* case_clause) {
  if (!case_clause->is_default()) {
    SerializeUint8(0);
    VisitNode(case_clause->label());
  } else {
    SerializeUint8(1);
  }
  SerializeInt32(case_clause->statements()->length());
  for (Statement* statement : *case_clause->statements()) {
    VisitNode(statement);
  }
}

inline void BinAstSerializeVisitor::VisitMaybeNode(AstNode* maybe_node) {
  if (maybe_node) {
    SerializeUint8(1);
    VisitNode(maybe_node);
  } else {
    SerializeUint8(0);
  }
}

inline void BinAstSerializeVisitor::ToDoBinAst(AstNode* node) {
  // TODO(binast): Delete this function when it's no longer needed.
  printf("BinAstSerializeVisitor encountered unhandled node type: %s\n", node->node_type_name());
  // UNREACHABLE();
  encountered_unhandled_nodes_++;
}

inline void BinAstSerializeVisitor::VisitFunctionLiteral(FunctionLiteral* function_literal) {
  size_t start = byte_data_.size();

  int original_unsupported_nodes = encountered_unhandled_nodes_;

  SerializeAstNodeHeader(function_literal);

  base::Optional<size_t> length_index;
  DCHECK(start <= UINT32_MAX);
  SerializeUint32(static_cast<uint32_t>(start));

  // make placeholder for length, save index so we can insert it later
  length_index.emplace(byte_data_.size());
  SerializeUint32(0);

  const AstConsString* name = function_literal->raw_name();
  SerializeConsString(name);
  SerializeDeclarationScope(function_literal->scope());

  SerializeInt32(function_literal->expected_property_count());
  SerializeInt32(function_literal->parameter_count());
  SerializeInt32(function_literal->function_length());
  SerializeInt32(function_literal->function_token_position());
  SerializeInt32(function_literal->suspend_count());
  SerializeInt32(function_literal->function_literal_id());

  SerializeInt32(function_literal->body()->length());
  for (Statement* statement : *function_literal->body()) {
    VisitNode(statement);
  }

  int unhandled_inner_nodes = encountered_unhandled_nodes_ - original_unsupported_nodes;
  if (unhandled_inner_nodes > 0) {
    skipped_functions_++;
  } 

  // Calculate length and insert at length_index
  auto length = byte_data_.size() - start;
  DCHECK(length <= UINT32_MAX);
  SerializeUint32(static_cast<uint32_t>(length), length_index.value());
}

inline void BinAstSerializeVisitor::VisitBlock(Block* block) {
  SerializeAstNodeHeader(block);
  if (block->scope()) {
    SerializeUint8(1);
    SerializeScope(block->scope());
  } else {
    SerializeUint8(0);
  }
  SerializeInt32(block->statements()->length());
  for (Statement* statement : *block->statements()) {
    VisitNode(statement);
  }
}

inline void BinAstSerializeVisitor::VisitIfStatement(IfStatement* if_statement) {
  SerializeAstNodeHeader(if_statement);
  VisitNode(if_statement->condition());
  VisitNode(if_statement->then_statement());
  VisitNode(if_statement->else_statement());
}

inline void BinAstSerializeVisitor::VisitExpressionStatement(ExpressionStatement* statement) {
  SerializeAstNodeHeader(statement);
  VisitNode(statement->expression());
}

inline void BinAstSerializeVisitor::VisitLiteral(Literal* literal) {
  SerializeAstNodeHeader(literal);
  switch(literal->type()) {
    case Literal::kSmi: {
      SerializeInt32(literal->smi_);
      break;
    }
    case Literal::kHeapNumber: {
      SerializeDouble(literal->number_);
      break;
    }
    case Literal::kBigInt: {
      SerializeCString(literal->bigint_.c_str());
      break;
    }
    case Literal::kString: {
      SerializeRawStringReference(literal->string_);
      break;
    }
    case Literal::kSymbol: {
      SerializeUint8(static_cast<uint8_t>(literal->symbol_));
      break;
    }
    case Literal::kBoolean: {
      SerializeUint8(literal->boolean_);
      break;
    }
    case Literal::kUndefined:
    case Literal::kNull:
    case Literal::kTheHole: {
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
}

inline void BinAstSerializeVisitor::VisitEmptyStatement(EmptyStatement* empty_statement) {
  SerializeAstNodeHeader(empty_statement);
}

inline void BinAstSerializeVisitor::VisitAssignment(Assignment* assignment) {
  SerializeAstNodeHeader(assignment);
  DCHECK(assignment->node_type() == AstNode::kAssignment);
  VisitNode(assignment->target());
  VisitNode(assignment->value());
}

inline void BinAstSerializeVisitor::VisitVariableProxyExpression(VariableProxyExpression* var_proxy_expr) {
  SerializeAstNodeHeader(var_proxy_expr);
  SerializeVariableProxy(var_proxy_expr->proxy_);
}

inline void BinAstSerializeVisitor::VisitForStatement(ForStatement* for_statement) {
  SerializeAstNodeHeader(for_statement);

  // TODO(binast): are we guaranteed that we'll have all of these nodes?
  VisitMaybeNode(for_statement->init());
  VisitMaybeNode(for_statement->cond());
  VisitMaybeNode(for_statement->next());

  VisitNode(for_statement->body());
}

inline void BinAstSerializeVisitor::VisitForInStatement(ForInStatement* for_in_statement) {
  SerializeAstNodeHeader(for_in_statement);
  VisitNode(for_in_statement->each());
  VisitNode(for_in_statement->subject());
  VisitNode(for_in_statement->body());
}

inline void BinAstSerializeVisitor::VisitWhileStatement(WhileStatement* while_statement) {
  SerializeAstNodeHeader(while_statement);
  VisitNode(while_statement->cond());
  VisitNode(while_statement->body());
}

inline void BinAstSerializeVisitor::VisitDoWhileStatement(DoWhileStatement* do_while_statement) {
  SerializeAstNodeHeader(do_while_statement);
  VisitNode(do_while_statement->cond());
  VisitNode(do_while_statement->body());
}

inline void BinAstSerializeVisitor::VisitCompareOperation(CompareOperation* compare) {
  SerializeAstNodeHeader(compare);
  VisitNode(compare->left());
  VisitNode(compare->right());
}

inline void BinAstSerializeVisitor::VisitCountOperation(CountOperation* operation) {
  SerializeAstNodeHeader(operation);
  VisitNode(operation->expression());
}

inline void BinAstSerializeVisitor::VisitCall(Call* call) {
  SerializeAstNodeHeader(call);
  VisitNode(call->expression());
  SerializeInt32(call->arguments()->length());
  for (Expression* arg : *call->arguments()) {
    VisitNode(arg);
  }
}

inline void BinAstSerializeVisitor::VisitCallNew(CallNew* call) {
  SerializeAstNodeHeader(call);
  VisitNode(call->expression());
  SerializeInt32(call->arguments()->length());
  for (Expression* arg : *call->arguments()) {
    VisitNode(arg);
  }
}

inline void BinAstSerializeVisitor::VisitProperty(Property* property) {
  SerializeAstNodeHeader(property);
  VisitNode(property->obj());
  VisitNode(property->key());
}

inline void BinAstSerializeVisitor::VisitReturnStatement(ReturnStatement* return_statement) {
  SerializeAstNodeHeader(return_statement);
  SerializeInt32(return_statement->end_position());
  VisitNode(return_statement->expression());
}

inline void BinAstSerializeVisitor::VisitUnaryOperation(UnaryOperation* unary_op) {
  SerializeAstNodeHeader(unary_op);
  VisitNode(unary_op->expression());
}

inline void BinAstSerializeVisitor::VisitBinaryOperation(BinaryOperation* binary_op) {
  SerializeAstNodeHeader(binary_op);
  VisitNode(binary_op->left());
  VisitNode(binary_op->right());
}

inline void BinAstSerializeVisitor::VisitNaryOperation(NaryOperation* nary_op) {
  SerializeAstNodeHeader(nary_op);
  VisitNode(nary_op->first());
  auto subsequent_length = static_cast<uint32_t>(nary_op->subsequent_length());

  SerializeUint32(subsequent_length);

  for (uint32_t i = 0; i < subsequent_length; i++) {
    VisitNode(nary_op->subsequent(i));
    SerializeInt32(nary_op->subsequent_op_position(i));
  }
}

inline void BinAstSerializeVisitor::VisitObjectLiteral(ObjectLiteral* object_literal) {
  SerializeAstNodeHeader(object_literal);

  SerializeInt32(object_literal->properties()->length());

  for (int i = 0; i < object_literal->properties()->length(); i++) {
    auto property = object_literal->properties()->at(i);

    VisitNode(property->key());
    VisitNode(property->value());
    SerializeUint8(property->kind());

    if (property->kind() == ObjectLiteral::Property::SPREAD) {
      printf(
          "BinAstSerializeVisitor encountered unhandled spread in object "
          "literal, skipping function\n");
      encountered_unhandled_nodes_++;
    }

    SerializeUint8(property->is_computed_name());
  }
}

inline void BinAstSerializeVisitor::VisitArrayLiteral(ArrayLiteral* array_literal) {
  SerializeAstNodeHeader(array_literal);

  if (array_literal->first_spread_index() != -1) {
    printf("BinAstSerializeVisitor encountered unhandled array spread, skipping function\n");
    encountered_unhandled_nodes_++;
  }

  SerializeInt32(array_literal->values()->length());
  SerializeInt32(array_literal->first_spread_index());

  for (int i = 0; i < array_literal->values()->length(); i++) {
    auto value = array_literal->values()->at(i);

    VisitNode(value);
  }
}

inline void BinAstSerializeVisitor::VisitCompoundAssignment(CompoundAssignment* compound_assignment) {
  SerializeAstNodeHeader(compound_assignment);
  VisitNode(compound_assignment->target());
  VisitNode(compound_assignment->value());
  VisitNode(compound_assignment->binary_operation());
}

inline void BinAstSerializeVisitor::VisitConditional(Conditional* conditional) {
  SerializeAstNodeHeader(conditional);
  VisitNode(conditional->condition());
  VisitNode(conditional->then_expression());
  VisitNode(conditional->else_expression());
}

inline void BinAstSerializeVisitor::VisitTryCatchStatement(
    TryCatchStatement* try_catch_statement) {
  SerializeAstNodeHeader(try_catch_statement);
  VisitNode(try_catch_statement->try_block());
  SerializeScope(try_catch_statement->scope());
  VisitNode(try_catch_statement->catch_block());
}

inline void BinAstSerializeVisitor::VisitThrow(Throw* throw_statement) {
  SerializeAstNodeHeader(throw_statement);
  VisitNode(throw_statement->exception());
}

inline void BinAstSerializeVisitor::VisitThisExpression(ThisExpression* this_expression) {
  SerializeAstNodeHeader(this_expression);
}

inline void BinAstSerializeVisitor::VisitRegExpLiteral(
    RegExpLiteral* reg_exp_literal) {
  SerializeAstNodeHeader(reg_exp_literal);
  SerializeRawStringReference(reg_exp_literal->raw_pattern());
  SerializeInt32(reg_exp_literal->flags());
}

inline void BinAstSerializeVisitor::VisitSwitchStatement(
    SwitchStatement* switch_statement) {
  SerializeAstNodeHeader(switch_statement);
  VisitNode(switch_statement->tag());
  SerializeInt32(switch_statement->cases()->length());
  for (int i = 0; i < switch_statement->cases()->length(); i++) {
    SerializeCaseClause(switch_statement->cases()->at(i));
  }
}

inline void BinAstSerializeVisitor::VisitUnhandledNodeType(AstNode* node) {
  SerializeAstNodeHeader(node);
  ToDoBinAst(node);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
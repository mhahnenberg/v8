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

// Serializes binAST format into a linear sequence of bytes.
class BinAstSerializeVisitor final : public BinAstVisitor {
 public:
  BinAstSerializeVisitor(AstValueFactory* ast_value_factory)
    : BinAstVisitor(),
      ast_value_factory_(ast_value_factory),
      active_function_literal_(nullptr),
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
  virtual void VisitBreakStatement(BreakStatement* break_statement) override;
  virtual void VisitContinueStatement(ContinueStatement* continue_statement) override;
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
  void SerializeNodeReference(const AstNode* node);
  void SerializeCString(const char* str);
  void SerializeRawStringHeader(const AstRawString* s);
  void SerializeRawStringContents(const AstRawString* s, uint32_t table_index, size_t table_start_offset);
  void SerializeConsString(const AstConsString* cons_string);
  void SerializeRawStringReference(const AstRawString* s);
  void SerializeGlobalRawStringReference(const AstRawString* s);
  void SerializeStringTable(const AstConsString* function_name);
  void SerializeProxyStringTable();
  void SerializeVariableTable();
  void SerializeVariable(Variable* variable);
  void SerializeVariableReference(Variable* variable);
  void SerializeScopeVariableMap(Scope* scope);
  void SerializeScopeUnresolvedList(Scope* scope);
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

  void RecordBreakableStatement(BreakableStatement* node);
  void PatchPendingNodeReferences(const AstNode* node, size_t offset);

  AstValueFactory* ast_value_factory_;
  std::unordered_map<const AstRawString*, uint32_t> string_indexes_;
  std::vector<uint8_t> byte_data_;
  std::unordered_map<Variable*, uint32_t> global_variable_indexes_;
  std::unordered_map<VariableProxy*, int> var_proxy_ids;
  std::unordered_map<const AstNode*, uint32_t> node_offsets_;
  std::unordered_map<const AstNode*, std::vector<uint32_t>> patchable_node_reference_offsets_;
  std::unordered_map<FunctionLiteral*, std::unordered_map<uint32_t, uint32_t>> used_string_indexes_by_function_;
  FunctionLiteral* active_function_literal_;
  int encountered_unhandled_nodes_;
  int skipped_functions_;
  int serialized_functions_;

  std::unique_ptr<uint8_t[]> compressed_byte_data_;
  size_t compressed_byte_data_length_;
};

// TODO(binast): Maybe templatize these to reduce duplication?
inline void BinAstSerializeVisitor::SerializeUint64(uint64_t value) {
  auto old_size = byte_data_.size();
  byte_data_.resize(byte_data_.size() + sizeof(uint64_t));
  uint8_t* store = &byte_data_[old_size];
  *reinterpret_cast<uint64_t*>(store) = value;
}

inline void BinAstSerializeVisitor::SerializeUint32(
    uint32_t value, const base::Optional<size_t> index = base::nullopt) {
  DCHECK(!index.has_value() ||
         (index.value() >= 0 && index <= byte_data_.size()));
  uint8_t* store;
  if (index.has_value()) {
    store = &byte_data_[index.value()];
  } else {
    auto old_size = byte_data_.size();
    byte_data_.resize(byte_data_.size() + sizeof(uint32_t));
    store = &byte_data_[old_size];
  }
  *reinterpret_cast<uint32_t*>(store) = value;
}

inline void BinAstSerializeVisitor::SerializeUint16(uint16_t value) {
  auto old_size = byte_data_.size();
  byte_data_.resize(byte_data_.size() + sizeof(uint16_t));
  uint8_t* store = &byte_data_[old_size];
  *reinterpret_cast<uint16_t*>(store) = value;
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
  auto old_size = byte_data_.size();
  byte_data_.resize(byte_data_.size() + sizeof(int32_t));
  uint8_t* store = &byte_data_[old_size];
  *reinterpret_cast<int32_t*>(store) = value;
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


// The size field of the table and the number of entries in the table (two uint32_t)
#define STRING_TABLE_HEADER_SIZE (sizeof(uint32_t) * 2)
#define RAW_STRING_HEADER_SIZE (sizeof(uint8_t) + 3 * sizeof(uint32_t))


inline void BinAstSerializeVisitor::SerializeRawStringHeader(const AstRawString* s) {
  DCHECK(s != nullptr);
  DCHECK(string_indexes_.count(s) == 0);
  DCHECK(s->byte_length() >= 0);
  uint32_t length = s->byte_length();
  bool is_one_byte = s->is_one_byte();
  uint32_t hash_field = s->hash_field();

  string_indexes_.insert({s, string_indexes_.size()});

  SerializeUint8(is_one_byte);
  SerializeUint32(hash_field);
  SerializeUint32(length);
  // We will fill this in later when we serialize the contents of the strings at the end of the string table.
  // Note that we can't skip this even if the length is 0 because all string headers must be a fixed size.
  SerializeUint32(0);
}

inline void BinAstSerializeVisitor::SerializeRawStringContents(const AstRawString* s, uint32_t table_index, size_t table_start_offset) {
  uint32_t length = s->byte_length();
  if (length == 0) {
    return;
  }

  size_t string_start_offset = byte_data_.size();
  const uint8_t* bytes = s->raw_data();
  for (uint32_t i = 0; i < length; ++i) {
    SerializeUint8(bytes[i]);
  }

  size_t content_pointer_offset = table_start_offset;
  content_pointer_offset += STRING_TABLE_HEADER_SIZE;
  content_pointer_offset += RAW_STRING_HEADER_SIZE * table_index;  // Index to the correct table entry.
  content_pointer_offset += sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t);  // Skip other fields in table entry.
  DCHECK(string_start_offset < UINT_MAX);
  DCHECK(content_pointer_offset < UINT_MAX);
  SerializeUint32(static_cast<uint32_t>(string_start_offset), static_cast<uint32_t>(content_pointer_offset));
}

inline void BinAstSerializeVisitor::SerializeRawStringReference(const AstRawString* s) {
  CHECK(s != nullptr);
  auto global_result = string_indexes_.find(s);
  if (global_result == string_indexes_.end()) {
    printf("ERROR: Failed to find raw string reference for '%.*s'\n", s->byte_length(), s->raw_data());
    DCHECK(false);
  }

  auto global_string_table_index = global_result->second;
  uint32_t local_string_table_index = 0;
  auto& local_map = used_string_indexes_by_function_[active_function_literal_];
  auto local_result = local_map.find(global_string_table_index);
  if (local_result == local_map.end()) {
    DCHECK(local_map.size() < UINT_MAX);
    local_string_table_index = static_cast<uint32_t>(local_map.size());
    local_map[global_string_table_index] = local_string_table_index;
  } else {
    local_string_table_index = local_result->second;
  }
  SerializeVarUint32(local_string_table_index);
}

inline void BinAstSerializeVisitor::SerializeGlobalRawStringReference(const AstRawString* s) {
  CHECK(s != nullptr);
  auto global_result = string_indexes_.find(s);
  if (global_result == string_indexes_.end()) {
    printf("ERROR: Failed to find raw string reference for '%.*s'\n", s->byte_length(), s->raw_data());
    DCHECK(false);
  }
  // We use a fixed size uint32_t reference here instead of a varuint32 because
  // global table entries need to be a constant size.
  auto global_string_table_index = global_result->second;
  SerializeUint32(global_string_table_index);
}

inline void BinAstSerializeVisitor::SerializeConsString(const AstConsString* cons_string) {
  if (cons_string == nullptr) {
    SerializeUint8(0);
    return;
  } else {
    SerializeUint8(1);
  }

  std::forward_list<const AstRawString*> strings = cons_string->ToRawStrings();
  uint32_t length = 0;
  for (const AstRawString* string : strings) {
    DCHECK(string != nullptr);
    DCHECK(string_indexes_.count(string) == 1);
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

  DCHECK(num_entries > 0);
  // Record the current offset to patch the total size of the string table later.
  size_t string_table_start_offset = byte_data_.size();
  SerializeUint32(0);
  SerializeUint32(num_entries);
  // Insert non-constant strings
  for (base::HashMap::Entry* entry = ast_value_factory_->string_table_.Start(); entry != nullptr; entry = ast_value_factory_->string_table_.Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    SerializeRawStringHeader(s);
  }

  if (function_name != nullptr) {
    for (const AstRawString* s : function_name->ToRawStrings()) {
      void* key = const_cast<AstRawString*>(s);
      if (ast_value_factory_->string_table_.Lookup(key, s->Hash()) == nullptr) {
        SerializeRawStringHeader(s);
      }
    }
  }

  DCHECK(string_indexes_.size() == num_entries);

  // Serialize the contents and patch the pointers in the headers.
  for (const auto& kv : string_indexes_) {
    const AstRawString* s = kv.first;
    uint32_t index = kv.second;
    SerializeRawStringContents(s, index, string_table_start_offset);
  }

  size_t string_table_end_offset = byte_data_.size();
  SerializeUint32(static_cast<uint32_t>(string_table_end_offset), static_cast<uint32_t>(string_table_start_offset));
}

inline void BinAstSerializeVisitor::SerializeVariableTable() {
  std::vector<std::pair<uint32_t, Variable*>> sorted_by_global_index;
  sorted_by_global_index.reserve(global_variable_indexes_.size());

  for (const auto& kv : global_variable_indexes_) {
    sorted_by_global_index.push_back({kv.second, kv.first});
  }

  std::sort(sorted_by_global_index.begin(), sorted_by_global_index.end());

  SerializeUint32(static_cast<uint32_t>(sorted_by_global_index.size()));
  for (const auto& kv : sorted_by_global_index) {
    Variable* var = kv.second;
    SerializeVariable(var);
  }
}

inline bool BinAstSerializeVisitor::SerializeAst(AstNode* root) {
  auto start = std::chrono::high_resolution_clock::now();
  FunctionLiteral* literal = root->AsFunctionLiteral();
  DCHECK(literal != nullptr);
  SerializeStringTable(literal->raw_name());

  DCHECK(byte_data_.size() < UINT_MAX);
  uint32_t variable_table_offset_index = static_cast<uint32_t>(byte_data_.size());
  SerializeUint32(0);

  VisitNode(root);

  SerializeUint32(static_cast<uint32_t>(byte_data_.size()), variable_table_offset_index);
  SerializeVariableTable();

  // Make sure we eventually patched everything.
  DCHECK(patchable_node_reference_offsets_.empty());

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

#define GLOBAL_VARIABLE_TABLE_HEADER_SIZE (sizeof(uint32_t))
#define GLOBAL_VARIABLE_TABLE_ENTRY_SIZE (sizeof(uint32_t) + 2 * sizeof(int32_t) + sizeof(uint16_t))

inline void BinAstSerializeVisitor::SerializeVariable(Variable* variable) {
  SerializeGlobalRawStringReference(variable->raw_name());

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  if (variable->has_local_if_not_shadowed()) {
    printf("BinAstSerializeVisitor encountered unsupported variable with local_if_not_shadowed field set\n");
    encountered_unhandled_nodes_++;
  }

  SerializeInt32(variable->index());
  SerializeInt32(variable->initializer_position());
  SerializeUint16(variable->bit_field_);
}

inline void BinAstSerializeVisitor::SerializeVariableReference(Variable* variable) {
  if (variable == nullptr) {
    SerializeVarUint32(0);
    return;
  }
  auto var_id_result = global_variable_indexes_.find(variable);
  uint32_t global_var_id = 0;
  if (var_id_result == global_variable_indexes_.end()) {
    // + 1 to reserve 0 for nullptr
    DCHECK(global_variable_indexes_.size() < UINT_MAX);
    global_var_id = static_cast<uint32_t>(global_variable_indexes_.size()) + 1;
    global_variable_indexes_[variable] = global_var_id;
  } else {
    global_var_id = var_id_result->second;
  }
  SerializeVarUint32(global_var_id);
}

inline void BinAstSerializeVisitor::SerializeScopeVariableMap(Scope* scope) {
  // Serialize locals first.
  uint32_t total_local_vars = 0;
  uint32_t total_temporaries = 0;
  std::unordered_set<const AstRawString*> deduped_locals;
  std::vector<const AstRawString*> temporaries;
  for (Variable* variable : scope->locals_) {
    total_local_vars++;
    deduped_locals.insert(variable->raw_name());

    if (variable->mode() == VariableMode::kTemporary) {
      total_temporaries++;
    }
  }

  SerializeUint32(total_local_vars);
  for (Variable* variable : scope->locals_) {
    SerializeVariableReference(variable);
  }

  // Now serialize any remaining variables we missed
  DCHECK(temporaries.size() < UINT32_MAX);
  uint32_t total_non_temporary_locals = total_local_vars - total_temporaries;
  DCHECK(static_cast<uint32_t>(scope->num_var()) >= total_non_temporary_locals);
  // Temporaries are only stored in locals_ (and not variables_), so don't include them in the non-local vars count.
  uint32_t total_nonlocal_vars = scope->num_var() - total_non_temporary_locals;
  uint32_t serialized_nonlocal_vars = 0;
  SerializeUint32(total_nonlocal_vars);
  for (VariableMap::Entry* entry = scope->variables_.Start(); entry != nullptr; entry = scope->variables_.Next(entry)) {
    Variable* variable = reinterpret_cast<Variable*>(entry->value);
    if (deduped_locals.count(variable->raw_name()) == 0) {
      SerializeVariableReference(variable);
      serialized_nonlocal_vars += 1;
    }
  }
  DCHECK(total_nonlocal_vars == serialized_nonlocal_vars);
}

inline void BinAstSerializeVisitor::SerializeScopeUnresolvedList(Scope* scope) {
  auto original_offset = byte_data_.size();
  SerializeUint32(0); // for patching the number of entries later

  uint32_t num_unresolved = 0;
  for (VariableProxy* proxy = scope->unresolved_list_.first(); proxy != nullptr; proxy = proxy->next_unresolved()) {
    SerializeVariableProxy(proxy);
    num_unresolved++;
  }

  SerializeUint32(num_unresolved, original_offset);
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
    default: {
      UNREACHABLE();
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

inline void BinAstSerializeVisitor::SerializeCommonScopeFields(Scope* scope) {
  SerializeScopeVariableMap(scope);
  SerializeScopeUnresolvedList(scope);
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
  SerializeScopeDeclarations(scope);
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

  SerializeVariableReference(scope->receiver_);
  SerializeVariableReference(scope->function_);
  SerializeVariableReference(scope->new_target_);
  SerializeVariableReference(scope->arguments_);

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

inline void BinAstSerializeVisitor::PatchPendingNodeReferences(const AstNode* node, size_t node_offset) {
  if (patchable_node_reference_offsets_.count(node) == 0) {
    return;
  }
  CHECK(node_offset <= UINT32_MAX);
  uint32_t bounded_node_offset = static_cast<uint32_t>(node_offset);
  std::vector<uint32_t>& offsets_to_patch = patchable_node_reference_offsets_[node];
  for (uint32_t patch_offset : offsets_to_patch) {
    SerializeUint32(bounded_node_offset, patch_offset);
  }
  patchable_node_reference_offsets_.erase(node);
}

inline void BinAstSerializeVisitor::SerializeNodeReference(const AstNode* node) {
  auto result = node_offsets_.find(node);
  if (result != node_offsets_.end()) {
    SerializeUint32(result->second);
    return;
  }

  // We don't have an offset for this node yet, so record the offset to be patched
  // when we eventually process it. Then serialize a placeholder value.
  size_t current_offset = byte_data_.size();
  CHECK(current_offset <= UINT32_MAX);
  SerializeUint32(0);
  patchable_node_reference_offsets_[node].push_back(static_cast<uint32_t>(current_offset));
}

inline void BinAstSerializeVisitor::SerializeAstNodeHeader(AstNode* node) {
  SerializeUint32(node->bit_field_);
  SerializeInt32(node->position_);
}

inline void BinAstSerializeVisitor::SerializeVariableProxy(VariableProxy* proxy) {
  SerializeInt32(proxy->position());
  SerializeUint32(proxy->bit_field_);
  if (proxy->is_resolved()) {
    SerializeVariableReference(proxy->var());
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

inline void BinAstSerializeVisitor::RecordBreakableStatement(BreakableStatement* node) {
  size_t current_offset = byte_data_.size();
  DCHECK(current_offset <= UINT32_MAX);
  node_offsets_.insert({{node, static_cast<uint32_t>(current_offset)}});
  PatchPendingNodeReferences(node, current_offset);
}

class ActiveFunctionLiteralScope {
 public:
  ActiveFunctionLiteralScope(FunctionLiteral** active_literal, FunctionLiteral* next_literal)
    : active_literal_(active_literal),
      previous_literal_(*active_literal_) {
    *active_literal_ = next_literal;
  }

  ~ActiveFunctionLiteralScope() {
    *active_literal_ = previous_literal_;
  }

 private:
  FunctionLiteral** active_literal_;
  FunctionLiteral* previous_literal_;
};

inline void BinAstSerializeVisitor::VisitFunctionLiteral(FunctionLiteral* function_literal) {
  ActiveFunctionLiteralScope function_literal_scope(&active_function_literal_, function_literal);
  size_t start = byte_data_.size();

  int original_unsupported_nodes = encountered_unhandled_nodes_;

  SerializeAstNodeHeader(function_literal);

  base::Optional<size_t> length_index;
  DCHECK(start <= UINT32_MAX);
  SerializeUint32(static_cast<uint32_t>(start));

  // make placeholder for length, save index so we can insert it later
  length_index.emplace(byte_data_.size());
  SerializeUint32(0);

  // This tells us how much to offset to get to the proxy string table since we
  // can't insert it here because it would mess up the indexes inside the serialized
  // inner functions.
  size_t proxy_string_table_offset_index = byte_data_.size();
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

  SerializeUint32(static_cast<uint32_t>(byte_data_.size()), static_cast<uint32_t>(proxy_string_table_offset_index));
  SerializeProxyStringTable();

  int unhandled_inner_nodes = encountered_unhandled_nodes_ - original_unsupported_nodes;
  if (unhandled_inner_nodes > 0) {
    skipped_functions_++;
  }

  // Calculate length and insert at length_index
  auto length = byte_data_.size() - start;
  DCHECK(length <= UINT32_MAX);
  SerializeUint32(static_cast<uint32_t>(length), length_index.value());
}

inline void BinAstSerializeVisitor::SerializeProxyStringTable() {
  std::unordered_map<uint32_t, uint32_t>& local_string_indexes = used_string_indexes_by_function_[active_function_literal_];
  std::vector<std::pair<uint32_t, uint32_t>> sorted_by_local_index;
  sorted_by_local_index.reserve(local_string_indexes.size());

  for (const auto& kv : local_string_indexes) {
    sorted_by_local_index.push_back({kv.second, kv.first});
  }

  std::sort(sorted_by_local_index.begin(), sorted_by_local_index.end());

  SerializeUint32(static_cast<uint32_t>(local_string_indexes.size()));
  for (const auto& kv : sorted_by_local_index) {
    auto global_string_index = kv.second;
    SerializeUint32(global_string_index);
  }
}

inline void BinAstSerializeVisitor::VisitBlock(Block* block) {
  RecordBreakableStatement(block);
  
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
  RecordBreakableStatement(for_statement);
  SerializeAstNodeHeader(for_statement);

  // TODO(binast): are we guaranteed that we'll have all of these nodes?
  VisitMaybeNode(for_statement->init());
  VisitMaybeNode(for_statement->cond());
  VisitMaybeNode(for_statement->next());

  VisitNode(for_statement->body());
}

inline void BinAstSerializeVisitor::VisitForInStatement(ForInStatement* for_in_statement) {
  RecordBreakableStatement(for_in_statement);
  SerializeAstNodeHeader(for_in_statement);
  VisitNode(for_in_statement->each());
  VisitNode(for_in_statement->subject());
  VisitNode(for_in_statement->body());
}

inline void BinAstSerializeVisitor::VisitWhileStatement(WhileStatement* while_statement) {
  RecordBreakableStatement(while_statement);
  SerializeAstNodeHeader(while_statement);
  VisitNode(while_statement->cond());
  VisitNode(while_statement->body());
}

inline void BinAstSerializeVisitor::VisitDoWhileStatement(DoWhileStatement* do_while_statement) {
  RecordBreakableStatement(do_while_statement);
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
  SerializeInt32(object_literal->properties_count()); // i.e. boilerplate_properties

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
  if (try_catch_statement->scope()) {
    SerializeUint8(1);
    SerializeScope(try_catch_statement->scope());
  } else {
    SerializeUint8(0);
  }
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
  RecordBreakableStatement(switch_statement);
  SerializeAstNodeHeader(switch_statement);
  VisitNode(switch_statement->tag());
  SerializeInt32(switch_statement->cases()->length());
  for (int i = 0; i < switch_statement->cases()->length(); i++) {
    SerializeCaseClause(switch_statement->cases()->at(i));
  }
}

inline void BinAstSerializeVisitor::VisitBreakStatement(BreakStatement* break_statement) {
  SerializeAstNodeHeader(break_statement);
  SerializeNodeReference(break_statement->target());
}

inline void BinAstSerializeVisitor::VisitContinueStatement(ContinueStatement* continue_statement) {
  SerializeAstNodeHeader(continue_statement);
  SerializeNodeReference(continue_statement->target());
}

inline void BinAstSerializeVisitor::VisitUnhandledNodeType(AstNode* node) {
  SerializeAstNodeHeader(node);
  ToDoBinAst(node);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
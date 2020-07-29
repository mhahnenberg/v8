// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
#define V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

#include "src/parsing/binast-visitor.h"
#include <mutex>
#include "src/ast/scopes.h"

namespace v8 {
namespace internal {

// TODO(binast)
// Serializes binAST format into a linear sequence of bytes.
class BinAstSerializeVisitor final : public BinAstVisitor {
 public:
  BinAstSerializeVisitor(BinAstValueFactory* ast_value_factory)
    : BinAstVisitor(),
      ast_value_factory_(ast_value_factory) {
  }
  std::vector<uint8_t>& serialized_bytes() { return byte_data_; }

  void SerializeAst(BinAstNode* root);

  virtual void VisitFunctionLiteral(BinAstFunctionLiteral* function_literal) override;
  virtual void VisitBlock(BinAstBlock* block) override;
  virtual void VisitIfStatement(BinAstIfStatement* if_statement) override;
  virtual void VisitExpressionStatement(BinAstExpressionStatement* statement) override;
  virtual void VisitLiteral(BinAstLiteral* literal) override;
  virtual void VisitEmptyStatement(BinAstEmptyStatement* empty_statement) override;
  virtual void VisitAssignment(BinAstAssignment* assignment) override;
  virtual void VisitVariableProxyExpression(BinAstVariableProxyExpression* var_proxy) override;
  virtual void VisitForStatement(BinAstForStatement* for_statement) override;
  virtual void VisitCompareOperation(BinAstCompareOperation* compare) override;
  virtual void VisitCountOperation(BinAstCountOperation* operation) override;
  virtual void VisitCall(BinAstCall* call) override;
  virtual void VisitProperty(BinAstProperty* property) override;
  virtual void VisitReturnStatement(BinAstReturnStatement* return_statement) override;
  virtual void VisitBinaryOperation(BinAstBinaryOperation* binary_op) override;
  virtual void VisitObjectLiteral(BinAstObjectLiteral* binary_op) override;

 private:
  void SerializeUint32(uint32_t value);
  void SerializeUint16(uint16_t value);
  void SerializeInt32(int32_t value);
  void SerializeUint8(uint8_t value);
  void SerializeRawString(const AstRawString* s);
  void SerializeConsString(const AstConsString* cons_string);
  void SerializeRawStringReference(const AstRawString* s);
  void SerializeStringTable(const AstConsString* function_name);
  void SerializeVariable(Variable* variable);
  void SerializeScopeVariableMap(Scope* scope);
  void SerializeDeclarationScope(DeclarationScope* scope);

  BinAstValueFactory* ast_value_factory_;
  std::unordered_map<const AstRawString*, uint32_t> string_table_indices_;
  std::vector<uint8_t> byte_data_;
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

void BinAstSerializeVisitor::SerializeRawString(const AstRawString* s) {
  DCHECK(s != nullptr);
  DCHECK(string_table_indices_.count(s) == 0);
  uint32_t length = s->byte_length();
  bool is_one_byte = s->is_one_byte();
  uint32_t hash_field = s->raw_hash_field();

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

void BinAstSerializeVisitor::SerializeRawStringReference(const AstRawString* s) {
  auto lookup_result = string_table_indices_.find(s);
  DCHECK(lookup_result != string_table_indices_.end());
  uint32_t string_table_index = lookup_result->second;
  SerializeUint32(string_table_index);
}

void BinAstSerializeVisitor::SerializeConsString(const AstConsString* cons_string) {
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

void BinAstSerializeVisitor::SerializeStringTable(const AstConsString* function_name) {
  uint32_t num_entries = ast_value_factory_->string_table_.occupancy();
  // We serialize the outer function raw_name too.
  // TODO(binast): Do we need to?
  if (function_name != nullptr) {
    for (const AstRawString* s : function_name->ToRawStrings()) {
      if (ast_value_factory_->string_table_.Lookup(s, s->Hash()) == nullptr) {
        num_entries += 1;
      }
    }
  }
  SerializeUint32(num_entries);
  uint32_t current_index = 1;
  for (AstRawStringMap::Entry* entry = ast_value_factory_->string_table_.Start(); entry != nullptr; entry = ast_value_factory_->string_table_.Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    SerializeRawString(s);
    string_table_indices_.insert({s, current_index});
    current_index += 1;
  }

  if (function_name != nullptr) {
    for (const AstRawString* s : function_name->ToRawStrings()) {
      if (ast_value_factory_->string_table_.Lookup(s, s->Hash()) == nullptr) {
        SerializeRawString(s);
        string_table_indices_.insert({s, current_index});
        current_index += 1;
      }
    }
  }

  DCHECK(current_index == num_entries + 1);
}

void BinAstSerializeVisitor::SerializeAst(BinAstNode* root) {
  auto start = std::chrono::high_resolution_clock::now();
  BinAstFunctionLiteral* literal = root->AsFunctionLiteral();
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

void BinAstSerializeVisitor::SerializeVariable(Variable* variable) {
  SerializeRawStringReference(variable->raw_name());

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?

  SerializeInt32(variable->index());
  SerializeInt32(variable->initializer_position());
  SerializeUint16(variable->bit_field_);
}

void BinAstSerializeVisitor::SerializeScopeVariableMap(Scope* scope) {
  // Serialize locals first.
  std::unordered_set<const AstRawString*> locals;
  for (Variable* variable : scope->locals_) {
    locals.insert(variable->raw_name());
  }

  DCHECK(locals.size() < UINT32_MAX);
  uint32_t total_local_vars = static_cast<uint32_t>(locals.size());
  SerializeUint32(total_local_vars);
  for (Variable* variable : scope->locals_) {
    SerializeVariable(variable);
  }

  // Now serialize any remaining variables we missed
  uint32_t total_nonlocal_vars = scope->num_var() - total_local_vars;
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

void BinAstSerializeVisitor::SerializeDeclarationScope(DeclarationScope* scope) {
  ScopeType scope_type = scope->scope_type();
  SerializeUint8(scope_type);
  SerializeUint8(scope->function_kind());
  SerializeScopeVariableMap(scope);
}

void BinAstSerializeVisitor::VisitFunctionLiteral(BinAstFunctionLiteral* function_literal) {
  SerializeUint32(function_literal->bit_field_);
  SerializeInt32(function_literal->position_);
  const AstConsString* name = function_literal->raw_name();
  SerializeConsString(name);
  SerializeDeclarationScope(function_literal->scope());
}

void BinAstSerializeVisitor::VisitBlock(BinAstBlock* block) {

}

void BinAstSerializeVisitor::VisitIfStatement(BinAstIfStatement* if_statement) {

}

void BinAstSerializeVisitor::VisitExpressionStatement(BinAstExpressionStatement* statement) {

}

void BinAstSerializeVisitor::VisitLiteral(BinAstLiteral* literal) {

}

void BinAstSerializeVisitor::VisitEmptyStatement(BinAstEmptyStatement* empty_statement) {

}

void BinAstSerializeVisitor::VisitAssignment(BinAstAssignment* assignment) {

}

void BinAstSerializeVisitor::VisitVariableProxyExpression(BinAstVariableProxyExpression* var_proxy) {

}

void BinAstSerializeVisitor::VisitForStatement(BinAstForStatement* for_statement) {

}

void BinAstSerializeVisitor::VisitCompareOperation(BinAstCompareOperation* compare) {

}

void BinAstSerializeVisitor::VisitCountOperation(BinAstCountOperation* operation) {

}

void BinAstSerializeVisitor::VisitCall(BinAstCall* call) {

}

void BinAstSerializeVisitor::VisitProperty(BinAstProperty* property) {

}

void BinAstSerializeVisitor::VisitReturnStatement(BinAstReturnStatement* return_statement) {

}

void BinAstSerializeVisitor::VisitBinaryOperation(BinAstBinaryOperation* binary_op) {

}

void BinAstSerializeVisitor::VisitObjectLiteral(BinAstObjectLiteral* object_literal) {
  
}


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_SERIALIZE_VISITOR_H_
#define V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

#include "src/parsing/binast-visitor.h"

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

 private:
  void SerializeUint32(uint32_t value);
  void SerializeInt32(int32_t value);
  void SerializeUint8(uint8_t value);
  void SerializeString(const AstRawString* s);
  void SerializeStringTable();

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

inline void BinAstSerializeVisitor::SerializeUint8(uint8_t value) {
  byte_data_.push_back(value);
}

void BinAstSerializeVisitor::SerializeString(const AstRawString* s) {
  DCHECK(s != nullptr);
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

void BinAstSerializeVisitor::SerializeStringTable() {
  uint32_t num_entries = ast_value_factory_->string_table_.occupancy();
  SerializeUint32(num_entries);
  uint32_t current_index = 0;
  for (AstRawStringMap::Entry* entry = ast_value_factory_->string_table_.Start(); entry != nullptr; entry = ast_value_factory_->string_table_.Next(entry)) {
    const AstRawString* s = reinterpret_cast<const AstRawString*>(entry->key);
    string_table_indices_.insert({s, current_index});
    SerializeString(s);
    current_index += 1;
  }
  DCHECK(current_index == num_entries);
}

void BinAstSerializeVisitor::SerializeAst(BinAstNode* root) {
  SerializeStringTable();
  VisitNode(root);
}

void BinAstSerializeVisitor::VisitFunctionLiteral(BinAstFunctionLiteral* function_literal) {
  SerializeUint32(function_literal->bit_field_);
  SerializeInt32(function_literal->position_);
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


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_SERIALIZE_VISITOR_H_

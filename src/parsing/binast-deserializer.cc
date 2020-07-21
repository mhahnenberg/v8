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

uint32_t BinAstDeserializer::DeserializeUint32(ByteArray bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes.get(offset + i);
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return result;
}

int32_t BinAstDeserializer::DeserializeInt32(ByteArray bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes.get(offset + i);
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return result;
}

uint8_t BinAstDeserializer::DeserializeUint8(ByteArray bytes, int offset) {
  return bytes.get(offset);
}

int BinAstDeserializer::DeserializeString(ByteArray serialized_ast, int offset) {
  bool is_one_byte = DeserializeUint8(serialized_ast, offset);
  uint32_t hash_field = DeserializeUint32(serialized_ast, offset + 1);
  uint32_t length = DeserializeUint32(serialized_ast, offset + 1 + 4);
  std::vector<uint8_t> raw_data;
  for (uint32_t i = 0; i < length; ++i) {
    int index = offset + 1 + 4 + 4 + i;
    raw_data.push_back(DeserializeUint8(serialized_ast, index));
  }
  const AstRawString* s = nullptr;
  if (raw_data.size() > 0) {
    Vector<const byte> literal_bytes(&raw_data[0], raw_data.size());
    s = parser_->ast_value_factory()->GetString(hash_field, is_one_byte, literal_bytes);
  } else {
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field, is_one_byte, literal_bytes);
  }
  string_table_.insert({string_table_.size(), s});
  return offset + 1 + 4 + 4 + length;
}

int BinAstDeserializer::DeserializeStringTable(ByteArray serialized_ast, int offset) {
  uint32_t num_entries = DeserializeUint32(serialized_ast, offset);
  offset += 4;
  for (uint32_t i = 0; i < num_entries; ++i) {
    offset = DeserializeString(serialized_ast, offset);
  }
  return offset;
}

AstNode* BinAstDeserializer::DeserializeAst(ByteArray serialized_ast) {
  int offset = 0;
  offset = DeserializeStringTable(serialized_ast, offset);
  return DeserializeAstNode(serialized_ast, offset);
}

AstNode* BinAstDeserializer::DeserializeAstNode(ByteArray serialized_binast, int offset) {
  uint32_t bit_field = DeserializeUint32(serialized_binast, offset);
  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field);
  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    return DeserializeFunctionLiteral(serialized_binast, bit_field, offset);
  }
  default: {
    UNREACHABLE();
  }
  }
}

FunctionLiteral* BinAstDeserializer::DeserializeFunctionLiteral(ByteArray serialized_binast, uint32_t bit_field, int offset) {
  int32_t position = DeserializeInt32(serialized_binast, offset + sizeof(bit_field));
  std::vector<void*> pointer_buffer;
  /* TODO */const AstRawString* name = nullptr;
  /* TODO */DeclarationScope* scope = nullptr;
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
    name, scope, body, expected_property_count, parameter_count, 
    function_length, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id);
  return result;
}


}  // namespace internal
}  // namespace v8

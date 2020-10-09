// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_DESERIALIZER_H_
#define V8_PARSING_BINAST_DESERIALIZER_H_

#include <unordered_map>
#include "src/handles/handles.h"
#include "src/ast/ast.h"

namespace v8 {
namespace internal {

class ByteArray;
class Declaration;
class ParseInfo;
class Property;
class AstConsString;
class AstRawString;
class Parser;
class VariableProxy;
class VariableProxyExpression;

class BinAstDeserializer {
 public:
  BinAstDeserializer(Isolate* isolate, Parser* parser, Handle<ByteArray> parse_data);

  AstNode* DeserializeAst(base::Optional<uint32_t> start_offset = base::nullopt,
                          base::Optional<uint32_t> length = base::nullopt);

 private:
  template <typename T>
  struct DeserializeResult {
    T value;
    int new_offset;
  };

  Zone* zone();
  static bool UseCompression() { return false; }

  DeserializeResult<AstNode*> DeserializeMaybeAstNode(uint8_t* serialized_binast, int offset);

  DeserializeResult<uint64_t> DeserializeUint64(uint8_t* bytes, int offset);
  DeserializeResult<uint32_t> DeserializeUint32(uint8_t* bytes, int offset);
  DeserializeResult<uint32_t> DeserializeVarUint32(uint8_t* bytes, int offset);

  DeserializeResult<uint16_t> DeserializeUint16(uint8_t* bytes, int offset);
  DeserializeResult<uint8_t> DeserializeUint8(uint8_t* bytes, int offset);
  DeserializeResult<int32_t> DeserializeInt32(uint8_t* bytes, int offset);
  DeserializeResult<std::array<bool, 16>> DeserializeUint16Flags(uint8_t* bytes, int offset);
  DeserializeResult<double> DeserializeDouble(uint8_t* bytes, int offset);

  DeserializeResult<const char*> DeserializeCString(uint8_t* bytes, int offset);
  DeserializeResult<const AstRawString*> DeserializeRawString(uint8_t* bytes, int offset);
  DeserializeResult<std::nullptr_t> DeserializeStringTable(uint8_t* bytes, int offset);
  DeserializeResult<const AstRawString*> DeserializeRawStringReference(uint8_t* bytes, int offset);
  DeserializeResult<AstConsString*> DeserializeConsString(uint8_t* bytes, int offset);

  DeserializeResult<Variable*> DeserializeLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeVariableReference(uint8_t* serialized_binast, int offset, Scope* scope = nullptr);
  DeserializeResult<Variable*> DeserializeScopeVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonScopeVariable(uint8_t* serialized_binast, int offset);
  DeserializeResult<Variable*> DeserializeScopeVariableOrReference(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonScopeVariableOrReference(uint8_t* serialized_binast, int offset);
  DeserializeResult<std::nullptr_t> DeserializeScopeVariableMap(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Declaration*> DeserializeDeclaration(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeDeclarations(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeParameters(uint8_t* serialized_binast, int offset, DeclarationScope* scope);
  DeserializeResult<std::nullptr_t> DeserializeCommonScopeFields(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Scope*> DeserializeScope(uint8_t* serialized_binast, int offset);
  DeserializeResult<DeclarationScope*> DeserializeDeclarationScope(uint8_t* serialized_binast, int offset);

  DeserializeResult<AstNode*> DeserializeAstNode(uint8_t* serialized_ast, int offset, bool is_toplevel = false);
  DeserializeResult<FunctionLiteral*> DeserializeFunctionLiteral(uint8_t* serialized_ast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ReturnStatement*> DeserializeReturnStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<BinaryOperation*> DeserializeBinaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Property*> DeserializeProperty(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ExpressionStatement*> DeserializeExpressionStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<VariableProxy*> DeserializeVariableProxy(uint8_t* serialized_binast, int offset);
  DeserializeResult<VariableProxyExpression*> DeserializeVariableProxyExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Literal*> DeserializeLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Call*> DeserializeCall(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CallNew*> DeserializeCallNew(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<IfStatement*> DeserializeIfStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Block*> DeserializeBlock(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Assignment*> DeserializeAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CompareOperation*> DeserializeCompareOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<EmptyStatement*> DeserializeEmptyStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ForStatement*> DeserializeForStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ForInStatement*> DeserializeForInStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<WhileStatement*> DeserializeWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<DoWhileStatement*> DeserializeDoWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CountOperation*> DeserializeCountOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CompoundAssignment*> DeserializeCompoundAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<UnaryOperation*> DeserializeUnaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ThisExpression*> DeserializeThisExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<std::nullptr_t> DeserializeNodeStub(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);

  Isolate* isolate_;
  Parser* parser_;
  Handle<ByteArray> parse_data_;
  std::unordered_map<uint32_t, const AstRawString*> string_table_;
  std::unordered_map<uint32_t, Variable*> variables_by_id_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_H_

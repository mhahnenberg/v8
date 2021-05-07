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


struct RawVariableData {
  uint32_t local_index;
  const AstRawString* name;
  int32_t index;
  int32_t initializer_position;
  uint16_t bit_field;
};

class BinAstDeserializer {
 public:
  BinAstDeserializer(Isolate* isolate, Parser* parser, Handle<ByteArray> parse_data, MaybeHandle<PreparseData> preparse_data);

  AstNode* DeserializeAst(base::Optional<uint32_t> start_offset = base::nullopt,
                          base::Optional<uint32_t> length = base::nullopt);

 private:
  template <typename T>
  struct DeserializeResult {
    T value;
    int new_offset;
  };

  Zone* zone();
  std::vector<void*>* pointer_buffer() { return &pointer_buffer_; }
  static bool UseCompression() { return false; }

  AstNode* DeserializeCompressedAst(base::Optional<uint32_t> start_offset,
                        base::Optional<uint32_t> length);
  AstNode* DeserializeUncompressedAst(base::Optional<uint32_t> start_offset,
                                      base::Optional<uint32_t> length,
                                      uint8_t* uncompressed_ast,
                                      size_t uncompressed_size);

  DeserializeResult<AstNode*> DeserializeMaybeAstNode(int offset);

  DeserializeResult<uint64_t> DeserializeUint64(int offset);
  DeserializeResult<uint32_t> DeserializeUint32(int offset);
  DeserializeResult<uint32_t> DeserializeVarUint32(int offset);

  DeserializeResult<uint16_t> DeserializeUint16(int offset);
  DeserializeResult<uint8_t> DeserializeUint8(int offset);
  DeserializeResult<int32_t> DeserializeInt32(int offset);
  DeserializeResult<std::array<bool, 16>> DeserializeUint16Flags(int offset);
  DeserializeResult<double> DeserializeDouble(int offset);

  DeserializeResult<AstNode*> DeserializeNodeReference(int offset, void** patchable_field);
  void RecordBreakableStatement(uint32_t offset, BreakableStatement* node);
  void PatchPendingNodeReferences(uint32_t offset, AstNode* node);

  DeserializeResult<const char*> DeserializeCString(int offset);
  DeserializeResult<const AstRawString*> DeserializeRawString(int offset);
  DeserializeResult<const AstRawString*> DeserializeProxyString(int offset);
  DeserializeResult<std::nullptr_t> DeserializeStringTable(int offset);
  DeserializeResult<std::nullptr_t> DeserializeProxyStringTable(int offset);
  DeserializeResult<const AstRawString*> DeserializeRawStringReference(int offset, bool fixed_size = false);
  DeserializeResult<const AstRawString*> DeserializeGlobalRawStringReference(int offset);
  DeserializeResult<AstConsString*> DeserializeConsString(int offset);

  Variable* CreateLocalTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);
  Variable* CreateLocalNonTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);

  void HandleFunctionSkipping(Scope* scope);

  DeserializeResult<std::nullptr_t> DeserializeProxyVariableTable(int offset);
  DeserializeResult<RawVariableData> DeserializeGlobalVariableReference(int offset);
  DeserializeResult<RawVariableData> DeserializeProxyVariableReference(int offset);
  DeserializeResult<Variable*> DeserializeLocalVariable(int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonLocalVariable(int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeVariableReference(int offset, Scope* scope = nullptr);
  DeserializeResult<Variable*> DeserializeScopeVariable(int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonScopeVariable(int offset);
  DeserializeResult<Variable*> DeserializeNonScopeVariableOrReference(int offset);
  DeserializeResult<std::nullptr_t> DeserializeScopeVariableMap(int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeUnresolvedList(int offset, Scope* scope);
  DeserializeResult<Declaration*> DeserializeDeclaration(int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeDeclarations(int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeParameters(int offset, DeclarationScope* scope);
  DeserializeResult<std::nullptr_t> DeserializeCommonScopeFields(int offset, Scope* scope);
  DeserializeResult<Scope*> DeserializeScope(int offset);
  DeserializeResult<DeclarationScope*> DeserializeDeclarationScope(int offset);

  DeserializeResult<AstNode*> DeserializeAstNode(int offset, bool is_toplevel = false);
  DeserializeResult<FunctionLiteral*> DeserializeFunctionLiteral(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ReturnStatement*> DeserializeReturnStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<BinaryOperation*> DeserializeBinaryOperation(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Property*> DeserializeProperty(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ExpressionStatement*> DeserializeExpressionStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<VariableProxy*> DeserializeVariableProxy(int offset, bool add_unresolved = true);
  DeserializeResult<VariableProxyExpression*> DeserializeVariableProxyExpression(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Literal*> DeserializeLiteral(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Call*> DeserializeCall(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CallNew*> DeserializeCallNew(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<IfStatement*> DeserializeIfStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Block*> DeserializeBlock(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Assignment*> DeserializeAssignment(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CompareOperation*> DeserializeCompareOperation(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<EmptyStatement*> DeserializeEmptyStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ForStatement*> DeserializeForStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ForInStatement*> DeserializeForInStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<WhileStatement*> DeserializeWhileStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<DoWhileStatement*> DeserializeDoWhileStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CountOperation*> DeserializeCountOperation(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<CompoundAssignment*> DeserializeCompoundAssignment(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<UnaryOperation*> DeserializeUnaryOperation(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ThisExpression*> DeserializeThisExpression(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ObjectLiteral*> DeserializeObjectLiteral(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ArrayLiteral*> DeserializeArrayLiteral(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<NaryOperation*> DeserializeNaryOperation(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Conditional*> DeserializeConditional(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<TryCatchStatement*> DeserializeTryCatchStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<RegExpLiteral*> DeserializeRegExpLiteral(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<SwitchStatement*> DeserializeSwitchStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<BreakStatement*> DeserializeBreakStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ContinueStatement*> DeserializeContinueStatement(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Throw*> DeserializeThrow(uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<std::nullptr_t> DeserializeNodeStub(uint32_t bit_field, int32_t position, int offset);

  Isolate* isolate_;
  Parser* parser_;
  Handle<ByteArray> parse_data_;
  MaybeHandle<PreparseData> preparse_data_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
  uint8_t* serialized_ast_;

  std::vector<void*> pointer_buffer_;
  std::vector<const AstRawString*> strings_;
  std::vector<Variable*> variables_;
  std::unordered_map<uint32_t, AstNode*> nodes_by_offset_;
  std::unordered_map<uint32_t, std::vector<void**>> patchable_fields_by_offset_;
  std::unordered_map<uint32_t, ProducedPreparseData*> produced_preparse_data_by_start_position_;
  uint32_t string_table_base_offset_;
  uint32_t proxy_variable_table_base_offset_;
  uint32_t global_variable_table_base_offset_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_H_

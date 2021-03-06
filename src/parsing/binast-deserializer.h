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

  DeserializeResult<AstNode*> DeserializeMaybeAstNode(uint8_t* serialized_binast, int offset);

  DeserializeResult<uint64_t> DeserializeUint64(uint8_t* bytes, int offset);
  DeserializeResult<uint32_t> DeserializeUint32(uint8_t* bytes, int offset);
  DeserializeResult<uint32_t> DeserializeVarUint32(uint8_t* bytes, int offset);

  DeserializeResult<uint16_t> DeserializeUint16(uint8_t* bytes, int offset);
  DeserializeResult<uint8_t> DeserializeUint8(uint8_t* bytes, int offset);
  DeserializeResult<int32_t> DeserializeInt32(uint8_t* bytes, int offset);
  DeserializeResult<std::array<bool, 16>> DeserializeUint16Flags(uint8_t* bytes, int offset);
  DeserializeResult<double> DeserializeDouble(uint8_t* bytes, int offset);

  DeserializeResult<AstNode*> DeserializeNodeReference(uint8_t* bytes, int offset, void** patchable_field);
  void RecordBreakableStatement(uint32_t offset, BreakableStatement* node);
  void PatchPendingNodeReferences(uint32_t offset, AstNode* node);

  DeserializeResult<const char*> DeserializeCString(uint8_t* bytes, int offset);
  DeserializeResult<const AstRawString*> DeserializeRawString(uint8_t* bytes, int offset);
  DeserializeResult<const AstRawString*> DeserializeProxyString(uint8_t* serialized_ast, int offset);
  DeserializeResult<std::nullptr_t> DeserializeStringTable(uint8_t* bytes, int offset);
  DeserializeResult<std::nullptr_t> DeserializeProxyStringTable(uint8_t* serialized_ast, int offset);
  DeserializeResult<const AstRawString*> DeserializeRawStringReference(uint8_t* bytes, int offset, bool fixed_size = false);
  DeserializeResult<const AstRawString*> DeserializeGlobalRawStringReference(uint8_t* serialized_ast, int offset);
  DeserializeResult<AstConsString*> DeserializeConsString(uint8_t* bytes, int offset);

  Variable* CreateLocalTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);
  Variable* CreateLocalNonTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);

  void HandleFunctionSkipping(Scope* scope);

  DeserializeResult<std::nullptr_t> DeserializeProxyVariableTable(uint8_t* serialized_ast, int offset);
  DeserializeResult<RawVariableData> DeserializeGlobalVariableReference(uint8_t* serialized_binast, int offset);
  DeserializeResult<RawVariableData> DeserializeProxyVariableReference(uint8_t* serialized_binast, int offset);
  DeserializeResult<Variable*> DeserializeLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeVariableReference(uint8_t* serialized_binast, int offset, Scope* scope = nullptr);
  DeserializeResult<Variable*> DeserializeScopeVariable(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonScopeVariable(uint8_t* serialized_binast, int offset);
  DeserializeResult<Variable*> DeserializeNonScopeVariableOrReference(uint8_t* serialized_binast, int offset);
  DeserializeResult<std::nullptr_t> DeserializeScopeVariableMap(uint8_t* serialized_binast, int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeUnresolvedList(uint8_t* serialized_binast, int offset, Scope* scope);
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
  DeserializeResult<VariableProxy*> DeserializeVariableProxy(uint8_t* serialized_binast, int offset, bool add_unresolved = true);
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
  DeserializeResult<ObjectLiteral*> DeserializeObjectLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ArrayLiteral*> DeserializeArrayLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<NaryOperation*> DeserializeNaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Conditional*> DeserializeConditional(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<TryCatchStatement*> DeserializeTryCatchStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<RegExpLiteral*> DeserializeRegExpLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<SwitchStatement*> DeserializeSwitchStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<BreakStatement*> DeserializeBreakStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<ContinueStatement*> DeserializeContinueStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<Throw*> DeserializeThrow(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);
  DeserializeResult<std::nullptr_t> DeserializeNodeStub(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset);

  Isolate* isolate_;
  Parser* parser_;
  Handle<ByteArray> parse_data_;
  MaybeHandle<PreparseData> preparse_data_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
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

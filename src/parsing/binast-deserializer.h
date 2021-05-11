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
  class OffsetRestorationScope {
   public:
    OffsetRestorationScope(BinAstDeserializer* deserializer)
      : deserializer_(deserializer)
      , old_current_offset_(deserializer->current_offset_)
    {
    }

    ~OffsetRestorationScope()
    {
      deserializer_->current_offset_ = old_current_offset_;
    }

   private:
    BinAstDeserializer* deserializer_;
    int old_current_offset_;
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

  AstNode* DeserializeMaybeAstNode();

  uint64_t DeserializeUint64();
  uint32_t DeserializeUint32();
  uint32_t DeserializeVarUint32();

  uint16_t DeserializeUint16();
  uint8_t DeserializeUint8();
  int32_t DeserializeInt32();
  std::array<bool, 16> DeserializeUint16Flags();
  double DeserializeDouble();

  void DeserializeNodeReference(void** patchable_field);
  void RecordBreakableStatement(uint32_t offset, BreakableStatement* node);
  void PatchPendingNodeReferences(uint32_t offset, AstNode* node);

  const char* DeserializeCString();
  const AstRawString* DeserializeRawString(int header_index);
  const AstRawString* DeserializeProxyString();
  void DeserializeStringTable();
  void DeserializeProxyStringTable();
  const AstRawString* DeserializeRawStringReference(bool fixed_size = false);
  AstConsString* DeserializeConsString();

  Variable* CreateLocalTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);
  Variable* CreateLocalNonTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field);

  void HandleFunctionSkipping(Scope* scope);

  void DeserializeProxyVariableTable();
  RawVariableData DeserializeGlobalVariableReference();
  RawVariableData DeserializeProxyVariableReference();
  void DeserializeLocalVariable(Scope* scope);
  Variable* DeserializeNonLocalVariable(Scope* scope);
  Variable* DeserializeVariableReference(Scope* scope = nullptr);
  Variable* DeserializeScopeVariable(Scope* scope);
  Variable* DeserializeNonScopeVariable();
  Variable* DeserializeNonScopeVariableOrReference();
  void DeserializeScopeVariableMap(Scope* scope);
  void DeserializeScopeUnresolvedList(Scope* scope);
  Declaration* DeserializeDeclaration(Scope* scope);
  void DeserializeScopeDeclarations(Scope* scope);
  void DeserializeScopeParameters(DeclarationScope* scope);
  void DeserializeCommonScopeFields(Scope* scope);
  Scope* DeserializeScope();
  DeclarationScope* DeserializeDeclarationScope();

  AstNode* DeserializeAstNode(bool is_toplevel = false);
  FunctionLiteral* DeserializeFunctionLiteral(uint32_t bit_field, int32_t position);
  ReturnStatement* DeserializeReturnStatement(uint32_t bit_field, int32_t position);
  BinaryOperation* DeserializeBinaryOperation(uint32_t bit_field, int32_t position);
  Property* DeserializeProperty(uint32_t bit_field, int32_t position);
  ExpressionStatement* DeserializeExpressionStatement(uint32_t bit_field, int32_t position);
  VariableProxy* DeserializeVariableProxy(bool add_unresolved = true);
  VariableProxyExpression* DeserializeVariableProxyExpression(uint32_t bit_field, int32_t position);
  Literal* DeserializeLiteral(uint32_t bit_field, int32_t position);
  Call* DeserializeCall(uint32_t bit_field, int32_t position);
  CallNew* DeserializeCallNew(uint32_t bit_field, int32_t position);
  IfStatement* DeserializeIfStatement(uint32_t bit_field, int32_t position);
  Block* DeserializeBlock(uint32_t bit_field, int32_t position);
  Assignment* DeserializeAssignment(uint32_t bit_field, int32_t position);
  CompareOperation* DeserializeCompareOperation(uint32_t bit_field, int32_t position);
  EmptyStatement* DeserializeEmptyStatement(uint32_t bit_field, int32_t position);
  ForStatement* DeserializeForStatement(uint32_t bit_field, int32_t position);
  ForInStatement* DeserializeForInStatement(uint32_t bit_field, int32_t position);
  WhileStatement* DeserializeWhileStatement(uint32_t bit_field, int32_t position);
  DoWhileStatement* DeserializeDoWhileStatement(uint32_t bit_field, int32_t position);
  CountOperation* DeserializeCountOperation(uint32_t bit_field, int32_t position);
  CompoundAssignment* DeserializeCompoundAssignment(uint32_t bit_field, int32_t position);
  UnaryOperation* DeserializeUnaryOperation(uint32_t bit_field, int32_t position);
  ThisExpression* DeserializeThisExpression(uint32_t bit_field, int32_t position);
  ObjectLiteral* DeserializeObjectLiteral(uint32_t bit_field, int32_t position);
  ArrayLiteral* DeserializeArrayLiteral(uint32_t bit_field, int32_t position);
  NaryOperation* DeserializeNaryOperation(uint32_t bit_field, int32_t position);
  Conditional* DeserializeConditional(uint32_t bit_field, int32_t position);
  TryCatchStatement* DeserializeTryCatchStatement(uint32_t bit_field, int32_t position);
  RegExpLiteral* DeserializeRegExpLiteral(uint32_t bit_field, int32_t position);
  SwitchStatement* DeserializeSwitchStatement(uint32_t bit_field, int32_t position);
  BreakStatement* DeserializeBreakStatement(uint32_t bit_field, int32_t position);
  ContinueStatement* DeserializeContinueStatement(uint32_t bit_field, int32_t position);
  Throw* DeserializeThrow(uint32_t bit_field, int32_t position);

  Isolate* isolate_;
  Parser* parser_;
  Handle<ByteArray> parse_data_;
  MaybeHandle<PreparseData> preparse_data_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
  uint8_t* serialized_ast_;
  int current_offset_;

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

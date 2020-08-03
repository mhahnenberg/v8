// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_DESERIALIZER_H_
#define V8_PARSING_BINAST_DESERIALIZER_H_

#include <unordered_map>
#include "src/parsing/parser.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {

class AstNode;
class ByteArray;
class Declaration;
class FunctionLiteral;
class ParseInfo;
class AstConsString;
class AstRawString;

class BinAstDeserializer {
 public:
  BinAstDeserializer(Parser* parser);

  AstNode* DeserializeAst(ByteArray serialized_ast);

 private:
  template <typename T>
  struct DeserializeResult {
    T value;
    int new_offset;
  };

  Zone* zone() { return parser_->zone(); }

  DeserializeResult<uint32_t> DeserializeUint32(ByteArray bytes, int offset);
  DeserializeResult<uint16_t> DeserializeUint16(ByteArray bytes, int offset);
  DeserializeResult<uint8_t> DeserializeUint8(ByteArray bytes, int offset);
  DeserializeResult<int32_t> DeserializeInt32(ByteArray bytes, int offset);

  DeserializeResult<const AstRawString*> DeserializeRawString(ByteArray bytes, int offset);
  DeserializeResult<std::nullptr_t> DeserializeStringTable(ByteArray bytes, int offset);
  DeserializeResult<const AstRawString*> DeserializeRawStringReference(ByteArray bytes, int offset);
  DeserializeResult<AstConsString*> DeserializeConsString(ByteArray bytes, int offset);

  DeserializeResult<Variable*> DeserializeLocalVariable(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeNonLocalVariable(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<Variable*> DeserializeScopeVariableReference(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeVariableMap(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<Declaration*> DeserializeDeclaration(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<std::nullptr_t> DeserializeScopeDeclarations(ByteArray serialized_binast, int offset, Scope* scope);
  DeserializeResult<DeclarationScope*> DeserializeDeclarationScope(ByteArray serialized_binast, int offset);

  DeserializeResult<AstNode*> DeserializeAstNode(ByteArray serialized_ast, int offset);
  DeserializeResult<FunctionLiteral*> DeserializeFunctionLiteral(ByteArray serialized_ast, uint32_t bit_field, int32_t position, int offset);

  Parser* parser_;
  std::unordered_map<uint32_t, const AstRawString*> string_table_;
  std::unordered_map<Scope*, std::unordered_map<uint32_t, Variable*>> variables_by_scope_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_H_
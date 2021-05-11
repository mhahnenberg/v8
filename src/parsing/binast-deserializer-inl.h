// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_DESERIALIZER_INL_H_
#define V8_PARSING_BINAST_DESERIALIZER_INL_H_

#include "src/parsing/binast-deserializer.h"
#include "src/parsing/parser.h"
#include "src/parsing/binast-serialize-visitor.h"

namespace v8 {
namespace internal {

template<typename First, typename Second>
union Uint64TwoFieldConverter {
  struct {
    First first;
    Second second;
  } fields;
  uint64_t raw_value;
};

inline Zone* BinAstDeserializer::zone() {
  return parser_->zone();
}

// TODO(binast): Use templates to de-dupe some of these functions.
inline uint64_t BinAstDeserializer::DeserializeUint64() {
  uint64_t result = *reinterpret_cast<uint64_t*>(&serialized_ast_[current_offset_]);
  current_offset_ += sizeof(uint64_t);
  return result;
}

inline uint32_t BinAstDeserializer::DeserializeUint32() {
  uint32_t result = *reinterpret_cast<uint32_t*>(&serialized_ast_[current_offset_]);
  current_offset_ += sizeof(uint32_t);
  return result;
}

inline uint32_t BinAstDeserializer::DeserializeVarUint32() {
  int i = 0;
  uint32_t result = 0;
  while (true) {
    DCHECK(i < 4);
    uint32_t current_byte = serialized_ast_[current_offset_ + i];
    uint32_t raw_byte_value = current_byte & 0x7f;
    uint32_t shifted_byte_value = raw_byte_value << (7 * i);
    result |= shifted_byte_value;

    bool has_next_byte = current_byte & 0x80;
    if (!has_next_byte) {
      break;
    }
    i += 1;
  }
  current_offset_ += i + 1;
  return result;
}

inline uint16_t BinAstDeserializer::DeserializeUint16() {
  uint16_t result = *reinterpret_cast<uint16_t*>(&serialized_ast_[current_offset_]);
  current_offset_ += sizeof(uint16_t);
  return result;
}

inline std::array<bool, 16> BinAstDeserializer::DeserializeUint16Flags() {
  std::array<bool, 16> flags;
  auto encoded_flags_result = DeserializeUint16();
  uint16_t encoded_flags = encoded_flags_result;
  for (size_t i = 0; i < flags.size(); ++i) {
    auto shift = flags.size() - i - 1;
    flags[i] = (encoded_flags >> shift) & 0x1;
  }
  return flags;
}

inline uint8_t BinAstDeserializer::DeserializeUint8() {
  uint8_t result = serialized_ast_[current_offset_];
  current_offset_ += sizeof(uint8_t);
  return result;
}

inline int32_t BinAstDeserializer::DeserializeInt32() {
  int32_t result = *reinterpret_cast<int32_t*>(&serialized_ast_[current_offset_]);
  current_offset_ += sizeof(int32_t);
  return result;
}

inline double BinAstDeserializer::DeserializeDouble() {
  union {
    double d;
    uint64_t ui;
  } converter;

  converter.ui = DeserializeUint64();
  return converter.d;
}

inline const char* BinAstDeserializer::DeserializeCString() {
  std::vector<char> characters;
  for (int i = 0; ; ++i) {
    char c = DeserializeUint8();
    characters.push_back(c);
    if (c == 0) {
      break;
    }
  }
  char* result = zone()->NewArray<char>(characters.size());
  DCHECK(characters.size() > 0);
  memcpy(result, &characters[0], characters.size());
  return result;
}

inline const AstRawString* BinAstDeserializer::DeserializeRawString(int header_index) {
  auto header_offset = string_table_base_offset_ + STRING_TABLE_HEADER_SIZE + RAW_STRING_HEADER_SIZE * header_index;
  DCHECK(header_offset < INT_MAX);
  current_offset_ = static_cast<int>(header_offset);

  uint8_t is_one_byte = DeserializeUint8();
  uint32_t hash_field = DeserializeUint32();
  uint32_t length = DeserializeUint32();
  uint32_t contents_offset = DeserializeUint32();

  const AstRawString* s = nullptr;
  if (length > 0) {
    // TODO(binast): We're causing a re-hash of each string here since the hash seed in this isolate
    // could be different. If this turns out to be a bottleneck we could potentially shift this 
    // re-hashing to the main-thread finalization step (or even the serialization step if we 
    // passed the hash seed to the background parse task).
    //if (is_one_byte) {
    //  Vector<const uint8_t> literal_bytes(&serialized_ast_[contents_offset], length);
    //  s = parser_->ast_value_factory()->GetOneByteString(literal_bytes);
    //} else {
    //  Vector<const uint16_t> literal_bytes((const uint16_t*)&serialized_ast_[contents_offset], length / 2);
    //  s = parser_->ast_value_factory()->GetTwoByteString(literal_bytes);
    //}
    Vector<const uint8_t> literal_bytes(&serialized_ast_[contents_offset], length);
    s = parser_->ast_value_factory()->GetString(hash_field, is_one_byte, literal_bytes);
  } else {
    DCHECK(contents_offset == 0);
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field, is_one_byte, literal_bytes);
  }
  return s;
}

inline const AstRawString* BinAstDeserializer::DeserializeProxyString() {
  uint32_t raw_index = DeserializeUint32();
  OffsetRestorationScope scope(this);
  return DeserializeRawString(raw_index);
} 

inline void BinAstDeserializer::DeserializeStringTable() {
  string_table_base_offset_ = current_offset_;
  uint32_t end_offset = DeserializeUint32();
  current_offset_ = end_offset;
}

inline void BinAstDeserializer::DeserializeProxyStringTable() {
  uint32_t num_proxy_strings = DeserializeUint32();
  strings_.reserve(num_proxy_strings);

  for (uint32_t i = 0; i < num_proxy_strings; ++i) {
    const AstRawString* string = DeserializeProxyString();
    strings_.push_back(string);
  }
}

inline const AstRawString* BinAstDeserializer::DeserializeRawStringReference(bool fixed_size) {
  uint32_t local_string_table_index = fixed_size ? DeserializeUint32() : DeserializeVarUint32();
  return strings_[local_string_table_index];
}

inline AstConsString* BinAstDeserializer::DeserializeConsString() {
  uint8_t has_value = DeserializeUint8();

  if (!has_value) {
    return nullptr;
  }

  uint32_t raw_string_count = DeserializeUint32();
  AstConsString* cons_string = parser_->ast_value_factory()->NewConsString();

  for (uint32_t i = 0; i < raw_string_count; ++i) {
    const AstRawString* string = DeserializeRawStringReference();
    cons_string->AddString(parser_->zone(), string);
  }

  return cons_string;
}

inline void BinAstDeserializer::DeserializeProxyVariableTable() {
  uint32_t num_proxy_variables = DeserializeUint32();
  variables_.resize(num_proxy_variables);
  current_offset_ += num_proxy_variables * PROXY_VARIABLE_TABLE_ENTRY_SIZE;
}

inline Variable* BinAstDeserializer::CreateLocalTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field) {
  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->NewTemporary(name);
  variable->index_ = index;
  variable->initializer_position_ = initializer_position;
  variable->bit_field_ = bit_field;
  return variable;
}

inline Variable* BinAstDeserializer::CreateLocalNonTemporaryVariable(Scope* scope, const AstRawString* name, int index, int initializer_position, uint32_t bit_field) {
  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->Declare(parser_->zone(), name, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, &was_added);
  variable->index_ = index;
  variable->initializer_position_ = initializer_position;
  variable->bit_field_ = bit_field;
  return variable;
}

inline RawVariableData BinAstDeserializer::DeserializeGlobalVariableReference() {
  uint32_t global_variable_index = DeserializeUint32();
  // Note we don't use the next offset, instead jumping to the proper offset in the global variable table.

  DCHECK(global_variable_index != 0);
  current_offset_ = global_variable_table_base_offset_ + GLOBAL_VARIABLE_TABLE_HEADER_SIZE + GLOBAL_VARIABLE_TABLE_ENTRY_SIZE * (global_variable_index - 1);
  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_
  Uint64TwoFieldConverter<int32_t, int32_t> index_and_initializer_position_convertor;
  index_and_initializer_position_convertor.raw_value = DeserializeUint64();

  int32_t index = index_and_initializer_position_convertor.fields.first;
  int32_t initializer_position = index_and_initializer_position_convertor.fields.second;

  uint16_t bit_field = DeserializeUint16();
  return {0, nullptr, index, initializer_position, bit_field};
}

inline RawVariableData BinAstDeserializer::DeserializeProxyVariableReference() {
  uint32_t local_variable_index = DeserializeVarUint32();
  
  if (local_variable_index == 0) {
    return {0, nullptr, 0, 0, 0};
  }

  OffsetRestorationScope restoration_scope(this);
  current_offset_ = proxy_variable_table_base_offset_ + PROXY_VARIABLE_TABLE_HEADER_SIZE + PROXY_VARIABLE_TABLE_ENTRY_SIZE * (local_variable_index - 1);
  bool fixed_size = true;
  const AstRawString* name = DeserializeRawStringReference(fixed_size);

  RawVariableData raw_variable_data = DeserializeGlobalVariableReference();
  int32_t index = raw_variable_data.index;
  int32_t initializer_position = raw_variable_data.initializer_position;
  uint16_t bit_field = raw_variable_data.bit_field;
  return {local_variable_index, name, index, initializer_position, bit_field};
}

inline void BinAstDeserializer::DeserializeLocalVariable(Scope* scope) {
  RawVariableData raw_variable = DeserializeProxyVariableReference();

  if (raw_variable.local_index == 0) {
    return;
  }

  DCHECK(variables_[raw_variable.local_index - 1] == nullptr);

  auto variable_mode = Variable::VariableModeField::decode(raw_variable.bit_field);
  if (variable_mode == VariableMode::kTemporary) {
    auto variable = CreateLocalTemporaryVariable(scope, raw_variable.name, raw_variable.index, raw_variable.initializer_position, raw_variable.bit_field);
    variables_[raw_variable.local_index - 1] = variable;
  } else {
    auto variable = CreateLocalNonTemporaryVariable(scope, raw_variable.name, raw_variable.index, raw_variable.initializer_position, raw_variable.bit_field);
    variables_[raw_variable.local_index - 1] = variable;
  }
}

inline Variable* BinAstDeserializer::DeserializeNonLocalVariable(Scope* scope) {
  RawVariableData raw_variable = DeserializeProxyVariableReference();

  if (raw_variable.local_index == 0) {
    return nullptr;
  }

  DCHECK(variables_[raw_variable.local_index - 1] == nullptr);

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->variables_.Declare(parser_->zone(), scope, raw_variable.name, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic, &was_added);
  variable->index_ = raw_variable.index;
  variable->initializer_position_ = raw_variable.initializer_position;
  variable->bit_field_ = raw_variable.bit_field;
  variables_[raw_variable.local_index - 1] = variable;
  return variable;
}

inline Variable* BinAstDeserializer::DeserializeVariableReference(Scope* scope) {
  // If we discover that we haven't encountered this Variable reference before,
  // we restart from the initial offset.
  auto original_offset = current_offset_;
  auto local_variable_index = DeserializeVarUint32();
  if (local_variable_index == 0) {
    return nullptr;
  }

  auto variable = variables_[local_variable_index - 1];
  if (variable != nullptr) {
    return variable;
  }

  current_offset_ = original_offset;
  if (scope == nullptr) {
    return DeserializeNonScopeVariable();
  } else {
    return DeserializeNonLocalVariable(scope);
  }
}


// This is for Variables that didn't belong to any particular Scope, i.e. their scope_ field was null.
inline Variable* BinAstDeserializer::DeserializeNonScopeVariable() {
  // TODO(binast): Not sure why this was here...
  //uint32_t local_variable_index = DeserializeVarUint32();
  RawVariableData raw_variable = DeserializeProxyVariableReference();

  if (raw_variable.local_index == 0) {
    return nullptr;
  }

  DCHECK(variables_[raw_variable.local_index - 1] == nullptr);

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  Variable* variable = zone()->New<Variable>(nullptr, raw_variable.name, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic);
  variable->index_ = raw_variable.index;
  variable->initializer_position_ = raw_variable.initializer_position;
  variable->bit_field_ = raw_variable.bit_field;
  variables_[raw_variable.local_index - 1] = variable;
  return variable;
}

inline AstNode* BinAstDeserializer::DeserializeAstNode(bool is_toplevel) {
  auto original_offset = current_offset_;

  Uint64TwoFieldConverter<uint32_t, int32_t> bit_field_and_position_convertor;
  bit_field_and_position_convertor.raw_value = DeserializeUint64();

  uint32_t bit_field = bit_field_and_position_convertor.fields.first;
  int32_t position = bit_field_and_position_convertor.fields.second;

  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field);

  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    uint32_t start_offset = DeserializeUint32();
    uint32_t length = DeserializeUint32();

    if (parser_->scope()->GetClosureScope()->is_skipped_function()) {
      current_offset_ = start_offset + length;
      return nullptr;
    }

    FunctionLiteral* result = DeserializeFunctionLiteral(bit_field, position);

    if (!is_toplevel) {
      MaybeHandle<PreparseData> preparse_data;
      if (result->produced_preparse_data() != nullptr) {
        preparse_data = result->produced_preparse_data()->Serialize(isolate_);
      }

      Handle<UncompiledDataWithInnerBinAstParseData> data =
          isolate_->factory()->NewUncompiledDataWithInnerBinAstParseData(
              result->GetInferredName(isolate_),
              result->start_position(), result->end_position(),
              parse_data_, preparse_data, start_offset, length);
      result->set_uncompiled_data_with_inner_bin_ast_parse_data(data);
    }

    return result;
  }
  case AstNode::kReturnStatement: {
    return DeserializeReturnStatement(bit_field, position);
  }
  case AstNode::kBinaryOperation: {
    return DeserializeBinaryOperation(bit_field, position);
  }
  case AstNode::kProperty: {
    return DeserializeProperty(bit_field, position);
  }
  case AstNode::kExpressionStatement: {
    return DeserializeExpressionStatement(bit_field, position);
  }
  case AstNode::kVariableProxyExpression: {
    return DeserializeVariableProxyExpression(bit_field, position);
  }
  case AstNode::kLiteral: {
    return DeserializeLiteral(bit_field, position);
  }
  case AstNode::kCall: {
    return DeserializeCall(bit_field, position);
  }
  case AstNode::kCallNew: {
    return DeserializeCallNew(bit_field, position);
  }
  case AstNode::kIfStatement: {
    return DeserializeIfStatement(bit_field, position);
  }
  case AstNode::kBlock: {
    Block* result = DeserializeBlock(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kAssignment: {
    return DeserializeAssignment(bit_field, position);
  }
  case AstNode::kCompareOperation: {
    return DeserializeCompareOperation(bit_field, position);
  }
  case AstNode::kEmptyStatement: {
    return DeserializeEmptyStatement(bit_field, position);
  }
  case AstNode::kForStatement: {
    ForStatement* result = DeserializeForStatement(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kForInStatement: {
    ForInStatement* result = DeserializeForInStatement(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kCountOperation: {
    return DeserializeCountOperation(bit_field, position);
  }
  case AstNode::kCompoundAssignment: {
    return DeserializeCompoundAssignment(bit_field, position);
  }
  case AstNode::kWhileStatement: {
    WhileStatement* result = DeserializeWhileStatement(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kDoWhileStatement: {
    DoWhileStatement* result = DeserializeDoWhileStatement(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kThisExpression: {
    return DeserializeThisExpression(bit_field, position);
  }
  case AstNode::kUnaryOperation: {
    return DeserializeUnaryOperation(bit_field, position);
  }
  case AstNode::kObjectLiteral: {
    return DeserializeObjectLiteral(bit_field, position);
  }
  case AstNode::kArrayLiteral: {
    return DeserializeArrayLiteral(bit_field, position);
  }
  case AstNode::kNaryOperation: {
    return DeserializeNaryOperation(bit_field, position);
  }
  case AstNode::kConditional: {
    return DeserializeConditional(bit_field, position);
  }
  case AstNode::kTryCatchStatement: {
    return DeserializeTryCatchStatement(bit_field, position);
  }
  case AstNode::kRegExpLiteral: {
    return DeserializeRegExpLiteral(bit_field, position);
  }
  case AstNode::kSwitchStatement: {
    SwitchStatement* result = DeserializeSwitchStatement(bit_field, position);
    RecordBreakableStatement(original_offset, result);
    return result;
  }
  case AstNode::kThrow: {
    return DeserializeThrow(bit_field, position);
  }
  case AstNode::kContinueStatement: {
    return DeserializeContinueStatement(bit_field, position);
  }
  case AstNode::kBreakStatement: {
    return DeserializeBreakStatement(bit_field, position);
  }
  case AstNode::kForOfStatement:
  case AstNode::kSloppyBlockFunctionStatement:
  case AstNode::kTryFinallyStatement:
  case AstNode::kDebuggerStatement:
  case AstNode::kInitializeClassMembersStatement:
  case AstNode::kInitializeClassStaticElementsStatement:
  case AstNode::kAwait:
  case AstNode::kCallRuntime:
  case AstNode::kClassLiteral:
  case AstNode::kEmptyParentheses:
  case AstNode::kGetTemplateObject:
  case AstNode::kImportCallExpression:
  case AstNode::kNativeFunctionLiteral:
  case AstNode::kOptionalChain:
  case AstNode::kSpread:
  case AstNode::kSuperCallReference:
  case AstNode::kSuperPropertyReference:
  case AstNode::kTemplateLiteral:
  case AstNode::kYield:
  case AstNode::kYieldStar:
  case AstNode::kWithStatement:
  case AstNode::kFailureExpression: {
    // We should never get here because the serializer should give up if it encounters these node types.
    AstNode temp(0, nodeType);
    printf("Got unhandled node type: %s\n", temp.node_type_name());
    UNREACHABLE();
  }
  }
}

inline void BinAstDeserializer::RecordBreakableStatement(uint32_t offset, BreakableStatement* node) {
  nodes_by_offset_[offset] = node;
  PatchPendingNodeReferences(offset, node);
}

inline void BinAstDeserializer::PatchPendingNodeReferences(uint32_t offset, AstNode* node) {
  if (patchable_fields_by_offset_.count(offset) == 0) {
    return;
  }
  auto& fields_to_patch = patchable_fields_by_offset_[offset];
  for (void** field : fields_to_patch) {
    DCHECK(*field == nullptr);
    *field = node;
  }
  patchable_fields_by_offset_.erase(offset);
}

inline void BinAstDeserializer::DeserializeNodeReference(void** patchable_field) {
  uint32_t node_offset = DeserializeUint32();
  auto result = nodes_by_offset_.find(node_offset);
  if (result == nodes_by_offset_.end()) {
    patchable_fields_by_offset_[node_offset].push_back(patchable_field);
    return;
  }
  *patchable_field = result->second;
}

inline ReturnStatement* BinAstDeserializer::DeserializeReturnStatement(uint32_t bit_field, int32_t position) {
  int32_t end_position = DeserializeInt32();
  AstNode* expression = DeserializeAstNode();

  ReturnStatement* result = parser_->factory()->NewReturnStatement(static_cast<Expression*>(expression), position, end_position);
  result->bit_field_ = bit_field;
  return result;
}

inline BinaryOperation* BinAstDeserializer::DeserializeBinaryOperation(uint32_t bit_field, int32_t position) {
  AstNode* left = DeserializeAstNode();
  AstNode* right = DeserializeAstNode();

  Token::Value op = BinaryOperation::OperatorField::decode(bit_field);

  BinaryOperation* result = parser_->factory()->NewBinaryOperation(op, static_cast<Expression*>(left), static_cast<Expression*>(right), position);
  result->bit_field_ = bit_field;
  return result;
}

inline Property* BinAstDeserializer::DeserializeProperty(uint32_t bit_field, int32_t position) {
  AstNode* obj = DeserializeAstNode();
  AstNode* key = DeserializeAstNode();

  Property* result = parser_->factory()->NewProperty(static_cast<Expression*>(obj), static_cast<Expression*>(key), position);
  result->bit_field_ = bit_field;
  return result;
}

inline ExpressionStatement* BinAstDeserializer::DeserializeExpressionStatement(uint32_t bit_field, int32_t position) {
  AstNode* expression = DeserializeAstNode();
  ExpressionStatement* result = parser_->factory()->NewExpressionStatement(static_cast<Expression*>(expression), position);
  result->bit_field_ = bit_field;
  return result;
}

inline VariableProxy* BinAstDeserializer::DeserializeVariableProxy(bool add_unresolved) {
  int32_t position = DeserializeInt32();
  uint32_t bit_field = DeserializeUint32();

  bool is_resolved = VariableProxy::IsResolvedField::decode(bit_field);

  VariableProxy* result;
  if (is_resolved) {
    // The resolved Variable should either be a reference (i.e. currently visible in scope) or should be a 
    // NonScope Variable definition (i.e. it's a Variable that is outside the current Scope boundaries, 
    // e.g. inside an eval).
    Variable* variable = DeserializeVariableReference();
    result = parser_->factory()->NewVariableProxy(variable, position);
  } else {
    const AstRawString* raw_name = DeserializeRawStringReference();
    // We use NORMAL_VARIABLE as a placeholder here.
    result = parser_->factory()->NewVariableProxy(raw_name, VariableKind::NORMAL_VARIABLE, position);

    if (add_unresolved) {
      parser_->scope()->AddUnresolved(result);
    }
  }
  result->bit_field_ = bit_field;
  return result;
}

inline VariableProxyExpression* BinAstDeserializer::DeserializeVariableProxyExpression(uint32_t bit_field, int32_t position) {
  VariableProxy* variable_proxy = DeserializeVariableProxy();
  VariableProxyExpression* result = parser_->factory()->NewVariableProxyExpression(variable_proxy);
  result->bit_field_ = bit_field;
  return result;
}

inline Call* BinAstDeserializer::DeserializeCall(uint32_t bit_field, int32_t position) {
  AstNode* expression = DeserializeAstNode();
  int32_t params_count = DeserializeInt32();

  ScopedPtrList<Expression> params(pointer_buffer());
  params.Reserve(params_count);
  for (int i = 0; i < params_count; ++i) {
    AstNode* param = DeserializeAstNode();
    params.Add(static_cast<Expression*>(param));
  }

  bool has_spread = CallBase::SpreadPositionField::decode(bit_field) != CallBase::kNoSpread;

  Call* result = parser_->factory()->NewCall(static_cast<Expression*>(expression), params, position, has_spread);
  result->bit_field_ = bit_field;
  return result;
}

inline CallNew* BinAstDeserializer::DeserializeCallNew(uint32_t bit_field, int32_t position) {
  AstNode* expression = DeserializeAstNode();
  int32_t params_count = DeserializeInt32();

  ScopedPtrList<Expression> params(pointer_buffer());
  params.Reserve(params_count);
  for (int i = 0; i < params_count; ++i) {
    AstNode* param = DeserializeAstNode();
    params.Add(static_cast<Expression*>(param));
  }

  bool has_spread = CallBase::SpreadPositionField::decode(bit_field) != CallBase::kNoSpread;

  CallNew* result = parser_->factory()->NewCallNew(static_cast<Expression*>(expression), params, position, has_spread);
  result->bit_field_ = bit_field;
  return result;
}

inline IfStatement* BinAstDeserializer::DeserializeIfStatement(uint32_t bit_field, int32_t position) {
  AstNode* condition = DeserializeAstNode();
  AstNode* then_statement = DeserializeAstNode();
  AstNode* else_statement = DeserializeAstNode();

  IfStatement* result = parser_->factory()->NewIfStatement(static_cast<Expression*>(condition), static_cast<Statement*>(then_statement), static_cast<Statement*>(else_statement), position);
  result->bit_field_ = bit_field;
  return result;
}

inline Block* BinAstDeserializer::DeserializeBlock(uint32_t bit_field, int32_t position) {
  uint8_t has_scope = DeserializeUint8();
  Scope* scope = has_scope ? DeserializeScope() : nullptr;

  int32_t statement_count = DeserializeInt32();
  ScopedPtrList<Statement> statements(pointer_buffer());
  statements.Reserve(statement_count);
  if (scope != nullptr) {
    Parser::BlockState block_state(&parser_->scope_, scope);
    for (int i = 0; i < statement_count; ++i) {
      AstNode* statement = DeserializeAstNode();
      statements.Add(static_cast<Statement*>(statement));
    }
  } else {
    for (int i = 0; i < statement_count; ++i) {
      AstNode* statement = DeserializeAstNode();
      statements.Add(static_cast<Statement*>(statement));
    }
  }

  bool ignore_completion_value = false; // Just a filler value.
  Block* result = parser_->factory()->NewBlock(ignore_completion_value, statements);
  result->bit_field_ = bit_field;
  result->set_scope(scope);
  return result;
}

inline Assignment* BinAstDeserializer::DeserializeAssignment(uint32_t bit_field, int32_t position) {
  AstNode* target = DeserializeAstNode();
  AstNode* value = DeserializeAstNode();

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target), static_cast<Expression*>(value), position);
  result->bit_field_ = bit_field;
  return result;
}

inline CompareOperation* BinAstDeserializer::DeserializeCompareOperation(uint32_t bit_field, int32_t position) {
  AstNode* left = DeserializeAstNode();
  AstNode* right = DeserializeAstNode();

  Token::Value op = CompareOperation::OperatorField::decode(bit_field);
  CompareOperation* result = parser_->factory()->NewCompareOperation(op, static_cast<Expression*>(left), static_cast<Expression*>(right), position);
  result->bit_field_ = bit_field;
  return result;
}

inline EmptyStatement* BinAstDeserializer::DeserializeEmptyStatement(uint32_t bit_field, int32_t position) {
  return parser_->factory()->EmptyStatement();
}

inline AstNode* BinAstDeserializer::DeserializeMaybeAstNode() {
  uint8_t has_node = DeserializeUint8();
  return has_node ? DeserializeAstNode() : nullptr;
}

inline ForStatement* BinAstDeserializer::DeserializeForStatement(uint32_t bit_field, int32_t position) {
  AstNode* init_node = DeserializeMaybeAstNode();
  AstNode* cond_node = DeserializeMaybeAstNode();
  AstNode* next_node = DeserializeMaybeAstNode();
  AstNode* body = DeserializeAstNode();

  ForStatement* result = parser_->factory()->NewForStatement(position);
  result->Initialize(
    static_cast<Statement*>(init_node),
    static_cast<Expression*>(cond_node),
    static_cast<Statement*>(next_node),
    static_cast<Statement*>(body));
  result->bit_field_ = bit_field;
  return result;
}

inline ForInStatement* BinAstDeserializer::DeserializeForInStatement(uint32_t bit_field, int32_t position) {
  AstNode* each = DeserializeAstNode();
  AstNode* subject = DeserializeAstNode();
  AstNode* body = DeserializeAstNode();

  ForEachStatement* result = parser_->factory()->NewForEachStatement(ForEachStatement::ENUMERATE, position);
  result->Initialize(static_cast<Expression*>(each), static_cast<Expression*>(subject), static_cast<Statement*>(body));
  result->bit_field_ = bit_field;
  DCHECK(result->IsForInStatement());
  return static_cast<ForInStatement*>(result);
}

inline WhileStatement* BinAstDeserializer::DeserializeWhileStatement(uint32_t bit_field, int32_t position) {
  AstNode* cond = DeserializeAstNode();
  AstNode* body = DeserializeAstNode();

  WhileStatement* result = parser_->factory()->NewWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond), static_cast<Statement*>(body));
  result->bit_field_ = bit_field;
  return result;
}

inline DoWhileStatement* BinAstDeserializer::DeserializeDoWhileStatement(uint32_t bit_field, int32_t position) {
  AstNode* cond = DeserializeAstNode();
  AstNode* body = DeserializeAstNode();

  DoWhileStatement* result = parser_->factory()->NewDoWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond), static_cast<Statement*>(body));
  result->bit_field_ = bit_field;
  return result;
}

inline CountOperation* BinAstDeserializer::DeserializeCountOperation(uint32_t bit_field, int32_t position) {
  AstNode* expression = DeserializeAstNode();

  Token::Value op = CountOperation::TokenField::decode(bit_field);
  bool is_prefix = CountOperation::IsPrefixField::decode(bit_field);

  CountOperation* result = parser_->factory()->NewCountOperation(op, is_prefix, static_cast<Expression*>(expression), position);
  result->bit_field_ = bit_field;
  return result;
}

inline CompoundAssignment* BinAstDeserializer::DeserializeCompoundAssignment(uint32_t bit_field, int32_t position) {
  AstNode* target = DeserializeAstNode();
  AstNode* value = DeserializeAstNode();
  // TODO(binast): remove this from the serialized output because the factory re-creates the binary node for the compound assignment.
  /* AstNode* binary_operation = */DeserializeAstNode();

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target), static_cast<Expression*>(value), position);
  result->bit_field_ = bit_field;
  return result->AsCompoundAssignment();
}

inline UnaryOperation* BinAstDeserializer::DeserializeUnaryOperation(uint32_t bit_field, int32_t position) {
  AstNode* expression = DeserializeAstNode();

  Token::Value op = UnaryOperation::OperatorField::decode(bit_field);
  UnaryOperation* result = parser_->factory()->NewUnaryOperation(op, static_cast<Expression*>(expression), position);
  result->bit_field_ = bit_field;
  return result;
}

inline ThisExpression* BinAstDeserializer::DeserializeThisExpression(uint32_t bit_field, int32_t position) {
  ThisExpression* result = parser_->factory()->ThisExpression();
  result->bit_field_ = bit_field;
  return result;
}

inline Literal* BinAstDeserializer::DeserializeLiteral(uint32_t bit_field, int32_t position) {
  Literal::Type type = Literal::TypeField::decode(bit_field);

  Literal* result;
  switch (type) {
    case Literal::kSmi: {
      int32_t smi = DeserializeInt32();
      result = parser_->factory()->NewSmiLiteral(smi, position);
      break;
    }
    case Literal::kString: {
      const AstRawString* string = DeserializeRawStringReference();
      result = parser_->factory()->NewStringLiteral(string, position);
      break;
    }
    case Literal::kNull: {
      result = parser_->factory()->NewNullLiteral(position);
      break;
    }
    case Literal::kBoolean: {
      uint8_t boolean = DeserializeUint8();
      result = parser_->factory()->NewBooleanLiteral(boolean, position);
      break;
    }
    case Literal::kUndefined: {
      result = parser_->factory()->NewUndefinedLiteral(position);
      break;
    }
    case Literal::kHeapNumber: {
      double number = DeserializeDouble();
      result = parser_->factory()->NewNumberLiteral(number, position);
      break;
    }
    case Literal::kBigInt: {
      const char* bigint_str = DeserializeCString();
      result = parser_->factory()->NewBigIntLiteral(AstBigInt(bigint_str), position);
      break;
    }
    case Literal::kTheHole: {
      result = parser_->factory()->NewTheHoleLiteral();
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  result->bit_field_ = bit_field;
  return result;
}


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_INL_H_

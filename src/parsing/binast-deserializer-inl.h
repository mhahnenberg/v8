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
inline BinAstDeserializer::DeserializeResult<uint64_t> BinAstDeserializer::DeserializeUint64(int offset) {
  uint64_t result = *reinterpret_cast<uint64_t*>(&serialized_ast_[offset]);
  return {result, offset + sizeof(uint64_t)};
}

inline BinAstDeserializer::DeserializeResult<uint32_t> BinAstDeserializer::DeserializeUint32(int offset) {
  uint32_t result = *reinterpret_cast<uint32_t*>(&serialized_ast_[offset]);
  return {result, offset + sizeof(uint32_t)};
}

inline BinAstDeserializer::DeserializeResult<uint32_t> BinAstDeserializer::DeserializeVarUint32(int offset) {
  int i = 0;
  uint32_t result = 0;
  while (true) {
    DCHECK(i < 4);
    uint32_t current_byte = serialized_ast_[offset + i];
    uint32_t raw_byte_value = current_byte & 0x7f;
    uint32_t shifted_byte_value = raw_byte_value << (7 * i);
    result |= shifted_byte_value;

    bool has_next_byte = current_byte & 0x80;
    if (!has_next_byte) {
      break;
    }
    i += 1;
  }
  return {result, offset + i + 1};
}

inline BinAstDeserializer::DeserializeResult<uint16_t> BinAstDeserializer::DeserializeUint16(int offset) {
  uint16_t result = *reinterpret_cast<uint16_t*>(&serialized_ast_[offset]);
  return {result, offset + sizeof(uint16_t)};
}

inline BinAstDeserializer::DeserializeResult<std::array<bool, 16>> BinAstDeserializer::DeserializeUint16Flags(int offset) {
  std::array<bool, 16> flags;
  auto encoded_flags_result = DeserializeUint16(offset);
  offset = encoded_flags_result.new_offset;
  uint16_t encoded_flags = encoded_flags_result.value;
  for (size_t i = 0; i < flags.size(); ++i) {
    auto shift = flags.size() - i - 1;
    flags[i] = (encoded_flags >> shift) & 0x1;
  }
  return {flags, offset};
}

inline BinAstDeserializer::DeserializeResult<uint8_t> BinAstDeserializer::DeserializeUint8(int offset) {
  return {serialized_ast_[offset], offset + sizeof(uint8_t)};
}

inline BinAstDeserializer::DeserializeResult<int32_t> BinAstDeserializer::DeserializeInt32(int offset) {
  int32_t result = *reinterpret_cast<int32_t*>(&serialized_ast_[offset]);
  return {result, offset + sizeof(int32_t)};
}

inline BinAstDeserializer::DeserializeResult<double> BinAstDeserializer::DeserializeDouble(int offset) {
  union {
    double d;
    uint64_t ui;
  } converter;

  auto result = DeserializeUint64(offset);
  offset = result.new_offset;
  converter.ui = result.value;
  return {converter.d, offset};
}

inline BinAstDeserializer::DeserializeResult<const char*> BinAstDeserializer::DeserializeCString(int offset) {
  std::vector<char> characters;
  for (int i = 0; ; ++i) {
    auto next_char = DeserializeUint8(offset);
    offset = next_char.new_offset;
    char c = next_char.value;
    characters.push_back(c);
    if (c == 0) {
      break;
    }
  }
  char* result = zone()->NewArray<char>(characters.size());
  DCHECK(characters.size() > 0);
  memcpy(result, &characters[0], characters.size());
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawString(int header_index) {
  auto header_offset = string_table_base_offset_ + STRING_TABLE_HEADER_SIZE + RAW_STRING_HEADER_SIZE * header_index;
  DCHECK(header_offset < INT_MAX);
  int offset = static_cast<int>(header_offset);

  auto is_one_byte = DeserializeUint8(offset);
  offset = is_one_byte.new_offset;

  auto hash_field = DeserializeUint32(offset);
  offset = hash_field.new_offset;

  auto length = DeserializeUint32(offset);
  offset = length.new_offset;

  auto contents_offset = DeserializeUint32(offset);
  offset = contents_offset.new_offset;

  const AstRawString* s = nullptr;
  if (length.value > 0) {
    // TODO(binast): We're causing a re-hash of each string here since the hash seed in this isolate
    // could be different. If this turns out to be a bottleneck we could potentially shift this 
    // re-hashing to the main-thread finalization step (or even the serialization step if we 
    // passed the hash seed to the background parse task).
    if (is_one_byte.value) {
      Vector<const uint8_t> literal_bytes(&serialized_ast[contents_offset.value], length.value);
      s = parser_->ast_value_factory()->GetOneByteString(literal_bytes);
    } else {
      Vector<const uint16_t> literal_bytes((const uint16_t*)&serialized_ast[contents_offset.value], length.value / 2);
      s = parser_->ast_value_factory()->GetTwoByteString(literal_bytes);
    }
  } else {
    DCHECK(contents_offset.value == 0);
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  }
  offset += sizeof(uint8_t) * length.value;
  return {s, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeProxyString(int offset) {
  auto raw_index = DeserializeUint32(offset);
  offset = raw_index.new_offset;

  auto deserialize_result = DeserializeRawString(raw_index.value);
  // Note: we don't use the offset returned.

  return {deserialize_result.value, offset};
} 

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeStringTable(int offset) {
  string_table_base_offset_ = offset;
  auto end_offset = DeserializeUint32(offset);
  return {nullptr, end_offset.value};
}

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeProxyStringTable(int offset) {
  auto num_proxy_strings = DeserializeUint32(offset);
  offset = num_proxy_strings.new_offset;

  strings_.reserve(num_proxy_strings.value);

  for (uint32_t i = 0; i < num_proxy_strings.value; ++i) {
    auto string = DeserializeProxyString(offset);
    offset = string.new_offset;
    strings_.push_back(string.value);
  }

  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawStringReference(int offset, bool fixed_size) {
  auto local_string_table_index_value = 0;
  if (fixed_size) {
    auto local_string_table_index = DeserializeUint32(offset);
    offset = local_string_table_index.new_offset;
    local_string_table_index_value = local_string_table_index.value;
  } else {
    auto local_string_table_index = DeserializeVarUint32(offset);
    offset = local_string_table_index.new_offset;
    local_string_table_index_value = local_string_table_index.value;
  }

  const AstRawString* result = strings_[local_string_table_index_value];
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeGlobalRawStringReference(int offset) {
  auto global_string_table_index = DeserializeUint32(offset);
  offset = global_string_table_index.new_offset;

  auto raw_string = DeserializeRawString(global_string_table_index.value);
  // Note: we don't use the offset returned.

  return {raw_string.value, offset};
}

inline BinAstDeserializer::DeserializeResult<AstConsString*> BinAstDeserializer::DeserializeConsString(int offset) {
  auto has_value = DeserializeUint8(offset);
  offset = has_value.new_offset;

  if (!has_value.value) {
    return {nullptr, offset};
  }

  auto raw_string_count = DeserializeUint32(offset);
  offset = raw_string_count.new_offset;

  AstConsString* cons_string = parser_->ast_value_factory()->NewConsString();

  for (uint32_t i = 0; i < raw_string_count.value; ++i) {
    auto string = DeserializeRawStringReference(offset);
    DCHECK(parser_->zone() != nullptr);
    cons_string->AddString(parser_->zone(), string.value);
    offset = string.new_offset;
  }

  return {cons_string, offset};
}

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeProxyVariableTable(int offset) {
  auto num_proxy_variables = DeserializeUint32(offset);
  offset = num_proxy_variables.new_offset;

  variables_.resize(num_proxy_variables.value);

  offset += num_proxy_variables.value * PROXY_VARIABLE_TABLE_ENTRY_SIZE;

  return {nullptr, offset};
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

inline BinAstDeserializer::DeserializeResult<RawVariableData> BinAstDeserializer::DeserializeGlobalVariableReference(int offset) {
  auto global_variable_index = DeserializeUint32(offset);
  // Note we don't use the next offset, instead jumping to the proper offset in the global variable table.

  DCHECK(global_variable_index.value != 0);
  offset = global_variable_table_base_offset_ + GLOBAL_VARIABLE_TABLE_HEADER_SIZE + GLOBAL_VARIABLE_TABLE_ENTRY_SIZE * (global_variable_index.value - 1);
  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_
  auto index_and_initializer_position = DeserializeUint64(offset);
  offset = index_and_initializer_position.new_offset;

  Uint64TwoFieldConverter<int32_t, int32_t> index_and_initializer_position_convertor;
  index_and_initializer_position_convertor.raw_value = index_and_initializer_position.value;

  int32_t index = index_and_initializer_position_convertor.fields.first;
  int32_t initializer_position = index_and_initializer_position_convertor.fields.second;

  auto bit_field = DeserializeUint16(offset);
  offset = bit_field.new_offset;

  return {{0, nullptr, index, initializer_position, bit_field.value}, global_variable_index.new_offset};
}

inline BinAstDeserializer::DeserializeResult<RawVariableData> BinAstDeserializer::DeserializeProxyVariableReference(int offset) {
  auto local_variable_index = DeserializeVarUint32(offset);
  
  if (local_variable_index.value == 0) {
    return {{0, nullptr, 0, 0, 0}, local_variable_index.new_offset};
  }

  offset = proxy_variable_table_base_offset_ + PROXY_VARIABLE_TABLE_HEADER_SIZE + PROXY_VARIABLE_TABLE_ENTRY_SIZE * (local_variable_index.value - 1);
  bool fixed_size = true;
  auto name = DeserializeRawStringReference(offset, fixed_size);
  offset = name.new_offset;

  auto raw_variable_data = DeserializeGlobalVariableReference(offset);
  offset = raw_variable_data.new_offset;

  auto index = raw_variable_data.value.index;
  auto initializer_position = raw_variable_data.value.initializer_position;
  auto bit_field = raw_variable_data.value.bit_field;
  return {{local_variable_index.value, name.value, index, initializer_position, bit_field}, local_variable_index.new_offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeLocalVariable(int offset, Scope* scope) {
  auto raw_variable = DeserializeProxyVariableReference(offset);
  offset = raw_variable.new_offset;

  if (raw_variable.value.local_index == 0) {
    return {nullptr, offset};
  }

  DCHECK(variables_[raw_variable.value.local_index - 1] == nullptr);

  auto variable_mode = Variable::VariableModeField::decode(raw_variable.value.bit_field);
  if (variable_mode == VariableMode::kTemporary) {
    auto variable = CreateLocalTemporaryVariable(scope, raw_variable.value.name, raw_variable.value.index, raw_variable.value.initializer_position, raw_variable.value.bit_field);
    variables_[raw_variable.value.local_index - 1] = variable;
    return {variable, offset};
  } else {
    auto variable = CreateLocalNonTemporaryVariable(scope, raw_variable.value.name, raw_variable.value.index, raw_variable.value.initializer_position, raw_variable.value.bit_field);
    variables_[raw_variable.value.local_index - 1] = variable;
    return {variable, offset};
  }
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonLocalVariable(int offset, Scope* scope) {
  auto raw_variable = DeserializeProxyVariableReference(offset);
  offset = raw_variable.new_offset;

  if (raw_variable.value.local_index == 0) {
    return {nullptr, offset};
  }

  DCHECK(variables_[raw_variable.value.local_index - 1] == nullptr);

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->variables_.Declare(parser_->zone(), scope, raw_variable.value.name, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic, &was_added);
  variable->index_ = raw_variable.value.index;
  variable->initializer_position_ = raw_variable.value.initializer_position;
  variable->bit_field_ = raw_variable.value.bit_field;
  variables_[raw_variable.value.local_index - 1] = variable;
  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*>
BinAstDeserializer::DeserializeVariableReference(int offset, Scope* scope) {
  // If we discover that we haven't encountered this Variable reference before,
  // we restart from the initial offset.
  auto original_offset = offset;
  auto local_variable_index = DeserializeVarUint32(offset);
  offset = local_variable_index.new_offset;

  if (local_variable_index.value == 0) {
    return {nullptr, offset};
  }

  auto variable = variables_[local_variable_index.value - 1];
  if (variable != nullptr) {
    return {variable, offset};
    
  }

  if (scope == nullptr) {
    return DeserializeNonScopeVariable(original_offset);
  } else {
    return DeserializeNonLocalVariable(original_offset, scope);
  }
}


// This is for Variables that didn't belong to any particular Scope, i.e. their scope_ field was null.
inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonScopeVariable(int offset) {
  auto local_variable_index = DeserializeVarUint32(offset);
  offset = local_variable_index.new_offset;

  auto raw_variable = DeserializeProxyVariableReference(offset);
  offset = raw_variable.new_offset;

  if (raw_variable.value.local_index == 0) {
    return {nullptr, offset};
  }

  DCHECK(variables_[raw_variable.value.local_index - 1] == nullptr);

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  Variable* variable = zone()->New<Variable>(nullptr, raw_variable.value.name, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic);
  variable->index_ = raw_variable.value.index;
  variable->initializer_position_ = raw_variable.value.initializer_position;
  variable->bit_field_ = raw_variable.value.bit_field;
  variables_[raw_variable.value.local_index - 1] = variable;
  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeAstNode(int offset, bool is_toplevel) {
  auto original_offset = offset;

  auto bit_field_and_position = DeserializeUint64(offset);
  offset = bit_field_and_position.new_offset;

  Uint64TwoFieldConverter<uint32_t, int32_t> bit_field_and_position_convertor;
  bit_field_and_position_convertor.raw_value = bit_field_and_position.value;

  uint32_t bit_field = bit_field_and_position_convertor.fields.first;
  int32_t position = bit_field_and_position_convertor.fields.second;

  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field);

  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    BinAstDeserializer::DeserializeResult<uint32_t> start_offset =
        DeserializeUint32(offset);
    offset = start_offset.new_offset;

    BinAstDeserializer::DeserializeResult<uint32_t> length =
        DeserializeUint32(offset);
    offset = length.new_offset;

    if (parser_->scope()->GetClosureScope()->is_skipped_function()) {
      return {nullptr, start_offset.value + length.value};
    }

    auto result = DeserializeFunctionLiteral(bit_field, position, offset);

    if (!is_toplevel) {
      MaybeHandle<PreparseData> preparse_data;
      if (result.value->produced_preparse_data() != nullptr) {
        preparse_data = result.value->produced_preparse_data()->Serialize(isolate_);
      }

      Handle<UncompiledDataWithInnerBinAstParseData> data =
          isolate_->factory()->NewUncompiledDataWithInnerBinAstParseData(
              result.value->GetInferredName(isolate_),
              result.value->start_position(), result.value->end_position(),
              parse_data_, preparse_data, start_offset.value, length.value);
      result.value->set_uncompiled_data_with_inner_bin_ast_parse_data(data);
    }

    return {result.value, result.new_offset};
  }
  case AstNode::kReturnStatement: {
    auto result = DeserializeReturnStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBinaryOperation: {
    auto result = DeserializeBinaryOperation(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kProperty: {
    auto result = DeserializeProperty(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kExpressionStatement: {
    auto result = DeserializeExpressionStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kVariableProxyExpression: {
    auto result = DeserializeVariableProxyExpression(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kLiteral: {
    auto result = DeserializeLiteral(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCall: {
    auto result = DeserializeCall(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCallNew: {
    auto result = DeserializeCallNew(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kIfStatement: {
    auto result = DeserializeIfStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBlock: {
    auto result = DeserializeBlock(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kAssignment: {
    auto result = DeserializeAssignment(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompareOperation: {
    auto result = DeserializeCompareOperation(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kEmptyStatement: {
    auto result = DeserializeEmptyStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForStatement: {
    auto result = DeserializeForStatement(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kForInStatement: {
    auto result = DeserializeForInStatement(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kCountOperation: {
    auto result = DeserializeCountOperation(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompoundAssignment: {
    auto result = DeserializeCompoundAssignment(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kWhileStatement: {
    auto result = DeserializeWhileStatement(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kDoWhileStatement: {
    auto result = DeserializeDoWhileStatement(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kThisExpression: {
    auto result = DeserializeThisExpression(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kUnaryOperation: {
    auto result = DeserializeUnaryOperation(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kObjectLiteral: {
    auto result = DeserializeObjectLiteral(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kArrayLiteral: {
    auto result = DeserializeArrayLiteral(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kNaryOperation: {
    auto result = DeserializeNaryOperation(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kConditional: {
    auto result = DeserializeConditional(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kTryCatchStatement: {
    auto result = DeserializeTryCatchStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kRegExpLiteral: {
    auto result = DeserializeRegExpLiteral(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kSwitchStatement: {
    auto result = DeserializeSwitchStatement(bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kThrow: {
    auto result = DeserializeThrow(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kContinueStatement: {
    auto result = DeserializeContinueStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBreakStatement: {
    auto result = DeserializeBreakStatement(bit_field, position, offset);
    return {result.value, result.new_offset};
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

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeNodeReference(int offset, void** patchable_field) {
  auto node_offset = DeserializeUint32(offset);
  offset = node_offset.new_offset;

  auto result = nodes_by_offset_.find(node_offset.value);
  if (result != nodes_by_offset_.end()) {
    *patchable_field = result->second;
    return {result->second, offset};
  }

  patchable_fields_by_offset_[node_offset.value].push_back(patchable_field);
  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<ReturnStatement*> BinAstDeserializer::DeserializeReturnStatement(uint32_t bit_field, int32_t position, int offset) {
  auto end_position = DeserializeInt32(offset);
  offset = end_position.new_offset;

  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  ReturnStatement* result = parser_->factory()->NewReturnStatement(static_cast<Expression*>(expression.value), position, end_position.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<BinaryOperation*> BinAstDeserializer::DeserializeBinaryOperation(uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(offset);
  offset = right.new_offset;

  Token::Value op = BinaryOperation::OperatorField::decode(bit_field);

  BinaryOperation* result = parser_->factory()->NewBinaryOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Property*> BinAstDeserializer::DeserializeProperty(uint32_t bit_field, int32_t position, int offset) {
  auto obj = DeserializeAstNode(offset);
  offset = obj.new_offset;

  auto key = DeserializeAstNode(offset);
  offset = key.new_offset;

  Property* result = parser_->factory()->NewProperty(static_cast<Expression*>(obj.value), static_cast<Expression*>(key.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<ExpressionStatement*> BinAstDeserializer::DeserializeExpressionStatement(uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  ExpressionStatement* result = parser_->factory()->NewExpressionStatement(static_cast<Expression*>(expression.value), offset);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<VariableProxy*> BinAstDeserializer::DeserializeVariableProxy(int offset, bool add_unresolved) {
  auto position = DeserializeInt32(offset);
  offset = position.new_offset;

  auto bit_field = DeserializeUint32(offset);
  offset = bit_field.new_offset;

  bool is_resolved = VariableProxy::IsResolvedField::decode(bit_field.value);

  VariableProxy* result;
  if (is_resolved) {
    // The resolved Variable should either be a reference (i.e. currently visible in scope) or should be a 
    // NonScope Variable definition (i.e. it's a Variable that is outside the current Scope boundaries, 
    // e.g. inside an eval).
    auto variable = DeserializeVariableReference(offset);
    offset = variable.new_offset;
    result = parser_->factory()->NewVariableProxy(variable.value, position.value);
  } else {
    auto raw_name = DeserializeRawStringReference(offset);
    offset = raw_name.new_offset;
    // We use NORMAL_VARIABLE as a placeholder here.
    result = parser_->factory()->NewVariableProxy(raw_name.value, VariableKind::NORMAL_VARIABLE, position.value);

    if (add_unresolved) {
      parser_->scope()->AddUnresolved(result);
    }
  }
  result->bit_field_ = bit_field.value;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<VariableProxyExpression*> BinAstDeserializer::DeserializeVariableProxyExpression(uint32_t bit_field, int32_t position, int offset) {
  auto variable_proxy = DeserializeVariableProxy(offset);
  offset = variable_proxy.new_offset;

  VariableProxyExpression* result = parser_->factory()->NewVariableProxyExpression(variable_proxy.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Call*> BinAstDeserializer::DeserializeCall(uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(offset);
  offset = params_count.new_offset;

  ScopedPtrList<Expression> params(pointer_buffer());
  params.Reserve(params_count.value);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  bool has_spread = CallBase::SpreadPositionField::decode(bit_field) != CallBase::kNoSpread;

  Call* result = parser_->factory()->NewCall(static_cast<Expression*>(expression.value), params, position, has_spread);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CallNew*> BinAstDeserializer::DeserializeCallNew(uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(offset);
  offset = params_count.new_offset;

  ScopedPtrList<Expression> params(pointer_buffer());
  params.Reserve(params_count.value);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  bool has_spread = CallBase::SpreadPositionField::decode(bit_field) != CallBase::kNoSpread;

  CallNew* result = parser_->factory()->NewCallNew(static_cast<Expression*>(expression.value), params, position, has_spread);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<IfStatement*> BinAstDeserializer::DeserializeIfStatement(uint32_t bit_field, int32_t position, int offset) {
  auto condition = DeserializeAstNode(offset);
  offset = condition.new_offset;

  auto then_statement = DeserializeAstNode(offset);
  offset = then_statement.new_offset;

  auto else_statement = DeserializeAstNode(offset);
  offset = else_statement.new_offset;

  IfStatement* result = parser_->factory()->NewIfStatement(static_cast<Expression*>(condition.value), static_cast<Statement*>(then_statement.value), static_cast<Statement*>(else_statement.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Block*> BinAstDeserializer::DeserializeBlock(uint32_t bit_field, int32_t position, int offset) {
  auto has_scope = DeserializeUint8(offset);
  offset = has_scope.new_offset;

  Scope* scope = nullptr;
  if (has_scope.value) {
    auto scope_result = DeserializeScope(offset);
    offset = scope_result.new_offset;
    scope = scope_result.value;
  }

  auto statement_count = DeserializeInt32(offset);
  offset = statement_count.new_offset;

  ScopedPtrList<Statement> statements(pointer_buffer());
  statements.Reserve(statement_count.value);
  if (scope != nullptr) {
    Parser::BlockState block_state(&parser_->scope_, scope);
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(offset);
      offset = statement.new_offset;
      statements.Add(static_cast<Statement*>(statement.value));
    }
  } else {
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(offset);
      offset = statement.new_offset;
      statements.Add(static_cast<Statement*>(statement.value));
    }
  }

  bool ignore_completion_value = false; // Just a filler value.
  Block* result = parser_->factory()->NewBlock(ignore_completion_value, statements);
  result->bit_field_ = bit_field;
  result->set_scope(scope);

  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Assignment*> BinAstDeserializer::DeserializeAssignment(uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(offset);
  offset = value.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CompareOperation*> BinAstDeserializer::DeserializeCompareOperation(uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(offset);
  offset = right.new_offset;

  Token::Value op = CompareOperation::OperatorField::decode(bit_field);
  CompareOperation* result = parser_->factory()->NewCompareOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<EmptyStatement*> BinAstDeserializer::DeserializeEmptyStatement(uint32_t bit_field, int32_t position, int offset) {
  EmptyStatement* result = parser_->factory()->EmptyStatement();
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeMaybeAstNode(int offset) {
  auto has_node = DeserializeUint8(offset);
  offset = has_node.new_offset;

  if (has_node.value) {
    auto node = DeserializeAstNode(offset);
    offset = node.new_offset;
    return {node.value, offset};
  }
  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<ForStatement*> BinAstDeserializer::DeserializeForStatement(uint32_t bit_field, int32_t position, int offset) {
  auto init_node = DeserializeMaybeAstNode(offset);
  offset = init_node.new_offset;

  auto cond_node = DeserializeMaybeAstNode(offset);
  offset = cond_node.new_offset;

  auto next_node = DeserializeMaybeAstNode(offset);
  offset = next_node.new_offset;

  auto body = DeserializeAstNode(offset);
  offset = body.new_offset;

  ForStatement* result = parser_->factory()->NewForStatement(position);
  result->Initialize(
    static_cast<Statement*>(init_node.value),
    static_cast<Expression*>(cond_node.value),
    static_cast<Statement*>(next_node.value),
    static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<ForInStatement*> BinAstDeserializer::DeserializeForInStatement(uint32_t bit_field, int32_t position, int offset) {
  auto each = DeserializeAstNode(offset);
  offset = each.new_offset;

  auto subject = DeserializeAstNode(offset);
  offset = subject.new_offset;

  auto body = DeserializeAstNode(offset);
  offset = body.new_offset;

  ForEachStatement* result = parser_->factory()->NewForEachStatement(ForEachStatement::ENUMERATE, position);
  result->Initialize(static_cast<Expression*>(each.value), static_cast<Expression*>(subject.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  DCHECK(result->IsForInStatement());
  return {static_cast<ForInStatement*>(result), offset};
}

inline BinAstDeserializer::DeserializeResult<WhileStatement*> BinAstDeserializer::DeserializeWhileStatement(uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(offset);
  offset = body.new_offset;

  WhileStatement* result = parser_->factory()->NewWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<DoWhileStatement*> BinAstDeserializer::DeserializeDoWhileStatement(uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(offset);
  offset = body.new_offset;

  DoWhileStatement* result = parser_->factory()->NewDoWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CountOperation*> BinAstDeserializer::DeserializeCountOperation(uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  Token::Value op = CountOperation::TokenField::decode(bit_field);
  bool is_prefix = CountOperation::IsPrefixField::decode(bit_field);

  CountOperation* result = parser_->factory()->NewCountOperation(op, is_prefix, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CompoundAssignment*> BinAstDeserializer::DeserializeCompoundAssignment(uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(offset);
  offset = value.new_offset;

  auto binary_operation = DeserializeAstNode(offset);
  offset = binary_operation.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result->AsCompoundAssignment(), offset};
}

inline BinAstDeserializer::DeserializeResult<UnaryOperation*> BinAstDeserializer::DeserializeUnaryOperation(uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(offset);
  offset = expression.new_offset;

  Token::Value op = UnaryOperation::OperatorField::decode(bit_field);
  UnaryOperation* result = parser_->factory()->NewUnaryOperation(op, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<ThisExpression*> BinAstDeserializer::DeserializeThisExpression(uint32_t bit_field, int32_t position, int offset) {
  ThisExpression* result = parser_->factory()->ThisExpression();
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Literal*> BinAstDeserializer::DeserializeLiteral(uint32_t bit_field, int32_t position, int offset) {
  Literal::Type type = Literal::TypeField::decode(bit_field);

  Literal* result;
  switch (type) {
    case Literal::kSmi: {
      auto smi = DeserializeInt32(offset);
      offset = smi.new_offset;
      result = parser_->factory()->NewSmiLiteral(smi.value, position);
      break;
    }
    case Literal::kHeapNumber: {
      auto number = DeserializeDouble(offset);
      offset = number.new_offset;
      result = parser_->factory()->NewNumberLiteral(number.value, position);
      break;
    }
    case Literal::kBigInt: {
      auto bigint_str = DeserializeCString(offset);
      offset = bigint_str.new_offset;
      result = parser_->factory()->NewBigIntLiteral(AstBigInt(bigint_str.value), position);
      break;
    }
    case Literal::kString: {
      auto string = DeserializeRawStringReference(offset);
      offset = string.new_offset;
      result = parser_->factory()->NewStringLiteral(string.value, position);
      break;
    }
    case Literal::kBoolean: {
      auto boolean = DeserializeUint8(offset);
      offset = boolean.new_offset;
      result = parser_->factory()->NewBooleanLiteral(boolean.value, position);
      break;
    }
    case Literal::kUndefined: {
      result = parser_->factory()->NewUndefinedLiteral(position);
      break;
    }
    case Literal::kNull: {
      result = parser_->factory()->NewNullLiteral(position);
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

  return {result, offset};
}


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_INL_H_

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
inline BinAstDeserializer::DeserializeResult<uint64_t> BinAstDeserializer::DeserializeUint64(uint8_t* bytes, int offset) {
  uint64_t result = 0;
  for (int i = 0; i < 8; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint64_t unshifted_value = bytes[offset + i];
    uint64_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(uint64_t)};
}

inline BinAstDeserializer::DeserializeResult<uint32_t> BinAstDeserializer::DeserializeUint32(uint8_t* bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes[offset + i];
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(uint32_t)};
}

inline BinAstDeserializer::DeserializeResult<uint32_t> BinAstDeserializer::DeserializeVarUint32(uint8_t* bytes, int offset) {
  int i = 0;
  uint32_t result = 0;
  while (true) {
    DCHECK(i < 4);
    uint32_t current_byte = bytes[offset + i];
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

inline BinAstDeserializer::DeserializeResult<uint16_t> BinAstDeserializer::DeserializeUint16(uint8_t* bytes, int offset) {
  uint16_t result = 0;
  for (int i = 0; i < 2; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes[offset + i];
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(uint16_t)};
}

inline BinAstDeserializer::DeserializeResult<std::array<bool, 16>> BinAstDeserializer::DeserializeUint16Flags(uint8_t* bytes, int offset) {
  std::array<bool, 16> flags;
  auto encoded_flags_result = DeserializeUint16(bytes, offset);
  offset = encoded_flags_result.new_offset;
  uint16_t encoded_flags = encoded_flags_result.value;
  for (size_t i = 0; i < flags.size(); ++i) {
    auto shift = flags.size() - i - 1;
    flags[i] = (encoded_flags >> shift) & 0x1;
  }
  return {flags, offset};
}

inline BinAstDeserializer::DeserializeResult<uint8_t> BinAstDeserializer::DeserializeUint8(uint8_t* bytes, int offset) {
  return {bytes[offset], offset + sizeof(uint8_t)};
}

inline BinAstDeserializer::DeserializeResult<int32_t> BinAstDeserializer::DeserializeInt32(uint8_t* bytes, int offset) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    size_t shift = sizeof(uint8_t) * 8 * i;
    uint32_t unshifted_value = bytes[offset + i];
    uint32_t shifted_value = unshifted_value << shift;
    result |= shifted_value;
  }
  return {result, offset + sizeof(int32_t)};
}

inline BinAstDeserializer::DeserializeResult<double> BinAstDeserializer::DeserializeDouble(uint8_t* bytes, int offset) {
  union {
    double d;
    uint64_t ui;
  } converter;

  auto result = DeserializeUint64(bytes, offset);
  offset = result.new_offset;
  converter.ui = result.value;
  return {converter.d, offset};
}

inline BinAstDeserializer::DeserializeResult<const char*> BinAstDeserializer::DeserializeCString(uint8_t* bytes, int offset) {
  std::vector<char> characters;
  for (int i = 0; ; ++i) {
    auto next_char = DeserializeUint8(bytes, offset);
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

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawString(uint8_t* serialized_ast, int offset) {
  auto original_offset = offset;

  auto is_one_byte = DeserializeUint8(serialized_ast, offset);
  offset = is_one_byte.new_offset;

  auto hash_field = DeserializeUint32(serialized_ast, offset);
  offset = hash_field.new_offset;

  auto length = DeserializeUint32(serialized_ast, offset);
  offset = length.new_offset;

  std::unique_ptr<uint8_t[]> raw_data(new uint8_t[length.value]);
  memcpy(raw_data.get(), &serialized_ast[offset], length.value);
  offset += sizeof(uint8_t) * length.value;
  const AstRawString* s = nullptr;
  if (length.value > 0) {
    Vector<const byte> literal_bytes(raw_data.get(), length.value);
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  } else {
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  }
  DCHECK(strings_by_offset_.count(original_offset) == 0);
  strings_by_offset_[original_offset] = s;
  return {s, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeProxyString(uint8_t* serialized_ast, int offset) {
  auto raw_offset = DeserializeUint32(serialized_ast, offset);
  offset = raw_offset.new_offset;

  {
    auto result = strings_by_offset_.find(raw_offset.value);
    if (result != strings_by_offset_.end()) {
      return {result->second, offset};
    }
  }

  auto result = DeserializeRawString(serialized_ast, raw_offset.value);
  return {result.value, offset};
} 

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeStringTable(uint8_t* serialized_ast, int offset) {
  auto end_offset = DeserializeUint32(serialized_ast, offset);
  return {nullptr, end_offset.value};
}

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeProxyStringTable(uint8_t* serialized_ast, int offset) {
  auto num_proxy_strings = DeserializeUint32(serialized_ast, offset);
  offset = num_proxy_strings.new_offset;

  strings_by_offset_.reserve(num_proxy_strings.value);

  for (uint32_t i = 0; i < num_proxy_strings.value; ++i) {
    auto string = DeserializeProxyString(serialized_ast, offset);
    offset = string.new_offset;
  }

  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawStringReference(uint8_t* serialized_ast, int offset) {
  auto string_table_offset = DeserializeVarUint32(serialized_ast, offset);
  offset = string_table_offset.new_offset;
  if (string_table_offset.value == 0) {
    return {nullptr, offset};
  }

  DCHECK(strings_by_offset_.count(string_table_offset.value) > 0);
  const AstRawString* result = strings_by_offset_[string_table_offset.value];
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<AstConsString*> BinAstDeserializer::DeserializeConsString(uint8_t* serialized_ast, int offset) {
  auto raw_string_count = DeserializeUint32(serialized_ast, offset);
  offset = raw_string_count.new_offset;

  if (raw_string_count.value == 0) {
    return {nullptr, offset};
  }

  AstConsString* cons_string = parser_->ast_value_factory()->NewConsString();

  for (uint32_t i = 0; i < raw_string_count.value; ++i) {
    auto string = DeserializeRawStringReference(serialized_ast, offset);
    DCHECK(parser_->zone() != nullptr);
    cons_string->AddString(parser_->zone(), string.value);
    offset = string.new_offset;
  }

  return {cons_string, offset};
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

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_
  auto index_and_initializer_position = DeserializeUint64(serialized_binast, offset);
  offset = index_and_initializer_position.new_offset;

  Uint64TwoFieldConverter<int32_t, int32_t> index_and_initializer_position_convertor;
  index_and_initializer_position_convertor.raw_value = index_and_initializer_position.value;

  int32_t index = index_and_initializer_position_convertor.fields.first;
  int32_t initializer_position = index_and_initializer_position_convertor.fields.second;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  auto variable_mode = Variable::VariableModeField::decode(bit_field.value);
  if (variable_mode == VariableMode::kTemporary) {
    auto variable = CreateLocalTemporaryVariable(scope, name.value, index, initializer_position, bit_field.value);
    return {variable, offset};
  } else {
    auto variable = CreateLocalNonTemporaryVariable(scope, name.value, index, initializer_position, bit_field.value);
    return {variable, offset};
  }
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  if (name.value == nullptr) {
    return {nullptr, offset};
  }

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_
  auto index_and_initializer_position = DeserializeUint64(serialized_binast, offset);
  offset = index_and_initializer_position.new_offset;

  Uint64TwoFieldConverter<int32_t, int32_t> index_and_initializer_position_convertor;
  index_and_initializer_position_convertor.raw_value = index_and_initializer_position.value;

  int32_t index = index_and_initializer_position_convertor.fields.first;
  int32_t initializer_position = index_and_initializer_position_convertor.fields.second;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->variables_.Declare(parser_->zone(), scope, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic, &was_added);
  variable->index_ = index;
  variable->initializer_position_ = initializer_position;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*>
BinAstDeserializer::DeserializeVariableReference(uint8_t* serialized_binast,
                                                 int offset, Scope* scope) {
  auto variable_reference = DeserializeVarUint32(serialized_binast, offset);
  offset = variable_reference.new_offset;

  if (variable_reference.value == 0) {
    return {nullptr, offset};
  }

  auto variable_result = variables_by_id_.find(variable_reference.value);
  DCHECK(variable_result != variables_by_id_.end() || scope);

  if (variable_result != variables_by_id_.end()) {
    return {variable_result->second, offset};
  } else {
    // When using an offset to deserialize inner functions, we may encounter variable references that are serialized earlier in the data.
    // To deal with this noncontiguous data, jump there to deserialize the variable, but do not follow the resulting offset.
    return {
      DeserializeScopeVariable(serialized_binast, variable_reference.value, scope).value,
      offset
    };
  }
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeScopeVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto original_offset = offset;
  auto variable_result = DeserializeNonLocalVariable(serialized_binast, offset, scope);
  offset = variable_result.new_offset;
  
  Variable* variable = variable_result.value;
  if (variable == nullptr) {
    return {nullptr, offset};
  }
  variables_by_id_.insert({original_offset, variable});
  return {variable, offset};
}

// This is for Variables that didn't belong to any particular Scope, i.e. their scope_ field was null.
inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonScopeVariable(uint8_t* serialized_binast, int offset) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  if (name.value == nullptr) {
    return {nullptr, offset};
  }

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_

  auto index = DeserializeInt32(serialized_binast, offset);
  offset = index.new_offset;

  auto initializer_position = DeserializeInt32(serialized_binast, offset);
  offset = initializer_position.new_offset;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  Variable* variable = new (zone()) Variable(nullptr, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}


inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeScopeVariableOrReference(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto marker_result = DeserializeUint8(serialized_binast, offset);
  offset = marker_result.new_offset;

  switch (marker_result.value) {
    case ScopeVariableKind::Null: {
      return {nullptr, offset};
    }
    case ScopeVariableKind::Definition: {
      auto scope_result = DeserializeScopeVariable(serialized_binast, offset, scope);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    case ScopeVariableKind::Reference: {
      auto scope_result = DeserializeVariableReference(serialized_binast, offset, scope);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    default: {
      UNREACHABLE();
    }
  }
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonScopeVariableOrReference(uint8_t* serialized_binast, int offset) {
  auto marker_result = DeserializeUint8(serialized_binast, offset);
  offset = marker_result.new_offset;

  switch (marker_result.value) {
    case ScopeVariableKind::Null: {
      return {nullptr, offset};
    }
    case ScopeVariableKind::Definition: {
      auto scope_result = DeserializeNonScopeVariable(serialized_binast, offset);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    case ScopeVariableKind::Reference: {
      auto scope_result = DeserializeVariableReference(serialized_binast, offset);
      offset = scope_result.new_offset;
      return {scope_result.value, offset};
    }
    default: {
      UNREACHABLE();
    }
  }
}

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeAstNode(uint8_t* serialized_binast, int offset, bool is_toplevel) {
  auto original_offset = offset;

  auto bit_field_and_position = DeserializeUint64(serialized_binast, offset);
  offset = bit_field_and_position.new_offset;

  Uint64TwoFieldConverter<uint32_t, int32_t> bit_field_and_position_convertor;
  bit_field_and_position_convertor.raw_value = bit_field_and_position.value;

  uint32_t bit_field = bit_field_and_position_convertor.fields.first;
  int32_t position = bit_field_and_position_convertor.fields.second;

  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field);

  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    BinAstDeserializer::DeserializeResult<uint32_t> start_offset =
        DeserializeUint32(serialized_binast, offset);
    offset = start_offset.new_offset;

    BinAstDeserializer::DeserializeResult<uint32_t> length =
        DeserializeUint32(serialized_binast, offset);
    offset = length.new_offset;

    auto result = DeserializeFunctionLiteral(serialized_binast, bit_field, position, offset);

    if (!is_toplevel) {
      Handle<PreparseData> preparse_data;
      if (result.value->produced_preparse_data() != nullptr) {
        preparse_data = result.value->produced_preparse_data()->Serialize(isolate_);
      }

      Handle<UncompiledDataWithInnerBinAstParseData> data =
          isolate_->factory()->NewUncompiledDataWithInnerBinAstParseData(
              result.value->GetInferredName(isolate_),
              result.value->start_position(), result.value->end_position(),
              parse_data_, start_offset.value, length.value,
              preparse_data);
      result.value->set_uncompiled_data_with_inner_bin_ast_parse_data(data);
    }

    return {result.value, result.new_offset};
  }
  case AstNode::kReturnStatement: {
    auto result = DeserializeReturnStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBinaryOperation: {
    auto result = DeserializeBinaryOperation(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kProperty: {
    auto result = DeserializeProperty(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kExpressionStatement: {
    auto result = DeserializeExpressionStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kVariableProxyExpression: {
    auto result = DeserializeVariableProxyExpression(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kLiteral: {
    auto result = DeserializeLiteral(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCall: {
    auto result = DeserializeCall(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCallNew: {
    auto result = DeserializeCallNew(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kIfStatement: {
    auto result = DeserializeIfStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBlock: {
    auto result = DeserializeBlock(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kAssignment: {
    auto result = DeserializeAssignment(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompareOperation: {
    auto result = DeserializeCompareOperation(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kEmptyStatement: {
    auto result = DeserializeEmptyStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForStatement: {
    auto result = DeserializeForStatement(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kForInStatement: {
    auto result = DeserializeForInStatement(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kCountOperation: {
    auto result = DeserializeCountOperation(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompoundAssignment: {
    auto result = DeserializeCompoundAssignment(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kWhileStatement: {
    auto result = DeserializeWhileStatement(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kDoWhileStatement: {
    auto result = DeserializeDoWhileStatement(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kThisExpression: {
    auto result = DeserializeThisExpression(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kUnaryOperation: {
    auto result = DeserializeUnaryOperation(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kObjectLiteral: {
    auto result = DeserializeObjectLiteral(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kArrayLiteral: {
    auto result = DeserializeArrayLiteral(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kNaryOperation: {
    auto result = DeserializeNaryOperation(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kConditional: {
    auto result = DeserializeConditional(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kTryCatchStatement: {
    auto result = DeserializeTryCatchStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kRegExpLiteral: {
    auto result = DeserializeRegExpLiteral(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kSwitchStatement: {
    auto result = DeserializeSwitchStatement(serialized_binast, bit_field, position, offset);
    RecordBreakableStatement(original_offset, result.value);
    return {result.value, result.new_offset};
  }
  case AstNode::kThrow: {
    auto result = DeserializeThrow(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kContinueStatement: {
    auto result = DeserializeContinueStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBreakStatement: {
    auto result = DeserializeBreakStatement(serialized_binast, bit_field, position, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForOfStatement:
  case AstNode::kSloppyBlockFunctionStatement:
  case AstNode::kTryFinallyStatement:
  case AstNode::kDebuggerStatement:
  case AstNode::kInitializeClassMembersStatement:
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

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeNodeReference(uint8_t* bytes, int offset, void** patchable_field) {
  auto node_offset = DeserializeUint32(bytes, offset);
  offset = node_offset.new_offset;

  auto result = nodes_by_offset_.find(node_offset.value);
  if (result != nodes_by_offset_.end()) {
    *patchable_field = result->second;
    return {result->second, offset};
  }

  patchable_fields_by_offset_[node_offset.value].push_back(patchable_field);
  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<ReturnStatement*> BinAstDeserializer::DeserializeReturnStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto end_position = DeserializeInt32(serialized_binast, offset);
  offset = end_position.new_offset;

  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ReturnStatement* result = parser_->factory()->NewReturnStatement(static_cast<Expression*>(expression.value), position, end_position.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<BinaryOperation*> BinAstDeserializer::DeserializeBinaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(serialized_binast, offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(serialized_binast, offset);
  offset = right.new_offset;

  Token::Value op = BinaryOperation::OperatorField::decode(bit_field);

  BinaryOperation* result = parser_->factory()->NewBinaryOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Property*> BinAstDeserializer::DeserializeProperty(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto obj = DeserializeAstNode(serialized_binast, offset);
  offset = obj.new_offset;

  auto key = DeserializeAstNode(serialized_binast, offset);
  offset = key.new_offset;

  Property* result = parser_->factory()->NewProperty(static_cast<Expression*>(obj.value), static_cast<Expression*>(key.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<ExpressionStatement*> BinAstDeserializer::DeserializeExpressionStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ExpressionStatement* result = parser_->factory()->NewExpressionStatement(static_cast<Expression*>(expression.value), offset);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<VariableProxy*> BinAstDeserializer::DeserializeVariableProxy(uint8_t* serialized_binast, int offset, bool add_unresolved) {
  auto position = DeserializeInt32(serialized_binast, offset);
  offset = position.new_offset;

  auto bit_field = DeserializeUint32(serialized_binast, offset);
  offset = bit_field.new_offset;

  bool is_resolved = VariableProxy::IsResolvedField::decode(bit_field.value);

  VariableProxy* result;
  if (is_resolved) {
    // The resolved Variable should either be a reference (i.e. currently visible in scope) or should be a 
    // NonScope Variable definition (i.e. it's a Variable that is outside the current Scope boundaries, 
    // e.g. inside an eval).
    auto variable = DeserializeNonScopeVariableOrReference(serialized_binast, offset);
    offset = variable.new_offset;
    result = parser_->factory()->NewVariableProxy(variable.value, position.value);
  } else {
    auto raw_name = DeserializeRawStringReference(serialized_binast, offset);
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

inline BinAstDeserializer::DeserializeResult<VariableProxyExpression*> BinAstDeserializer::DeserializeVariableProxyExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto variable_proxy = DeserializeVariableProxy(serialized_binast, offset);
  offset = variable_proxy.new_offset;

  VariableProxyExpression* result = parser_->factory()->NewVariableProxyExpression(variable_proxy.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Call*> BinAstDeserializer::DeserializeCall(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(serialized_binast, offset);
  offset = params_count.new_offset;

  std::vector<void*> pointer_buffer;
  pointer_buffer.reserve(params_count.value);
  ScopedPtrList<Expression> params(&pointer_buffer);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(serialized_binast, offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  Call* result = parser_->factory()->NewCall(static_cast<Expression*>(expression.value), params, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CallNew*> BinAstDeserializer::DeserializeCallNew(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(serialized_binast, offset);
  offset = params_count.new_offset;

  std::vector<void*> pointer_buffer;
  pointer_buffer.reserve(params_count.value);
  ScopedPtrList<Expression> params(&pointer_buffer);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(serialized_binast, offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  CallNew* result = parser_->factory()->NewCallNew(static_cast<Expression*>(expression.value), params, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<IfStatement*> BinAstDeserializer::DeserializeIfStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto condition = DeserializeAstNode(serialized_binast, offset);
  offset = condition.new_offset;

  auto then_statement = DeserializeAstNode(serialized_binast, offset);
  offset = then_statement.new_offset;

  auto else_statement = DeserializeAstNode(serialized_binast, offset);
  offset = else_statement.new_offset;

  IfStatement* result = parser_->factory()->NewIfStatement(static_cast<Expression*>(condition.value), static_cast<Statement*>(then_statement.value), static_cast<Statement*>(else_statement.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Block*> BinAstDeserializer::DeserializeBlock(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto has_scope = DeserializeUint8(serialized_binast, offset);
  offset = has_scope.new_offset;

  Scope* scope = nullptr;
  if (has_scope.value) {
    auto scope_result = DeserializeScope(serialized_binast, offset);
    offset = scope_result.new_offset;
    scope = scope_result.value;
  }

  auto statement_count = DeserializeInt32(serialized_binast, offset);
  offset = statement_count.new_offset;

  std::vector<void*> pointer_buffer;
  pointer_buffer.reserve(statement_count.value);
  ScopedPtrList<Statement> statements(&pointer_buffer);
  if (scope != nullptr) {
    Parser::BlockState block_state(&parser_->scope_, scope);
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;
      statements.Add(static_cast<Statement*>(statement.value));
    }
  } else {
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
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

inline BinAstDeserializer::DeserializeResult<Assignment*> BinAstDeserializer::DeserializeAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(serialized_binast, offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(serialized_binast, offset);
  offset = value.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CompareOperation*> BinAstDeserializer::DeserializeCompareOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(serialized_binast, offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(serialized_binast, offset);
  offset = right.new_offset;

  Token::Value op = CompareOperation::OperatorField::decode(bit_field);
  CompareOperation* result = parser_->factory()->NewCompareOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<EmptyStatement*> BinAstDeserializer::DeserializeEmptyStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  EmptyStatement* result = parser_->factory()->EmptyStatement();
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeMaybeAstNode(uint8_t* serialized_binast, int offset) {
  auto has_node = DeserializeUint8(serialized_binast, offset);
  offset = has_node.new_offset;

  if (has_node.value) {
    auto node = DeserializeAstNode(serialized_binast, offset);
    offset = node.new_offset;
    return {node.value, offset};
  }
  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<ForStatement*> BinAstDeserializer::DeserializeForStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto init_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = init_node.new_offset;

  auto cond_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = cond_node.new_offset;

  auto next_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = next_node.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
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

inline BinAstDeserializer::DeserializeResult<ForInStatement*> BinAstDeserializer::DeserializeForInStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto each = DeserializeAstNode(serialized_binast, offset);
  offset = each.new_offset;

  auto subject = DeserializeAstNode(serialized_binast, offset);
  offset = subject.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  ForEachStatement* result = parser_->factory()->NewForEachStatement(ForEachStatement::ENUMERATE, position);
  result->Initialize(static_cast<Expression*>(each.value), static_cast<Expression*>(subject.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  DCHECK(result->IsForInStatement());
  return {static_cast<ForInStatement*>(result), offset};
}

inline BinAstDeserializer::DeserializeResult<WhileStatement*> BinAstDeserializer::DeserializeWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(serialized_binast, offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  WhileStatement* result = parser_->factory()->NewWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<DoWhileStatement*> BinAstDeserializer::DeserializeDoWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(serialized_binast, offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  DoWhileStatement* result = parser_->factory()->NewDoWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CountOperation*> BinAstDeserializer::DeserializeCountOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  Token::Value op = CountOperation::TokenField::decode(bit_field);
  bool is_prefix = CountOperation::IsPrefixField::decode(bit_field);

  CountOperation* result = parser_->factory()->NewCountOperation(op, is_prefix, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<CompoundAssignment*> BinAstDeserializer::DeserializeCompoundAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(serialized_binast, offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(serialized_binast, offset);
  offset = value.new_offset;

  auto binary_operation = DeserializeAstNode(serialized_binast, offset);
  offset = binary_operation.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result->AsCompoundAssignment(), offset};
}

inline BinAstDeserializer::DeserializeResult<UnaryOperation*> BinAstDeserializer::DeserializeUnaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  Token::Value op = UnaryOperation::OperatorField::decode(bit_field);
  UnaryOperation* result = parser_->factory()->NewUnaryOperation(op, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ThisExpression*> BinAstDeserializer::DeserializeThisExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  ThisExpression* result = parser_->factory()->ThisExpression();
  result->bit_field_ = bit_field;
  return {result, offset};
}

inline BinAstDeserializer::DeserializeResult<Literal*> BinAstDeserializer::DeserializeLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  Literal::Type type = Literal::TypeField::decode(bit_field);

  Literal* result;
  switch (type) {
    case Literal::kSmi: {
      auto smi = DeserializeInt32(serialized_binast, offset);
      offset = smi.new_offset;
      result = parser_->factory()->NewSmiLiteral(smi.value, position);
      break;
    }
    case Literal::kHeapNumber: {
      auto number = DeserializeDouble(serialized_binast, offset);
      offset = number.new_offset;
      result = parser_->factory()->NewNumberLiteral(number.value, position);
      break;
    }
    case Literal::kBigInt: {
      auto bigint_str = DeserializeCString(serialized_binast, offset);
      offset = bigint_str.new_offset;
      result = parser_->factory()->NewBigIntLiteral(AstBigInt(bigint_str.value), position);
      break;
    }
    case Literal::kString: {
      auto string = DeserializeRawStringReference(serialized_binast, offset);
      offset = string.new_offset;
      result = parser_->factory()->NewStringLiteral(string.value, position);
      break;
    }
    case Literal::kSymbol: {
      auto symbol = DeserializeUint8(serialized_binast, offset);
      offset = symbol.new_offset;
      result = parser_->factory()->NewSymbolLiteral(static_cast<AstSymbol>(symbol.value), position);
      break;
    }
    case Literal::kBoolean: {
      auto boolean = DeserializeUint8(serialized_binast, offset);
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
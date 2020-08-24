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
  auto is_one_byte = DeserializeUint8(serialized_ast, offset);
  offset = is_one_byte.new_offset;

  auto hash_field = DeserializeUint32(serialized_ast, offset);
  offset = hash_field.new_offset;

  auto length = DeserializeUint32(serialized_ast, offset);
  offset = length.new_offset;

  std::vector<uint8_t> raw_data;
  for (uint32_t i = 0; i < length.value; ++i) {
    auto next_byte = DeserializeUint8(serialized_ast, offset);
    offset = next_byte.new_offset;
    raw_data.push_back(next_byte.value);
  }
  const AstRawString* s = nullptr;
  if (raw_data.size() > 0) {
    Vector<const byte> literal_bytes(&raw_data[0], raw_data.size());
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  } else {
    Vector<const byte> literal_bytes;
    s = parser_->ast_value_factory()->GetString(hash_field.value, is_one_byte.value, literal_bytes);
  }
  string_table_.insert({string_table_.size() + 1, s});
  return {s, offset};
}

inline BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeStringTable(uint8_t* serialized_ast, int offset) {
  auto num_non_constant_entries = DeserializeUint32(serialized_ast, offset);
  offset = num_non_constant_entries.new_offset;

  for (uint32_t i = 0; i < num_non_constant_entries.value; ++i) {
    auto string = DeserializeRawString(serialized_ast, offset);
    offset = string.new_offset;
  }

  for (AstRawStringMap::Entry* entry = parser_->ast_value_factory()->string_constants_->string_table()->Start(); entry != nullptr; entry = parser_->ast_value_factory()->string_constants_->string_table()->Next(entry)) {
    string_table_.insert({string_table_.size() + 1, entry->key});
  }

  return {nullptr, offset};
}

inline BinAstDeserializer::DeserializeResult<const AstRawString*> BinAstDeserializer::DeserializeRawStringReference(uint8_t* serialized_ast, int offset) {
  auto string_table_index = DeserializeVarUint32(serialized_ast, offset);
  offset = string_table_index.new_offset;
  if (string_table_index.value == 0) {
    return {nullptr, offset};
  }
  auto lookup_result = string_table_.find(string_table_index.value);
  DCHECK(lookup_result != string_table_.end());
  const AstRawString* result = lookup_result->second;
  DCHECK(result != nullptr);
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

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto name = DeserializeRawStringReference(serialized_binast, offset);
  offset = name.new_offset;

  // local_if_not_shadowed_: TODO(binast): how to reference other local variables like this? index?
  // next_

  auto index = DeserializeInt32(serialized_binast, offset);
  offset = index.new_offset;

  auto initializer_position = DeserializeInt32(serialized_binast, offset);
  offset = initializer_position.new_offset;

  auto bit_field = DeserializeUint16(serialized_binast, offset);
  offset = bit_field.new_offset;

  // We just use bogus values for mode, etc. since they're already encoded in the bit field
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->Declare(parser_->zone(), name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, &was_added);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeNonLocalVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
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
  bool was_added = false;
  // The main difference between local and non-local is whether the Variable appeared in the locals_ list when the Scope was serialized.
  Variable* variable = scope->variables_.Declare(parser_->zone(), scope, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic, &was_added);
  variable->index_ = index.value;
  variable->initializer_position_ = initializer_position.value;
  variable->bit_field_ = bit_field.value;
  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeVariableReference(uint8_t* serialized_binast, int offset) {
  auto variable_reference = DeserializeVarUint32(serialized_binast, offset);
  offset = variable_reference.new_offset;

  if (variable_reference.value == 0) {
    return {nullptr, offset};
  }

  auto variable_result = variables_by_id_.find(variable_reference.value);
  DCHECK(variable_result != variables_by_id_.end());
  Variable* variable = variable_result->second;

  return {variable, offset};
}

inline BinAstDeserializer::DeserializeResult<Variable*> BinAstDeserializer::DeserializeScopeVariable(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto variable_result = DeserializeNonLocalVariable(serialized_binast, offset, scope);
  offset = variable_result.new_offset;
  
  Variable* variable = variable_result.value;
  if (variable == nullptr) {
    return {nullptr, offset};
  }
  variables_by_id_.insert({variables_by_id_.size() + 1, variable});
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
  Variable* variable = zone()->New<Variable>(nullptr, name.value, VariableMode::kVar, NORMAL_VARIABLE, kCreatedInitialized, kMaybeAssigned, IsStaticFlag::kNotStatic);
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
      auto scope_result = DeserializeVariableReference(serialized_binast, offset);
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
    case Literal::kBoolean: {
      auto boolean = DeserializeUint8(serialized_binast, position);
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
  DCHECK(result->bit_field_ == bit_field);

  return {result, offset};
}


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_DESERIALIZER_INL_H_

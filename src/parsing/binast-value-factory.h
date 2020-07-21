// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_VALUE_FACTORY_H_
#define V8_PARSING_BINAST_VALUE_FACTORY_H_

#include "src/zone/zone.h"
#include "src/ast/ast.h"
#include "src/strings/string-hasher.h"
#include "src/strings/string-hasher-inl.h"

namespace v8 {
namespace internal {

class BinAstValueFactory {
 public:
  BinAstValueFactory(Zone* zone, const AstStringConstants* string_constants)
    : string_constants_(string_constants),
      string_table_(string_constants->string_table()),
      strings_(nullptr),
      strings_end_(&strings_),
      empty_cons_string_(nullptr),
      zone_(zone),
      hash_seed_(0)  // TODO(binast)
  {
    DCHECK_NOT_NULL(zone_);
    // DCHECK_EQ(hash_seed, string_constants->hash_seed());
    std::fill(one_character_strings_,
              one_character_strings_ + arraysize(one_character_strings_),
            nullptr);
    empty_cons_string_ = NewConsString();
  }

  Zone* zone() const {
    DCHECK_NOT_NULL(zone_);
    return zone_;
  }

  const AstRawString* GetOneByteString(Vector<const uint8_t> literal) {
    return GetOneByteStringInternal(literal);
  }
  const AstRawString* GetOneByteString(const char* string) {
    return GetOneByteString(OneByteVector(string));
  }
  const AstRawString* GetTwoByteString(Vector<const uint16_t> literal) {
    return GetTwoByteStringInternal(literal);
  }

  const AstRawString* GetOneByteStringInternal(Vector<const uint8_t> literal) {
    if (literal.length() == 1 && literal[0] < kMaxOneCharStringValue) {
      int key = literal[0];
      if (V8_UNLIKELY(one_character_strings_[key] == nullptr)) {
        uint32_t hash_field = StringHasher::HashSequentialString<uint8_t>(
            literal.begin(), literal.length(), hash_seed_);
        one_character_strings_[key] = GetString(hash_field, true, literal);
      }
      return one_character_strings_[key];
    }
    uint32_t hash_field = StringHasher::HashSequentialString<uint8_t>(
        literal.begin(), literal.length(), hash_seed_);
    return GetString(hash_field, true, literal);
  }

  const AstRawString* GetTwoByteStringInternal(
      Vector<const uint16_t> literal) {
    uint32_t hash_field = StringHasher::HashSequentialString<uint16_t>(
        literal.begin(), literal.length(), hash_seed_);
    return GetString(hash_field, false, Vector<const byte>::cast(literal));
  }

  const AstRawString* GetString(uint32_t raw_hash_field, bool is_one_byte,
                                         Vector<const byte> literal_bytes) {
    // literal_bytes here points to whatever the user passed, and this is OK
    // because we use vector_compare (which checks the contents) to compare
    // against the AstRawStrings which are in the string_table_. We should not
    // return this AstRawString.
    AstRawString key(is_one_byte, literal_bytes, raw_hash_field);
    AstRawStringMap::Entry* entry = string_table_.LookupOrInsert(
      &key, key.Hash(),
      [&]() {
        // Copy literal contents for later comparison.
        int length = literal_bytes.length();
        byte* new_literal_bytes = zone()->NewArray<byte>(length);
        base::Memcpy(new_literal_bytes, literal_bytes.begin(), length);
        AstRawString* new_string = zone()->New<AstRawString>(
            is_one_byte, Vector<const byte>(new_literal_bytes, length),
            raw_hash_field);
        CHECK_NOT_NULL(new_string);
        AddString(new_string);
        return new_string;
      },
      [&]() { return base::NoHashMapValue(); });
    return entry->key;
  }

  V8_EXPORT_PRIVATE AstConsString* NewConsString() {
      return zone()->New<AstConsString>();
  }
  V8_EXPORT_PRIVATE AstConsString* NewConsString(const AstRawString* str) {
      return NewConsString()->AddString(zone(), str);
  }
  V8_EXPORT_PRIVATE AstConsString* NewConsString(const AstRawString* str1,
                                                 const AstRawString* str2) {
    return NewConsString()->AddString(zone(), str1)->AddString(zone(), str2);
  }

#define F(name, str)                           \
  const AstRawString* name##_string() const {  \
    return string_constants_->name##_string(); \
  }
  AST_STRING_CONSTANTS(F)
#undef F
  AstConsString* empty_cons_string() const { return empty_cons_string_; }

 private:
  friend class BinAstSerializeVisitor;
  
  AstRawString* AddString(AstRawString* string) {
    *strings_end_ = string;
    strings_end_ = string->next_location();
    return string;
  }
  void ResetStrings() {
    strings_ = nullptr;
    strings_end_ = &strings_;
  }

  const AstStringConstants* string_constants_;
    // All strings are copied here, one after another (no zeroes inbetween).
  AstRawStringMap string_table_;

  AstRawString* strings_;
  AstRawString** strings_end_;

  AstConsString* empty_cons_string_;

  // Caches one character lowercase strings (for minified code).
  static const int kMaxOneCharStringValue = 128;
  const AstRawString* one_character_strings_[kMaxOneCharStringValue];

  Zone* zone_;
  uint64_t hash_seed_;
};


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_VALUE_FACTORY_H_

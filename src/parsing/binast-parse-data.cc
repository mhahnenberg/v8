// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/parsing/binast-parse-data.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/fixed-array-inl.h"
#include "src/zone/zone-containers.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/parsing/binast-serialize-visitor.h"

namespace v8 {
namespace internal {

// A serialized BinAstParseData in zone memory (as apposed to being on-heap).
class ZoneBinAstParseData : public ZoneObject {
 public:
  V8_EXPORT_PRIVATE ZoneBinAstParseData(Zone* zone, Vector<uint8_t>* byte_data);

  Handle<BinAstParseData> Serialize(Isolate* isolate);
  Handle<BinAstParseData> Serialize(LocalIsolate* isolate);

  ZoneVector<uint8_t>* byte_data() { return &byte_data_; }

 private:
  ZoneVector<uint8_t> byte_data_;
};

class ZoneProducedBinAstParseData final : public ProducedBinAstParseData {
 public:
  explicit ZoneProducedBinAstParseData(ZoneBinAstParseData* data) : data_(data) {}

  // If there is data, move the data into the heap and return a Handle to it; 
  // otherwise return a null MaybeHandle.
  virtual Handle<BinAstParseData> Serialize(Isolate* isolate) override;

  // If there is data, move the data into the heap and return a Handle to it; 
  // otherwise return a null MaybeHandle.
  virtual Handle<BinAstParseData> Serialize(LocalIsolate* isolate) override;

 private:
  ZoneBinAstParseData* data_;
};

ZoneBinAstParseData::ZoneBinAstParseData(Zone* zone, Vector<uint8_t>* byte_data)
  : byte_data_(byte_data->begin(), byte_data->end(), zone) {
}

Handle<BinAstParseData> ZoneProducedBinAstParseData::Serialize(Isolate* isolate) {
  return data_->Serialize(isolate);
}

Handle<BinAstParseData> ZoneProducedBinAstParseData::Serialize(LocalIsolate* isolate) {
  return data_->Serialize(isolate);
}

Handle<BinAstParseData> ZoneBinAstParseData::Serialize(Isolate* isolate) {
  int data_size = static_cast<int>(byte_data()->size());
  Handle<ByteArray> serialized_ast = isolate->factory()->NewByteArray(data_size, AllocationType::kOld);
  serialized_ast->copy_in(0, byte_data()->data(), data_size);
  return isolate->factory()->NewBinAstParseData(serialized_ast);
}

Handle<BinAstParseData> ZoneBinAstParseData::Serialize(LocalIsolate* isolate) {
  int data_size = static_cast<int>(byte_data()->size());
  Handle<ByteArray> serialized_ast = isolate->factory()->NewByteArray(data_size, AllocationType::kOld);
  serialized_ast->copy_in(0, byte_data()->data(), data_size);
  return isolate->factory()->NewBinAstParseData(serialized_ast);
}

ProducedBinAstParseData* ProducedBinAstParseData::For(ZoneBinAstParseData* data,
                                                Zone* zone) {
  return zone->New<ZoneProducedBinAstParseData>(data);
}

ZoneBinAstParseData* ZoneBinAstParseDataBuilder::Serialize(Zone* zone, AstValueFactory* ast_value_factory, FunctionLiteral* function_literal, SpeculativeParseFailureReason* failure_reason_ptr) {
  BinAstSerializeVisitor visitor(ast_value_factory);
  SpeculativeParseFailureReason failure_reason = visitor.SerializeAst(function_literal);
  if (failure_reason_ptr != nullptr) {
    *failure_reason_ptr = failure_reason;
  }
  if (failure_reason != kSucceeded) {
    return nullptr;
  }
  Vector<uint8_t> vector_byte_data(visitor.serialized_bytes(), visitor.serialized_bytes_length());
  ZoneBinAstParseData* result = zone->New<ZoneBinAstParseData>(zone, &vector_byte_data);
  return result;
}

}  // namespace internal
}  // namespace v8

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_PARSE_DATA_H_
#define V8_PARSING_BINAST_PARSE_DATA_H_

#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class BinAstFunctionLiteral;
class ZoneBinAstParseData;
class BinAstParseData;
class BinAstValueFactory;

class ProducedBinAstParseData : public ZoneObject {
 public:
  // If there is data, move the data into the heap and return a Handle to it;
  // otherwise return a null MaybeHandle.
  virtual Handle<BinAstParseData> Serialize(Isolate* isolate) = 0;

  // If there is data, move the data into the heap and return a Handle to it;
  // otherwise return a null MaybeHandle.
  virtual Handle<BinAstParseData> Serialize(LocalIsolate* isolate) = 0;

  static ProducedBinAstParseData* For(ZoneBinAstParseData* data, Zone* zone);
};

class ZoneBinAstParseDataBuilder {
 public:
  static ZoneBinAstParseData* Serialize(Zone* zone, BinAstValueFactory* ast_value_factory, BinAstFunctionLiteral* function_literal);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_PARSE_DATA_H_

// Copyright 2006-2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_FUNC_NAME_INFERRER_H_
#define V8_PARSING_FUNC_NAME_INFERRER_H_

#include <vector>

#include "src/base/macros.h"
#include "src/utils/pointer-with-payload.h"
#include "src/ast/ast-value-factory.h"

namespace v8 {
namespace internal {

class AstConsString;
class AstRawString;
class AstValueFactory;
class FunctionLiteral;

enum class InferName { kYes, kNo };

template <>
struct PointerWithPayloadTraits<AstRawString> {
  static constexpr int value = 2;
};

// FuncNameInferrer is a stateful class that is used to perform name
// inference for anonymous functions during static analysis of source code.
// Inference is performed in cases when an anonymous function is assigned
// to a variable or a property (see test-func-name-inference.cc for examples.)
//
// The basic idea is that during parsing of LHSs of certain expressions
// (assignments, declarations, object literals) we collect name strings,
// and during parsing of the RHS, a function literal can be collected. After
// parsing the RHS we can infer a name for function literals that do not have
// a name.
template <typename Types>
class FuncNameInferrer {
 public:
  using AstValueFactory = typename Types::AstValueFactory;
  using FunctionLiteral = typename Types::FunctionLiteral;

  explicit FuncNameInferrer(AstValueFactory* ast_value_factory)
    : ast_value_factory_(ast_value_factory) {}

  FuncNameInferrer(const FuncNameInferrer&) = delete;
  FuncNameInferrer& operator=(const FuncNameInferrer&) = delete;

  // To enter function name inference state, put a FuncNameInferrer::State
  // on the stack.
  class State {
   public:
    explicit State(FuncNameInferrer* fni)
        : fni_(fni), top_(fni->names_stack_.size()) {
      ++fni_->scope_depth_;
    }
    ~State() {
      DCHECK(fni_->IsOpen());
      fni_->names_stack_.resize(top_);
      --fni_->scope_depth_;
    }
    State(const State&) = delete;
    State& operator=(const State&) = delete;

   private:
    FuncNameInferrer* fni_;
    size_t top_;
  };

  // Returns whether we have entered name collection state.
  bool IsOpen() const { return scope_depth_ > 0; }

  // Pushes an enclosing the name of enclosing function onto names stack.
  void PushEnclosingName(const AstRawString* name) {
    // Enclosing name is a name of a constructor function. To check
    // that it is really a constructor, we check that it is not empty
    // and starts with a capital letter.
    if (!name->IsEmpty() && unibrow::Uppercase::Is(name->FirstCharacter())) {
      names_stack_.push_back(Name(name, kEnclosingConstructorName));
    }
  }

  // Pushes an encountered name onto names stack when in collection state.
  void PushLiteralName(const AstRawString* name) {
    if (IsOpen() && name != ast_value_factory_->prototype_string()) {
      names_stack_.push_back(Name(name, kLiteralName));
    }
  }

  void PushVariableName(const AstRawString* name) {
    if (IsOpen() && name != ast_value_factory_->dot_result_string()) {
      names_stack_.push_back(Name(name, kVariableName));
    }
  }

  // Adds a function to infer name for.
  void AddFunction(FunctionLiteral func_to_infer) {
    if (IsOpen()) {
      funcs_to_infer_.push_back(func_to_infer);
    }
  }

  void RemoveLastFunction() {
    if (IsOpen() && !funcs_to_infer_.empty()) funcs_to_infer_.pop_back();
  }

  void RemoveAsyncKeywordFromEnd() {
    if (IsOpen()) {
      CHECK_GT(names_stack_.size(), 0);
      CHECK(names_stack_.back().name()->IsOneByteEqualTo("async"));
      names_stack_.pop_back();
    }
  }

  // Infers a function name and leaves names collection state.
  void Infer() {
    DCHECK(IsOpen());
    if (!funcs_to_infer_.empty()) InferFunctionsNames();
  }

 private:
  enum NameType : uint8_t {
    kEnclosingConstructorName,
    kLiteralName,
    kVariableName
  };
  struct Name {
    // Needed for names_stack_.resize()
    Name() { UNREACHABLE(); }
    Name(const AstRawString* name, NameType type)
        : name_and_type_(name, type) {}

    PointerWithPayload<const AstRawString, NameType, 2> name_and_type_;
    inline const AstRawString* name() const {
      return name_and_type_.GetPointer();
    }
    inline NameType type() const { return name_and_type_.GetPayload(); }
  };

  // Constructs a full name in dotted notation from gathered names.
  AstConsString* MakeNameFromStack() {
    if (names_stack_.size() == 0) {
      return ast_value_factory_->empty_cons_string();
    }
    AstConsString* result = ast_value_factory_->NewConsString();
    auto it = names_stack_.begin();
    while (it != names_stack_.end()) {
      // Advance the iterator to be able to peek the next value.
      auto current = it++;
      // Skip consecutive variable declarations.
      if (it != names_stack_.end() && current->type() == kVariableName &&
          it->type() == kVariableName) {
        continue;
      }
      // Add name. Separate names with ".".
      Zone* zone = ast_value_factory_->zone();
      if (!result->IsEmpty()) {
        result->AddString(zone, ast_value_factory_->dot_string());
      }
      result->AddString(zone, current->name());
    }
    return result;
  }

  // Performs name inferring for added functions.
  void InferFunctionsNames() {
    AstConsString* func_name = MakeNameFromStack();
    for (FunctionLiteral func : funcs_to_infer_) {
      func->set_raw_inferred_name(func_name);
    }
    funcs_to_infer_.resize(0);
  }

  AstValueFactory* ast_value_factory_;
  std::vector<Name> names_stack_;
  std::vector<FunctionLiteral> funcs_to_infer_;
  size_t scope_depth_ = 0;
};


}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_FUNC_NAME_INFERRER_H_

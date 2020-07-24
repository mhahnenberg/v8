// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_H_
#define V8_PARSING_BINAST_H_

#include "src/common/globals.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/scanner.h"
#include "src/zone/zone.h"
#include "src/objects/name.h"
#include "src/ast/variables.h"
#include "src/ast/ast.h"
#include "src/strings/string-hasher.h"
#include "src/strings/string-hasher-inl.h"

namespace v8 {
namespace internal {

class ProducedBinAstParseData;

class BinAstParseInfo {
 public:
  BinAstParseInfo(ParseInfo* parse_info, const UnoptimizedCompileFlags flags);
  BinAstParseInfo(Zone* zone, const UnoptimizedCompileFlags flags, const AstStringConstants* ast_string_constants);

  AstValueFactory* GetOrCreateAstValueFactory();
  Zone* zone() const { return zone_.get(); }
  const UnoptimizedCompileFlags& flags() const { return flags_; }
    // Accessors for per-thread state.
  uintptr_t stack_limit() const { return stack_limit_; }
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void SetPerThreadState(uintptr_t stack_limit,
                         RuntimeCallStats* runtime_call_stats) {
    stack_limit_ = stack_limit;
    runtime_call_stats_ = runtime_call_stats;
  }

  LanguageMode language_mode() const { return language_mode_; }
  void set_language_mode(LanguageMode value) { language_mode_ = value; }
  bool contains_asm_module() const { return contains_asm_module_; }
  void set_contains_asm_module(bool value) { contains_asm_module_ = value; }
  Utf16CharacterStream* character_stream() const {
    return character_stream_.get();
  }
  void set_character_stream(
      std::unique_ptr<Utf16CharacterStream> character_stream);
  void ResetCharacterStream();

  DeclarationScope* script_scope() const { return script_scope_; }
  void set_script_scope(DeclarationScope* script_scope) {
    script_scope_ = script_scope;
  }

  AstValueFactory* ast_value_factory() const {
    DCHECK(ast_value_factory_.get());
    return ast_value_factory_.get();
  }
  
  const AstStringConstants* ast_string_constants() const {
    return ast_string_constants_;
  }

  const AstRawString* function_name() const { return function_name_; }
  void set_function_name(const AstRawString* function_name) {
    function_name_ = function_name;
  }

  FunctionLiteral* literal() const { return literal_; }
  void set_literal(FunctionLiteral* literal) { literal_ = literal; }

  DeclarationScope* scope() const;

  int parameters_end_pos() const { return parameters_end_pos_; }
  void set_parameters_end_pos(int parameters_end_pos) {
    parameters_end_pos_ = parameters_end_pos;
  }

  bool is_wrapped_as_function() const {
    return flags().function_syntax_kind() == FunctionSyntaxKind::kWrapped;
  }

  Logger* logger() const { return logger_; }
  PendingCompilationErrorHandler* pending_error_handler() {
    return &pending_error_handler_;
  }
  const PendingCompilationErrorHandler* pending_error_handler() const {
    return &pending_error_handler_;
  }

 private:
  const UnoptimizedCompileFlags flags_;

  std::unique_ptr<Zone> zone_;
  DeclarationScope* script_scope_;
  uintptr_t stack_limit_;
  int parameters_end_pos_;
  int max_function_literal_id_;

  //----------- Inputs+Outputs of parsing and scope analysis -----------------
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
  std::unique_ptr<AstValueFactory> ast_value_factory_;
  const AstRawString* function_name_;
  RuntimeCallStats* runtime_call_stats_;
  SourceRangeMap* source_range_map_;  // Used when block coverage is enabled.
  PendingCompilationErrorHandler pending_error_handler_;
  Logger* logger_;
  const AstStringConstants* ast_string_constants_;

  //----------- Output of parsing and scope analysis ------------------------
  FunctionLiteral* literal_;
  bool allow_eval_cache_ : 1;
  bool contains_asm_module_ : 1;
  LanguageMode language_mode_ : 1;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_H_
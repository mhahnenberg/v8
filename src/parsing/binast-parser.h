// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_PARSER_H_
#define V8_PARSING_BINAST_PARSER_H_

#include "src/strings/string-hasher-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser-base.h"
#include "src/ast/ast.h"
#include "src/parsing/parser.h"
#include "src/base/overflowing-math.h"
#include "src/base/ieee754.h"

namespace v8 {
namespace internal {

class ParseInfo;
    

class BinAstParser;

template <>
struct ParserTypes<BinAstParser> : AbstractParserTypes<BinAstParser> {
};

class BinAstParser : public AbstractParser<BinAstParser> {
 public:
  explicit BinAstParser(ParseInfo* info);

  // Sets the literal on |info| if parsing succeeded.
  void ParseOnBackground(ParseInfo* info, int start_position, int end_position,
                         int function_literal_id);

  void ParseProgram(ParseInfo* info);


  FunctionLiteral* DoParseFunction(ParseInfo* info,
                                   int start_position, int end_position,
                                   int function_literal_id,
                                   const AstRawString* raw_name);

 private:
  friend class AbstractParser<BinAstParser>;
  friend class ParserBase<BinAstParser>;
  friend class i::ExpressionScope<ParserTypes<BinAstParser>>;
  friend class i::VariableDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ParameterDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ArrowHeadParsingScope<ParserTypes<BinAstParser>>;

  bool parse_lazily() const {
    // TODO(binast)
    return false;
  }

  FunctionLiteral* DoParseProgram(ParseInfo* info);
  void PostProcessParseResult(ParseInfo* info, FunctionLiteral* literal);

  void DeclareFunctionNameVar(const AstRawString* function_name,
                              FunctionSyntaxKind function_syntax_kind,
                              DeclarationScope* function_scope);

  Statement* DeclareFunction(const AstRawString* variable_name,
                             FunctionLiteral* function, VariableMode mode,
                             VariableKind kind, int beg_pos, int end_pos,
                             ZonePtrList<const AstRawString>* names);

  void Declare(Declaration* declaration, const AstRawString* name,
               VariableKind variable_kind, VariableMode mode, InitializationFlag init,
               Scope* scope, bool* was_added, int var_begin_pos,
               int var_end_pos = kNoSourcePosition)
  {
    bool local_ok = true;
    bool sloppy_mode_block_scope_function_redefinition = false;
    scope->DeclareVariable(
        declaration, name, var_begin_pos, mode, variable_kind, init, was_added,
        &sloppy_mode_block_scope_function_redefinition, &local_ok);
    if (!local_ok) {
      // If we only have the start position of a proxy, we can't highlight the
      // whole variable name.  Pretend its length is 1 so that we highlight at
      // least the first character.
      Scanner::Location loc(var_begin_pos, var_end_pos != kNoSourcePosition
                                              ? var_end_pos
                                              : var_begin_pos + 1);
      if (variable_kind == PARAMETER_VARIABLE) {
        ReportMessageAt(loc, MessageTemplate::kParamDupe);
      } else {
        ReportMessageAt(loc, MessageTemplate::kVarRedeclaration,
                        declaration->var()->raw_name());
      }
    } else if (sloppy_mode_block_scope_function_redefinition) {
      // TODO(binast)
      DCHECK(false);
      // ++use_counts_[v8::Isolate::kSloppyModeBlockScopedFunctionRedefinition];
    }
  }

  void ParseFunction(
      ScopedPtrList<Statement>* body, const AstRawString* function_name,
      int pos, FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
      DeclarationScope* function_scope, int* num_parameters,
      int* function_length, bool* has_duplicate_parameters,
      int* expected_property_count, int* suspend_count,
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function);

  FunctionLiteral* ParseFunctionLiteral(
    const AstRawString* name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_position, FunctionSyntaxKind type,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_PARSER_H_

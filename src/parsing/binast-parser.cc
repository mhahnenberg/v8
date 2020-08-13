// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-parser.h"
#include "src/parsing/parse-info.h"
#include "src/zone/zone-list-inl.h"
#include "src/parsing/binast-parse-data.h"

#include <stdio.h>

namespace v8 {
namespace internal {

BinAstParser::BinAstParser(ParseInfo* info)
    : AbstractParser<BinAstParser>(info) {}

void BinAstParser::ParseProgram(ParseInfo* info)
{
  FunctionLiteral* result = nullptr;

  scanner_.Initialize();

  result = DoParseProgram(nullptr, info);

  // TODO(binast): Not sure if we need these...
  MaybeResetCharacterStream(info, result);
  // MaybeProcessSourceRanges(info, result, stack_limit_);
  PostProcessParseResult(nullptr, info, result);
}

FunctionLiteral* BinAstParser::DoParseProgram(Isolate* isolate, ParseInfo* info)
{
  // TODO(binast): START
  // Need to figure out exactly how to setup scope stuff.
  DCHECK_NULL(scope_);
  DeclarationScope* scope = NewScriptScope(REPLMode::kNo);

  if (flags().is_module()) {
    // TODO(binast)
    DCHECK(false);
    // scope = NewModuleScope(scope);
  }

  FunctionState top_scope(&function_state_, &scope_, scope);
  original_scope_ = scope_;
  // TODO(binast): END

  FunctionLiteral* result = nullptr;
  {
    Scope* outer = original_scope_;
    DCHECK_NOT_NULL(outer);
    if (flags().is_eval()) {
      // TODO
      DCHECK(false);
    } else if (flags().is_module()) {
      // TODO
      DCHECK(false);
    }

    DeclarationScope* scope = outer->AsDeclarationScope();
    scope->set_start_position(0);

    FunctionState function_state(&function_state_, &scope_, scope);
    ScopedPtrList<Statement> body(pointer_buffer());
    int beg_pos = scanner()->location().beg_pos;
    if (flags().is_module()) {
      // TODO
      DCHECK(false);
    } else if (flags().is_repl_mode()) {
      // TODO
      DCHECK(false);
    } else {
      // Don't count the mode in the use counters--give the program a chance
      // to enable script-wide strict mode below.
      this->scope()->SetLanguageMode(info->language_mode());
      ParseStatementList(&body, Token::EOS);
    }

    // The parser will peek but not consume EOS.  Our scope logically goes all
    // the way to the EOS, though.
    scope->set_end_position(peek_position());

    if (is_strict(language_mode())) {
      CheckStrictOctalLiteral(beg_pos, end_position());
    }
    if (is_sloppy(language_mode())) {
      // TODO(littledan): Function bindings on the global object that modify
      // pre-existing bindings should be made writable, enumerable and
      // nonconfigurable if possible, whereas this code will leave attributes
      // unchanged if the property already exists.
      InsertSloppyBlockFunctionVarBindings(scope);
    }
    // Internalize the ast strings in the case of eval so we can check for
    // conflicting var declarations with outer scope-info-backed scopes.
    if (flags().is_eval()) {
      // DCHECK(parsing_on_main_thread_);
      // info->ast_value_factory()->Internalize(isolate);

      // TODO
      DCHECK(false);
    }
    CheckConflictingVarDeclarations(scope);
    original_scope_ = nullptr; // TODO(binast): sketchy scope stuff

    if (flags().parse_restriction() == ONLY_SINGLE_FUNCTION_LITERAL) {
      // TODO
      DCHECK(false);

      // if (body.length() != 1 || !body.at(0)->IsExpressionStatement() ||
      //     !body.at(0)
      //          ->AsExpressionStatement()
      //          ->expression()
      //          ->IsFunctionLiteral()) {
      //   ReportMessage(MessageTemplate::kSingleFunctionLiteral);
      // }
    }

    int parameter_count = 0;
    result = factory()->NewScriptOrEvalFunctionLiteral(
        scope, body, function_state.expected_property_count(), parameter_count);
    result->set_suspend_count(function_state.suspend_count());
  }

  if (has_error()) {
    return nullptr;
  }

  return result;
}

void BinAstParser::PostProcessParseResult(Isolate* isolate, ParseInfo* info, FunctionLiteral* literal)
{
  if (literal == nullptr) return;

  ZoneBinAstParseData* zone_binast_parse_data = ZoneBinAstParseDataBuilder::Serialize(zone(), ast_value_factory(), literal);
  ProducedBinAstParseData* produced_binast_parse_data = ProducedBinAstParseData::For(zone_binast_parse_data, zone());
  literal->set_produced_binast_parse_data(produced_binast_parse_data);

  info->set_literal(literal);
  info->set_language_mode(literal->language_mode());

  // if (info->flags().is_eval()) {
  //   info->set_allow_eval_cache(allow_eval_cache());
  // }

  // TODO(binast)
  // {
  //   RuntimeCallTimerScope runtimeTimer(info->runtime_call_stats(),
  //                                      RuntimeCallCounterId::kCompileAnalyse,
  //                                      RuntimeCallStats::kThreadSpecific);
  //   if (!Rewriter::Rewrite(info) || !DeclarationScope::Analyze(info)) {
  //     // Null out the literal to indicate that something failed.
  //     info->set_literal(nullptr);
  //     return;
  //   }
  // }
}

FunctionLiteral* BinAstParser::ParseFunctionLiteral(
    const AstRawString* function_name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_pos, FunctionSyntaxKind function_syntax_kind,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'
  //
  // Getter ::
  //   '(' ')' '{' FunctionBody '}'
  //
  // Setter ::
  //   '(' PropertySetParameterList ')' '{' FunctionBody '}'

  bool is_wrapped = function_syntax_kind == FunctionSyntaxKind::kWrapped;
  DCHECK_EQ(is_wrapped, arguments_for_wrapped_function != nullptr);

  int pos = function_token_pos == kNoSourcePosition ? peek_position()
                                                    : function_token_pos;
  DCHECK_NE(kNoSourcePosition, pos);

  // Anonymous functions were passed either the empty symbol or a null
  // handle as the function name.  Remember if we were passed a non-empty
  // handle to decide whether to invoke function name inference.
  bool should_infer_name = function_name == nullptr;

  // We want a non-null handle as the function name by default. We will handle
  // the "function does not have a shared name" case later.
  if (should_infer_name) {
    function_name = ast_value_factory()->empty_string();
  }

  FunctionLiteral::EagerCompileHint eager_compile_hint =
      function_state_->next_function_is_likely_called() || is_wrapped
          ? FunctionLiteral::kShouldEagerCompile
          : default_eager_compile_hint();

  // Determine if the function can be parsed lazily. Lazy parsing is
  // different from lazy compilation; we need to parse more eagerly than we
  // compile.

  // We can only parse lazily if we also compile lazily. The heuristics for lazy
  // compilation are:
  // - It must not have been prohibited by the caller to Parse (some callers
  //   need a full AST).
  // - The outer scope must allow lazy compilation of inner functions.
  // - The function mustn't be a function expression with an open parenthesis
  //   before; we consider that a hint that the function will be called
  //   immediately, and it would be a waste of time to make it lazily
  //   compiled.
  // These are all things we can know at this point, without looking at the
  // function itself.

  // We separate between lazy parsing top level functions and lazy parsing inner
  // functions, because the latter needs to do more work. In particular, we need
  // to track unresolved variables to distinguish between these cases:
  // (function foo() {
  //   bar = function() { return 1; }
  //  })();
  // and
  // (function foo() {
  //   var a = 1;
  //   bar = function() { return a; }
  //  })();

  // Now foo will be parsed eagerly and compiled eagerly (optimization: assume
  // parenthesis before the function means that it will be called
  // immediately). bar can be parsed lazily, but we need to parse it in a mode
  // that tracks unresolved variables.
  DCHECK_IMPLIES(parse_lazily(), info()->flags().allow_lazy_compile());
  // TODO(binast): Figure out the situation for lazy parsing
  // DCHECK_IMPLIES(parse_lazily(), has_error() || allow_lazy_);
  DCHECK_IMPLIES(parse_lazily(), extension_ == nullptr);

  // TOOD(binast): Fix hack of ParserBase passing in enums from other AST
  // const bool is_lazy =
  //     eager_compile_hint == FunctionLiteral::kShouldLazyCompile;
  const bool is_top_level = AllowsLazyParsingWithoutUnresolvedVariables();
  // const bool is_eager_top_level_function = !is_lazy && is_top_level;
  // const bool is_lazy_top_level_function = is_lazy && is_top_level;
  // const bool is_lazy_inner_function = is_lazy && !is_top_level;

  RuntimeCallTimerScope runtime_timer(
      runtime_call_stats_, RuntimeCallCounterId::kParseFunctionLiteral,
      RuntimeCallStats::kThreadSpecific);
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(FLAG_log_function_events)) timer.Start();

  // TODO(binast): We should determine whether we want to be able to switch back to lazily parsing inner functions with the BinAstParser or if we should just eagerly parse all nested functions.
  // Determine whether we can still lazy parse the inner function.
  // The preconditions are:
  // - Lazy compilation has to be enabled.
  // - Neither V8 natives nor native function declarations can be allowed,
  //   since parsing one would retroactively force the function to be
  //   eagerly compiled.
  // - The invoker of this parser can't depend on the AST being eagerly
  //   built (either because the function is about to be compiled, or
  //   because the AST is going to be inspected for some reason).
  // - Because of the above, we can't be attempting to parse a
  //   FunctionExpression; even without enclosing parentheses it might be
  //   immediately invoked.
  // - The function literal shouldn't be hinted to eagerly compile.

  // Inner functions will be parsed using a temporary Zone. After parsing, we
  // will migrate unresolved variable into a Scope in the main Zone.

  // const bool should_preparse_inner = parse_lazily() && is_lazy_inner_function;

  // TODO(binast): I don't think we need to worry about this here.
  // If parallel compile tasks are enabled, and the function is an eager
  // top level function, then we can pre-parse the function and parse / compile
  // in a parallel task on a worker thread.
  bool should_post_parallel_task = false;
  // bool should_post_parallel_task =
  //     parse_lazily() && is_eager_top_level_function &&
  //     FLAG_parallel_compile_tasks && info()->parallel_tasks() &&
  //     scanner()->stream()->can_be_cloned_for_parallel_access();

  // This may be modified later to reflect preparsing decision taken
  // bool should_preparse = (parse_lazily() && is_lazy_top_level_function) ||
  //                        should_preparse_inner || should_post_parallel_task;
  bool should_preparse = false;

  ScopedPtrList<Statement> body(pointer_buffer());
  int expected_property_count = 0;
  int suspend_count = -1;
  int num_parameters = -1;
  int function_length = -1;
  bool has_duplicate_parameters = false;
  int function_literal_id = GetNextFunctionLiteralId();
  ProducedPreparseData* produced_preparse_data = nullptr;

  // This Scope lives in the main zone. We'll migrate data into that zone later.
  Zone* parse_zone = zone();// TODO(binast) should_preparse ? &preparser_zone_ : zone();
  DeclarationScope* scope = NewFunctionScope(kind, parse_zone);
  SetLanguageMode(scope, language_mode);
#ifdef DEBUG
  scope->SetScopeName(function_name);
#endif

  if (!is_wrapped && V8_UNLIKELY(!Check(Token::LPAREN))) {
    ReportUnexpectedToken(Next());
    return nullptr;
  }
  scope->set_start_position(position());

  // Eager or lazy parse? If is_lazy_top_level_function, we'll parse
  // lazily. We'll call SkipFunction, which may decide to
  // abort lazy parsing if it suspects that wasn't a good idea. If so (in
  // which case the parser is expected to have backtracked), or if we didn't
  // try to lazy parse in the first place, we'll have to parse eagerly.
  bool did_preparse_successfully =
      should_preparse &&
      SkipFunction(function_name, kind, function_syntax_kind, scope,
                   &num_parameters, &function_length, &produced_preparse_data);

  if (!did_preparse_successfully) {
    // If skipping aborted, it rewound the scanner until before the LPAREN.
    // Consume it in that case.
    if (should_preparse) Consume(Token::LPAREN);
    should_post_parallel_task = false;
    ParseFunction(&body, function_name, pos, kind, function_syntax_kind, scope,
                  &num_parameters, &function_length, &has_duplicate_parameters,
                  &expected_property_count, &suspend_count,
                  arguments_for_wrapped_function);
  }

  if (V8_UNLIKELY(FLAG_log_function_events)) {
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name =
        should_preparse
            ? (is_top_level ? "preparse-no-resolution" : "preparse-resolution")
            : "full-parse";
    logger_->FunctionEvent(
        event_name, flags().script_id(), ms, scope->start_position(),
        scope->end_position(),
        reinterpret_cast<const char*>(function_name->raw_data()),
        function_name->byte_length());
  }
  if (V8_UNLIKELY(TracingFlags::is_runtime_stats_enabled()) &&
      did_preparse_successfully) {
    if (runtime_call_stats_) {
      runtime_call_stats_->CorrectCurrentCounterId(
          RuntimeCallCounterId::kPreParseWithVariableResolution,
          RuntimeCallStats::kThreadSpecific);
    }
  }

  // Validate function name. We can do this only after parsing the function,
  // since the function can declare itself strict.
  language_mode = scope->language_mode();
  CheckFunctionName(language_mode, function_name, function_name_validity,
                    function_name_location);

  if (is_strict(language_mode)) {
    CheckStrictOctalLiteral(scope->start_position(), scope->end_position());
  }

  FunctionLiteral::ParameterFlag duplicate_parameters =
      has_duplicate_parameters ? FunctionLiteral::kHasDuplicateParameters
                               : FunctionLiteral::kNoDuplicateParameters;

  // Note that the FunctionLiteral needs to be created in the main Zone again.
  FunctionLiteral* function_literal = factory()->NewFunctionLiteral(
      function_name, scope, body, expected_property_count, num_parameters,
      function_length, duplicate_parameters, function_syntax_kind,
      eager_compile_hint, pos, true, function_literal_id,
      produced_preparse_data);
  function_literal->set_function_token_position(function_token_pos);
  function_literal->set_suspend_count(suspend_count);

  RecordFunctionLiteralSourceRange(function_literal);

  if (should_post_parallel_task) {
    // Start a parallel parse / compile task on the compiler dispatcher.
    // info()->parallel_tasks()->Enqueue(info(), function_name, function_literal);
    // TODO(binast)
    DCHECK(false);
  }

  if (should_infer_name) {
    fni_.AddFunction(function_literal);
  }

  return function_literal;
}
    
}  // namespace internal
}  // namespace v8

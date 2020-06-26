// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-parser.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/binast.h"
#include "src/zone/zone-list-inl.h"

#include <stdio.h>

namespace v8 {
namespace internal {

BinAstParser::BinAstParser(BinAstParseInfo* info)
  : ParserBase<BinAstParser>(
    info->zone(), &scanner_, info->stack_limit(), static_cast<v8::Extension*>(nullptr),
    info->GetOrCreateAstValueFactory(), info->pending_error_handler(),
    info->runtime_call_stats(), info->logger(), info->flags(), /* parsing_on_main_thread */false),
  info_(info),
  scanner_(info->character_stream(), flags()),
  parameters_end_pos_(info->parameters_end_pos())
{
}

void BinAstParser::ParseProgram(BinAstParseInfo* info)
{
  BinAstFunctionLiteral* result = nullptr;

  scanner_.Initialize();

  result = DoParseProgram(info);

  // TODO: Not sure if we need these...
  // MaybeResetCharacterStream(info, result);
  // MaybeProcessSourceRanges(info, result, stack_limit_);
  PostProcessParseResult(info, result);
}

BinAstFunctionLiteral* BinAstParser::DoParseProgram(BinAstParseInfo* info)
{
  // TODO(binast): START
  // Need to figure out exactly how to setup scope stuff.
  DCHECK_NULL(scope_);
  DeclarationScope* scope = NewScriptScope(REPLMode::kNo);

  // TODO(binast)
  // if (flags().is_module()) scope = NewModuleScope(scope);

  FunctionState top_scope(&function_state_, &scope_, scope);
  original_scope_ = scope_;
  // TODO(binast): END

  BinAstFunctionLiteral* result = nullptr;
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
    ScopedPtrList<BinAstStatement> body(pointer_buffer());
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

      // TODO
      // InsertSloppyBlockFunctionVarBindings(scope);
      DCHECK(false);
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

void BinAstParser::PostProcessParseResult(BinAstParseInfo* info, BinAstFunctionLiteral* literal)
{
  if (literal == nullptr) return;

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

void BinAstParser::DeclareFunctionNameVar(const AstRawString* function_name,
                                    FunctionSyntaxKind function_syntax_kind,
                                    DeclarationScope* function_scope) {
  if (function_syntax_kind == FunctionSyntaxKind::kNamedExpression &&
      function_scope->LookupLocal(function_name) == nullptr) {
    DCHECK_EQ(function_scope, scope());
    function_scope->DeclareFunctionVar(function_name);
  }
}

void BinAstParserFormalParameters::ValidateDuplicate(BinAstParser* parser) const {
  if (has_duplicate()) {
    parser->ReportMessageAt(duplicate_loc, MessageTemplate::kParamDupe);
  }
}
void BinAstParserFormalParameters::ValidateStrictMode(BinAstParser* parser) const {
  if (strict_error_loc.IsValid()) {
    parser->ReportMessageAt(strict_error_loc, strict_error_message);
  }
}

BinAstExpression* BinAstParser::ExpressionFromLiteral(Token::Value token, int pos) {
  switch (token) {
    case Token::NULL_LITERAL:
      return factory()->NewNullLiteral(pos);
    case Token::TRUE_LITERAL:
      return factory()->NewBooleanLiteral(true, pos);
    case Token::FALSE_LITERAL:
      return factory()->NewBooleanLiteral(false, pos);
    case Token::SMI: {
      uint32_t value = scanner()->smi_value();
      return factory()->NewSmiLiteral(value, pos);
    }
    case Token::NUMBER: {
      double value = scanner()->DoubleValue();
      return factory()->NewNumberLiteral(value, pos);
    }
    case Token::BIGINT:
      return factory()->NewBigIntLiteral(
          AstBigInt(scanner()->CurrentLiteralAsCString(zone())), pos);
    case Token::STRING: {
      return factory()->NewStringLiteral(GetSymbol(), pos);
    }
    default:
      DCHECK(false);
  }
  return FailureExpression();
}

BinAstStatement* BinAstParser::DeclareFunction(const AstRawString* variable_name,
                                   BinAstFunctionLiteral* function, VariableMode mode,
                                   VariableKind kind, int beg_pos, int end_pos,
                                   ZonePtrList<const AstRawString>* names) {
  Declaration* declaration =
      factory()->NewFunctionDeclaration(function, beg_pos);
  bool was_added;
  Declare(declaration, variable_name, kind, mode, kCreatedInitialized, scope(),
          &was_added, beg_pos);
  if (info()->flags().coverage_enabled()) {
    // Force the function to be allocated when collecting source coverage, so
    // that even dead functions get source coverage data.
    declaration->var()->set_is_used();
  }
  if (names) names->Add(variable_name, zone());
  if (kind == SLOPPY_BLOCK_FUNCTION_VARIABLE) {
    // Token::Value init = loop_nesting_depth() > 0 ? Token::ASSIGN : Token::INIT;
    // SloppyBlockFunctionStatement* statement =
    //     factory()->NewSloppyBlockFunctionStatement(end_pos, declaration->var(),
    //                                                init);
    // GetDeclarationScope()->DeclareSloppyBlockFunction(statement);
    // return statement;
    // TODO(binast)
    DCHECK(false);
  }
  return factory()->EmptyStatement();
}

void BinAstParser::ParseFunction(
    ScopedPtrList<BinAstStatement>* body, const AstRawString* function_name, int pos,
    FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
    DeclarationScope* function_scope, int* num_parameters, int* function_length,
    bool* has_duplicate_parameters, int* expected_property_count,
    int* suspend_count,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  FunctionParsingScope function_parsing_scope(this);
  // TODO(binast): Figure out lazy parsing
  // ParsingModeScope mode(this, allow_lazy_ ? PARSE_LAZILY : PARSE_EAGERLY);
  ParsingModeScope mode(this, PARSE_EAGERLY);

  FunctionState function_state(&function_state_, &scope_, function_scope);

  bool is_wrapped = function_syntax_kind == FunctionSyntaxKind::kWrapped;

  int expected_parameters_end_pos = parameters_end_pos_;
  if (expected_parameters_end_pos != kNoSourcePosition) {
    // This is the first function encountered in a CreateDynamicFunction eval.
    parameters_end_pos_ = kNoSourcePosition;
    // The function name should have been ignored, giving us the empty string
    // here.
    DCHECK_EQ(function_name, ast_value_factory()->empty_string());
  }

  BinAstParserFormalParameters formals(function_scope);

  {
    ParameterDeclarationParsingScope formals_scope(this);
    if (is_wrapped) {
      // For a function implicitly wrapped in function header and footer, the
      // function arguments are provided separately to the source, and are
      // declared directly here.
      for (const AstRawString* arg : *arguments_for_wrapped_function) {
        const bool is_rest = false;
        BinAstExpression* argument = ExpressionFromIdentifier(arg, kNoSourcePosition);
        AddFormalParameter(&formals, argument, NullExpression(),
                           kNoSourcePosition, is_rest);
      }
      DCHECK_EQ(arguments_for_wrapped_function->length(),
                formals.num_parameters());
      DeclareFormalParameters(&formals);
    } else {
      // For a regular function, the function arguments are parsed from source.
      DCHECK_NULL(arguments_for_wrapped_function);
      ParseFormalParameterList(&formals);
      if (expected_parameters_end_pos != kNoSourcePosition) {
        // Check for '(' or ')' shenanigans in the parameter string for dynamic
        // functions.
        int position = peek_position();
        if (position < expected_parameters_end_pos) {
          ReportMessageAt(Scanner::Location(position, position + 1),
                          MessageTemplate::kArgStringTerminatesParametersEarly);
          return;
        } else if (position > expected_parameters_end_pos) {
          ReportMessageAt(Scanner::Location(expected_parameters_end_pos - 2,
                                            expected_parameters_end_pos),
                          MessageTemplate::kUnexpectedEndOfArgString);
          return;
        }
      }
      Expect(Token::RPAREN);
      int formals_end_position = scanner()->location().end_pos;

      CheckArityRestrictions(formals.arity, kind, formals.has_rest,
                             function_scope->start_position(),
                             formals_end_position);
      Expect(Token::LBRACE);
    }
    formals.duplicate_loc = formals_scope.duplicate_location();
  }

  *num_parameters = formals.num_parameters();
  *function_length = formals.function_length;

  AcceptINScope scope(this, true);
  ParseFunctionBody(body, function_name, pos, formals, kind,
                    function_syntax_kind, FunctionBodyType::kBlock);

  *has_duplicate_parameters = formals.has_duplicate();

  *expected_property_count = function_state.expected_property_count();
  *suspend_count = function_state.suspend_count();
}

BinAstFunctionLiteral* BinAstParser::ParseFunctionLiteral(
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

  ScopedPtrList<BinAstStatement> body(pointer_buffer());
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
  BinAstFunctionLiteral* function_literal = factory()->NewFunctionLiteral(
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

void BinAstParser::ReportUnexpectedTokenAt(Scanner::Location location,
                                     Token::Value token,
                                     MessageTemplate message) {
  const char* arg = nullptr;
  switch (token) {
    case Token::EOS:
      message = MessageTemplate::kUnexpectedEOS;
      break;
    case Token::SMI:
    case Token::NUMBER:
    case Token::BIGINT:
      message = MessageTemplate::kUnexpectedTokenNumber;
      break;
    case Token::STRING:
      message = MessageTemplate::kUnexpectedTokenString;
      break;
    case Token::PRIVATE_NAME:
    case Token::IDENTIFIER:
      message = MessageTemplate::kUnexpectedTokenIdentifier;
      break;
    case Token::AWAIT:
    case Token::ENUM:
      message = MessageTemplate::kUnexpectedReserved;
      break;
    case Token::LET:
    case Token::STATIC:
    case Token::YIELD:
    case Token::FUTURE_STRICT_RESERVED_WORD:
      message = is_strict(language_mode())
                    ? MessageTemplate::kUnexpectedStrictReserved
                    : MessageTemplate::kUnexpectedTokenIdentifier;
      break;
    case Token::TEMPLATE_SPAN:
    case Token::TEMPLATE_TAIL:
      message = MessageTemplate::kUnexpectedTemplateString;
      break;
    case Token::ESCAPED_STRICT_RESERVED_WORD:
    case Token::ESCAPED_KEYWORD:
      message = MessageTemplate::kInvalidEscapedReservedWord;
      break;
    case Token::ILLEGAL:
      if (scanner()->has_error()) {
        message = scanner()->error();
        location = scanner()->error_location();
      } else {
        message = MessageTemplate::kInvalidOrUnexpectedToken;
      }
      break;
    case Token::REGEXP_LITERAL:
      message = MessageTemplate::kUnexpectedTokenRegExp;
      break;
    default:
      const char* name = Token::String(token);
      DCHECK_NOT_NULL(name);
      arg = name;
      break;
  }
  ReportMessageAt(location, message, arg);
}
    
}  // namespace internal
}  // namespace v8

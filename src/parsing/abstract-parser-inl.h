// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_ABSTRACT_PARSER_INL_H_
#define V8_PARSING_ABSTRACT_PARSER_INL_H_

#include "src/parsing/abstract-parser.h"

#include <algorithm>
#include <memory>

#include "src/ast/ast-function-literal-id-reindexer.h"
#include "src/ast/ast-traversal-visitor.h"
#include "src/ast/ast.h"
#include "src/ast/source-range-ast-visitor.h"
#include "src/base/ieee754.h"
#include "src/base/overflowing-math.h"
#include "src/base/platform/platform.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/compiler-dispatcher/compiler-dispatcher.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/scope-info.h"
#include "src/parsing/binast-deserializer.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/rewriter.h"
#include "src/runtime/runtime.h"
#include "src/strings/char-predicates-inl.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-inl.h"
#include "src/tracing/trace-event.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

template <typename Impl>
inline FunctionLiteral* AbstractParser<Impl>::DefaultConstructor(const AstRawString* name,
                                            bool call_super, int pos,
                                            int end_pos) {
  int expected_property_count = 0;
  const int parameter_count = 0;

  FunctionKind kind = call_super ? FunctionKind::kDefaultDerivedConstructor
                                 : FunctionKind::kDefaultBaseConstructor;
  DeclarationScope* function_scope = impl()->NewFunctionScope(kind);
  SetLanguageMode(function_scope, LanguageMode::kStrict);
  // Set start and end position to the same value
  function_scope->set_start_position(pos);
  function_scope->set_end_position(pos);
  ScopedPtrList<Statement> body(impl()->pointer_buffer());

  {
    FunctionState function_state(&impl()->function_state_, &impl()->scope_, function_scope);

    if (call_super) {
      // Create a SuperCallReference and handle in BytecodeGenerator.
      auto constructor_args_name = impl()->ast_value_factory()->empty_string();
      bool is_rest = true;
      bool is_optional = false;
      Variable* constructor_args = function_scope->DeclareParameter(
          constructor_args_name, VariableMode::kTemporary, is_optional, is_rest,
          impl()->ast_value_factory()->arguments_string(), pos);

      Expression* call;
      {
        ScopedPtrList<Expression> args(impl()->pointer_buffer());
        Spread* spread_args = impl()->factory()->NewSpread(
            impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(constructor_args)), pos, pos);

        args.Add(spread_args);
        Expression* super_call_ref = NewSuperCallReference(pos);
        constexpr bool has_spread = true;
        call = impl()->factory()->NewCall(super_call_ref, args, pos, has_spread);
      }
      body.Add(impl()->factory()->NewReturnStatement(call, pos));
    }

    expected_property_count = function_state.expected_property_count();
  }

  FunctionLiteral* function_literal = impl()->factory()->NewFunctionLiteral(
      name, function_scope, body, expected_property_count, parameter_count,
      parameter_count, FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAnonymousExpression, impl()->default_eager_compile_hint(),
      pos, true, impl()->GetNextFunctionLiteralId(), impl()->speculative_parse_failure_reason());
  return function_literal;
}

template <typename Impl>
inline void AbstractParser<Impl>::ReportUnexpectedTokenAt(Scanner::Location location,
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
      message = is_strict(impl()->language_mode())
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
      if (impl()->scanner()->has_error()) {
        message = impl()->scanner()->error();
        location = impl()->scanner()->error_location();
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

// ----------------------------------------------------------------------------
// Implementation of Parser

template <typename Impl>
inline bool AbstractParser<Impl>::ShortcutNumericLiteralBinaryExpression(Expression** x,
                                                    Expression* y,
                                                    Token::Value op, int pos) {
  if ((*x)->IsNumberLiteral() && y->IsNumberLiteral()) {
    double x_val = (*x)->AsLiteral()->AsNumber();
    double y_val = y->AsLiteral()->AsNumber();
    switch (op) {
      case Token::ADD:
        *x = impl()->factory()->NewNumberLiteral(x_val + y_val, pos);
        return true;
      case Token::SUB:
        *x = impl()->factory()->NewNumberLiteral(x_val - y_val, pos);
        return true;
      case Token::MUL:
        *x = impl()->factory()->NewNumberLiteral(x_val * y_val, pos);
        return true;
      case Token::DIV:
        *x = impl()->factory()->NewNumberLiteral(base::Divide(x_val, y_val), pos);
        return true;
      case Token::BIT_OR: {
        int value = DoubleToInt32(x_val) | DoubleToInt32(y_val);
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::BIT_AND: {
        int value = DoubleToInt32(x_val) & DoubleToInt32(y_val);
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::BIT_XOR: {
        int value = DoubleToInt32(x_val) ^ DoubleToInt32(y_val);
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SHL: {
        int value =
            base::ShlWithWraparound(DoubleToInt32(x_val), DoubleToInt32(y_val));
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SHR: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        uint32_t value = DoubleToUint32(x_val) >> shift;
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SAR: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        int value = ArithmeticShiftRight(DoubleToInt32(x_val), shift);
        *x = impl()->factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::EXP:
        *x = impl()->factory()->NewNumberLiteral(base::ieee754::pow(x_val, y_val), pos);
        return true;
      default:
        break;
    }
  }
  return false;
}

template <typename Impl>
inline bool AbstractParser<Impl>::CollapseNaryExpression(Expression** x, Expression* y,
                                    Token::Value op, int pos,
                                    const SourceRange& range) {
  // Filter out unsupported ops.
  if (!Token::IsBinaryOp(op) || op == Token::EXP) return false;

  // Convert *x into an nary operation with the given op, returning false if
  // this is not possible.
  NaryOperation* nary = nullptr;
  if ((*x)->IsBinaryOperation()) {
    BinaryOperation* binop = (*x)->AsBinaryOperation();
    if (binop->op() != op) return false;

    nary = impl()->factory()->NewNaryOperation(op, binop->left(), 2);
    nary->AddSubsequent(binop->right(), binop->position());
    ConvertBinaryToNaryOperationSourceRange(binop, nary);
    *x = nary;
  } else if ((*x)->IsNaryOperation()) {
    nary = (*x)->AsNaryOperation();
    if (nary->op() != op) return false;
  } else {
    return false;
  }

  // Append our current expression to the nary operation.
  // TODO(leszeks): Do some literal collapsing here if we're appending Smi or
  // String literals.
  nary->AddSubsequent(y, pos);
  nary->clear_parenthesized();
  AppendNaryOperationSourceRange(nary, range);

  return true;
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::BuildUnaryExpression(Expression* expression,
                                         Token::Value op, int pos) {
  DCHECK_NOT_NULL(expression);
  const Literal* literal = expression->AsLiteral();
  if (literal != nullptr) {
    if (op == Token::NOT) {
      // Convert the literal to a boolean condition and negate it.
      return impl()->factory()->NewBooleanLiteral(literal->ToBooleanIsFalse(), pos);
    } else if (literal->IsNumberLiteral()) {
      // Compute some expressions involving only number literals.
      double value = literal->AsNumber();
      switch (op) {
        case Token::ADD:
          return expression;
        case Token::SUB:
          return impl()->factory()->NewNumberLiteral(-value, pos);
        case Token::BIT_NOT:
          return impl()->factory()->NewNumberLiteral(~DoubleToInt32(value), pos);
        default:
          break;
      }
    }
  }
  return impl()->factory()->NewUnaryOperation(op, expression, pos);
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::NewThrowError(Runtime::FunctionId id,
                                  MessageTemplate message,
                                  const AstRawString* arg, int pos) {
  ScopedPtrList<Expression> args(impl()->pointer_buffer());
  args.Add(impl()->factory()->NewSmiLiteral(static_cast<int>(message), pos));
  args.Add(impl()->factory()->NewStringLiteral(arg, pos));
  CallRuntime* call_constructor = impl()->factory()->NewCallRuntime(id, args, pos);
  return impl()->factory()->NewThrow(call_constructor, pos);
}

template <typename Impl>
Expression* AbstractParser<Impl>::NewSuperPropertyReference(int pos) {
  const AstRawString* home_object_name;
  if (IsStatic(impl()->scope()->GetReceiverScope()->function_kind())) {
    home_object_name = impl()->ast_value_factory_->dot_static_home_object_string();
  } else {
    home_object_name = impl()->ast_value_factory_->dot_home_object_string();
  }
  return impl()->factory()->NewSuperPropertyReference(
      impl()->NewUnresolved(home_object_name, pos), pos);
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::NewSuperCallReference(int pos) {
  VariableProxy* new_target_proxy =
      impl()->NewUnresolved(impl()->ast_value_factory()->new_target_string(), pos);
  VariableProxy* this_function_proxy =
      impl()->NewUnresolved(impl()->ast_value_factory()->this_function_string(), pos);
  return impl()->factory()->NewSuperCallReference(
      impl()->factory()->NewVariableProxyExpression(new_target_proxy),
      impl()->factory()->NewVariableProxyExpression(this_function_proxy),
      pos);
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::NewTargetExpression(int pos) {
  auto proxy = impl()->NewUnresolved(impl()->ast_value_factory()->new_target_string(), pos);
  auto proxy_expr = impl()->factory()->NewVariableProxyExpression(proxy);
  proxy_expr->set_is_new_target();
  return proxy_expr;
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::ImportMetaExpression(int pos) {
  ScopedPtrList<Expression> args(impl()->pointer_buffer());
  return impl()->factory()->NewCallRuntime(Runtime::kInlineGetImportMetaObject, args,
                                   pos);
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::ExpressionFromLiteral(Token::Value token, int pos) {
  switch (token) {
    case Token::NULL_LITERAL:
      return impl()->factory()->NewNullLiteral(pos);
    case Token::TRUE_LITERAL:
      return impl()->factory()->NewBooleanLiteral(true, pos);
    case Token::FALSE_LITERAL:
      return impl()->factory()->NewBooleanLiteral(false, pos);
    case Token::SMI: {
      uint32_t value = impl()->scanner()->smi_value();
      return impl()->factory()->NewSmiLiteral(value, pos);
    }
    case Token::NUMBER: {
      double value = impl()->scanner()->DoubleValue();
      return impl()->factory()->NewNumberLiteral(value, pos);
    }
    case Token::BIGINT:
      return impl()->factory()->NewBigIntLiteral(
          AstBigInt(impl()->scanner()->CurrentLiteralAsCString(impl()->zone())), pos);
    case Token::STRING: {
      return impl()->factory()->NewStringLiteral(GetSymbol(), pos);
    }
    default:
      DCHECK(false);
  }
  return FailureExpression();
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::NewV8Intrinsic(const AstRawString* name,
                                   const ScopedPtrList<Expression>& args,
                                   int pos) {
  if (impl()->extension_ != nullptr) {
    // The extension structures are only accessible while parsing the
    // very first time, not when reparsing because of lazy compilation.
    impl()->GetClosureScope()->ForceEagerCompilation();
  }

  if (!name->is_one_byte()) {
    // There are no two-byte named intrinsics.
    impl()->ReportMessage(MessageTemplate::kNotDefined, name);
    return FailureExpression();
  }

  const Runtime::Function* function =
      Runtime::FunctionForName(name->raw_data(), name->length());

  // Be more permissive when fuzzing. Intrinsics are not supported.
  if (FLAG_fuzzing) {
    return NewV8RuntimeFunctionForFuzzing(function, args, pos);
  }

  if (function != nullptr) {
    // Check for possible name clash.
    DCHECK_EQ(Context::kNotFound,
              Context::IntrinsicIndexForName(name->raw_data(), name->length()));

    // Check that the expected number of arguments are being passed.
    if (function->nargs != -1 && function->nargs != args.length()) {
      impl()->ReportMessage(MessageTemplate::kRuntimeWrongNumArgs);
      return FailureExpression();
    }

    return impl()->factory()->NewCallRuntime(function, args, pos);
  }

  int context_index =
      Context::IntrinsicIndexForName(name->raw_data(), name->length());

  // Check that the function is defined.
  if (context_index == Context::kNotFound) {
    impl()->ReportMessage(MessageTemplate::kNotDefined, name);
    return FailureExpression();
  }

  return impl()->factory()->NewCallRuntime(context_index, args, pos);
}

// More permissive runtime-function creation on fuzzers.
template <typename Impl>
inline Expression* AbstractParser<Impl>::NewV8RuntimeFunctionForFuzzing(
    const Runtime::Function* function, const ScopedPtrList<Expression>& args,
    int pos) {
  CHECK(FLAG_fuzzing);

  // Intrinsics are not supported for fuzzing. Only allow allowlisted runtime
  // functions. Also prevent later errors due to too few arguments and just
  // ignore this call.
  if (function == nullptr ||
      !Runtime::IsAllowListedForFuzzing(function->function_id) ||
      function->nargs > args.length()) {
    return impl()->factory()->NewUndefinedLiteral(kNoSourcePosition);
  }

  // Flexible number of arguments permitted.
  if (function->nargs == -1) {
    return impl()->factory()->NewCallRuntime(function, args, pos);
  }

  // Otherwise ignore superfluous arguments.
  ScopedPtrList<Expression> permissive_args(impl()->pointer_buffer());
  for (int i = 0; i < function->nargs; i++) {
    permissive_args.Add(args.at(i));
  }
  return impl()->factory()->NewCallRuntime(function, permissive_args, pos);
}

template<typename Impl>
inline AbstractParser<Impl>::AbstractParser(ParseInfo* info)
    : ParserBase<Impl>(
          info->zone(), &scanner_, info->stack_limit(), info->extension(),
          info->GetOrCreateAstValueFactory(), info->pending_error_handler(),
          info->runtime_call_stats(), info->logger(), info->flags(), true),
      info_(info),
      scanner_(info->character_stream(), impl()->flags()),
      preparser_zone_(info->zone()->allocator(), "pre-parser-zone"),
      reusable_preparser_(nullptr),
      mode_(PARSE_EAGERLY),  // Lazy mode must be set explicitly.
      source_range_map_(info->source_range_map()),
      total_preparse_skipped_(0),
      consumed_preparse_data_(info->consumed_preparse_data()),
      preparse_data_buffer_(),
      parameters_end_pos_(info->parameters_end_pos()) {
  // Even though we were passed ParseInfo, we should not store it in
  // Parser - this makes sure that Isolate is not accidentally accessed via
  // ParseInfo during background parsing.
  DCHECK_NOT_NULL(info->character_stream());
  // Determine if functions can be lazily compiled. This is necessary to
  // allow some of our builtin JS files to be lazily compiled. These
  // builtins cannot be handled lazily by the parser, since we have to know
  // if a function uses the special natives syntax, which is something the
  // parser records.
  // If the debugger requests compilation for break points, we cannot be
  // aggressive about lazy compilation, because it might trigger compilation
  // of functions without an outer context when setting a breakpoint through
  // Debug::FindSharedFunctionInfoInScript
  // We also compile eagerly for kProduceExhaustiveCodeCache.
  bool can_compile_lazily = impl()->flags().allow_lazy_compile() && !impl()->flags().is_eager();

  impl()->set_default_eager_compile_hint(can_compile_lazily
                                     ? FunctionLiteral::kShouldLazyCompile
                                     : FunctionLiteral::kShouldEagerCompile);
  allow_lazy_ = impl()->flags().allow_lazy_compile() && impl()->flags().allow_lazy_parsing() &&
                info->extension() == nullptr && can_compile_lazily;
  for (int feature = 0; feature < v8::Isolate::kUseCounterFeatureCount;
       ++feature) {
    use_counts_[feature] = 0;
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::InitializeEmptyScopeChain(ParseInfo* info) {
  DCHECK_NULL(impl()->original_scope_);
  DCHECK_NULL(info->script_scope());
  DeclarationScope* script_scope =
      impl()->NewScriptScope(impl()->flags().is_repl_mode() ? REPLMode::kYes : REPLMode::kNo);
  info->set_script_scope(script_scope);
  impl()->original_scope_ = script_scope;
}

template <typename Impl>
inline void AbstractParser<Impl>::DeserializeScopeChain(
    Isolate* isolate, ParseInfo* info,
    MaybeHandle<ScopeInfo> maybe_outer_scope_info,
    Scope::DeserializationMode mode) {
  InitializeEmptyScopeChain(info);
  Handle<ScopeInfo> outer_scope_info;
  if (maybe_outer_scope_info.ToHandle(&outer_scope_info)) {
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
    impl()->original_scope_ = Scope::DeserializeScopeChain(
        isolate, impl()->zone(), *outer_scope_info, info->script_scope(),
        impl()->ast_value_factory(), mode);
    if (impl()->flags().is_eval() || IsArrowFunction(impl()->flags().function_kind())) {
      impl()->original_scope_->GetReceiverScope()->DeserializeReceiver(
          impl()->ast_value_factory());
    }
  }
}

namespace {

inline void MaybeResetCharacterStream(ParseInfo* info, FunctionLiteral* literal) {
#if V8_ENABLE_WEBASSEMBLY
  // Don't reset the character stream if there is an asm.js module since it will
  // be used again by the asm-parser.
  if (info->contains_asm_module()) {
    if (FLAG_stress_validate_asm) return;
    if (literal != nullptr && literal->scope()->ContainsAsmModule()) return;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  info->ResetCharacterStream();
}

inline void MaybeProcessSourceRanges(ParseInfo* parse_info, Expression* root,
                              uintptr_t stack_limit_) {
  if (root != nullptr && parse_info->source_range_map() != nullptr) {
    SourceRangeAstVisitor visitor(stack_limit_, root,
                                  parse_info->source_range_map());
    visitor.Run();
  }
}

}  // namespace

template <typename Impl>
inline void AbstractParser<Impl>::ParseProgram(Isolate* isolate, Handle<Script> script,
                          ParseInfo* info,
                          MaybeHandle<ScopeInfo> maybe_outer_scope_info) {
  // TODO(bmeurer): We temporarily need to pass allow_nesting = true here,
  // see comment for HistogramTimerScope class.
  DCHECK_EQ(script->id(), impl()->flags().script_id());

  // It's OK to use the Isolate & counters here, since this function is only
  // called in the main thread.
  DCHECK(impl()->parsing_on_main_thread_);
  RCS_SCOPE(impl()->runtime_call_stats_, impl()->flags().is_eval()
                                     ? RuntimeCallCounterId::kParseEval
                                     : RuntimeCallCounterId::kParseProgram);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.ParseProgram");
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(FLAG_log_function_events)) timer.Start();

  // Initialize parser state.
  DeserializeScopeChain(isolate, info, maybe_outer_scope_info,
                        Scope::DeserializationMode::kIncludingVariables);

  DCHECK_EQ(script->is_wrapped(), info->is_wrapped_as_function());
  if (script->is_wrapped()) {
    maybe_wrapped_arguments_ = handle(script->wrapped_arguments(), isolate);
  }

  scanner_.Initialize();
  FunctionLiteral* result = DoParseProgram(isolate, info);
  MaybeResetCharacterStream(info, result);
  MaybeProcessSourceRanges(info, result, impl()->stack_limit_);
  impl()->PostProcessParseResult(isolate, info, result);

  HandleSourceURLComments(isolate, script);

  if (V8_UNLIKELY(FLAG_log_function_events) && result != nullptr) {
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name = "parse-eval";
    int start = -1;
    int end = -1;
    if (!impl()->flags().is_eval()) {
      event_name = "parse-script";
      start = 0;
      end = String::cast(script->source()).length();
    }
    LOG(isolate,
        FunctionEvent(event_name, impl()->flags().script_id(), ms, start, end, "", 0));
  }
}

template <typename Impl>
inline FunctionLiteral* AbstractParser<Impl>::DoParseProgram(Isolate* isolate, ParseInfo* info) {
  // Note that this function can be called from the main thread or from a
  // background thread. We should not access anything Isolate / heap dependent
  // via ParseInfo, and also not pass it forward. If not on the main thread
  // isolate will be nullptr.
  DCHECK_EQ(impl()->parsing_on_main_thread_, isolate != nullptr);
  DCHECK_NULL(impl()->scope_);

  ParsingModeScope mode(impl(), allow_lazy_ ? PARSE_LAZILY : PARSE_EAGERLY);
  impl()->ResetFunctionLiteralId();

  FunctionLiteral* result = nullptr;
  {
    Scope* outer = impl()->original_scope_;
    DCHECK_NOT_NULL(outer);
    if (impl()->flags().is_eval()) {
      outer = impl()->NewEvalScope(outer);
    } else if (impl()->flags().is_module()) {
      DCHECK_EQ(outer, info->script_scope());
      outer = impl()->NewModuleScope(info->script_scope());
    }

    DeclarationScope* scope = outer->AsDeclarationScope();
    scope->set_start_position(0);

    FunctionState function_state(&impl()->function_state_, &impl()->scope_, scope);
    ScopedPtrList<Statement> body(impl()->pointer_buffer());
    int beg_pos = impl()->scanner()->location().beg_pos;
    if (impl()->flags().is_module()) {
      DCHECK(impl()->flags().is_module());

      PrepareGeneratorVariables();
      Expression* initial_yield =
          BuildInitialYield(kNoSourcePosition, kGeneratorFunction);
      body.Add(
          impl()->factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
      if (impl()->flags().allow_harmony_top_level_await()) {
        // First parse statements into a buffer. Then, if there was a
        // top level await, create an inner block and rewrite the body of the
        // module as an async function. Otherwise merge the statements back
        // into the main body.
        BlockT block = impl()->NullBlock();
        {
          StatementListT statements(impl()->pointer_buffer());
          ParseModuleItemList(&statements);
          // Modules will always have an initial yield. If there are any
          // additional suspends, i.e. awaits, then we treat the module as an
          // AsyncModule.
          if (function_state.suspend_count() > 1) {
            scope->set_is_async_module();
            block = impl()->factory()->NewBlock(true, statements);
          } else {
            statements.MergeInto(&body);
          }
        }
        if (IsAsyncModule(scope->function_kind())) {
          impl()->RewriteAsyncFunctionBody(
              &body, block, impl()->factory()->NewUndefinedLiteral(kNoSourcePosition));
        }
      } else {
        ParseModuleItemList(&body);
      }
      if (!impl()->has_error() &&
          !impl()->module()->Validate(impl()->scope()->AsModuleScope(),
                              impl()->pending_error_handler(), impl()->zone())) {
        impl()->scanner()->set_parser_error();
      }
    } else if (info->is_wrapped_as_function()) {
      DCHECK(impl()->parsing_on_main_thread_);
      ParseWrapped(isolate, info, &body, scope, impl()->zone());
    } else if (impl()->flags().is_repl_mode()) {
      ParseREPLProgram(info, &body, scope);
    } else {
      // Don't count the mode in the use counters--give the program a chance
      // to enable script-wide strict mode below.
      impl()->scope()->SetLanguageMode(info->language_mode());
      impl()->ParseStatementList(&body, Token::EOS);
    }

    // The parser will peek but not consume EOS.  Our scope logically goes all
    // the way to the EOS, though.
    scope->set_end_position(impl()->peek_position());

    if (is_strict(impl()->language_mode())) {
      impl()->CheckStrictOctalLiteral(beg_pos, impl()->end_position());
    }
    if (is_sloppy(impl()->language_mode())) {
      // TODO(littledan): Function bindings on the global object that modify
      // pre-existing bindings should be made writable, enumerable and
      // nonconfigurable if possible, whereas this code will leave attributes
      // unchanged if the property already exists.
      InsertSloppyBlockFunctionVarBindings(scope);
    }
    // Internalize the ast strings in the case of eval so we can check for
    // conflicting var declarations with outer scope-info-backed scopes.
    if (impl()->flags().is_eval()) {
      DCHECK(impl()->parsing_on_main_thread_);
      info->ast_value_factory()->Internalize(isolate);
    }
    impl()->CheckConflictingVarDeclarations(scope);

    if (impl()->flags().parse_restriction() == ONLY_SINGLE_FUNCTION_LITERAL) {
      if (body.length() != 1 || !body.at(0)->IsExpressionStatement() ||
          !body.at(0)
               ->AsExpressionStatement()
               ->expression()
               ->IsFunctionLiteral()) {
        impl()->ReportMessage(MessageTemplate::kSingleFunctionLiteral);
      }
    }

    int parameter_count = 0;
    result = impl()->factory()->NewScriptOrEvalFunctionLiteral(
        scope, body, function_state.expected_property_count(), parameter_count);
    result->set_suspend_count(function_state.suspend_count());
  }

  info->set_max_function_literal_id(impl()->GetLastFunctionLiteralId());

  if (impl()->has_error()) return nullptr;

  RecordFunctionLiteralSourceRange(result);

  return result;
}

template <typename Impl>
inline void AbstractParser<Impl>::PostProcessParseResult(Isolate* isolate, ParseInfo* info,
                                    FunctionLiteral* literal) {
  if (literal == nullptr) return;

  info->set_literal(literal);
  info->set_language_mode(literal->language_mode());
  if (info->flags().is_eval()) {
    info->set_allow_eval_cache(impl()->allow_eval_cache());
  }

  // We cannot internalize on a background thread; a foreground task will take
  // care of calling AstValueFactory::Internalize just before compilation.
  DCHECK_EQ(isolate != nullptr, impl()->parsing_on_main_thread_);
  if (isolate) {
    //printf("Internalizing ast value factory\n");
    info->ast_value_factory()->Internalize(isolate);
  }

  {
    RCS_SCOPE(info->runtime_call_stats(), RuntimeCallCounterId::kCompileAnalyse,
              RuntimeCallStats::kThreadSpecific);
    if (!Rewriter::Rewrite(info) || !DeclarationScope::Analyze(info)) {
      // Null out the literal to indicate that something failed.
      info->set_literal(nullptr);
      return;
    }
  }
}

template <typename Impl>
inline ZonePtrList<const AstRawString>* AbstractParser<Impl>::PrepareWrappedArguments(
    Isolate* isolate, ParseInfo* info, Zone* zone) {
  DCHECK(impl()->parsing_on_main_thread_);
  DCHECK_NOT_NULL(isolate);
  Handle<FixedArray> arguments = maybe_wrapped_arguments_.ToHandleChecked();
  int arguments_length = arguments->length();
  ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
      zone->New<ZonePtrList<const AstRawString>>(arguments_length, zone);
  for (int i = 0; i < arguments_length; i++) {
    const AstRawString* argument_string = impl()->ast_value_factory()->GetString(
        Handle<String>(String::cast(arguments->get(i)), isolate));
    arguments_for_wrapped_function->Add(argument_string, zone);
  }
  return arguments_for_wrapped_function;
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseWrapped(Isolate* isolate, ParseInfo* info,
                          ScopedPtrList<Statement>* body,
                          DeclarationScope* outer_scope, Zone* zone) {
  DCHECK(impl()->parsing_on_main_thread_);
  DCHECK(info->is_wrapped_as_function());
  ParsingModeScope parsing_mode(impl(), PARSE_EAGERLY);

  // Set function and block state for the outer eval scope.
  DCHECK(outer_scope->is_eval_scope());
  FunctionState function_state(&impl()->function_state_, &impl()->scope_, outer_scope);

  const AstRawString* function_name = nullptr;
  Scanner::Location location(0, 0);

  ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
      PrepareWrappedArguments(isolate, info, zone);

  FunctionLiteral* function_literal = ParseFunctionLiteral(
      function_name, location, kSkipFunctionNameCheck, kNormalFunction,
      kNoSourcePosition, FunctionSyntaxKind::kWrapped, LanguageMode::kSloppy,
      arguments_for_wrapped_function);

  Statement* return_statement = impl()->factory()->NewReturnStatement(
      function_literal, kNoSourcePosition, kNoSourcePosition);
  body->Add(return_statement);
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseREPLProgram(ParseInfo* info, ScopedPtrList<Statement>* body,
                              DeclarationScope* scope) {
  // REPL scripts are handled nearly the same way as the body of an async
  // function. The difference is the value used to resolve the async
  // promise.
  // For a REPL script this is the completion value of the
  // script instead of the expression of some "return" statement. The
  // completion value of the script is obtained by manually invoking
  // the {Rewriter} which will return a VariableProxy referencing the
  // result.
  DCHECK(impl()->flags().is_repl_mode());
  impl()->scope()->SetLanguageMode(info->language_mode());
  PrepareGeneratorVariables();

  BlockT block = impl()->NullBlock();
  {
    StatementListT statements(impl()->pointer_buffer());
    impl()->ParseStatementList(&statements, Token::EOS);
    block = impl()->factory()->NewBlock(true, statements);
  }

  if (impl()->has_error()) return;

  base::Optional<VariableProxyExpression*> maybe_result =
      Rewriter::RewriteBody(info, scope, block->statements());
  Expression* result_value =
      (maybe_result && *maybe_result)
          ? static_cast<Expression*>(*maybe_result)
          : impl()->factory()->NewUndefinedLiteral(kNoSourcePosition);

  impl()->RewriteAsyncFunctionBody(body, block, WrapREPLResult(result_value),
                                   REPLMode::kYes);
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::WrapREPLResult(Expression* value) {
  // REPL scripts additionally wrap the ".result" variable in an
  // object literal:
  //
  //     return %_AsyncFunctionResolve(
  //                .generator_object, {.repl_result: .result});
  //
  // Should ".result" be a resolved promise itself, the async return
  // would chain the promises and return the resolve value instead of
  // the promise.

  Literal* property_name = impl()->factory()->NewStringLiteral(
      impl()->ast_value_factory()->dot_repl_result_string(), kNoSourcePosition);
  ObjectLiteralProperty* property =
      impl()->factory()->NewObjectLiteralProperty(property_name, value, true);

  ScopedPtrList<ObjectLiteralProperty> properties(impl()->pointer_buffer());
  properties.Add(property);
  return impl()->factory()->NewObjectLiteral(properties, false, kNoSourcePosition,
                                     false);
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseFunction(Isolate* isolate, ParseInfo* info,
                           Handle<SharedFunctionInfo> shared_info) {
  // It's OK to use the Isolate & counters here, since this function is only
  // called in the main thread.
  DCHECK(impl()->parsing_on_main_thread_);
  RCS_SCOPE(impl()->runtime_call_stats_, RuntimeCallCounterId::kParseFunction);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.ParseFunction");
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(FLAG_log_function_events)) timer.Start();

  MaybeHandle<ScopeInfo> maybe_outer_scope_info;
  if (shared_info->HasOuterScopeInfo()) {
    maybe_outer_scope_info = handle(shared_info->GetOuterScopeInfo(), isolate);
  }
  DeserializeScopeChain(isolate, info, maybe_outer_scope_info,
                        Scope::DeserializationMode::kIncludingVariables);
  DCHECK_EQ(impl()->factory()->zone(), info->zone());

  Handle<Script> script = handle(Script::cast(shared_info->script()), isolate);
  if (shared_info->is_wrapped()) {
    maybe_wrapped_arguments_ = handle(script->wrapped_arguments(), isolate);
  }

  //Handle<String> fn_source = Object::ToString(isolate, SharedFunctionInfo::GetSourceCode(shared_info)).ToHandleChecked();
  //std::unique_ptr<char[]> raw_fn_source = fn_source->ToCString();
  //printf("Source: '%s'\n", raw_fn_source.get());

  int start_position = shared_info->StartPosition();
  int end_position = shared_info->EndPosition();
  int function_literal_id = shared_info->function_literal_id();
  if V8_UNLIKELY (script->type() == Script::TYPE_WEB_SNAPSHOT) {
    // Function literal IDs for inner functions haven't been allocated when
    // deserializing. Put the inner function SFIs to the end of the list;
    // they'll be deduplicated later (if the corresponding SFIs exist already)
    // in Script::FindSharedFunctionInfo. (-1 here because function_literal_id
    // is the parent's id. The inner function will get ids starting from
    // function_literal_id + 1.)
    function_literal_id = script->shared_function_info_count() - 1;
  }

  // Initialize parser state.
  Handle<String> name(shared_info->Name(), isolate);
  info->set_function_name(impl()->ast_value_factory()->GetString(name));
  scanner_.Initialize();

  long long deserialize_nanoseconds = 0;
  FunctionLiteral* result = nullptr;
  bool is_inner_binast = false;
  MaybeHandle<PreparseData> preparse_data;
  bool try_deserialize = true;
  if (try_deserialize) {
    if (V8_UNLIKELY(shared_info->HasUncompiledDataWithBinAstParseData() ||
                    shared_info->HasUncompiledDataWithInnerBinAstParseData())) {

      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.DeserializeFunction");

      RuntimeCallTimerScope runtime_timer(
          impl()->runtime_call_stats_, RuntimeCallCounterId::kDeserializeBinAst);
      auto start = std::chrono::high_resolution_clock::now();
      is_inner_binast = shared_info->HasUncompiledDataWithInnerBinAstParseData();
      Handle<ByteArray> binast_parse_data;
      base::Optional<uint32_t> offset;
      base::Optional<uint32_t> length;
      if (is_inner_binast) {
        Handle<UncompiledDataWithInnerBinAstParseData> uncompiled_data =
            handle(shared_info->uncompiled_data_with_inner_bin_ast_parse_data(),
                  isolate);

        binast_parse_data = handle(uncompiled_data->binast_parse_data(), isolate);
        if (!uncompiled_data->preparse_data().IsUndefined()) {
          preparse_data = handle(PreparseData::cast(uncompiled_data->preparse_data()), isolate);
        }

        offset.emplace(uncompiled_data->data_offset());
        length.emplace(uncompiled_data->data_length());
      } else {
        Handle<UncompiledDataWithBinAstParseData> uncompiled_data = handle(
            shared_info->uncompiled_data_with_binast_parse_data(), isolate);

        binast_parse_data = handle(uncompiled_data->binast_parse_data(), isolate);

        if (!uncompiled_data->preparse_data().IsUndefined()) {
          preparse_data = handle(PreparseData::cast(uncompiled_data->preparse_data()), isolate);
        }
      }
      FunctionLiteral* literal;
      {
        // We need to setup the parser/initial outer scope before we can start
        // deserialization.
        Scope* outer = impl()->original_scope_;
        DeclarationScope* outer_function = outer->GetClosureScope();
        DCHECK(outer);
        FunctionState function_state(&impl()->function_state_, &impl()->scope_, outer_function);
        BlockState block_state(&impl()->scope_, outer);
        BinAstDeserializer deserializer(isolate, impl(), binast_parse_data, preparse_data);
        AstNode* ast_node = deserializer.DeserializeAst(offset, length);
        literal = ast_node->AsFunctionLiteral();
        DCHECK(literal != nullptr);
        result = literal;

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        deserialize_nanoseconds =
          std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        // int function_length = result->end_position() - result->start_position();
        // printf("PREPARSE++: Deserialize time for %sfunction (%d bytes) in %lld ns\n", is_inner_binast ? "inner " : "", function_length, deserialize_nanoseconds);
        // printf("PREPARSE++: %zu: %lld\n", source_hash, deserialize_nanoseconds);
        //printf("PREPARSE++ deserialize: %lld\n", deserialize_nanoseconds);
      }
      // }
    }
  }

  if (result == nullptr && V8_UNLIKELY(shared_info->private_name_lookup_skips_outer_class() &&
                  impl()->original_scope_->is_class_scope())) {
    // If the function skips the outer class and the outer scope is a class, the
    // function is in heritage position. Otherwise the function scope's skip bit
    // will be correctly inherited from the outer scope.
    ClassScope::HeritageParsingScope heritage(impl()->original_scope_->AsClassScope());
    result = DoParseFunction(isolate, info, start_position, end_position,
                             function_literal_id, info->function_name());
  } else if (result == nullptr) {
    //auto start = std::chrono::high_resolution_clock::now();
    result = DoParseFunction(isolate, info, start_position, end_position,
                             function_literal_id, info->function_name());
    //auto elapsed = std::chrono::high_resolution_clock::now() - start;
    //long long parse_nanoseconds =
    //    std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    // int function_length = result->end_position() - result->start_position();
    // printf("PREPARSE++: Parse time: %lld ns for %d bytes\n", parse_nanoseconds, function_length);
    //if (!try_deserialize) {
      // printf("PREPARSE++: %zu: %lld\n", source_hash, parse_nanoseconds);
      //printf("PREPARSE++ parse: %lld\n", parse_nanoseconds);
    //}
  }
  MaybeResetCharacterStream(info, result);
  MaybeProcessSourceRanges(info, result, impl()->stack_limit_);
  if (result != nullptr) {
    Handle<String> inferred_name(shared_info->inferred_name(), isolate);
    result->set_inferred_name(inferred_name);
    // Fix the function_literal_id in case we changed it earlier.
    result->set_function_literal_id(shared_info->function_literal_id());
  }
  impl()->PostProcessParseResult(isolate, info, result);
  if (V8_UNLIKELY(FLAG_log_function_events) && result != nullptr) {
    double ms = timer.Elapsed().InMillisecondsF();
    // We should already be internalized by now, so the debug name will be
    // available.
    DeclarationScope* function_scope = result->scope();
    std::unique_ptr<char[]> function_name = result->GetDebugName();
    LOG(isolate,
        FunctionEvent("parse-function", impl()->flags().script_id(), ms,
                      function_scope->start_position(),
                      function_scope->end_position(), function_name.get(),
                      strlen(function_name.get())));
  }
}

template <typename Impl>
inline FunctionLiteral* AbstractParser<Impl>::DoParseFunction(Isolate* isolate, ParseInfo* info,
                                         int start_position, int end_position,
                                         int function_literal_id,
                                         const AstRawString* raw_name) {
  DCHECK_EQ(impl()->parsing_on_main_thread_, isolate != nullptr);
  DCHECK_NOT_NULL(raw_name);
  DCHECK_NULL(impl()->scope_);

  DCHECK(impl()->ast_value_factory());
  impl()->fni_.PushEnclosingName(raw_name);

  impl()->ResetFunctionLiteralId();
  DCHECK_LT(0, function_literal_id);
  impl()->SkipFunctionLiterals(function_literal_id - 1);

  ParsingModeScope parsing_mode(impl(), PARSE_EAGERLY);

  // Place holder for the result.
  FunctionLiteral* result = nullptr;

  {
    // Parse the function literal.
    Scope* outer = impl()->original_scope_;
    DeclarationScope* outer_function = outer->GetClosureScope();
    DCHECK(outer);
    FunctionState function_state(&impl()->function_state_, &impl()->scope_, outer_function);
    BlockState block_state(&impl()->scope_, outer);
    DCHECK(is_sloppy(outer->language_mode()) ||
           is_strict(info->language_mode()));
    FunctionKind kind = impl()->flags().function_kind();
    DCHECK_IMPLIES(IsConciseMethod(kind) || IsAccessorFunction(kind),
                   impl()->flags().function_syntax_kind() ==
                       FunctionSyntaxKind::kAccessorOrMethod);

    if (IsArrowFunction(kind)) {
      if (IsAsyncFunction(kind)) {
        DCHECK(!impl()->scanner()->HasLineTerminatorAfterNext());
        if (!impl()->Check(Token::ASYNC)) {
          CHECK(impl()->stack_overflow());
          return nullptr;
        }
        if (!(impl()->peek_any_identifier() || impl()->peek() == Token::LPAREN)) {
          CHECK(impl()->stack_overflow());
          return nullptr;
        }
      }

      // TODO(adamk): We should construct this scope from the ScopeInfo.
      DeclarationScope* scope = impl()->NewFunctionScope(kind);
      scope->set_has_checked_syntax(true);

      // This bit only needs to be explicitly set because we're
      // not passing the ScopeInfo to the Scope constructor.
      SetLanguageMode(scope, info->language_mode());

      scope->set_start_position(start_position);
      ParserFormalParameters formals(scope);
      {
        ParameterDeclarationParsingScope formals_scope(impl());
        // Parsing patterns as variable reference expression creates
        // NewUnresolved references in current scope. Enter arrow function
        // scope for formal parameter parsing.
        BlockState block_state(&impl()->scope_, scope);
        if (impl()->Check(Token::LPAREN)) {
          // '(' StrictFormalParameters ')'
          impl()->ParseFormalParameterList(&formals);
          impl()->Expect(Token::RPAREN);
        } else {
          // BindingIdentifier
          ParameterParsingScope scope(impl(), &formals);
          impl()->ParseFormalParameter(&formals);
          DeclareFormalParameters(&formals);
        }
        formals.duplicate_loc = formals_scope.duplicate_location();
      }

      if (impl()->GetLastFunctionLiteralId() != function_literal_id - 1) {
        if (impl()->has_error()) return nullptr;
        // If there were FunctionLiterals in the parameters, we need to
        // renumber them to shift down so the next function literal id for
        // the arrow function is the one requested.
        AstFunctionLiteralIdReindexer reindexer(
            impl()->stack_limit_,
            (function_literal_id - 1) - impl()->GetLastFunctionLiteralId());
        for (auto p : formals.params) {
          if (p->pattern != nullptr) reindexer.Reindex(p->pattern);
          if (p->initializer() != nullptr) {
            reindexer.Reindex(p->initializer());
          }
        }
        impl()->ResetFunctionLiteralId();
        impl()->SkipFunctionLiterals(function_literal_id - 1);
      }

      Expression* expression = impl()->ParseArrowFunctionLiteral(formals);
      // Scanning must end at the same position that was recorded
      // previously. If not, parsing has been interrupted due to a stack
      // overflow, at which point the partially parsed arrow function
      // concise body happens to be a valid expression. This is a problem
      // only for arrow functions with single expression bodies, since there
      // is no end token such as "}" for normal functions.
      if (impl()->scanner()->location().end_pos == end_position) {
        // The pre-parser saw an arrow function here, so the full parser
        // must produce a FunctionLiteral.
        DCHECK(expression->IsFunctionLiteral());
        result = expression->AsFunctionLiteral();
      }
    } else if (IsDefaultConstructor(kind)) {
      DCHECK_EQ(impl()->scope(), outer);
      result = DefaultConstructor(raw_name, IsDerivedConstructor(kind),
                                  start_position, end_position);
    } else {
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
          info->is_wrapped_as_function()
              ? PrepareWrappedArguments(isolate, info, impl()->zone())
              : nullptr;
      result = ParseFunctionLiteral(
          raw_name, Scanner::Location::invalid(), kSkipFunctionNameCheck, kind,
          kNoSourcePosition, impl()->flags().function_syntax_kind(),
          info->language_mode(), arguments_for_wrapped_function);
    }

    if (impl()->has_error()) return nullptr;
    result->set_requires_instance_members_initializer(
        impl()->flags().requires_instance_members_initializer());
    result->set_class_scope_has_private_brand(
        impl()->flags().class_scope_has_private_brand());
    result->set_has_static_private_methods_or_accessors(
        impl()->flags().has_static_private_methods_or_accessors());
    if (impl()->flags().is_oneshot_iife()) {
      result->mark_as_oneshot_iife();
    }
  }

  DCHECK_IMPLIES(result, function_literal_id == result->function_literal_id());
  return result;
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::ParseModuleItem() {
  // ecma262/#prod-ModuleItem
  // ModuleItem :
  //    ImportDeclaration
  //    ExportDeclaration
  //    StatementListItem

  Token::Value next = impl()->peek();

  if (next == Token::EXPORT) {
    return ParseExportDeclaration();
  }

  if (next == Token::IMPORT) {
    // We must be careful not to parse a dynamic import expression as an import
    // declaration. Same for import.meta expressions.
    Token::Value peek_ahead = impl()->PeekAhead();
    if (peek_ahead != Token::LPAREN && peek_ahead != Token::PERIOD) {
      ParseImportDeclaration();
      return impl()->factory()->EmptyStatement();
    }
  }

  return impl()->ParseStatementListItem();
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseModuleItemList(ScopedPtrList<Statement>* body) {
  // ecma262/#prod-Module
  // Module :
  //    ModuleBody?
  //
  // ecma262/#prod-ModuleItemList
  // ModuleBody :
  //    ModuleItem*

  DCHECK(impl()->scope()->is_module_scope());
  while (impl()->peek() != Token::EOS) {
    Statement* stat = ParseModuleItem();
    if (stat == nullptr) return;
    if (stat->IsEmptyStatement()) continue;
    body->Add(stat);
  }
}

template <typename Impl>
inline const AstRawString* AbstractParser<Impl>::ParseModuleSpecifier() {
  // ModuleSpecifier :
  //    StringLiteral

  impl()->Expect(Token::STRING);
  return GetSymbol();
}

template <typename Impl>
inline ZoneChunkList<typename AbstractParser<Impl>::ExportClauseData>* AbstractParser<Impl>::ParseExportClause(
    Scanner::Location* reserved_loc,
    Scanner::Location* string_literal_local_name_loc) {
  // ExportClause :
  //   '{' '}'
  //   '{' ExportsList '}'
  //   '{' ExportsList ',' '}'
  //
  // ExportsList :
  //   ExportSpecifier
  //   ExportsList ',' ExportSpecifier
  //
  // ExportSpecifier :
  //   IdentifierName
  //   IdentifierName 'as' IdentifierName
  //   IdentifierName 'as' ModuleExportName
  //   ModuleExportName
  //   ModuleExportName 'as' ModuleExportName
  //
  // ModuleExportName :
  //   StringLiteral
  ZoneChunkList<ExportClauseData>* export_data =
      impl()->zone()->template New<ZoneChunkList<ExportClauseData>>(impl()->zone());

  impl()->Expect(Token::LBRACE);

  Token::Value name_tok;
  while ((name_tok = impl()->peek()) != Token::RBRACE) {
    const AstRawString* local_name = ParseExportSpecifierName();
    if (!string_literal_local_name_loc->IsValid() &&
        name_tok == Token::STRING) {
      // Keep track of the first string literal local name exported for error
      // reporting. These must be followed by a 'from' clause.
      *string_literal_local_name_loc = impl()->scanner()->location();
    } else if (!reserved_loc->IsValid() &&
               !Token::IsValidIdentifier(name_tok, LanguageMode::kStrict, false,
                                         impl()->flags().is_module())) {
      // Keep track of the first reserved word encountered in case our
      // caller needs to report an error.
      *reserved_loc = impl()->scanner()->location();
    }
    const AstRawString* export_name;
    Scanner::Location location = impl()->scanner()->location();
    if (impl()->CheckContextualKeyword(impl()->ast_value_factory()->as_string())) {
      export_name = ParseExportSpecifierName();
      // Set the location to the whole "a as b" string, so that it makes sense
      // both for errors due to "a" and for errors due to "b".
      location.end_pos = impl()->scanner()->location().end_pos;
    } else {
      export_name = local_name;
    }
    export_data->push_back({export_name, local_name, location});
    if (impl()->peek() == Token::RBRACE) break;
    if (V8_UNLIKELY(!impl()->Check(Token::COMMA))) {
      impl()->ReportUnexpectedToken(impl()->Next());
      break;
    }
  }

  impl()->Expect(Token::RBRACE);
  return export_data;
}

template <typename Impl>
inline const AstRawString* AbstractParser<Impl>::ParseExportSpecifierName() {
  Token::Value next = impl()->Next();

  // IdentifierName
  if (V8_LIKELY(Token::IsPropertyName(next))) {
    return GetSymbol();
  }

  // ModuleExportName
  if (next == Token::STRING) {
    const AstRawString* export_name = GetSymbol();
    if (V8_LIKELY(export_name->is_one_byte())) return export_name;
    if (!unibrow::Utf16::HasUnpairedSurrogate(
            reinterpret_cast<const uint16_t*>(export_name->raw_data()),
            export_name->length())) {
      return export_name;
    }
    impl()->ReportMessage(MessageTemplate::kInvalidModuleExportName);
    return EmptyIdentifierString();
  }

  impl()->ReportUnexpectedToken(next);
  return EmptyIdentifierString();
}

template <typename Impl>
inline ZonePtrList<const typename AbstractParser<Impl>::NamedImport>* AbstractParser<Impl>::ParseNamedImports(int pos) {
  // NamedImports :
  //   '{' '}'
  //   '{' ImportsList '}'
  //   '{' ImportsList ',' '}'
  //
  // ImportsList :
  //   ImportSpecifier
  //   ImportsList ',' ImportSpecifier
  //
  // ImportSpecifier :
  //   BindingIdentifier
  //   IdentifierName 'as' BindingIdentifier
  //   ModuleExportName 'as' BindingIdentifier

  impl()->Expect(Token::LBRACE);

  auto result = impl()->zone()->template New<ZonePtrList<const NamedImport>>(1, impl()->zone());
  while (impl()->peek() != Token::RBRACE) {
    const AstRawString* import_name = ParseExportSpecifierName();
    const AstRawString* local_name = import_name;
    Scanner::Location location = impl()->scanner()->location();
    // In the presence of 'as', the left-side of the 'as' can
    // be any IdentifierName. But without 'as', it must be a valid
    // BindingIdentifier.
    if (impl()->CheckContextualKeyword(impl()->ast_value_factory()->as_string())) {
      local_name = impl()->ParsePropertyName();
    }
    if (!Token::IsValidIdentifier(impl()->scanner()->current_token(),
                                  LanguageMode::kStrict, false,
                                  impl()->flags().is_module())) {
      impl()->ReportMessage(MessageTemplate::kUnexpectedReserved);
      return nullptr;
    } else if (IsEvalOrArguments(local_name)) {
      impl()->ReportMessage(MessageTemplate::kStrictEvalArguments);
      return nullptr;
    }

    DeclareUnboundVariable(local_name, VariableMode::kConst,
                           kNeedsInitialization, impl()->position());

    NamedImport* import =
        impl()->zone()->template New<NamedImport>(import_name, local_name, location);
    result->Add(import, impl()->zone());

    if (impl()->peek() == Token::RBRACE) break;
    impl()->Expect(Token::COMMA);
  }

  impl()->Expect(Token::RBRACE);
  return result;
}

template <typename Impl>
inline ImportAssertions* AbstractParser<Impl>::ParseImportAssertClause() {
  // AssertClause :
  //    assert '{' '}'
  //    assert '{' AssertEntries '}'

  // AssertEntries :
  //    IdentifierName: AssertionKey
  //    IdentifierName: AssertionKey , AssertEntries

  // AssertionKey :
  //     IdentifierName
  //     StringLiteral

  auto import_assertions = impl()->zone()->template New<ImportAssertions>(impl()->zone());

  if (!FLAG_harmony_import_assertions) {
    return import_assertions;
  }

  // Assert clause is optional, and cannot be preceded by a LineTerminator.
  if (impl()->scanner()->HasLineTerminatorBeforeNext() ||
      !impl()->CheckContextualKeyword(impl()->ast_value_factory()->assert_string())) {
    return import_assertions;
  }

  impl()->Expect(Token::LBRACE);

  while (impl()->peek() != Token::RBRACE) {
    const AstRawString* attribute_key = nullptr;
    if (impl()->Check(Token::STRING)) {
      attribute_key = GetSymbol();
    } else {
      attribute_key = impl()->ParsePropertyName();
    }

    Scanner::Location location = impl()->scanner()->location();

    impl()->Expect(Token::COLON);
    impl()->Expect(Token::STRING);

    const AstRawString* attribute_value = GetSymbol();

    // Set the location to the whole "key: 'value'"" string, so that it makes
    // sense both for errors due to the key and errors due to the value.
    location.end_pos = impl()->scanner()->location().end_pos;

    auto result = import_assertions->insert(std::make_pair(
        attribute_key, std::make_pair(attribute_value, location)));
    if (!result.second) {
      // It is a syntax error if two AssertEntries have the same key.
      ReportMessageAt(location, MessageTemplate::kImportAssertionDuplicateKey,
                      attribute_key);
      break;
    }

    if (impl()->peek() == Token::RBRACE) break;
    if (V8_UNLIKELY(!impl()->Check(Token::COMMA))) {
      impl()->ReportUnexpectedToken(impl()->Next());
      break;
    }
  }

  impl()->Expect(Token::RBRACE);

  return import_assertions;
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseImportDeclaration() {
  // ImportDeclaration :
  //   'import' ImportClause 'from' ModuleSpecifier ';'
  //   'import' ModuleSpecifier ';'
  //   'import' ImportClause 'from' ModuleSpecifier [no LineTerminator here]
  //       AssertClause ';'
  //   'import' ModuleSpecifier [no LineTerminator here] AssertClause';'
  //
  // ImportClause :
  //   ImportedDefaultBinding
  //   NameSpaceImport
  //   NamedImports
  //   ImportedDefaultBinding ',' NameSpaceImport
  //   ImportedDefaultBinding ',' NamedImports
  //
  // NameSpaceImport :
  //   '*' 'as' ImportedBinding

  int pos = impl()->peek_position();
  impl()->Expect(Token::IMPORT);

  Token::Value tok = impl()->peek();

  // 'import' ModuleSpecifier ';'
  if (tok == Token::STRING) {
    Scanner::Location specifier_loc = impl()->scanner()->peek_location();
    const AstRawString* module_specifier = ParseModuleSpecifier();
    const ImportAssertions* import_assertions = ParseImportAssertClause();
    impl()->ExpectSemicolon();
    impl()->module()->AddEmptyImport(module_specifier, import_assertions, specifier_loc,
                             impl()->zone());
    return;
  }

  // Parse ImportedDefaultBinding if present.
  const AstRawString* import_default_binding = nullptr;
  Scanner::Location import_default_binding_loc;
  if (tok != Token::MUL && tok != Token::LBRACE) {
    import_default_binding = impl()->ParseNonRestrictedIdentifier();
    import_default_binding_loc = impl()->scanner()->location();
    DeclareUnboundVariable(import_default_binding, VariableMode::kConst,
                           kNeedsInitialization, pos);
  }

  // Parse NameSpaceImport or NamedImports if present.
  const AstRawString* module_namespace_binding = nullptr;
  Scanner::Location module_namespace_binding_loc;
  const ZonePtrList<const NamedImport>* named_imports = nullptr;
  if (import_default_binding == nullptr || impl()->Check(Token::COMMA)) {
    switch (impl()->peek()) {
      case Token::MUL: {
        impl()->Consume(Token::MUL);
        impl()->ExpectContextualKeyword(impl()->ast_value_factory()->as_string());
        module_namespace_binding = impl()->ParseNonRestrictedIdentifier();
        module_namespace_binding_loc = impl()->scanner()->location();
        DeclareUnboundVariable(module_namespace_binding, VariableMode::kConst,
                               kCreatedInitialized, pos);
        break;
      }

      case Token::LBRACE:
        named_imports = ParseNamedImports(pos);
        break;

      default:
        impl()->ReportUnexpectedToken(impl()->scanner()->current_token());
        return;
    }
  }

  impl()->ExpectContextualKeyword(impl()->ast_value_factory()->from_string());
  Scanner::Location specifier_loc = impl()->scanner()->peek_location();
  const AstRawString* module_specifier = ParseModuleSpecifier();
  const ImportAssertions* import_assertions = ParseImportAssertClause();
  impl()->ExpectSemicolon();

  // Now that we have all the information, we can make the appropriate
  // declarations.

  // TODO(neis): Would prefer to call DeclareVariable for each case below rather
  // than above and in ParseNamedImports, but then a possible error message
  // would point to the wrong location.  Maybe have a DeclareAt version of
  // Declare that takes a location?

  if (module_namespace_binding != nullptr) {
    impl()->module()->AddStarImport(module_namespace_binding, module_specifier,
                            import_assertions, module_namespace_binding_loc,
                            specifier_loc, impl()->zone());
  }

  if (import_default_binding != nullptr) {
    impl()->module()->AddImport(impl()->ast_value_factory()->default_string(),
                        import_default_binding, module_specifier,
                        import_assertions, import_default_binding_loc,
                        specifier_loc, impl()->zone());
  }

  if (named_imports != nullptr) {
    if (named_imports->length() == 0) {
      impl()->module()->AddEmptyImport(module_specifier, import_assertions,
                               specifier_loc, impl()->zone());
    } else {
      for (const NamedImport* import : *named_imports) {
        impl()->module()->AddImport(import->import_name, import->local_name,
                            module_specifier, import_assertions,
                            import->location, specifier_loc, impl()->zone());
      }
    }
  }
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::ParseExportDefault() {
  //  Supports the following productions, starting after the 'default' token:
  //    'export' 'default' HoistableDeclaration
  //    'export' 'default' ClassDeclaration
  //    'export' 'default' AssignmentExpression[In] ';'

  impl()->Expect(Token::DEFAULT);
  Scanner::Location default_loc = impl()->scanner()->location();

  ZonePtrList<const AstRawString> local_names(1, impl()->zone());
  Statement* result = nullptr;
  switch (impl()->peek()) {
    case Token::FUNCTION:
      result = impl()->ParseHoistableDeclaration(&local_names, true);
      break;

    case Token::CLASS:
      impl()->Consume(Token::CLASS);
      result = impl()->ParseClassDeclaration(&local_names, true);
      break;

    case Token::ASYNC:
      if (impl()->PeekAhead() == Token::FUNCTION &&
          !impl()->scanner()->HasLineTerminatorAfterNext()) {
        impl()->Consume(Token::ASYNC);
        result = impl()->ParseAsyncFunctionDeclaration(&local_names, true);
        break;
      }
      V8_FALLTHROUGH;

    default: {
      int pos = impl()->position();
      AcceptINScope scope(impl(), true);
      Expression* value = impl()->ParseAssignmentExpression();
      SetFunctionName(value, impl()->ast_value_factory()->default_string());

      const AstRawString* local_name =
          impl()->ast_value_factory()->dot_default_string();
      local_names.Add(local_name, impl()->zone());

      // It's fine to declare this as VariableMode::kConst because the user has
      // no way of writing to it.
      VariableProxyExpression* proxy =
          impl()->factory()->NewVariableProxyExpression(DeclareBoundVariable(local_name, VariableMode::kConst, pos));
      proxy->var()->set_initializer_position(impl()->position());

      Assignment* assignment = impl()->factory()->NewAssignment(
          Token::INIT, proxy, value, kNoSourcePosition);
      result = IgnoreCompletion(
          impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition));

      impl()->ExpectSemicolon();
      break;
    }
  }

  if (result != nullptr) {
    DCHECK_EQ(local_names.length(), 1);
    impl()->module()->AddExport(local_names.first(),
                        impl()->ast_value_factory()->default_string(), default_loc,
                        impl()->zone());
  }

  return result;
}

template <typename Impl>
inline const AstRawString* AbstractParser<Impl>::NextInternalNamespaceExportName() {
  const char* prefix = ".ns-export";
  std::string s(prefix);
  s.append(std::to_string(number_of_named_namespace_exports_++));
  return impl()->ast_value_factory()->GetOneByteString(s.c_str());
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseExportStar() {
  int pos = impl()->position();
  impl()->Consume(Token::MUL);

  if (!impl()->PeekContextualKeyword(impl()->ast_value_factory()->as_string())) {
    // 'export' '*' 'from' ModuleSpecifier ';'
    Scanner::Location loc = impl()->scanner()->location();
    impl()->ExpectContextualKeyword(impl()->ast_value_factory()->from_string());
    Scanner::Location specifier_loc = impl()->scanner()->peek_location();
    const AstRawString* module_specifier = ParseModuleSpecifier();
    const ImportAssertions* import_assertions = ParseImportAssertClause();
    impl()->ExpectSemicolon();
    impl()->module()->AddStarExport(module_specifier, import_assertions, loc,
                            specifier_loc, impl()->zone());
    return;
  }

  // 'export' '*' 'as' IdentifierName 'from' ModuleSpecifier ';'
  //
  // Desugaring:
  //   export * as x from "...";
  // ~>
  //   import * as .x from "..."; export {.x as x};
  //
  // Note that the desugared internal namespace export name (.x above) will
  // never conflict with a string literal export name, as literal string export
  // names in local name positions (i.e. left of 'as' or in a clause without
  // 'as') are disallowed without a following 'from' clause.

  impl()->ExpectContextualKeyword(impl()->ast_value_factory()->as_string());
  const AstRawString* export_name = ParseExportSpecifierName();
  Scanner::Location export_name_loc = impl()->scanner()->location();
  const AstRawString* local_name = NextInternalNamespaceExportName();
  Scanner::Location local_name_loc = Scanner::Location::invalid();
  DeclareUnboundVariable(local_name, VariableMode::kConst, kCreatedInitialized,
                         pos);

  impl()->ExpectContextualKeyword(impl()->ast_value_factory()->from_string());
  Scanner::Location specifier_loc = impl()->scanner()->peek_location();
  const AstRawString* module_specifier = ParseModuleSpecifier();
  const ImportAssertions* import_assertions = ParseImportAssertClause();
  impl()->ExpectSemicolon();

  impl()->module()->AddStarImport(local_name, module_specifier, import_assertions,
                          local_name_loc, specifier_loc, impl()->zone());
  impl()->module()->AddExport(local_name, export_name, export_name_loc, impl()->zone());
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::ParseExportDeclaration() {
  // ExportDeclaration:
  //    'export' '*' 'from' ModuleSpecifier ';'
  //    'export' '*' 'from' ModuleSpecifier [no LineTerminator here]
  //        AssertClause ';'
  //    'export' '*' 'as' IdentifierName 'from' ModuleSpecifier ';'
  //    'export' '*' 'as' IdentifierName 'from' ModuleSpecifier
  //        [no LineTerminator here] AssertClause ';'
  //    'export' '*' 'as' ModuleExportName 'from' ModuleSpecifier ';'
  //    'export' '*' 'as' ModuleExportName 'from' ModuleSpecifier ';'
  //        [no LineTerminator here] AssertClause ';'
  //    'export' ExportClause ('from' ModuleSpecifier)? ';'
  //    'export' ExportClause ('from' ModuleSpecifier [no LineTerminator here]
  //        AssertClause)? ';'
  //    'export' VariableStatement
  //    'export' Declaration
  //    'export' 'default' ... (handled in ParseExportDefault)
  //
  // ModuleExportName :
  //   StringLiteral

  impl()->Expect(Token::EXPORT);
  Statement* result = nullptr;
  ZonePtrList<const AstRawString> names(1, impl()->zone());
  Scanner::Location loc = impl()->scanner()->peek_location();
  switch (impl()->peek()) {
    case Token::DEFAULT:
      return ParseExportDefault();

    case Token::MUL:
      ParseExportStar();
      return impl()->factory()->EmptyStatement();

    case Token::LBRACE: {
      // There are two cases here:
      //
      // 'export' ExportClause ';'
      // and
      // 'export' ExportClause FromClause ';'
      //
      // In the first case, the exported identifiers in ExportClause must
      // not be reserved words, while in the latter they may be. We
      // pass in a location that gets filled with the first reserved word
      // encountered, and then throw a SyntaxError if we are in the
      // non-FromClause case.
      Scanner::Location reserved_loc = Scanner::Location::invalid();
      Scanner::Location string_literal_local_name_loc =
          Scanner::Location::invalid();
      ZoneChunkList<ExportClauseData>* export_data =
          ParseExportClause(&reserved_loc, &string_literal_local_name_loc);
      if (impl()->CheckContextualKeyword(impl()->ast_value_factory()->from_string())) {
        Scanner::Location specifier_loc = impl()->scanner()->peek_location();
        const AstRawString* module_specifier = ParseModuleSpecifier();
        const ImportAssertions* import_assertions = ParseImportAssertClause();
        impl()->ExpectSemicolon();

        if (export_data->is_empty()) {
          impl()->module()->AddEmptyImport(module_specifier, import_assertions,
                                   specifier_loc, impl()->zone());
        } else {
          for (const ExportClauseData& data : *export_data) {
            impl()->module()->AddExport(data.local_name, data.export_name,
                                module_specifier, import_assertions,
                                data.location, specifier_loc, impl()->zone());
          }
        }
      } else {
        if (reserved_loc.IsValid()) {
          // No FromClause, so reserved words are invalid in ExportClause.
          ReportMessageAt(reserved_loc, MessageTemplate::kUnexpectedReserved);
          return nullptr;
        } else if (string_literal_local_name_loc.IsValid()) {
          ReportMessageAt(string_literal_local_name_loc,
                          MessageTemplate::kModuleExportNameWithoutFromClause);
          return nullptr;
        }

        impl()->ExpectSemicolon();

        for (const ExportClauseData& data : *export_data) {
          impl()->module()->AddExport(data.local_name, data.export_name, data.location,
                              impl()->zone());
        }
      }
      return impl()->factory()->EmptyStatement();
    }

    case Token::FUNCTION:
      result = impl()->ParseHoistableDeclaration(&names, false);
      break;

    case Token::CLASS:
      impl()->Consume(Token::CLASS);
      result = impl()->ParseClassDeclaration(&names, false);
      break;

    case Token::VAR:
    case Token::LET:
    case Token::CONST:
      result = impl()->ParseVariableStatement(impl()->kStatementListItem, &names);
      break;

    case Token::ASYNC:
      impl()->Consume(Token::ASYNC);
      if (impl()->peek() == Token::FUNCTION &&
          !impl()->scanner()->HasLineTerminatorBeforeNext()) {
        result = impl()->ParseAsyncFunctionDeclaration(&names, false);
        break;
      }
      V8_FALLTHROUGH;

    default:
      impl()->ReportUnexpectedToken(impl()->scanner()->current_token());
      return nullptr;
  }
  loc.end_pos = impl()->scanner()->location().end_pos;

  SourceTextModuleDescriptor* descriptor = impl()->module();
  for (const AstRawString* name : names) {
    descriptor->AddExport(name, name, loc, impl()->zone());
  }

  return result;
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclareUnboundVariable(const AstRawString* name, VariableMode mode,
                                    InitializationFlag init, int pos) {
  bool was_added;
  Variable* var = DeclareVariable(name, NORMAL_VARIABLE, mode, init, impl()->scope(),
                                  &was_added, pos, impl()->end_position());
  // The variable will be added to the declarations list, but since we are not
  // binding it to anything, we can simply ignore it here.
  USE(var);
}

template <typename Impl>
inline VariableProxy* AbstractParser<Impl>::DeclareBoundVariable(const AstRawString* name,
                                            VariableMode mode, int pos) {
  DCHECK_NOT_NULL(name);
  VariableProxy* proxy =
      impl()->factory()->NewVariableProxy(name, NORMAL_VARIABLE, impl()->position());
  bool was_added;
  Variable* var = DeclareVariable(name, NORMAL_VARIABLE, mode,
                                  Variable::DefaultInitializationFlag(mode),
                                  impl()->scope(), &was_added, pos, impl()->end_position());
  proxy->BindTo(var);
  return proxy;
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclareAndBindVariable(VariableProxy* proxy, VariableKind kind,
                                    VariableMode mode, Scope* scope,
                                    bool* was_added, int initializer_position) {
  Variable* var = DeclareVariable(
      proxy->raw_name(), kind, mode, Variable::DefaultInitializationFlag(mode),
      scope, was_added, proxy->position(), kNoSourcePosition);
  var->set_initializer_position(initializer_position);
  proxy->BindTo(var);
}

template <typename Impl>
inline Variable* AbstractParser<Impl>::DeclareVariable(const AstRawString* name, VariableKind kind,
                                  VariableMode mode, InitializationFlag init,
                                  Scope* scope, bool* was_added, int begin,
                                  int end) {
  Declaration* declaration;
  if (mode == VariableMode::kVar && !scope->is_declaration_scope()) {
    DCHECK(scope->is_block_scope() || scope->is_with_scope());
    declaration = impl()->factory()->NewNestedVariableDeclaration(scope, begin);
  } else {
    declaration = impl()->factory()->NewVariableDeclaration(begin);
  }
  Declare(declaration, name, kind, mode, init, scope, was_added, begin, end);
  return declaration->var();
}

template <typename Impl>
inline void AbstractParser<Impl>::Declare(Declaration* declaration, const AstRawString* name,
                     VariableKind variable_kind, VariableMode mode,
                     InitializationFlag init, Scope* scope, bool* was_added,
                     int var_begin_pos, int var_end_pos) {
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
    ++use_counts_[v8::Isolate::kSloppyModeBlockScopedFunctionRedefinition];
  }
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::BuildInitializationBlock(
    DeclarationParsingResult* parsing_result) {
  ScopedPtrList<Statement> statements(impl()->pointer_buffer());
  for (const auto& declaration : parsing_result->declarations) {
    if (!declaration.initializer) continue;
    InitializeVariables(&statements, parsing_result->descriptor.kind,
                        &declaration);
  }
  return impl()->factory()->NewBlock(true, statements);
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::DeclareFunction(const AstRawString* variable_name,
                                   FunctionLiteral* function, VariableMode mode,
                                   VariableKind kind, int beg_pos, int end_pos,
                                   ZonePtrList<const AstRawString>* names) {
  Declaration* declaration =
      impl()->factory()->NewFunctionDeclaration(function, beg_pos);
  bool was_added;
  Declare(declaration, variable_name, kind, mode, kCreatedInitialized, impl()->scope(),
          &was_added, beg_pos);
  if (info()->flags().coverage_enabled()) {
    // Force the function to be allocated when collecting source coverage, so
    // that even dead functions get source coverage data.
    declaration->var()->set_is_used();
  }
  if (names) names->Add(variable_name, impl()->zone());
  if (kind == SLOPPY_BLOCK_FUNCTION_VARIABLE) {
    Token::Value init = impl()->loop_nesting_depth() > 0 ? Token::ASSIGN : Token::INIT;
    SloppyBlockFunctionStatement* statement =
        impl()->factory()->NewSloppyBlockFunctionStatement(end_pos, declaration->var(),
                                                   init);
    impl()->GetDeclarationScope()->DeclareSloppyBlockFunction(statement);
    return statement;
  }
  return impl()->factory()->EmptyStatement();
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::DeclareClass(const AstRawString* variable_name,
                                Expression* value,
                                ZonePtrList<const AstRawString>* names,
                                int class_token_pos, int end_pos) {
  VariableProxy* proxy =
      DeclareBoundVariable(variable_name, VariableMode::kLet, class_token_pos);
  proxy->var()->set_initializer_position(end_pos);
  VariableProxyExpression* proxy_expr =
      impl()->factory()->NewVariableProxyExpression(proxy);
  if (names) names->Add(variable_name, impl()->zone());

  Assignment* assignment =
      impl()->factory()->NewAssignment(Token::INIT, proxy_expr, value, class_token_pos);
  return IgnoreCompletion(
      impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition));
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::DeclareNative(const AstRawString* name, int pos) {
  // Make sure that the function containing the native declaration
  // isn't lazily compiled. The extension structures are only
  // accessible while parsing the first time not when reparsing
  // because of lazy compilation.
  impl()->GetClosureScope()->ForceEagerCompilation();

  // TODO(1240846): It's weird that native function declarations are
  // introduced dynamically when we meet their declarations, whereas
  // other functions are set up when entering the surrounding scope.
  VariableProxyExpression* proxy = impl()->factory()->NewVariableProxyExpression(DeclareBoundVariable(name, VariableMode::kVar, pos));
  NativeFunctionLiteral* lit =
      impl()->factory()->NewNativeFunctionLiteral(name, impl()->extension_, kNoSourcePosition);
  return impl()->factory()->NewExpressionStatement(
      impl()->factory()->NewAssignment(Token::INIT, proxy, lit, kNoSourcePosition),
      pos);
}

template <typename Impl>
inline Block* AbstractParser<Impl>::IgnoreCompletion(Statement* statement) {
  Block* block = impl()->factory()->NewBlock(1, true);
  block->statements()->Add(statement, impl()->zone());
  return block;
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::RewriteReturn(Expression* return_value, int pos) {
  if (IsDerivedConstructor(impl()->function_state_->kind())) {
    // For subclass constructors we need to return this in case of undefined;
    // other primitive values trigger an exception in the ConstructStub.
    //
    //   return expr;
    //
    // Is rewritten as:
    //
    //   return (temp = expr) === undefined ? this : temp;

    // temp = expr
    Variable* temp = NewTemporary(impl()->ast_value_factory()->empty_string());
    Assignment* assign = impl()->factory()->NewAssignment(
        Token::ASSIGN, impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temp)), return_value, pos);

    // temp === undefined
    Expression* is_undefined = impl()->factory()->NewCompareOperation(
        Token::EQ_STRICT, assign,
        impl()->factory()->NewUndefinedLiteral(kNoSourcePosition), pos);

    // is_undefined ? this : temp
    // We don't need to call UseThis() since it's guaranteed to be called
    // for derived constructors after parsing the constructor in
    // ParseFunctionBody.
    return_value =
        impl()->factory()->NewConditional(is_undefined, impl()->factory()->ThisExpression(),
                                  impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temp)), pos);
  }
  return return_value;
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::RewriteSwitchStatement(SwitchStatement* switch_statement,
                                          Scope* scope) {
  // In order to get the CaseClauses to execute in their own lexical scope,
  // but without requiring downstream code to have special scope handling
  // code for switch statements, desugar into blocks as follows:
  // {  // To group the statements--harmless to evaluate Expression in scope
  //   .tag_variable = Expression;
  //   {  // To give CaseClauses a scope
  //     switch (.tag_variable) { CaseClause* }
  //   }
  // }
  DCHECK_NOT_NULL(scope);
  DCHECK(scope->is_block_scope());
  DCHECK_GE(switch_statement->position(), scope->start_position());
  DCHECK_LT(switch_statement->position(), scope->end_position());

  Block* switch_block = impl()->factory()->NewBlock(2, false);

  Expression* tag = switch_statement->tag();
  Variable* tag_variable =
      NewTemporary(impl()->ast_value_factory()->dot_switch_tag_string());
  Assignment* tag_assign = impl()->factory()->NewAssignment(
      Token::ASSIGN, impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(tag_variable)), tag,
      tag->position());
  // Wrap with IgnoreCompletion so the tag isn't returned as the completion
  // value, in case the switch statements don't have a value.
  Statement* tag_statement = IgnoreCompletion(
      impl()->factory()->NewExpressionStatement(tag_assign, kNoSourcePosition));
  switch_block->statements()->Add(tag_statement, impl()->zone());

  switch_statement->set_tag(impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(tag_variable)));
  Block* cases_block = impl()->factory()->NewBlock(1, false);
  cases_block->statements()->Add(switch_statement, impl()->zone());
  cases_block->set_scope(scope);
  switch_block->statements()->Add(cases_block, impl()->zone());
  return switch_block;
}

template <typename Impl>
inline void AbstractParser<Impl>::InitializeVariables(
    ScopedPtrList<Statement>* statements, VariableKind kind,
    const typename DeclarationParsingResult::Declaration* declaration) {
  if (impl()->has_error()) return;

  DCHECK_NOT_NULL(declaration->initializer);

  int pos = declaration->value_beg_pos;
  if (pos == kNoSourcePosition) {
    pos = declaration->initializer->position();
  }
  Assignment* assignment = impl()->factory()->NewAssignment(
      Token::INIT, declaration->pattern, declaration->initializer, pos);
  statements->Add(impl()->factory()->NewExpressionStatement(assignment, pos));
}

template <typename Impl>
inline Block* AbstractParser<Impl>::RewriteCatchPattern(CatchInfo* catch_info) {
  DCHECK_NOT_NULL(catch_info->pattern);

  typename DeclarationParsingResult::Declaration decl(
      catch_info->pattern, impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(catch_info->variable)));

  ScopedPtrList<Statement> init_statements(impl()->pointer_buffer());
  InitializeVariables(&init_statements, NORMAL_VARIABLE, &decl);
  return impl()->factory()->NewBlock(true, init_statements);
}

template <typename Impl>
inline void AbstractParser<Impl>::ReportVarRedeclarationIn(const AstRawString* name, Scope* scope) {
  for (Declaration* decl : *scope->declarations()) {
    if (decl->var()->raw_name() == name) {
      int position = decl->position();
      Scanner::Location location =
          position == kNoSourcePosition
              ? Scanner::Location::invalid()
              : Scanner::Location(position, position + name->length());
      ReportMessageAt(location, MessageTemplate::kVarRedeclaration, name);
      return;
    }
  }
  UNREACHABLE();
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::RewriteTryStatement(Block* try_block, Block* catch_block,
                                       const SourceRange& catch_range,
                                       Block* finally_block,
                                       const SourceRange& finally_range,
                                       const CatchInfo& catch_info, int pos) {
  // Simplify the AST nodes by converting:
  //   'try B0 catch B1 finally B2'
  // to:
  //   'try { try B0 catch B1 } finally B2'

  if (catch_block != nullptr && finally_block != nullptr) {
    // If we have both, create an inner try/catch.
    TryCatchStatement* statement;
    statement = impl()->factory()->NewTryCatchStatement(try_block, catch_info.scope,
                                                catch_block, kNoSourcePosition);
    RecordTryCatchStatementSourceRange(statement, catch_range);

    try_block = impl()->factory()->NewBlock(1, false);
    try_block->statements()->Add(statement, impl()->zone());
    catch_block = nullptr;  // Clear to indicate it's been handled.
  }

  if (catch_block != nullptr) {
    DCHECK_NULL(finally_block);
    TryCatchStatement* stmt = impl()->factory()->NewTryCatchStatement(
        try_block, catch_info.scope, catch_block, pos);
    RecordTryCatchStatementSourceRange(stmt, catch_range);
    return stmt;
  } else {
    DCHECK_NOT_NULL(finally_block);
    TryFinallyStatement* stmt =
        impl()->factory()->NewTryFinallyStatement(try_block, finally_block, pos);
    RecordTryFinallyStatementSourceRange(stmt, finally_range);
    return stmt;
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseAndRewriteGeneratorFunctionBody(
    int pos, FunctionKind kind, ScopedPtrList<Statement>* body) {
  // For ES6 Generators, we just prepend the initial yield.
  Expression* initial_yield = BuildInitialYield(pos, kind);
  body->Add(
      impl()->factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
  impl()->ParseStatementList(body, Token::RBRACE);
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseAndRewriteAsyncGeneratorFunctionBody(
    int pos, FunctionKind kind, ScopedPtrList<Statement>* body) {
  // For ES2017 Async Generators, we produce:
  //
  // try {
  //   InitialYield;
  //   ...body...;
  //   // fall through to the implicit return after the try-finally
  // } catch (.catch) {
  //   %AsyncGeneratorReject(generator, .catch);
  // } finally {
  //   %_GeneratorClose(generator);
  // }
  //
  // - InitialYield yields the actual generator object.
  // - Any return statement inside the body will have its argument wrapped
  //   in an iterator result object with a "done" property set to `true`.
  // - If the generator terminates for whatever reason, we must close it.
  //   Hence the finally clause.
  // - BytecodeGenerator performs special handling for ReturnStatements in
  //   async generator functions, resolving the appropriate Promise with an
  //   "done" iterator result object containing a Promise-unwrapped value.
  DCHECK(IsAsyncGeneratorFunction(kind));

  Block* try_block;
  {
    ScopedPtrList<Statement> statements(impl()->pointer_buffer());
    Expression* initial_yield = BuildInitialYield(pos, kind);
    statements.Add(
        impl()->factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
    impl()->ParseStatementList(&statements, Token::RBRACE);
    // Since the whole body is wrapped in a try-catch, make the implicit
    // end-of-function return explicit to ensure BytecodeGenerator's special
    // handling for ReturnStatements in async generators applies.
    statements.Add(impl()->factory()->NewSyntheticAsyncReturnStatement(
        impl()->factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition));

    // Don't create iterator result for async generators, as the resume methods
    // will create it.
    try_block = impl()->factory()->NewBlock(false, statements);
  }

  // For AsyncGenerators, a top-level catch block will reject the Promise.
  Scope* catch_scope = NewHiddenCatchScope();

  Block* catch_block;
  {
    ScopedPtrList<Expression> reject_args(impl()->pointer_buffer());
    reject_args.Add(impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(
        impl()->function_state_->scope()->generator_object_var())));
    reject_args.Add(impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(catch_scope->catch_variable())));

    Expression* reject_call = impl()->factory()->NewCallRuntime(
        Runtime::kInlineAsyncGeneratorReject, reject_args, kNoSourcePosition);
    catch_block = IgnoreCompletion(
        impl()->factory()->NewReturnStatement(reject_call, kNoSourcePosition));
  }

  {
    ScopedPtrList<Statement> statements(impl()->pointer_buffer());
    TryStatement* try_catch = impl()->factory()->NewTryCatchStatementForAsyncAwait(
        try_block, catch_scope, catch_block, kNoSourcePosition);
    statements.Add(try_catch);
    try_block = impl()->factory()->NewBlock(false, statements);
  }

  Expression* close_call;
  {
    ScopedPtrList<Expression> close_args(impl()->pointer_buffer());
    VariableProxy* call_proxy = impl()->factory()->NewVariableProxy(
        impl()->function_state_->scope()->generator_object_var());
    close_args.Add(impl()->factory()->NewVariableProxyExpression(call_proxy));
    close_call = impl()->factory()->NewCallRuntime(Runtime::kInlineGeneratorClose,
                                           close_args, kNoSourcePosition);
  }

  Block* finally_block;
  {
    ScopedPtrList<Statement> statements(impl()->pointer_buffer());
    statements.Add(
        impl()->factory()->NewExpressionStatement(close_call, kNoSourcePosition));
    finally_block = impl()->factory()->NewBlock(false, statements);
  }

  body->Add(impl()->factory()->NewTryFinallyStatement(try_block, finally_block,
                                              kNoSourcePosition));
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclareFunctionNameVar(const AstRawString* function_name,
                                    FunctionSyntaxKind function_syntax_kind,
                                    DeclarationScope* function_scope) {
  if (function_syntax_kind == FunctionSyntaxKind::kNamedExpression &&
      function_scope->LookupLocal(function_name) == nullptr) {
    DCHECK_EQ(function_scope, impl()->scope());
    function_scope->DeclareFunctionVar(function_name);
  }
}

// Special case for legacy for
//
//    for (var x = initializer in enumerable) body
//
// An initialization block of the form
//
//    {
//      x = initializer;
//    }
//
// is returned in this case.  It has reserved space for two statements,
// so that (later on during parsing), the equivalent of
//
//   for (x in enumerable) body
//
// is added as a second statement to it.
template <typename Impl>
inline Block* AbstractParser<Impl>::RewriteForVarInLegacy(const ForInfo& for_info) {
  const typename DeclarationParsingResult::Declaration& decl =
      for_info.parsing_result.declarations[0];
  if (!IsLexicalVariableMode(for_info.parsing_result.descriptor.mode) &&
      decl.initializer != nullptr && decl.pattern->IsVariableProxyExpression()) {
    ++use_counts_[v8::Isolate::kForInInitializer];
    const AstRawString* name = decl.pattern->AsVariableProxyExpression()->raw_name();
    VariableProxyExpression* single_var = impl()->factory()->NewVariableProxyExpression(impl()->NewUnresolved(name));
    Block* init_block = impl()->factory()->NewBlock(2, true);
    init_block->statements()->Add(
        impl()->factory()->NewExpressionStatement(
            impl()->factory()->NewAssignment(Token::ASSIGN, single_var,
                                     decl.initializer, decl.value_beg_pos),
            kNoSourcePosition),
        impl()->zone());
    return init_block;
  }
  return nullptr;
}

// Rewrite a for-in/of statement of the form
//
//   for (let/const/var x in/of e) b
//
// into
//
//   {
//     var temp;
//     for (temp in/of e) {
//       let/const/var x = temp;
//       b;
//     }
//     let x;  // for TDZ
//   }
template <typename Impl>
inline void AbstractParser<Impl>::DesugarBindingInForEachStatement(ForInfo* for_info,
                                              Block** body_block,
                                              Expression** each_variable) {
  DCHECK_EQ(1, for_info->parsing_result.declarations.size());
  typename DeclarationParsingResult::Declaration& decl =
      for_info->parsing_result.declarations[0];
  Variable* temp = NewTemporary(impl()->ast_value_factory()->dot_for_string());
  ScopedPtrList<Statement> each_initialization_statements(impl()->pointer_buffer());
  DCHECK_IMPLIES(!impl()->has_error(), decl.pattern != nullptr);
  decl.initializer = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temp, for_info->position));
  InitializeVariables(&each_initialization_statements, NORMAL_VARIABLE, &decl);

  *body_block = impl()->factory()->NewBlock(3, false);
  (*body_block)
      ->statements()
      ->Add(impl()->factory()->NewBlock(true, each_initialization_statements), impl()->zone());
  *each_variable = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temp, for_info->position));
}

// Create a TDZ for any lexically-bound names in for in/of statements.
template <typename Impl>
inline Block* AbstractParser<Impl>::CreateForEachStatementTDZ(Block* init_block,
                                         const ForInfo& for_info) {
  if (IsLexicalVariableMode(for_info.parsing_result.descriptor.mode)) {
    DCHECK_NULL(init_block);

    init_block = impl()->factory()->NewBlock(1, false);

    for (const AstRawString* bound_name : for_info.bound_names) {
      // TODO(adamk): This needs to be some sort of special
      // INTERNAL variable that's invisible to the debugger
      // but visible to everything else.
      VariableProxy* tdz_proxy = DeclareBoundVariable(
          bound_name, VariableMode::kLet, kNoSourcePosition);
      tdz_proxy->var()->set_initializer_position(impl()->position());
    }
  }
  return init_block;
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::DesugarLexicalBindingsInForStatement(
    ForStatement* loop, Statement* init, Expression* cond, Statement* next,
    Statement* body, Scope* inner_scope, const ForInfo& for_info) {
  // ES6 13.7.4.8 specifies that on each loop iteration the let variables are
  // copied into a new environment.  Moreover, the "next" statement must be
  // evaluated not in the environment of the just completed iteration but in
  // that of the upcoming one.  We achieve this with the following desugaring.
  // Extra care is needed to preserve the completion value of the original loop.
  //
  // We are given a for statement of the form
  //
  //  labels: for (let/const x = i; cond; next) body
  //
  // and rewrite it as follows.  Here we write {{ ... }} for init-blocks, ie.,
  // blocks whose ignore_completion_value_ flag is set.
  //
  //  {
  //    let/const x = i;
  //    temp_x = x;
  //    first = 1;
  //    undefined;
  //    outer: for (;;) {
  //      let/const x = temp_x;
  //      {{ if (first == 1) {
  //           first = 0;
  //         } else {
  //           next;
  //         }
  //         flag = 1;
  //         if (!cond) break;
  //      }}
  //      labels: for (; flag == 1; flag = 0, temp_x = x) {
  //        body
  //      }
  //      {{ if (flag == 1)  // Body used break.
  //           break;
  //      }}
  //    }
  //  }

  DCHECK_GT(for_info.bound_names.length(), 0);
  ScopedPtrList<Variable> temps(impl()->pointer_buffer());

  Block* outer_block =
      impl()->factory()->NewBlock(for_info.bound_names.length() + 4, false);

  // Add statement: let/const x = i.
  outer_block->statements()->Add(init, impl()->zone());

  const AstRawString* temp_name = impl()->ast_value_factory()->dot_for_string();

  // For each lexical variable x:
  //   make statement: temp_x = x.
  for (const AstRawString* bound_name : for_info.bound_names) {
    VariableProxyExpression* proxy = impl()->factory()->NewVariableProxyExpression(impl()->NewUnresolved(bound_name));
    Variable* temp = NewTemporary(temp_name);
    VariableProxyExpression* temp_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temp));
    Assignment* assignment = impl()->factory()->NewAssignment(Token::ASSIGN, temp_proxy,
                                                      proxy, kNoSourcePosition);
    Statement* assignment_statement =
        impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    outer_block->statements()->Add(assignment_statement, impl()->zone());
    temps.Add(temp);
  }

  Variable* first = nullptr;
  // Make statement: first = 1.
  if (next) {
    first = NewTemporary(temp_name);
    VariableProxyExpression* first_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(first));
    Expression* const1 = impl()->factory()->NewSmiLiteral(1, kNoSourcePosition);
    Assignment* assignment = impl()->factory()->NewAssignment(
        Token::ASSIGN, first_proxy, const1, kNoSourcePosition);
    Statement* assignment_statement =
        impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    outer_block->statements()->Add(assignment_statement, impl()->zone());
  }

  // make statement: undefined;
  outer_block->statements()->Add(
      impl()->factory()->NewExpressionStatement(
          impl()->factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition),
      impl()->zone());

  // Make statement: outer: for (;;)
  // Note that we don't actually create the label, or set this loop up as an
  // explicit break target, instead handing it directly to those nodes that
  // need to know about it. This should be safe because we don't run any code
  // in this function that looks up break targets.
  ForStatement* outer_loop = impl()->factory()->NewForStatement(kNoSourcePosition);
  outer_block->statements()->Add(outer_loop, impl()->zone());
  outer_block->set_scope(impl()->scope());

  Block* inner_block = impl()->factory()->NewBlock(3, false);
  {
    BlockState block_state(&impl()->scope_, inner_scope);

    Block* ignore_completion_block =
        impl()->factory()->NewBlock(for_info.bound_names.length() + 3, true);
    ScopedPtrList<Variable> inner_vars(impl()->pointer_buffer());
    // For each let variable x:
    //    make statement: let/const x = temp_x.
    for (int i = 0; i < for_info.bound_names.length(); i++) {
      VariableProxyExpression* proxy = impl()->factory()->NewVariableProxyExpression(DeclareBoundVariable(
          for_info.bound_names[i], for_info.parsing_result.descriptor.mode,
          kNoSourcePosition));
      inner_vars.Add(proxy->var());
      VariableProxyExpression* temp_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temps.at(i)));
      Assignment* assignment = impl()->factory()->NewAssignment(
          Token::INIT, proxy, temp_proxy, kNoSourcePosition);
      Statement* assignment_statement =
          impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      int declaration_pos = for_info.parsing_result.descriptor.declaration_pos;
      DCHECK_NE(declaration_pos, kNoSourcePosition);
      proxy->var()->set_initializer_position(declaration_pos);
      ignore_completion_block->statements()->Add(assignment_statement, impl()->zone());
    }

    // Make statement: if (first == 1) { first = 0; } else { next; }
    if (next) {
      DCHECK(first);
      Expression* compare = nullptr;
      // Make compare expression: first == 1.
      {
        Expression* const1 = impl()->factory()->NewSmiLiteral(1, kNoSourcePosition);
        VariableProxyExpression* first_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(first));
        compare = impl()->factory()->NewCompareOperation(Token::EQ, first_proxy, const1,
                                                 kNoSourcePosition);
      }
      Statement* clear_first = nullptr;
      // Make statement: first = 0.
      {
        VariableProxyExpression* first_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(first));
        Expression* const0 = impl()->factory()->NewSmiLiteral(0, kNoSourcePosition);
        Assignment* assignment = impl()->factory()->NewAssignment(
            Token::ASSIGN, first_proxy, const0, kNoSourcePosition);
        clear_first =
            impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      }
      Statement* clear_first_or_next = impl()->factory()->NewIfStatement(
          compare, clear_first, next, kNoSourcePosition);
      ignore_completion_block->statements()->Add(clear_first_or_next, impl()->zone());
    }

    Variable* flag = NewTemporary(temp_name);
    // Make statement: flag = 1.
    {
      VariableProxyExpression* flag_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(flag));
      Expression* const1 = impl()->factory()->NewSmiLiteral(1, kNoSourcePosition);
      Assignment* assignment = impl()->factory()->NewAssignment(
          Token::ASSIGN, flag_proxy, const1, kNoSourcePosition);
      Statement* assignment_statement =
          impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      ignore_completion_block->statements()->Add(assignment_statement, impl()->zone());
    }

    // Make statement: if (!cond) break.
    if (cond) {
      Statement* stop =
          impl()->factory()->NewBreakStatement(outer_loop, kNoSourcePosition);
      Statement* noop = impl()->factory()->EmptyStatement();
      ignore_completion_block->statements()->Add(
          impl()->factory()->NewIfStatement(cond, noop, stop, cond->position()),
          impl()->zone());
    }

    inner_block->statements()->Add(ignore_completion_block, impl()->zone());
    // Make cond expression for main loop: flag == 1.
    Expression* flag_cond = nullptr;
    {
      Expression* const1 = impl()->factory()->NewSmiLiteral(1, kNoSourcePosition);
      VariableProxyExpression* flag_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(flag));
      flag_cond = impl()->factory()->NewCompareOperation(Token::EQ, flag_proxy, const1,
                                                 kNoSourcePosition);
    }

    // Create chain of expressions "flag = 0, temp_x = x, ..."
    Statement* compound_next_statement = nullptr;
    {
      Expression* compound_next = nullptr;
      // Make expression: flag = 0.
      {
        VariableProxyExpression* flag_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(flag));
        Expression* const0 = impl()->factory()->NewSmiLiteral(0, kNoSourcePosition);
        compound_next = impl()->factory()->NewAssignment(Token::ASSIGN, flag_proxy,
                                                 const0, kNoSourcePosition);
      }

      // Make the comma-separated list of temp_x = x assignments.
      int inner_var_proxy_pos = impl()->scanner()->location().beg_pos;
      for (int i = 0; i < for_info.bound_names.length(); i++) {
        VariableProxyExpression* temp_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(temps.at(i)));
        VariableProxyExpression* proxy =
            impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(inner_vars.at(i), inner_var_proxy_pos));
        Assignment* assignment = impl()->factory()->NewAssignment(
            Token::ASSIGN, temp_proxy, proxy, kNoSourcePosition);
        compound_next = impl()->factory()->NewBinaryOperation(
            Token::COMMA, compound_next, assignment, kNoSourcePosition);
      }

      compound_next_statement =
          impl()->factory()->NewExpressionStatement(compound_next, kNoSourcePosition);
    }

    // Make statement: labels: for (; flag == 1; flag = 0, temp_x = x)
    // Note that we re-use the original loop node, which retains its labels
    // and ensures that any break or continue statements in body point to
    // the right place.
    loop->Initialize(nullptr, flag_cond, compound_next_statement, body);
    inner_block->statements()->Add(loop, impl()->zone());

    // Make statement: {{if (flag == 1) break;}}
    {
      Expression* compare = nullptr;
      // Make compare expresion: flag == 1.
      {
        Expression* const1 = impl()->factory()->NewSmiLiteral(1, kNoSourcePosition);
        VariableProxyExpression* flag_proxy = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(flag));
        compare = impl()->factory()->NewCompareOperation(Token::EQ, flag_proxy, const1,
                                                 kNoSourcePosition);
      }
      Statement* stop =
          impl()->factory()->NewBreakStatement(outer_loop, kNoSourcePosition);
      Statement* empty = impl()->factory()->EmptyStatement();
      Statement* if_flag_break =
          impl()->factory()->NewIfStatement(compare, stop, empty, kNoSourcePosition);
      inner_block->statements()->Add(IgnoreCompletion(if_flag_break), impl()->zone());
    }

    inner_block->set_scope(inner_scope);
  }

  outer_loop->Initialize(nullptr, nullptr, nullptr, inner_block);

  return outer_block;
}

template <typename Impl>
inline void ParserFormalParameters::ValidateDuplicate(AbstractParser<Impl>* parser) const {
  if (has_duplicate()) {
    parser->ReportMessageAt(duplicate_loc, MessageTemplate::kParamDupe);
  }
}
template <typename Impl>
inline void ParserFormalParameters::ValidateStrictMode(AbstractParser<Impl>* parser) const {
  if (strict_error_loc.IsValid()) {
    parser->ReportMessageAt(strict_error_loc, strict_error_message);
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::AddArrowFunctionFormalParameters(
    ParserFormalParameters* parameters, Expression* expr, int end_pos) {
  // ArrowFunctionFormals ::
  //    Nary(Token::COMMA, VariableProxy*, Tail)
  //    Binary(Token::COMMA, NonTailArrowFunctionFormals, Tail)
  //    Tail
  // NonTailArrowFunctionFormals ::
  //    Binary(Token::COMMA, NonTailArrowFunctionFormals, VariableProxy)
  //    VariableProxy
  // Tail ::
  //    VariableProxy
  //    Spread(VariableProxy)
  //
  // We need to visit the parameters in left-to-right order
  //

  // For the Nary case, we simply visit the parameters in a loop.
  if (expr->IsNaryOperation()) {
    NaryOperation* nary = expr->AsNaryOperation();
    // The classifier has already run, so we know that the expression is a valid
    // arrow function formals production.
    DCHECK_EQ(nary->op(), Token::COMMA);
    // Each op position is the end position of the *previous* expr, with the
    // second (i.e. first "subsequent") op position being the end position of
    // the first child expression.
    Expression* next = nary->first();
    for (size_t i = 0; i < nary->subsequent_length(); ++i) {
      AddArrowFunctionFormalParameters(parameters, next,
                                       nary->subsequent_op_position(i));
      next = nary->subsequent(i);
    }
    AddArrowFunctionFormalParameters(parameters, next, end_pos);
    return;
  }

  // For the binary case, we recurse on the left-hand side of binary comma
  // expressions.
  if (expr->IsBinaryOperation()) {
    BinaryOperation* binop = expr->AsBinaryOperation();
    // The classifier has already run, so we know that the expression is a valid
    // arrow function formals production.
    DCHECK_EQ(binop->op(), Token::COMMA);
    Expression* left = binop->left();
    Expression* right = binop->right();
    int comma_pos = binop->position();
    AddArrowFunctionFormalParameters(parameters, left, comma_pos);
    // LHS of comma expression should be unparenthesized.
    expr = right;
  }

  // Only the right-most expression may be a rest parameter.
  DCHECK(!parameters->has_rest);

  bool is_rest = expr->IsSpread();
  if (is_rest) {
    expr = expr->AsSpread()->expression();
    parameters->has_rest = true;
  }
  DCHECK_IMPLIES(parameters->is_simple, !is_rest);
  DCHECK_IMPLIES(parameters->is_simple, expr->IsVariableProxyExpression());

  Expression* initializer = nullptr;
  if (expr->IsAssignment()) {
    Assignment* assignment = expr->AsAssignment();
    DCHECK(!assignment->IsCompoundAssignment());
    initializer = assignment->value();
    expr = assignment->target();
  }

  AddFormalParameter(parameters, expr, initializer, end_pos, is_rest);
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclareArrowFunctionFormalParameters(
    ParserFormalParameters* parameters, Expression* expr,
    const Scanner::Location& params_loc) {
  if (expr->IsEmptyParentheses() || impl()->has_error()) return;

  AddArrowFunctionFormalParameters(parameters, expr, params_loc.end_pos);

  if (parameters->arity > Code::kMaxArguments) {
    ReportMessageAt(params_loc, MessageTemplate::kMalformedArrowFunParamList);
    return;
  }

  DeclareFormalParameters(parameters);
  DCHECK_IMPLIES(parameters->is_simple,
                 parameters->scope->has_simple_parameters());
}

template <typename Impl>
inline void AbstractParser<Impl>::PrepareGeneratorVariables() {
  // Calling a generator returns a generator object.  That object is stored
  // in a temporary variable, a definition that is used by "yield"
  // expressions.
  impl()->function_state_->scope()->DeclareGeneratorObjectVar(
      impl()->ast_value_factory()->dot_generator_object_string());
}

template <typename Impl>
inline FunctionLiteral* AbstractParser<Impl>::ParseFunctionLiteral(
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

  int pos = function_token_pos == kNoSourcePosition ? impl()->peek_position()
                                                    : function_token_pos;
  DCHECK_NE(kNoSourcePosition, pos);

  // Anonymous functions were passed either the empty symbol or a null
  // handle as the function name.  Remember if we were passed a non-empty
  // handle to decide whether to invoke function name inference.
  bool should_infer_name = function_name == nullptr;

  // We want a non-null handle as the function name by default. We will handle
  // the "function does not have a shared name" case later.
  if (should_infer_name) {
    function_name = impl()->ast_value_factory()->empty_string();
  }

  FunctionLiteral::EagerCompileHint eager_compile_hint =
      impl()->function_state_->next_function_is_likely_called() || is_wrapped
          ? FunctionLiteral::kShouldEagerCompile
          : impl()->default_eager_compile_hint();

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
  DCHECK_IMPLIES(parse_lazily(), impl()->has_error() || allow_lazy_);
  DCHECK_IMPLIES(parse_lazily(), impl()->extension_ == nullptr);

  const bool is_lazy =
      eager_compile_hint == FunctionLiteral::kShouldLazyCompile;
  const bool is_top_level = AllowsLazyParsingWithoutUnresolvedVariables();
  const bool is_eager_top_level_function = !is_lazy && is_top_level;
  const bool is_lazy_top_level_function = is_lazy && is_top_level;
  const bool is_lazy_inner_function = is_lazy && !is_top_level;

  RCS_SCOPE(impl()->runtime_call_stats_, RuntimeCallCounterId::kParseFunctionLiteral,
            RuntimeCallStats::kThreadSpecific);
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(FLAG_log_function_events)) timer.Start();

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

  const bool should_preparse_inner = parse_lazily() && is_lazy_inner_function;

  // If parallel compile tasks are enabled, and the function is an eager
  // top level function, then we can pre-parse the function and parse / compile
  // in a parallel task on a worker thread.
  bool should_post_parallel_task =
      parse_lazily() && is_eager_top_level_function &&
      FLAG_parallel_compile_tasks && info()->parallel_tasks() &&
      impl()->scanner()->stream()->can_be_cloned_for_parallel_access();

  // This may be modified later to reflect preparsing decision taken
  bool should_preparse = (parse_lazily() && is_lazy_top_level_function) ||
                         should_preparse_inner || should_post_parallel_task;

  bool should_post_parallel_binast_task = 
      should_preparse && /* is_top_level &&*/
      impl()->original_scope_->scope_type() == SCRIPT_SCOPE &&
      info()->parallel_tasks() &&
      impl()->scanner()->stream()->can_be_cloned_for_parallel_access();

  ScopedPtrList<Statement> body(impl()->pointer_buffer());
  int expected_property_count = 0;
  int suspend_count = -1;
  int num_parameters = -1;
  int function_length = -1;
  bool has_duplicate_parameters = false;
  int function_literal_id = impl()->GetNextFunctionLiteralId();
  ProducedPreparseData* produced_preparse_data = nullptr;

  // This Scope lives in the main zone. We'll migrate data into that zone later.
  Zone* parse_zone = should_preparse ? &preparser_zone_ : impl()->zone();
  DeclarationScope* scope = impl()->NewFunctionScope(kind, parse_zone);
  SetLanguageMode(scope, language_mode);
#ifdef DEBUG
  scope->SetScopeName(function_name);
#endif

  if (!is_wrapped && V8_UNLIKELY(!impl()->Check(Token::LPAREN))) {
    impl()->ReportUnexpectedToken(impl()->Next());
    return nullptr;
  }
  scope->set_start_position(impl()->position());

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
    if (should_preparse) impl()->Consume(Token::LPAREN);
    should_post_parallel_task = false;
    should_post_parallel_binast_task = false;
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
    impl()->logger_->FunctionEvent(
        event_name, impl()->flags().script_id(), ms, scope->start_position(),
        scope->end_position(),
        reinterpret_cast<const char*>(function_name->raw_data()),
        function_name->byte_length(), function_name->is_one_byte());
  }
#ifdef V8_RUNTIME_CALL_STATS
  if (did_preparse_successfully && impl()->runtime_call_stats_ &&
      V8_UNLIKELY(TracingFlags::is_runtime_stats_enabled())) {
    impl()->runtime_call_stats_->CorrectCurrentCounterId(
        RuntimeCallCounterId::kPreParseWithVariableResolution,
        RuntimeCallStats::kThreadSpecific);
  }
#endif  // V8_RUNTIME_CALL_STATS

  // Validate function name. We can do this only after parsing the function,
  // since the function can declare itself strict.
  language_mode = scope->language_mode();
  impl()->CheckFunctionName(language_mode, function_name, function_name_validity,
                    function_name_location);

  if (is_strict(language_mode)) {
    impl()->CheckStrictOctalLiteral(scope->start_position(), scope->end_position());
  }

  FunctionLiteral::ParameterFlag duplicate_parameters =
      has_duplicate_parameters ? FunctionLiteral::kHasDuplicateParameters
                               : FunctionLiteral::kNoDuplicateParameters;

  // Note that the FunctionLiteral needs to be created in the main Zone again.
  FunctionLiteral* function_literal = impl()->factory()->NewFunctionLiteral(
      function_name, scope, body, expected_property_count, num_parameters,
      function_length, duplicate_parameters, function_syntax_kind,
      eager_compile_hint, pos, true, function_literal_id, impl()->speculative_parse_failure_reason(),
      produced_preparse_data);
  function_literal->set_function_token_position(function_token_pos);
  function_literal->set_suspend_count(suspend_count);

  RecordFunctionLiteralSourceRange(function_literal);

  if (should_post_parallel_task) {
    // Start a parallel parse / compile task on the compiler dispatcher.
    info()->parallel_tasks()->EnqueueCompileTask(info(), function_name, function_literal);
  }

  // TODO(binast): Figure out and fix issue where class constructors references to super cause background parse task to fail.
  // Seems to be an issue with finding home object in scope chain. If it's not fixable then refactor this into something more
  // formal/descriptive (e.g. AllowsBackgroundLazyParsing)
  //printf("function kind = %d\n", function_literal->kind());
  bool can_background_parse_function_kind = 
    !IsClassConstructor(function_literal->kind()) &&
    !IsConciseMethod(function_literal->kind());

  if (should_post_parallel_binast_task && can_background_parse_function_kind) {
    // Start a parallel binAST parse task on the compiler dispatcher.
    info()->parallel_tasks()->EnqueueBinAstParseTask(info(), function_name,
                                                     function_literal);
  } else {
    auto reason = SpeculativeParseFailureReason::kNoTaskEnqueued;
  
    if (!impl()->scanner()->stream()->can_be_cloned()) {
      switch (impl()->scanner()->stream()->stream_kind()) {
        case kOnHeapStream: {
          reason = SpeculativeParseFailureReason::kUnclonableOnHeapStream;
          break;
        }
        case kExternalStringStream: {
          reason = SpeculativeParseFailureReason::kUncloneableExternalStringStream;
          break;
        }
        case kTestingStream: {
          reason = SpeculativeParseFailureReason::kUncloneableTestingStream;
          break;
        }
        case kChunkedStream: {
          reason = SpeculativeParseFailureReason::kUncloneableChunkedStream;
          break;
        }
        case kExternalUtf8Stream: {
          reason = SpeculativeParseFailureReason::kUncloneableExternalUtf8Stream;
          break;
        }
        default: {
          UNREACHABLE();
          break;
        }
      }
    } else if (impl()->scanner()->stream()->can_access_heap()) {
      reason = SpeculativeParseFailureReason::kScannerStreamHeapAccess;
    } else if (!can_background_parse_function_kind) {
      reason = SpeculativeParseFailureReason::kFunctionDisallowsLazyCompilation;
    }
    function_literal->set_speculative_parse_failure_reason(reason);
  }

  if (should_infer_name) {
    impl()->fni_.AddFunction(function_literal);
  }
  return function_literal;
}

template <typename Impl>
inline bool AbstractParser<Impl>::SkipFunction(const AstRawString* function_name, FunctionKind kind,
                          FunctionSyntaxKind function_syntax_kind,
                          DeclarationScope* function_scope, int* num_parameters,
                          int* function_length,
                          ProducedPreparseData** produced_preparse_data) {
  FunctionState function_state(&impl()->function_state_, &impl()->scope_, function_scope);
  function_scope->set_zone(&preparser_zone_);

  DCHECK_NE(kNoSourcePosition, function_scope->start_position());
  DCHECK_EQ(kNoSourcePosition, parameters_end_pos_);

  DCHECK_IMPLIES(IsArrowFunction(kind),
                 impl()->scanner()->current_token() == Token::ARROW);

  // FIXME(marja): There are 2 ways to skip functions now. Unify them.
  if (consumed_preparse_data_) {
    int end_position;
    LanguageMode language_mode;
    int num_inner_functions;
    bool uses_super_property;
    if (impl()->stack_overflow()) return true;
    *produced_preparse_data =
        consumed_preparse_data_->GetDataForSkippableFunction(
            impl()->main_zone(), function_scope->start_position(), &end_position,
            num_parameters, function_length, &num_inner_functions,
            &uses_super_property, &language_mode);

    function_scope->outer_scope()->SetMustUsePreparseData();
    function_scope->set_is_skipped_function(true);
    function_scope->set_end_position(end_position);
    impl()->scanner()->SeekForward(end_position - 1);
    impl()->Expect(Token::RBRACE);
    SetLanguageMode(function_scope, language_mode);
    if (uses_super_property) {
      function_scope->RecordSuperPropertyUsage();
    }
    impl()->SkipFunctionLiterals(num_inner_functions);
    function_scope->ResetAfterPreparsing(impl()->ast_value_factory_, false);
    return true;
  }

  Scanner::BookmarkScope bookmark(impl()->scanner());
  bookmark.Set(function_scope->start_position());

  UnresolvedList::Iterator unresolved_private_tail;
  PrivateNameScopeIterator private_name_scope_iter(function_scope);
  if (!private_name_scope_iter.Done()) {
    unresolved_private_tail =
        private_name_scope_iter.GetScope()->GetUnresolvedPrivateNameTail();
  }

  // With no cached data, we partially parse the function, without building an
  // AST. This gathers the data needed to build a lazy function.
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.PreParse");

  PreParser::PreParseResult result = reusable_preparser()->PreParseFunction(
      function_name, kind, function_syntax_kind, function_scope, use_counts_,
      produced_preparse_data);

  if (result == PreParser::kPreParseStackOverflow) {
    // Propagate stack overflow.
    impl()->set_stack_overflow();
  } else if (impl()->pending_error_handler()->has_error_unidentifiable_by_preparser()) {
    // Make sure we don't re-preparse inner functions of the aborted function.
    // The error might be in an inner function.
    allow_lazy_ = false;
    mode_ = PARSE_EAGERLY;
    DCHECK(!impl()->pending_error_handler()->stack_overflow());
    // If we encounter an error that the preparser can not identify we reset to
    // the state before preparsing. The caller may then fully parse the function
    // to identify the actual error.
    bookmark.Apply();
    if (!private_name_scope_iter.Done()) {
      private_name_scope_iter.GetScope()->ResetUnresolvedPrivateNameTail(
          unresolved_private_tail);
    }
    function_scope->ResetAfterPreparsing(impl()->ast_value_factory_, true);
    impl()->pending_error_handler()->clear_unidentifiable_error();
    return false;
  } else if (impl()->pending_error_handler()->has_pending_error()) {
    DCHECK(!impl()->pending_error_handler()->stack_overflow());
    DCHECK(impl()->has_error());
  } else {
    DCHECK(!impl()->pending_error_handler()->stack_overflow());
    impl()->set_allow_eval_cache(reusable_preparser()->allow_eval_cache());

    PreParserLogger* logger = reusable_preparser()->logger();
    function_scope->set_end_position(logger->end());
    impl()->Expect(Token::RBRACE);
    total_preparse_skipped_ +=
        function_scope->end_position() - function_scope->start_position();
    *num_parameters = logger->num_parameters();
    *function_length = logger->function_length();
    impl()->SkipFunctionLiterals(logger->num_inner_functions());
    if (!private_name_scope_iter.Done()) {
      private_name_scope_iter.GetScope()->MigrateUnresolvedPrivateNameTail(
          impl()->factory(), unresolved_private_tail);
    }
    function_scope->AnalyzePartially(impl(), impl()->factory(), impl()->MaybeParsingArrowhead());
  }

  return true;
}

template <typename Impl>
inline Block* AbstractParser<Impl>::BuildParameterInitializationBlock(
    const ParserFormalParameters& parameters) {
  DCHECK(!parameters.is_simple);
  DCHECK(impl()->scope()->is_function_scope());
  DCHECK_EQ(impl()->scope(), parameters.scope);
  ScopedPtrList<Statement> init_statements(impl()->pointer_buffer());
  int index = 0;
  for (auto parameter : parameters.params) {
    Expression* initial_value =
        impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(parameters.scope->parameter(index)));
    if (parameter->initializer() != nullptr) {
      // IS_UNDEFINED($param) ? initializer : $param

      auto condition = impl()->factory()->NewCompareOperation(
          Token::EQ_STRICT,
          impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(parameters.scope->parameter(index))),
          impl()->factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition);
      initial_value =
          impl()->factory()->NewConditional(condition, parameter->initializer(),
                                    initial_value, kNoSourcePosition);
    }

    BlockState block_state(&impl()->scope_, impl()->scope()->AsDeclarationScope());
    typename DeclarationParsingResult::Declaration decl(parameter->pattern,
                                               initial_value);
    InitializeVariables(&init_statements, PARAMETER_VARIABLE, &decl);

    ++index;
  }
  return impl()->factory()->NewBlock(true, init_statements);
}

template <typename Impl>
inline Scope* AbstractParser<Impl>::NewHiddenCatchScope() {
  Scope* catch_scope = impl()->NewScopeWithParent(impl()->scope(), CATCH_SCOPE);
  bool was_added;
  catch_scope->DeclareLocal(impl()->ast_value_factory()->dot_catch_string(),
                            VariableMode::kVar, NORMAL_VARIABLE, &was_added);
  DCHECK(was_added);
  catch_scope->set_is_hidden();
  return catch_scope;
}

template <typename Impl>
inline Block* AbstractParser<Impl>::BuildRejectPromiseOnException(Block* inner_block,
                                             REPLMode repl_mode) {
  // try {
  //   <inner_block>
  // } catch (.catch) {
  //   return %_AsyncFunctionReject(.generator_object, .catch, can_suspend);
  // }
  Block* result = impl()->factory()->NewBlock(1, true);

  // catch (.catch) {
  //   return %_AsyncFunctionReject(.generator_object, .catch, can_suspend)
  // }
  Scope* catch_scope = NewHiddenCatchScope();

  Expression* reject_promise;
  {
    ScopedPtrList<Expression> args(impl()->pointer_buffer());
    args.Add(impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(
        impl()->function_state_->scope()->generator_object_var())));
    args.Add(impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(catch_scope->catch_variable())));
    args.Add(impl()->factory()->NewBooleanLiteral(impl()->function_state_->CanSuspend(),
                                          kNoSourcePosition));
    reject_promise = impl()->factory()->NewCallRuntime(
        Runtime::kInlineAsyncFunctionReject, args, kNoSourcePosition);
  }
  Block* catch_block = IgnoreCompletion(
      impl()->factory()->NewReturnStatement(reject_promise, kNoSourcePosition));

  // Treat the exception for REPL mode scripts as UNCAUGHT. This will
  // keep the corresponding JSMessageObject alive on the Isolate. The
  // message object is used by the inspector to provide better error
  // messages for REPL inputs that throw.
  TryStatement* try_catch_statement =
      repl_mode == REPLMode::kYes
          ? impl()->factory()->NewTryCatchStatementForReplAsyncAwait(
                inner_block, catch_scope, catch_block, kNoSourcePosition)
          : impl()->factory()->NewTryCatchStatementForAsyncAwait(
                inner_block, catch_scope, catch_block, kNoSourcePosition);
  result->statements()->Add(try_catch_statement, impl()->zone());
  return result;
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::BuildInitialYield(int pos, FunctionKind kind) {
  Expression* yield_result = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(
      impl()->function_state_->scope()->generator_object_var()));
  // The position of the yield is important for reporting the exception
  // caused by calling the .throw method on a generator suspended at the
  // initial yield (i.e. right after generator instantiation).
  impl()->function_state_->AddSuspend();
  return impl()->factory()->NewYield(yield_result, impl()->scope()->start_position(),
                             Suspend::kOnExceptionThrow);
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseFunction(
    ScopedPtrList<Statement>* body, const AstRawString* function_name, int pos,
    FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
    DeclarationScope* function_scope, int* num_parameters, int* function_length,
    bool* has_duplicate_parameters, int* expected_property_count,
    int* suspend_count,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  FunctionParsingScope function_parsing_scope(impl());
  ParsingModeScope mode(impl(), allow_lazy_ ? PARSE_LAZILY : PARSE_EAGERLY);

  FunctionState function_state(&impl()->function_state_, &impl()->scope_, function_scope);

  bool is_wrapped = function_syntax_kind == FunctionSyntaxKind::kWrapped;

  int expected_parameters_end_pos = parameters_end_pos_;
  if (expected_parameters_end_pos != kNoSourcePosition) {
    // This is the first function encountered in a CreateDynamicFunction eval.
    parameters_end_pos_ = kNoSourcePosition;
    // The function name should have been ignored, giving us the empty string
    // here.
    DCHECK_EQ(function_name, impl()->ast_value_factory()->empty_string());
  }

  ParserFormalParameters formals(function_scope);

  {
    ParameterDeclarationParsingScope formals_scope(impl());
    if (is_wrapped) {
      // For a function implicitly wrapped in function header and footer, the
      // function arguments are provided separately to the source, and are
      // declared directly here.
      for (const AstRawString* arg : *arguments_for_wrapped_function) {
        const bool is_rest = false;
        Expression* argument = ExpressionFromIdentifier(arg, kNoSourcePosition);
        AddFormalParameter(&formals, argument, NullExpression(),
                           kNoSourcePosition, is_rest);
      }
      DCHECK_EQ(arguments_for_wrapped_function->length(),
                formals.num_parameters());
      DeclareFormalParameters(&formals);
    } else {
      // For a regular function, the function arguments are parsed from source.
      DCHECK_NULL(arguments_for_wrapped_function);
      impl()->ParseFormalParameterList(&formals);
      if (expected_parameters_end_pos != kNoSourcePosition) {
        // Check for '(' or ')' shenanigans in the parameter string for dynamic
        // functions.
        int position = impl()->peek_position();
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
      impl()->Expect(Token::RPAREN);
      int formals_end_position = impl()->scanner()->location().end_pos;

      impl()->CheckArityRestrictions(formals.arity, kind, formals.has_rest,
                             function_scope->start_position(),
                             formals_end_position);
      impl()->Expect(Token::LBRACE);
    }
    formals.duplicate_loc = formals_scope.duplicate_location();
  }

  *num_parameters = formals.num_parameters();
  *function_length = formals.function_length;

  AcceptINScope scope(impl(), true);
  impl()->ParseFunctionBody(body, function_name, pos, formals, kind,
                    function_syntax_kind,  ParserBase<Impl>::FunctionBodyType::kBlock);

  *has_duplicate_parameters = formals.has_duplicate();

  *expected_property_count = function_state.expected_property_count();
  *suspend_count = function_state.suspend_count();
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclareClassVariable(ClassScope* scope, const AstRawString* name,
                                  ClassInfo* class_info, int class_token_pos) {
#ifdef DEBUG
  scope->SetScopeName(name);
#endif

  DCHECK_IMPLIES(name == nullptr, class_info->is_anonymous);
  // Declare a special class variable for anonymous classes with the dot
  // if we need to save it for static private method access.
  Variable* class_variable =
      scope->DeclareClassVariable(impl()->ast_value_factory(), name, class_token_pos);
  Declaration* declaration = impl()->factory()->NewVariableDeclaration(class_token_pos);
  scope->declarations()->Add(declaration);
  declaration->set_var(class_variable);
}

// TODO(gsathya): Ideally, this should just bypass scope analysis and
// allocate a slot directly on the context. We should just store this
// index in the AST, instead of storing the variable.
template <typename Impl>
inline Variable* AbstractParser<Impl>::CreateSyntheticContextVariable(const AstRawString* name) {
  VariableProxy* proxy =
      DeclareBoundVariable(name, VariableMode::kConst, kNoSourcePosition);
  proxy->var()->ForceContextAllocation();
  return proxy->var();
}

template <typename Impl>
inline Variable* AbstractParser<Impl>::CreatePrivateNameVariable(ClassScope* scope,
                                            VariableMode mode,
                                            IsStaticFlag is_static_flag,
                                            const AstRawString* name) {
  DCHECK_NOT_NULL(name);
  int begin = impl()->position();
  int end = impl()->end_position();
  bool was_added = false;
  DCHECK(IsConstVariableMode(mode));
  Variable* var =
      scope->DeclarePrivateName(name, mode, is_static_flag, &was_added);
  if (!was_added) {
    Scanner::Location loc(begin, end);
    ReportMessageAt(loc, MessageTemplate::kVarRedeclaration, var->raw_name());
  }
  VariableProxy* proxy = impl()->factory()->NewVariableProxy(var, begin);
  return proxy->var();
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclarePublicClassField(ClassScope* scope,
                                     ClassLiteralProperty* property,
                                     bool is_static, bool is_computed_name,
                                     ClassInfo* class_info) {
  if (is_static) {
    class_info->static_elements->Add(
        impl()->factory()->NewClassLiteralStaticElement(property), impl()->zone());
  } else {
    class_info->instance_fields->Add(property, impl()->zone());
  }

  if (is_computed_name) {
    // We create a synthetic variable name here so that scope
    // analysis doesn't dedupe the vars.
    Variable* computed_name_var =
        CreateSyntheticContextVariable(impl()->ClassFieldVariableName(
            impl()->ast_value_factory(), class_info->computed_field_count));
    property->set_computed_name_var(computed_name_var);
    class_info->public_members->Add(property, impl()->zone());
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::DeclarePrivateClassMember(ClassScope* scope,
                                       const AstRawString* property_name,
                                       ClassLiteralProperty* property,
                                       ClassLiteralProperty::Kind kind,
                                       bool is_static, ClassInfo* class_info) {
  if (kind == ClassLiteralProperty::Kind::FIELD) {
    if (is_static) {
      class_info->static_elements->Add(
          impl()->factory()->NewClassLiteralStaticElement(property), impl()->zone());
    } else {
      class_info->instance_fields->Add(property, impl()->zone());
    }
  }

  Variable* private_name_var = CreatePrivateNameVariable(
      scope, impl()->GetVariableMode(kind),
      is_static ? IsStaticFlag::kStatic : IsStaticFlag::kNotStatic,
      property_name);
  int pos = property->value()->position();
  if (pos == kNoSourcePosition) {
    pos = property->key()->position();
  }
  private_name_var->set_initializer_position(pos);
  property->set_private_name_var(private_name_var);
  class_info->private_members->Add(property, impl()->zone());
}

// This method declares a property of the given class.  It updates the
// following fields of class_info, as appropriate:
//   - constructor
//   - properties
template <typename Impl>
inline void AbstractParser<Impl>::DeclarePublicClassMethod(const AstRawString* class_name,
                                      ClassLiteralProperty* property,
                                      bool is_constructor,
                                      ClassInfo* class_info) {
  if (is_constructor) {
    DCHECK(!class_info->constructor);
    class_info->constructor = property->value()->AsFunctionLiteral();
    DCHECK_NOT_NULL(class_info->constructor);
    class_info->constructor->set_raw_name(
        class_name != nullptr ? impl()->ast_value_factory()->NewConsString(class_name)
                              : nullptr);
    return;
  }

  class_info->public_members->Add(property, impl()->zone());
}

template <typename Impl>
inline void AbstractParser<Impl>::AddClassStaticBlock(Block* block, ClassInfo* class_info) {
  DCHECK(class_info->has_static_elements);
  class_info->static_elements->Add(
      impl()->factory()->NewClassLiteralStaticElement(block), impl()->zone());
}

template <typename Impl>
inline FunctionLiteral* AbstractParser<Impl>::CreateInitializerFunction(
    const char* name, DeclarationScope* scope, Statement* initializer_stmt) {
  DCHECK(IsClassMembersInitializerFunction(scope->function_kind()));
  // function() { .. class fields initializer .. }
  ScopedPtrList<Statement> statements(impl()->pointer_buffer());
  statements.Add(initializer_stmt);
  FunctionLiteral* result = impl()->factory()->NewFunctionLiteral(
      impl()->ast_value_factory()->GetOneByteString(name), scope, statements, 0, 0, 0,
      FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAccessorOrMethod,
      FunctionLiteral::kShouldEagerCompile, scope->start_position(), false,
      impl()->GetNextFunctionLiteralId(), impl()->speculative_parse_failure_reason());

  RecordFunctionLiteralSourceRange(result);

  return result;
}

// This method generates a ClassLiteral AST node.
// It uses the following fields of class_info:
//   - constructor (if missing, it updates it with a default constructor)
//   - proxy
//   - extends
//   - properties
//   - has_name_static_property
//   - has_static_computed_names
template <typename Impl>
inline Expression* AbstractParser<Impl>::RewriteClassLiteral(ClassScope* block_scope,
                                        const AstRawString* name,
                                        ClassInfo* class_info, int pos,
                                        int end_pos) {
  DCHECK_NOT_NULL(block_scope);
  DCHECK_EQ(block_scope->scope_type(), CLASS_SCOPE);
  DCHECK_EQ(block_scope->language_mode(), LanguageMode::kStrict);

  bool has_extends = class_info->extends != nullptr;
  bool has_default_constructor = class_info->constructor == nullptr;
  if (has_default_constructor) {
    class_info->constructor =
        DefaultConstructor(name, has_extends, pos, end_pos);
  }

  if (name != nullptr) {
    DCHECK_NOT_NULL(block_scope->class_variable());
    block_scope->class_variable()->set_initializer_position(end_pos);
  }

  FunctionLiteral* static_initializer = nullptr;
  if (class_info->has_static_elements) {
    static_initializer = CreateInitializerFunction(
        "<static_initializer>", class_info->static_elements_scope,
        impl()->factory()->NewInitializeClassStaticElementsStatement(
            class_info->static_elements, kNoSourcePosition));
  }

  FunctionLiteral* instance_members_initializer_function = nullptr;
  if (class_info->has_instance_members) {
    instance_members_initializer_function = CreateInitializerFunction(
        "<instance_members_initializer>", class_info->instance_members_scope,
        impl()->factory()->NewInitializeClassMembersStatement(
            class_info->instance_fields, kNoSourcePosition));
    class_info->constructor->set_requires_instance_members_initializer(true);
    class_info->constructor->add_expected_properties(
        class_info->instance_fields->length());
  }

  if (class_info->requires_brand) {
    class_info->constructor->set_class_scope_has_private_brand(true);
  }
  if (class_info->has_static_private_methods) {
    class_info->constructor->set_has_static_private_methods_or_accessors(true);
  }
  ClassLiteral* class_literal = impl()->factory()->NewClassLiteral(
      block_scope, class_info->extends, class_info->constructor,
      class_info->public_members, class_info->private_members,
      static_initializer, instance_members_initializer_function, pos, end_pos,
      class_info->has_name_static_property,
      class_info->has_static_computed_names, class_info->is_anonymous,
      class_info->has_private_methods, class_info->home_object_variable,
      class_info->static_home_object_variable);

  AddFunctionForNameInference(class_info->constructor);
  return class_literal;
}

template <typename Impl>
inline void AbstractParser<Impl>::InsertShadowingVarBindingInitializers(Block* inner_block) {
  // For each var-binding that shadows a parameter, insert an assignment
  // initializing the variable with the parameter.
  Scope* inner_scope = inner_block->scope();
  DCHECK(inner_scope->is_declaration_scope());
  Scope* function_scope = inner_scope->outer_scope();
  DCHECK(function_scope->is_function_scope());
  BlockState block_state(&impl()->scope_, inner_scope);
  for (Declaration* decl : *inner_scope->declarations()) {
    if (decl->var()->mode() != VariableMode::kVar ||
        !decl->IsVariableDeclaration()) {
      continue;
    }
    const AstRawString* name = decl->var()->raw_name();
    Variable* parameter = function_scope->LookupLocal(name);
    if (parameter == nullptr) continue;
    VariableProxyExpression* to = impl()->factory()->NewVariableProxyExpression(impl()->NewUnresolved(name));
    VariableProxyExpression* from = impl()->factory()->NewVariableProxyExpression(impl()->factory()->NewVariableProxy(parameter));
    Expression* assignment =
        impl()->factory()->NewAssignment(Token::ASSIGN, to, from, kNoSourcePosition);
    Statement* statement =
        impl()->factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    inner_block->statements()->InsertAt(0, statement, impl()->zone());
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::InsertSloppyBlockFunctionVarBindings(DeclarationScope* scope) {
  // For the outermost eval scope, we cannot hoist during parsing: let
  // declarations in the surrounding scope may prevent hoisting, but the
  // information is unaccessible during parsing. In this case, we hoist later in
  // DeclarationScope::Analyze.
  if (scope->is_eval_scope() && scope->outer_scope() == impl()->original_scope_) {
    return;
  }
  scope->HoistSloppyBlockFunctions(impl()->factory());
}

// ----------------------------------------------------------------------------
// Parser support

template <typename Impl>
template <typename LocalIsolate>
inline void AbstractParser<Impl>::HandleSourceURLComments(LocalIsolate* isolate,
                                     Handle<Script> script) {
  Handle<String> source_url = scanner_.SourceUrl(isolate);
  if (!source_url.is_null()) {
    script->set_source_url(*source_url);
  }
  Handle<String> source_mapping_url = scanner_.SourceMappingUrl(isolate);
  if (!source_mapping_url.is_null()) {
    script->set_source_mapping_url(*source_mapping_url);
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::UpdateStatistics(Isolate* isolate, Handle<Script> script) {
  CHECK_NOT_NULL(isolate);

  // Move statistics to Isolate.
  for (int feature = 0; feature < v8::Isolate::kUseCounterFeatureCount;
       ++feature) {
    if (use_counts_[feature] > 0) {
      isolate->CountUsage(v8::Isolate::UseCounterFeature(feature));
    }
  }
  if (scanner_.FoundHtmlComment()) {
    isolate->CountUsage(v8::Isolate::kHtmlComment);
    if (script->line_offset() == 0 && script->column_offset() == 0) {
      isolate->CountUsage(v8::Isolate::kHtmlCommentInExternalScript);
    }
  }
  isolate->counters()->total_preparse_skipped()->Increment(
      total_preparse_skipped_);
}

template <typename Impl>
inline void AbstractParser<Impl>::ParseOnBackground(ParseInfo* info, int start_position,
                               int end_position, int function_literal_id) {
  RCS_SCOPE(impl()->runtime_call_stats_, RuntimeCallCounterId::kParseBackgroundProgram);
  impl()->parsing_on_main_thread_ = false;

  DCHECK_NULL(info->literal());
  FunctionLiteral* result = nullptr;

  scanner_.Initialize();

  DCHECK(impl()->original_scope_);

  // When streaming, we don't know the length of the source until we have parsed
  // it. The raw data can be UTF-8, so we wouldn't know the source length until
  // we have decoded it anyway even if we knew the raw data length (which we
  // don't). We work around this by storing all the scopes which need their end
  // position set at the end of the script (the top scope and possible eval
  // scopes) and set their end position after we know the script length.
  if (impl()->flags().is_toplevel()) {
    DCHECK_EQ(start_position, 0);
    DCHECK_EQ(end_position, 0);
    DCHECK_EQ(function_literal_id, kFunctionLiteralIdTopLevel);
    result = impl()->DoParseProgram(/* isolate = */ nullptr, info);
  } else {
    result = impl()->DoParseFunction(/* isolate = */ nullptr, info, start_position,
                             end_position, function_literal_id,
                             info->function_name());
  }
  MaybeResetCharacterStream(info, result);
  MaybeProcessSourceRanges(info, result, impl()->stack_limit_);
  impl()->PostProcessParseResult(/* isolate = */ nullptr, info, result);
}

template <typename Impl>
inline typename AbstractParser<Impl>::TemplateLiteralState
AbstractParser<Impl>::OpenTemplateLiteral(int pos) {
  return impl()->zone()->template New<TemplateLiteral>(impl()->zone(), pos);
}

template <typename Impl>
inline void AbstractParser<Impl>::AddTemplateSpan(TemplateLiteralState* state, bool should_cook,
                             bool tail) {
  int end = impl()->scanner()->location().end_pos - (tail ? 1 : 2);
  const AstRawString* raw = impl()->scanner()->CurrentRawSymbol(impl()->ast_value_factory());
  if (should_cook) {
    const AstRawString* cooked = impl()->scanner()->CurrentSymbol(impl()->ast_value_factory());
    (*state)->AddTemplateSpan(cooked, raw, end, impl()->zone());
  } else {
    (*state)->AddTemplateSpan(nullptr, raw, end, impl()->zone());
  }
}

template <typename Impl>
inline void AbstractParser<Impl>::AddTemplateExpression(TemplateLiteralState* state,
                                   Expression* expression) {
  (*state)->AddExpression(expression, impl()->zone());
}

template <typename Impl>
inline Expression* AbstractParser<Impl>::CloseTemplateLiteral(TemplateLiteralState* state, int start,
                                         Expression* tag) {
  TemplateLiteral* lit = *state;
  int pos = lit->position();
  const ZonePtrList<const AstRawString>* cooked_strings = lit->cooked();
  const ZonePtrList<const AstRawString>* raw_strings = lit->raw();
  const ZonePtrList<Expression>* expressions = lit->expressions();
  DCHECK_EQ(cooked_strings->length(), raw_strings->length());
  DCHECK_EQ(cooked_strings->length(), expressions->length() + 1);

  if (!tag) {
    if (cooked_strings->length() == 1) {
      return impl()->factory()->NewStringLiteral(cooked_strings->first(), pos);
    }
    return impl()->factory()->NewTemplateLiteral(cooked_strings, expressions, pos);
  } else {
    // GetTemplateObject
    Expression* template_object =
        impl()->factory()->NewGetTemplateObject(cooked_strings, raw_strings, pos);

    // Call TagFn
    ScopedPtrList<Expression> call_args(impl()->pointer_buffer());
    call_args.Add(template_object);
    call_args.AddAll(expressions->ToConstVector());
    return impl()->factory()->NewTaggedTemplate(tag, call_args, pos);
  }
}

template <typename Impl>
inline ArrayLiteral* AbstractParser<Impl>::ArrayLiteralFromListWithSpread(
    const ScopedPtrList<Expression>& list) {
  // If there's only a single spread argument, a fast path using CallWithSpread
  // is taken.
  DCHECK_LT(1, list.length());

  // The arguments of the spread call become a single ArrayLiteral.
  int first_spread = 0;
  for (; first_spread < list.length() && !list.at(first_spread)->IsSpread();
       ++first_spread) {
  }

  DCHECK_LT(first_spread, list.length());
  return impl()->factory()->NewArrayLiteral(list, first_spread, kNoSourcePosition);
}

template <typename Impl>
inline void AbstractParser<Impl>::SetLanguageMode(Scope* scope, LanguageMode mode) {
  v8::Isolate::UseCounterFeature feature;
  if (is_sloppy(mode))
    feature = v8::Isolate::kSloppyMode;
  else if (is_strict(mode))
    feature = v8::Isolate::kStrictMode;
  else
    UNREACHABLE();
  ++use_counts_[feature];
  scope->SetLanguageMode(mode);
}

#if V8_ENABLE_WEBASSEMBLY
template <typename Impl>
inline void AbstractParser<Impl>::SetAsmModule() {
  // Store the usage count; The actual use counter on the isolate is
  // incremented after parsing is done.
  ++use_counts_[v8::Isolate::kUseAsm];
  DCHECK(impl()->scope()->is_declaration_scope());
  impl()->scope()->AsDeclarationScope()->set_is_asm_module();
  info_->set_contains_asm_module(true);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Impl>
inline Expression* AbstractParser<Impl>::ExpressionListToExpression(
    const ScopedPtrList<Expression>& args) {
  Expression* expr = args.at(0);
  if (args.length() == 1) return expr;
  if (args.length() == 2) {
    return impl()->factory()->NewBinaryOperation(Token::COMMA, expr, args.at(1),
                                         args.at(1)->position());
  }
  NaryOperation* result =
      impl()->factory()->NewNaryOperation(Token::COMMA, expr, args.length() - 1);
  for (int i = 1; i < args.length(); i++) {
    result->AddSubsequent(args.at(i), args.at(i)->position());
  }
  return result;
}

// This method completes the desugaring of the body of async_function.
template <typename Impl>
inline void AbstractParser<Impl>::RewriteAsyncFunctionBody(ScopedPtrList<Statement>* body,
                                      Block* block, Expression* return_value,
                                      REPLMode repl_mode) {
  // function async_function() {
  //   .generator_object = %_AsyncFunctionEnter();
  //   BuildRejectPromiseOnException({
  //     ... block ...
  //     return %_AsyncFunctionResolve(.generator_object, expr);
  //   })
  // }

  block->statements()->Add(impl()->factory()->NewSyntheticAsyncReturnStatement(
                               return_value, return_value->position()),
                           impl()->zone());
  block = BuildRejectPromiseOnException(block, repl_mode);
  body->Add(block);
}

template <typename Impl>
inline void AbstractParser<Impl>::SetFunctionNameFromPropertyName(LiteralProperty* property,
                                             const AstRawString* name,
                                             const AstRawString* prefix) {
  if (impl()->has_error()) return;
  // Ensure that the function we are going to create has shared name iff
  // we are not going to set it later.
  if (property->NeedsSetFunctionName()) {
    name = nullptr;
    prefix = nullptr;
  } else {
    // If the property value is an anonymous function or an anonymous class or
    // a concise method or an accessor function which doesn't require the name
    // to be set then the shared name must be provided.
    DCHECK_IMPLIES(property->value()->IsAnonymousFunctionDefinition() ||
                       property->value()->IsConciseMethodDefinition() ||
                       property->value()->IsAccessorFunctionDefinition(),
                   name != nullptr);
  }

  Expression* value = property->value();
  SetFunctionName(value, name, prefix);
}

template <typename Impl>
inline void AbstractParser<Impl>::SetFunctionNameFromPropertyName(ObjectLiteralProperty* property,
                                             const AstRawString* name,
                                             const AstRawString* prefix) {
  // Ignore "__proto__" as a name when it's being used to set the [[Prototype]]
  // of an object literal.
  // See ES #sec-__proto__-property-names-in-object-initializers.
  if (property->IsPrototype() || impl()->has_error()) return;

  DCHECK(!property->value()->IsAnonymousFunctionDefinition() ||
         property->kind() == ObjectLiteralProperty::COMPUTED);

  SetFunctionNameFromPropertyName(static_cast<LiteralProperty*>(property), name,
                                  prefix);
}

template <typename Impl>
inline void AbstractParser<Impl>::SetFunctionNameFromIdentifierRef(Expression* value,
                                              Expression* identifier) {
  if (!identifier->IsVariableProxyExpression()) return;
  SetFunctionName(value, identifier->AsVariableProxyExpression()->raw_name());
}

template <typename Impl>
inline void AbstractParser<Impl>::SetFunctionName(Expression* value, const AstRawString* name,
                             const AstRawString* prefix) {
  if (!value->IsAnonymousFunctionDefinition() &&
      !value->IsConciseMethodDefinition() &&
      !value->IsAccessorFunctionDefinition()) {
    return;
  }
  auto function = value->AsFunctionLiteral();
  if (value->IsClassLiteral()) {
    function = value->AsClassLiteral()->constructor();
  }
  if (function != nullptr) {
    AstConsString* cons_name = nullptr;
    if (name != nullptr) {
      if (prefix != nullptr) {
        cons_name = impl()->ast_value_factory()->NewConsString(prefix, name);
      } else {
        cons_name = impl()->ast_value_factory()->NewConsString(name);
      }
    } else {
      DCHECK_NULL(prefix);
    }
    function->set_raw_name(cons_name);
  }
}

template <typename Impl>
inline Statement* AbstractParser<Impl>::CheckCallable(Variable* var, Expression* error, int pos) {
  const int nopos = kNoSourcePosition;
  Statement* validate_var;
  {
    Expression* type_of = impl()->factory()->NewUnaryOperation(
        Token::TYPEOF, impl()->factory()->NewVariableProxy(var), nopos);
    Expression* function_literal = impl()->factory()->NewStringLiteral(
        impl()->ast_value_factory()->function_string(), nopos);
    Expression* condition = impl()->factory()->NewCompareOperation(
        Token::EQ_STRICT, type_of, function_literal, nopos);

    Statement* throw_call = impl()->factory()->NewExpressionStatement(error, pos);

    validate_var = impl()->factory()->NewIfStatement(
        condition, impl()->factory()->EmptyStatement(), throw_call, nopos);
  }
  return validate_var;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_ABSTRACT_PARSER_INL_H_

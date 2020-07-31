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
    
// TODO: Figure out what to do here. Probably just want the same functionality as the normal (eager) parser.
// class BinAstFuncNameInferrer {
//  public:
//   explicit BinAstFuncNameInferrer(AstValueFactory* avf) {}
//   void RemoveAsyncKeywordFromEnd() const {}
//   void Infer() const {}
//   void RemoveLastFunction() const {}

//   class State {
//    public:
//     explicit State(BinAstFuncNameInferrer* fni) {}

//    private:
//     DISALLOW_COPY_AND_ASSIGN(State);
//   };

//  private:
//   DISALLOW_COPY_AND_ASSIGN(BinAstFuncNameInferrer);
// };

class BinAstParser;

struct BinAstParserFormalParameters : FormalParametersBase {
  struct Parameter : public ZoneObject {
    Parameter(Expression* pattern, Expression* initializer, int position,
              int initializer_end_position, bool is_rest)
        : initializer_and_is_rest(initializer, is_rest),
          pattern(pattern),
          position(position),
          initializer_end_position(initializer_end_position) {}

    PointerWithPayload<Expression, bool, 1> initializer_and_is_rest;

    Expression* pattern;
    Expression* initializer() const {
      return initializer_and_is_rest.GetPointer();
    }
    int position;
    int initializer_end_position;
    inline bool is_rest() const { return initializer_and_is_rest.GetPayload(); }

    Parameter* next_parameter = nullptr;
    bool is_simple() const {
      return pattern->IsVariableProxyExpression() && initializer() == nullptr &&
             !is_rest();
    }

    const AstRawString* name() const {
      DCHECK(is_simple());
      return pattern->AsVariableProxyExpression()->raw_name();
    }

    Parameter** next() { return &next_parameter; }
    Parameter* const* next() const { return &next_parameter; }
  };

  void set_strict_parameter_error(const Scanner::Location& loc,
                                  MessageTemplate message) {
    strict_error_loc = loc;
    strict_error_message = message;
  }

  bool has_duplicate() const { return duplicate_loc.IsValid(); }
  void ValidateDuplicate(BinAstParser* parser) const;
  void ValidateStrictMode(BinAstParser* parser) const;

  explicit BinAstParserFormalParameters(DeclarationScope* scope)
      : FormalParametersBase(scope) {}

  base::ThreadedList<Parameter> params;
  Scanner::Location duplicate_loc = Scanner::Location::invalid();
  Scanner::Location strict_error_loc = Scanner::Location::invalid();
  MessageTemplate strict_error_message = MessageTemplate::kNone;
};


template <>
struct ParserTypes<BinAstParser> {
  using Base = ParserBase<BinAstParser>;
  using Impl = BinAstParser;

  // Return types for traversing functions.
  using Block = v8::internal::Block*;
  using BreakableStatement = v8::internal::BreakableStatement*;

  // TODO(binast): Add support for classes
  using ClassLiteralProperty = /* TODO(binast) */ v8::internal::ClassLiteralProperty*;
  using ClassPropertyList = /* TODO(binast) */ZonePtrList<v8::internal::ClassLiteralProperty>*;
  using ClassStaticElementList = /* TODO(binast) */ZonePtrList<ClassLiteral::StaticElement>*;

  using Expression = v8::internal::Expression*;
  using ExpressionList = ScopedPtrList<v8::internal::Expression>;
  using FormalParameters = /* TODO(binast) */BinAstParserFormalParameters;
  using ForStatement = v8::internal::ForStatement*;
  using FunctionLiteral = v8::internal::FunctionLiteral*;
  using Identifier = const AstRawString*;
  using IterationStatement = v8::internal::IterationStatement*;
  using ObjectLiteralProperty = ObjectLiteral::Property*;
  using ObjectPropertyList = ScopedPtrList<v8::internal::ObjectLiteralProperty>;
  using Statement = v8::internal::Statement*;
  using StatementList = ScopedPtrList<v8::internal::Statement>;

  // TODO(binast): Add support for suspend
  using Suspend = /* TODO(binast) */int;

  // For constructing objects returned by the traversing functions.
  using Factory = AstNodeFactory;

  // Other implementation-specific tasks.
  using FuncNameInferrer = v8::internal::FuncNameInferrer<ParserTypes<BinAstParser>>;
  using SourceRange = v8::internal::SourceRange;
  using SourceRangeScope = v8::internal::SourceRangeScope;
  using AstValueFactory = AstValueFactory;
  using AstRawString = AstRawString;
};


class BinAstParser : public ParserBase<BinAstParser> {
 public:
  explicit BinAstParser(ParseInfo* info);

  static bool IsPreParser() { return false; }

  // Sets the literal on |info| if parsing succeeded.
  void ParseOnBackground(ParseInfo* info, int start_position, int end_position,
                         int function_literal_id);

  // Initializes an empty scope chain for top-level scripts, or scopes which
  // consist of only the native context.
  void InitializeEmptyScopeChain(ParseInfo* info);

  void PrepareGeneratorVariables()
  {
    // TODO(binast)
    DCHECK(false);
  }

  ClassLiteralProperty* ParseClassPropertyDefinition(
      ClassInfo* class_info, ParsePropertyInfo* prop_info, bool has_extends) {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  void ParseProgram(ParseInfo* info);


  FunctionLiteral* DoParseFunction(ParseInfo* info,
                                   int start_position, int end_position,
                                   int function_literal_id,
                                   const AstRawString* raw_name);

 private:
  friend class ParserBase<BinAstParser>;
  friend struct BinAstParserFormalParameters;
  friend class i::ExpressionScope<ParserTypes<BinAstParser>>;
  friend class i::VariableDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ParameterDeclarationParsingScope<ParserTypes<BinAstParser>>;
  friend class i::ArrowHeadParsingScope<ParserTypes<BinAstParser>>;

  bool AllowsLazyParsingWithoutUnresolvedVariables() const {
    return !MaybeParsingArrowhead() &&
           scope()->AllowsLazyParsingWithoutUnresolvedVariables(
               original_scope_);
  }

  bool parse_lazily() const {
    // TODO(binast)
    return false;
  }

  enum Mode { PARSE_LAZILY, PARSE_EAGERLY };

  class ParsingModeScope {
   public:
    ParsingModeScope(BinAstParser* parser, Mode mode)
        : parser_(parser), old_mode_(parser->mode_) {
      parser_->mode_ = mode;
    }
    ~ParsingModeScope() { parser_->mode_ = old_mode_; }

   private:
    BinAstParser* parser_;
    Mode old_mode_;
  };

  Variable* NewTemporary(const AstRawString* name) {
    return scope()->NewTemporary(name);
  }

  FunctionLiteral* DoParseProgram(ParseInfo* info);
  void PostProcessParseResult(ParseInfo* info, FunctionLiteral* literal);

  // If we assign a function literal to a property we pretenure the
  // literal so it can be added as a constant function property.
  V8_INLINE static void CheckAssigningFunctionLiteralToProperty(
      Expression* left, Expression* right) {
    DCHECK_NOT_NULL(left);
    if (left->IsProperty() && right->IsFunctionLiteral()) {
      right->AsFunctionLiteral()->set_pretenure();
    }
  }

  void ParseModuleItemList(ScopedPtrList<Statement>* body)
  {
    // TODO(binast)
    DCHECK(false);
  }
  Statement* ParseModuleItem()
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  const AstRawString* ParseModuleSpecifier()
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  void ParseImportDeclaration()
  {
    // TODO(binast)
    DCHECK(false);
  }
  Statement* ParseExportDeclaration()
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Statement* ParseExportDefault()
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  void ParseExportStar()
  {
    // TODO(binast)
    DCHECK(false);
  }
  struct ExportClauseData {
    const AstRawString* export_name;
    const AstRawString* local_name;
    Scanner::Location location;
  };
  ZoneChunkList<ExportClauseData>* ParseExportClause(
      Scanner::Location* reserved_loc);
  struct NamedImport : public ZoneObject {
    const AstRawString* import_name;
    const AstRawString* local_name;
    const Scanner::Location location;
    NamedImport(const AstRawString* import_name, const AstRawString* local_name,
                Scanner::Location location)
        : import_name(import_name),
          local_name(local_name),
          location(location) {}
  };
  ZonePtrList<const NamedImport>* ParseNamedImports(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Statement* BuildInitializationBlock(DeclarationParsingResult* parsing_result)
  {
    ScopedPtrList<Statement> statements(pointer_buffer());
    for (const auto& declaration : parsing_result->declarations) {
      if (!declaration.initializer) continue;
      InitializeVariables(&statements, parsing_result->descriptor.kind,
                          &declaration);
    }
    return factory()->NewBlock(true, statements);
  }
  Expression* RewriteReturn(Expression* return_value, int pos)
  {
    if (IsDerivedConstructor(function_state_->kind())) {
    // For subclass constructors we need to return this in case of undefined;
    // other primitive values trigger an exception in the ConstructStub.
    //
    //   return expr;
    //
    // Is rewritten as:
    //
    //   return (temp = expr) === undefined ? this : temp;

    // temp = expr
    Variable* temp = NewTemporary(ast_value_factory()->empty_string());
    VariableProxy* proxy = factory()->NewVariableProxy(temp);
    Assignment* assign = factory()->NewAssignment(
        Token::ASSIGN, factory()->NewVariableProxyExpression(proxy), return_value, pos);

    // temp === undefined
    Expression* is_undefined = factory()->NewCompareOperation(
        Token::EQ_STRICT, assign,
        factory()->NewUndefinedLiteral(kNoSourcePosition), pos);

    // is_undefined ? this : temp
    // We don't need to call UseThis() since it's guaranteed to be called
    // for derived constructors after parsing the constructor in
    // ParseFunctionBody.
    return_value =
        factory()->NewConditional(is_undefined, factory()->ThisExpression(),
                                  factory()->NewVariableProxyExpression(proxy), pos);
  }
  return return_value;
  }
  Statement* RewriteSwitchStatement(SwitchStatement* switch_statement,
                                    Scope* scope)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Block* RewriteCatchPattern(CatchInfo* catch_info)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  void ReportVarRedeclarationIn(const AstRawString* name, Scope* scope)
  {
    // TODO(binast)
    DCHECK(false);
  }
  Statement* RewriteTryStatement(Block* try_block, Block* catch_block,
                                 const SourceRange& catch_range,
                                 Block* finally_block,
                                 const SourceRange& finally_range,
                                 const CatchInfo& catch_info, int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  void ParseAndRewriteGeneratorFunctionBody(int pos, FunctionKind kind,
                                            ScopedPtrList<Statement>* body)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void ParseAndRewriteAsyncGeneratorFunctionBody(
      int pos, FunctionKind kind, ScopedPtrList<Statement>* body)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void DeclareFunctionNameVar(const AstRawString* function_name,
                              FunctionSyntaxKind function_syntax_kind,
                              DeclarationScope* function_scope);

  Statement* DeclareFunction(const AstRawString* variable_name,
                             FunctionLiteral* function, VariableMode mode,
                             VariableKind kind, int beg_pos, int end_pos,
                             ZonePtrList<const AstRawString>* names);

  Variable* CreateSyntheticContextVariable(const AstRawString* synthetic_name)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Variable* CreatePrivateNameVariable(ClassScope* scope, VariableMode mode,
                                      IsStaticFlag is_static_flag,
                                      const AstRawString* name)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  FunctionLiteral* CreateInitializerFunction(
      const char* name, DeclarationScope* scope,
      ZonePtrList<ClassLiteral::Property>* fields)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  bool IdentifierEquals(const AstRawString* identifier,
                        const AstRawString* other) {
    return identifier == other;
  }

  // Insert initializer statements for var-bindings shadowing parameter bindings
  // from a non-simple parameter list.
  void InsertShadowingVarBindingInitializers(Block* block)
  {
    // TODO(binast)
    DCHECK(false);
  }

  // Implement sloppy block-scoped functions, ES2015 Annex B 3.3
  void InsertSloppyBlockFunctionVarBindings(DeclarationScope* scope)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void DeclareUnboundVariable(const AstRawString* name, VariableMode mode,
                              InitializationFlag init, int pos)
  {
    // TODO(binast)
    DCHECK(false);
  }
  V8_WARN_UNUSED_RESULT
  VariableProxy* DeclareBoundVariable(const AstRawString* name,
                                      VariableMode mode, int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  void DeclareAndBindVariable(VariableProxy* proxy, VariableKind kind,
                              VariableMode mode, Scope* declaration_scope,
                              bool* was_added, int initializer_position)
  {
    // TODO(binast)
    DCHECK(false);
  }

  V8_WARN_UNUSED_RESULT
  Variable* DeclareVariable(const AstRawString* name, VariableKind kind,
                            VariableMode mode, InitializationFlag init,
                            Scope* scope, bool* was_added,
                            int begin, int end = kNoSourcePosition)
  {
    Declaration* declaration;
    if (mode == VariableMode::kVar && !scope->is_declaration_scope()) {
      DCHECK(scope->is_block_scope() || scope->is_with_scope());
      declaration = factory()->NewNestedVariableDeclaration(scope, begin);
    } else {
      declaration = factory()->NewVariableDeclaration(begin);
    }
    Declare(declaration, name, kind, mode, init, scope, was_added, begin, end);
    return declaration->var();
  }

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

  void DeclareArrowFunctionFormalParameters(
    BinAstParserFormalParameters* parameters, Expression* params,
    const Scanner::Location& params_loc)
  {
    // TODO(binast)
    DCHECK(false);
  }

  Expression* ExpressionListToExpression(const ScopedPtrList<Expression>& args)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Statement* DeclareClass(const AstRawString* variable_name, Expression* value,
                        ZonePtrList<const AstRawString>* names,
                        int class_token_pos, int end_pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  void DeclareClassVariable(ClassScope* scope, const AstRawString* name,
                            ClassInfo* class_info, int class_token_pos)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void DeclareClassBrandVariable(ClassScope* scope, ClassInfo* class_info,
                                 int class_token_pos)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void DeclarePrivateClassMember(ClassScope* scope,
                                 const AstRawString* property_name,
                                 ClassLiteralProperty* property,
                                 ClassLiteralProperty::Kind kind,
                                 bool is_static, ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void DeclarePublicClassMethod(const AstRawString* class_name,
                                ClassLiteralProperty* property,
                                bool is_constructor, ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void DeclarePublicClassField(ClassScope* scope,
                               ClassLiteralProperty* property, bool is_static,
                               bool is_computed_name, ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void DeclareClassProperty(ClassScope* scope, const AstRawString* class_name,
                            ClassLiteralProperty* property, bool is_constructor,
                            ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void DeclareClassField(ClassScope* scope, ClassLiteralProperty* property,
                         const AstRawString* property_name, bool is_static,
                         bool is_computed_name, bool is_private,
                         ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void AddClassStaticBlock(Block* block, ClassInfo* class_info)
  {
    // TODO(binast)
    DCHECK(false);
  }
  Expression* RewriteClassLiteral(ClassScope* block_scope,
                                const AstRawString* name,
                                ClassInfo* class_info, int pos, int end_pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }


  // Skip over a lazy function, either using cached data if we have it, or
  // by parsing the function with PreParser. Consumes the ending }.
  // In case the preparser detects an error it cannot identify, it resets the
  // scanner- and preparser state to the initial one, before PreParsing the
  // function.
  // SkipFunction returns true if it correctly parsed the function, including
  // cases where we detect an error. It returns false, if we needed to stop
  // parsing or could not identify an error correctly, meaning the caller needs
  // to fully reparse. In this case it resets the scanner and preparser state.
  bool SkipFunction(const AstRawString* function_name, FunctionKind kind,
                    FunctionSyntaxKind function_syntax_kind,
                    DeclarationScope* function_scope, int* num_parameters,
                    int* function_length,
                    ProducedPreparseData** produced_preparsed_scope_data)
  {
    // TODO(bigint)
    DCHECK(false);
    return false;
  }

  Block* BuildParameterInitializationBlock(
      const BinAstParserFormalParameters& parameters)
  {
    // TODO(bigint)
    DCHECK(false);
    return nullptr;
  }

  Block* BuildRejectPromiseOnException(Block* block,
                                       REPLMode repl_mode = REPLMode::kNo)
  {
    // TODO(bigint)
    DCHECK(false);
    return nullptr;
  }

  void ParseFunction(
      ScopedPtrList<Statement>* body, const AstRawString* function_name,
      int pos, FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
      DeclarationScope* function_scope, int* num_parameters,
      int* function_length, bool* has_duplicate_parameters,
      int* expected_property_count, int* suspend_count,
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function);

  class TemplateLiteral : public ZoneObject {
   public:
    TemplateLiteral(Zone* zone, int pos)
        : cooked_(8, zone), raw_(8, zone), expressions_(8, zone), pos_(pos) {}

    const ZonePtrList<const AstRawString>* cooked() const { return &cooked_; }
    const ZonePtrList<const AstRawString>* raw() const { return &raw_; }
    const ZonePtrList<Expression>* expressions() const { return &expressions_; }
    int position() const { return pos_; }

    void AddTemplateSpan(const AstRawString* cooked, const AstRawString* raw,
                         int end, Zone* zone) {
      DCHECK_NOT_NULL(raw);
      cooked_.Add(cooked, zone);
      raw_.Add(raw, zone);
    }

    void AddExpression(Expression* expression, Zone* zone) {
      expressions_.Add(expression, zone);
    }

   private:
    ZonePtrList<const AstRawString> cooked_;
    ZonePtrList<const AstRawString> raw_;
    ZonePtrList<Expression> expressions_;
    int pos_;
  };

  using TemplateLiteralState = TemplateLiteral*;

  TemplateLiteralState OpenTemplateLiteral(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  // "should_cook" means that the span can be "cooked": in tagged template
  // literals, both the raw and "cooked" representations are available to user
  // code ("cooked" meaning that escape sequences are converted to their
  // interpreted values). Invalid escape sequences cause the cooked span
  // to be represented by undefined, instead of being a syntax error.
  // "tail" indicates that this span is the last in the literal.
  void AddTemplateSpan(TemplateLiteralState* state, bool should_cook,
                       bool tail)
  {
    // TODO(binast)
    DCHECK(false);
  }
  void AddTemplateExpression(TemplateLiteralState* state,
                             Expression* expression)
  {
    // TODO(binast)
    DCHECK(false);
  }
  Expression* CloseTemplateLiteral(TemplateLiteralState* state, int start,
                                   Expression* tag)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Statement* DeclareNative(const AstRawString* name, int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Block* IgnoreCompletion(Statement* statement)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Scope* NewHiddenCatchScope()
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  bool HasCheckedSyntax() {
    return scope()->GetDeclarationScope()->has_checked_syntax();
  }

  void InitializeVariables(
      ScopedPtrList<Statement>* statements, VariableKind kind,
      const DeclarationParsingResult::Declaration* declaration)
  {
    if (has_error()) return;

    DCHECK_NOT_NULL(declaration->initializer);

    int pos = declaration->value_beg_pos;
    if (pos == kNoSourcePosition) {
      pos = declaration->initializer->position();
    }
    Assignment* assignment = factory()->NewAssignment(
        Token::INIT, declaration->pattern, declaration->initializer, pos);
    statements->Add(factory()->NewExpressionStatement(assignment, pos));
  }

  Block* RewriteForVarInLegacy(const ForInfo& for_info)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  void DesugarBindingInForEachStatement(ForInfo* for_info, Block** body_block,
                                        Expression** each_variable)
  {
    // TODO(binast)
    DCHECK(false);
  }
  Block* CreateForEachStatementTDZ(Block* init_block, const ForInfo& for_info)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Statement* DesugarLexicalBindingsInForStatement(
      ForStatement* loop, Statement* init, Expression* cond, Statement* next,
      Statement* body, Scope* inner_scope, const ForInfo& for_info)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }


  FunctionLiteral* ParseFunctionLiteral(
    const AstRawString* name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_position, FunctionSyntaxKind type,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function);

  ObjectLiteral* InitializeObjectLiteral(ObjectLiteral* object_literal) {
    object_literal->CalculateEmitStore(main_zone());
    return object_literal;
  }

  // Producing data during the recursive descent.
  V8_INLINE const AstRawString* GetSymbol() const {
    const AstRawString* result = scanner()->CurrentSymbol(ast_value_factory());
    DCHECK_NOT_NULL(result);
    return result;
  }

  V8_INLINE const AstRawString* GetIdentifier() const { return GetSymbol(); }

  V8_INLINE const AstRawString* GetNextSymbol() const {
    return scanner()->NextSymbol(ast_value_factory());
  }

  V8_INLINE const AstRawString* GetNumberAsSymbol() const {
    double double_value = scanner()->DoubleValue();
    char array[100];
    const char* string = DoubleToCString(double_value, ArrayVector(array));
    return ast_value_factory()->GetOneByteString(string);
  }


  V8_INLINE void CountUsage(v8::Isolate::UseCounterFeature feature) {
    // ++use_counts_[feature];
    // TODO(binast)
    DCHECK(false);
  }

  class ThisExpression* ThisExpression() {
    // UseThis();
    // return factory()->ThisExpression();
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  
  class ThisExpression* NewThisExpression(int pos) {
    //UseThis();
    //return factory()->NewThisExpression(pos);
    DCHECK(false);
    return nullptr;
  }


  Expression* NewSuperPropertyReference(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Expression* NewSuperCallReference(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Expression* NewTargetExpression(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Expression* ImportMetaExpression(int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Expression* ExpressionFromLiteral(Token::Value token, int pos);

  V8_INLINE VariableProxyExpression* ExpressionFromPrivateName(
      PrivateNameScopeIterator* private_name_scope, const AstRawString* name,
      int start_position) {
    // VariableProxy* proxy = factory()->ast_node_factory()->NewVariableProxy(
    //     name, NORMAL_VARIABLE, start_position);
    // private_name_scope->AddUnresolvedPrivateName(proxy);
    // return proxy;
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  V8_INLINE VariableProxyExpression* ExpressionFromIdentifier(
    const AstRawString* name, int start_position,
    InferName infer = InferName::kYes) {
    if (infer == InferName::kYes) {
      fni_.PushVariableName(name);
    }
    VariableProxy* proxy = expression_scope()->NewVariable(name, start_position);
    return factory()->NewVariableProxyExpression(proxy);
  }

  V8_INLINE void DeclareIdentifier(const AstRawString* name,
                                   int start_position) {
    // expression_scope()->Declare(name, start_position);
    // TODO(binast)
    DCHECK(false);
  }
  V8_INLINE Variable* DeclareCatchVariableName(Scope* scope,
                                               const AstRawString* name) {
    // return scope->DeclareCatchVariableName(name);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  V8_INLINE ZonePtrList<Expression>* NewExpressionList(int size) const {
    return zone()->New<ZonePtrList<Expression>>(size, zone());
  }
  V8_INLINE ZonePtrList<ObjectLiteral::Property>* NewObjectPropertyList(
      int size) const {
    // return zone()->New<ZonePtrList<ObjectLiteral::Property>>(size, zone());
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  V8_INLINE ZonePtrList<ClassLiteral::Property>* NewClassPropertyList(
      int size) const {
    // return zone()->New<ZonePtrList<ClassLiteral::Property>>(size, zone());
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  V8_INLINE ZonePtrList<ClassLiteral::StaticElement>* NewClassStaticElementList(
      int size) const {
    //return zone()->New<ZonePtrList<ClassLiteral::StaticElement>>(size, zone());
    DCHECK(false);
    return nullptr;
  }
  V8_INLINE ZonePtrList<Statement>* NewStatementList(int size) const {
    return zone()->New<ZonePtrList<Statement>>(size, zone());
  }

  Expression* NewV8Intrinsic(const AstRawString* name,
                             const ScopedPtrList<Expression>& args, int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Expression* NewV8RuntimeFunctionForFuzzing(
      const Runtime::Function* function, const ScopedPtrList<Expression>& args,
      int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  V8_INLINE Statement* NewThrowStatement(Expression* exception, int pos) {
    return factory()->NewExpressionStatement(
        factory()->NewThrow(exception, pos), pos);
  }

  V8_INLINE void AddFormalParameter(BinAstParserFormalParameters* parameters,
                                    Expression* pattern,
                                    Expression* initializer,
                                    int initializer_end_position,
                                    bool is_rest) {
    parameters->UpdateArityAndFunctionLength(initializer != nullptr, is_rest);
    auto parameter = parameters->scope->zone()->New<BinAstParserFormalParameters::Parameter>(
      pattern, initializer,
      scanner()->location().beg_pos,
      initializer_end_position, is_rest);

    parameters->params.Add(parameter);
  }

  V8_INLINE void DeclareFormalParameters(BinAstParserFormalParameters* parameters) {
    bool is_simple = parameters->is_simple;
    DeclarationScope* scope = parameters->scope;
    if (!is_simple) scope->MakeParametersNonSimple();
    for (auto parameter : parameters->params) {
      bool is_optional = parameter->initializer() != nullptr;
      // If the parameter list is simple, declare the parameters normally with
      // their names. If the parameter list is not simple, declare a temporary
      // for each parameter - the corresponding named variable is declared by
      // BuildParamerterInitializationBlock.
      scope->DeclareParameter(
          is_simple ? parameter->name() : ast_value_factory()->empty_string(),
          is_simple ? VariableMode::kVar : VariableMode::kTemporary,
          is_optional, parameter->is_rest(), ast_value_factory()->arguments_string(),
          parameter->position);
    }
  }

  
  // Returns true if we have a binary expression between two numeric
  // literals. In that case, *x will be changed to an expression which is the
  // computed value.
  bool ShortcutNumericLiteralBinaryExpression(Expression** x, Expression* y,
                                              Token::Value op, int pos)
  {
    // TODO(binast): Dedupe this with normal AST implementation
    if ((*x)->IsNumberLiteral() && y->IsNumberLiteral()) {
    double x_val = (*x)->AsLiteral()->AsNumber();
    double y_val = y->AsLiteral()->AsNumber();
    switch (op) {
      case Token::ADD:
        *x = factory()->NewNumberLiteral(x_val + y_val, pos);
        return true;
      case Token::SUB:
        *x = factory()->NewNumberLiteral(x_val - y_val, pos);
        return true;
      case Token::MUL:
        *x = factory()->NewNumberLiteral(x_val * y_val, pos);
        return true;
      case Token::DIV:
        *x = factory()->NewNumberLiteral(base::Divide(x_val, y_val), pos);
        return true;
      case Token::BIT_OR: {
        int value = DoubleToInt32(x_val) | DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::BIT_AND: {
        int value = DoubleToInt32(x_val) & DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::BIT_XOR: {
        int value = DoubleToInt32(x_val) ^ DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SHL: {
        int value =
            base::ShlWithWraparound(DoubleToInt32(x_val), DoubleToInt32(y_val));
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SHR: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        uint32_t value = DoubleToUint32(x_val) >> shift;
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::SAR: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        int value = ArithmeticShiftRight(DoubleToInt32(x_val), shift);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::EXP:
        *x = factory()->NewNumberLiteral(base::ieee754::pow(x_val, y_val), pos);
        return true;
      default:
        break;
    }
  }
  return false;
  }

  // Returns true if we have a binary operation between a binary/n-ary
  // expression (with the same operation) and a value, which can be collapsed
  // into a single n-ary expression. In that case, *x will be changed to an
  // n-ary expression.
  bool CollapseNaryExpression(Expression** x, Expression* y, Token::Value op,
                              int pos, const SourceRange& range)
  {
    // TODO(binast): Dedupe this with other AST implementation
    // Filter out unsupported ops.
    if (!Token::IsBinaryOp(op) || op == Token::EXP) return false;

    // Convert *x into an nary operation with the given op, returning false if
    // this is not possible.
    NaryOperation* nary = nullptr;
    if ((*x)->IsBinaryOperation()) {
      BinaryOperation* binop = (*x)->AsBinaryOperation();
      if (binop->op() != op) return false;

      nary = factory()->NewNaryOperation(op, binop->left(), 2);
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

  // Returns a UnaryExpression or, in one of the following cases, a Literal.
  // ! <literal> -> true / false
  // + <Number literal> -> <Number literal>
  // - <Number literal> -> <Number literal with value negated>
  // ~ <literal> -> true / false
  Expression* BuildUnaryExpression(Expression* expression, Token::Value op,
                                   int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Expression* SpreadCall(Expression* function,
                         const ScopedPtrList<Expression>& args, int pos,
                         Call::PossiblyEval is_possibly_eval,
                         bool optional_chain)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }
  Expression* SpreadCallNew(Expression* function,
                            const ScopedPtrList<Expression>& args, int pos)
  {
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  void SetFunctionNameFromPropertyName(ClassLiteralProperty* property,
                                       const AstRawString* name,
                                       const AstRawString* prefix = nullptr) {
    // TODO(binast)
    DCHECK(false);
  }

  void SetFunctionNameFromPropertyName(LiteralProperty* property,
                                       const AstRawString* name,
                                       const AstRawString* prefix = nullptr) {
    if (has_error()) return;
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

  void SetFunctionNameFromPropertyName(ObjectLiteralProperty* property,
                                       const AstRawString* name,
                                       const AstRawString* prefix = nullptr)
  {
    // Ignore "__proto__" as a name when it's being used to set the [[Prototype]]
    // of an object literal.
    // See ES #sec-__proto__-property-names-in-object-initializers.
    if (property->IsPrototype() || has_error()) return;

    DCHECK(!property->value()->IsAnonymousFunctionDefinition() ||
          property->kind() == ObjectLiteralProperty::COMPUTED);

    SetFunctionNameFromPropertyName(static_cast<LiteralProperty*>(property), name,
                                    prefix);
  }

  void SetFunctionNameFromIdentifierRef(Expression* value,
                                        Expression* identifier)
  {
    if (!identifier->IsVariableProxyExpression()) return;
    SetFunctionName(value, identifier->AsVariableProxyExpression()->raw_name());
  }

  // "null" return type creators.
  V8_INLINE static std::nullptr_t NullIdentifier() { return nullptr; }
  V8_INLINE static std::nullptr_t NullExpression() { return nullptr; }
  V8_INLINE static std::nullptr_t NullLiteralProperty() { return nullptr; }
  V8_INLINE static ZonePtrList<Expression>* NullExpressionList() {
    return nullptr;
  }
  V8_INLINE static ZonePtrList<Statement>* NullStatementList() {
    return nullptr;
  }
  V8_INLINE static std::nullptr_t NullStatement() { return nullptr; }
  V8_INLINE static std::nullptr_t NullBlock() { return nullptr; }
  Expression* FailureExpression() { return factory()->FailureExpression(); }

  template <typename T>
  V8_INLINE static bool IsNull(T subject) {
    return subject == nullptr;
  }

  V8_INLINE static bool IsIterationStatement(Statement* subject) {
    return subject->AsIterationStatement() != nullptr;
  }

  // Non-null empty string.
  V8_INLINE const AstRawString* EmptyIdentifierString() const {
    return ast_value_factory()->empty_string();
  }

  V8_INLINE void GetDefaultStrings(const AstRawString** default_string,
                                   const AstRawString** dot_default_string) {
    *default_string = ast_value_factory()->default_string();
    *dot_default_string = ast_value_factory()->dot_default_string();
  }

  // Functions for encapsulating the differences between parsing and preparsing;
  // operations interleaved with the recursive descent.
  V8_INLINE void PushLiteralName(const AstRawString* id) {
    fni_.PushLiteralName(id);
  }

  V8_INLINE void PushVariableName(const AstRawString* id) {
    fni_.PushVariableName(id);
  }

  V8_INLINE void PushPropertyName(Expression* expression) {
    if (expression->IsPropertyName()) {
      fni_.PushLiteralName(expression->AsLiteral()->AsRawPropertyName());
    } else {
      fni_.PushLiteralName(ast_value_factory()->computed_string());
    }
  }


  V8_INLINE void PushEnclosingName(const AstRawString* name)
  {
    fni_.PushEnclosingName(name);
  }

  V8_INLINE void AddFunctionForNameInference(FunctionLiteral* func_to_infer) {
    // fni_.AddFunction(func_to_infer);
    // TODO(binast)
    DCHECK(false);
  }


  V8_INLINE void InferFunctionName()
  {
    // TODO(binast)
    DCHECK(false);
    // fni_.Infer();
  }

  void SetLanguageMode(Scope* scope, LanguageMode mode)
  {
    // TODO(binast): Feature counts
    scope->SetLanguageMode(mode);
  }

  void SetAsmModule() {
    // TODO(binast): Feature counts
    DCHECK(scope()->is_declaration_scope());
    scope()->AsDeclarationScope()->set_is_asm_module();
    info_->set_contains_asm_module(true);
  }

  V8_INLINE void RecordBlockSourceRange(Block* node,
                                        int32_t continuation_position) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node, new (zone()) BlockSourceRanges(continuation_position));
    // TODO(binast)
  }

  V8_INLINE void RecordCaseClauseSourceRange(CaseClause* node,
                                             const SourceRange& body_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(node,
    //                           new (zone()) CaseClauseSourceRanges(body_range));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordConditionalSourceRange(Expression* node,
                                              const SourceRange& then_range,
                                              const SourceRange& else_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node->AsConditional(),
    //     new (zone()) ConditionalSourceRanges(then_range, else_range));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordFunctionLiteralSourceRange(FunctionLiteral* node) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(node, new (zone()) FunctionLiteralSourceRanges);
    // TODO(binast)
  }

  V8_INLINE void RecordBinaryOperationSourceRange(
      Expression* node, const SourceRange& right_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(node->AsBinaryOperation(),
    //                           new (zone())
    //                               BinaryOperationSourceRanges(right_range));
    // TODO(binast)
  }

  V8_INLINE void RecordJumpStatementSourceRange(Statement* node,
                                                int32_t continuation_position) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     static_cast<JumpStatement*>(node),
    //     new (zone()) JumpStatementSourceRanges(continuation_position));
    // TODO(binast)
  }

  V8_INLINE void RecordIfStatementSourceRange(Statement* node,
                                              const SourceRange& then_range,
                                              const SourceRange& else_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node->AsIfStatement(),
    //     new (zone()) IfStatementSourceRanges(then_range, else_range));
    // TODO(binast)
  }

  V8_INLINE void RecordIterationStatementSourceRange(
      IterationStatement* node, const SourceRange& body_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node, new (zone()) IterationStatementSourceRanges(body_range));
    // TODO(binast)
  }

  // Used to record source ranges of expressions associated with optional chain:
  V8_INLINE void RecordExpressionSourceRange(Expression* node,
                                             const SourceRange& right_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(node,
    //                           zone()->New<ExpressionSourceRanges>(right_range));
    // TODO(binast)
  }

  V8_INLINE void RecordSuspendSourceRange(Expression* node,
                                          int32_t continuation_position) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(static_cast<Suspend*>(node),
    //                           new (zone())
    //                               SuspendSourceRanges(continuation_position));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordSwitchStatementSourceRange(
      Statement* node, int32_t continuation_position) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node->AsSwitchStatement(),
    //     new (zone()) SwitchStatementSourceRanges(continuation_position));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordThrowSourceRange(Statement* node,
                                        int32_t continuation_position) {
    // if (source_range_map_ == nullptr) return;
    // ExpressionStatement* expr_stmt = static_cast<ExpressionStatement*>(node);
    // Throw* throw_expr = expr_stmt->expression()->AsThrow();
    // source_range_map_->Insert(
    //     throw_expr, new (zone()) ThrowSourceRanges(continuation_position));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordTryCatchStatementSourceRange(
      TryCatchStatement* node, const SourceRange& body_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node, new (zone()) TryCatchStatementSourceRanges(body_range));
    // TODO(binast)
    DCHECK(false);
  }

  V8_INLINE void RecordTryFinallyStatementSourceRange(
      TryFinallyStatement* node, const SourceRange& body_range) {
    // if (source_range_map_ == nullptr) return;
    // source_range_map_->Insert(
    //     node, new (zone()) TryFinallyStatementSourceRanges(body_range));
    // TODO(binast)
    DCHECK(false);
  }


  // Returns true iff we're parsing the first function literal during
  // CreateDynamicFunction().
  V8_INLINE bool ParsingDynamicFunctionDeclaration() const {
    return parameters_end_pos_ != kNoSourcePosition;
  }

  V8_INLINE void ConvertBinaryToNaryOperationSourceRange(
      BinaryOperation* binary_op, NaryOperation* nary_op) {
    // TODO(binast)
    DCHECK(false);
    // if (source_range_map_ == nullptr) return;
    // DCHECK_NULL(source_range_map_->Find(nary_op));

    // BinaryOperationSourceRanges* ranges =
    //     static_cast<BinaryOperationSourceRanges*>(
    //         source_range_map_->Find(binary_op));
    // if (ranges == nullptr) return;

    // SourceRange range = ranges->GetRange(SourceRangeKind::kRight);
    // source_range_map_->Insert(
    //     nary_op, new (zone()) NaryOperationSourceRanges(zone(), range));
  }

  V8_INLINE void AppendNaryOperationSourceRange(NaryOperation* node,
                                                const SourceRange& range) {
    // TODO(binast)
    DCHECK(false);
    // if (source_range_map_ == nullptr) return;
    // NaryOperationSourceRanges* ranges =
    //     static_cast<NaryOperationSourceRanges*>(source_range_map_->Find(node));
    // if (ranges == nullptr) return;

    // ranges->AddRange(range);
    // DCHECK_EQ(node->subsequent_length(), ranges->RangeCount());
  }

  void RewriteAsyncFunctionBody(ScopedPtrList<Statement>* body, Block* block,
                              Expression* return_value,
                              REPLMode repl_mode = REPLMode::kNo)
  {
    // TODO(binast)
    DCHECK(false);
  }

  void SetFunctionName(Expression* value, const AstRawString* name,
                      const AstRawString* prefix = nullptr)
  {
    if (!value->IsAnonymousFunctionDefinition() &&
        !value->IsConciseMethodDefinition() &&
        !value->IsAccessorFunctionDefinition()) {
      return;
    }
    auto function = value->AsFunctionLiteral();
    if (value->IsClassLiteral()) {
      // TODO(binast): Add support for classes
      DCHECK(false);
      // function = value->AsClassLiteral()->constructor();
    }
    if (function != nullptr) {
      AstConsString* cons_name = nullptr;
      if (name != nullptr) {
        if (prefix != nullptr) {
          cons_name = ast_value_factory()->NewConsString(prefix, name);
        } else {
          cons_name = ast_value_factory()->NewConsString(name);
        }
      } else {
        DCHECK_NULL(prefix);
      }
      function->set_raw_name(cons_name);
    }
  }

  // Helper functions for recursive descent.
  V8_INLINE bool IsEval(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->eval_string();
  }

  V8_INLINE bool IsAsync(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->async_string();
  }

  V8_INLINE bool IsArguments(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->arguments_string();
  }

  V8_INLINE bool IsEvalOrArguments(const AstRawString* identifier) const {
    return IsEval(identifier) || IsArguments(identifier);
  }

  // Returns true if the expression is of type "this.foo".
  V8_INLINE static bool IsThisProperty(Expression* expression) {
    DCHECK_NOT_NULL(expression);
    Property* property = expression->AsProperty();
    return property != nullptr && property->obj()->IsThisExpression();
    // TODO(binast)
    // DCHECK(false);
    // return false;
  }

  // Returns true if the expression is of type "obj.#foo".
  V8_INLINE static bool IsPrivateReference(Expression* expression) {
    // DCHECK_NOT_NULL(expression);
    // Property* property = expression->AsProperty();
    // return property != nullptr && property->IsPrivateReference();
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // This returns true if the expression is an indentifier (wrapped
  // inside a variable proxy).  We exclude the case of 'this', which
  // has been converted to a variable proxy.
  /* V8_INLINE */ static bool IsIdentifier(Expression* expression) {
    VariableProxyExpression* operand = expression->AsVariableProxyExpression();
    return operand != nullptr && !operand->is_new_target();
    // TODO(binast)
    // DCHECK(false);
    // return false;
  }

  V8_INLINE static const AstRawString* AsIdentifier(Expression* expression) {
    DCHECK(IsIdentifier(expression));
    return expression->AsVariableProxyExpression()->raw_name();
  }

  V8_INLINE VariableProxy* AsIdentifierExpression(Expression* expression) {
    // return expression->AsVariableProxyExpression();
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  V8_INLINE bool IsConstructor(const AstRawString* identifier) const {
    // return identifier == ast_value_factory()->constructor_string();
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  V8_INLINE bool IsName(const AstRawString* identifier) const {
    // return identifier == ast_value_factory()->name_string();
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  V8_INLINE static bool IsBoilerplateProperty(
      ObjectLiteral::Property* property) {
    return !property->IsPrototype();
  }

  V8_INLINE bool IsNative(Expression* expr) const {
    // DCHECK_NOT_NULL(expr);
    // return expr->IsVariableProxyExpression() &&
    //        expr->AsVariableProxyExpression()->raw_name() ==
    //            ast_value_factory()->native_string();
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  V8_INLINE static bool IsArrayIndex(const AstRawString* string,
                                     uint32_t* index) {
    // return string->AsArrayIndex(index);
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // Returns true if the statement is an expression statement containing
  // a single string literal.  If a second argument is given, the literal
  // is also compared with it and the result is true only if they are equal.
  V8_INLINE bool IsStringLiteral(Statement* statement,
                                 const AstRawString* arg = nullptr) const {
    ExpressionStatement* e_stat = statement->AsExpressionStatement();
    if (e_stat == nullptr) return false;
    Literal* literal = e_stat->expression()->AsLiteral();
    if (literal == nullptr || !literal->IsString()) return false;
    return arg == nullptr || literal->AsRawString() == arg;
  }

  // Generate AST node that throws a ReferenceError with the given type.
  V8_INLINE Expression* NewThrowReferenceError(MessageTemplate message,
                                               int pos) {
    // return NewThrowError(Runtime::kNewReferenceError, message,
    //                      ast_value_factory()->empty_string(), pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  // Generate AST node that throws a SyntaxError with the given
  // type. The first argument may be null (in the handle sense) in
  // which case no arguments are passed to the constructor.
  V8_INLINE Expression* NewThrowSyntaxError(MessageTemplate message,
                                            const AstRawString* arg, int pos) {
    // return NewThrowError(Runtime::kNewSyntaxError, message, arg, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  // Generate AST node that throws a TypeError with the given
  // type. Both arguments must be non-null (in the handle sense).
  V8_INLINE Expression* NewThrowTypeError(MessageTemplate message,
                                          const AstRawString* arg, int pos) {
    // return NewThrowError(Runtime::kNewTypeError, message, arg, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }


  // Reporting errors.
  void ReportMessageAt(Scanner::Location source_location,
                       MessageTemplate message, const char* arg = nullptr) {
    pending_error_handler()->ReportMessageAt(
        source_location.beg_pos, source_location.end_pos, message, arg);
    scanner_.set_parser_error();
  }

    // Dummy implementation. The parser should never have a unidentifiable
  // error.
  V8_INLINE void ReportUnidentifiableError() { UNREACHABLE(); }

  void ReportMessageAt(Scanner::Location source_location,
                       MessageTemplate message, const AstRawString* arg) {
    pending_error_handler()->ReportMessageAt(
        source_location.beg_pos, source_location.end_pos, message, arg);
    scanner_.set_parser_error();
  }

  const AstRawString* GetRawNameFromIdentifier(const AstRawString* arg) {
    return arg;
  }

  IterationStatement* AsIterationStatement(BreakableStatement* s) {
    return s->AsIterationStatement();
  }

  void ReportUnexpectedTokenAt(
    Scanner::Location location, Token::Value token,
    MessageTemplate message = MessageTemplate::kUnexpectedToken);

  ParseInfo* info() const { return info_; }

  ParseInfo* info_;
  Scanner scanner_;
  Mode mode_;

  // TODO(binast)
  // SourceRangeMap* source_range_map_ = nullptr;

  // If not kNoSourcePosition, indicates that the first function literal
  // encountered is a dynamic function, see CreateDynamicFunction(). This field
  // indicates the correct position of the ')' that closes the parameter list.
  // After that ')' is encountered, this field is reset to kNoSourcePosition.
  int parameters_end_pos_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_PARSER_H_

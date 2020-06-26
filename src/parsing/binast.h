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
#include "src/parsing/binast-value-factory.h"

namespace v8 {
namespace internal {

// #define BINAST_DECLARATION_NODE_LIST(V) \
//   V(VariableDeclaration)                \
//   V(FunctionDeclaration)

#define BINAST_ITERATION_NODE_LIST(V) \
  V(DoWhileStatement)                 \
  V(WhileStatement)                   \
  V(ForStatement)                     \
  V(ForInStatement)                   \
  V(ForOfStatement)

#define BINAST_BREAKABLE_NODE_LIST(V)   \
  V(Block)                              \
  V(SwitchStatement)

#define BINAST_STATEMENT_NODE_LIST(V)    \
  BINAST_ITERATION_NODE_LIST(V)          \
  BINAST_BREAKABLE_NODE_LIST(V)          \
  V(EmptyStatement)                      \
  V(ExpressionStatement)                 \
  V(SloppyBlockFunctionStatement)        \
  V(IfStatement)                         \
  V(ContinueStatement)                   \
  V(BreakStatement)                      \
  V(ReturnStatement)                     \
  V(WithStatement)                       \
  V(TryCatchStatement)                   \
  V(TryFinallyStatement)                 \
  V(DebuggerStatement)                   \
  V(InitializeClassMembersStatement)

#define BINAST_LITERAL_NODE_LIST(V) \
  V(RegExpLiteral)                  \
  V(ObjectLiteral)                  \
  V(ArrayLiteral)

#define BINAST_EXPRESSION_NODE_LIST(V) \
  BINAST_LITERAL_NODE_LIST(V)          \
  V(Assignment)                 \
  V(Await)                      \
  V(BinaryOperation)            \
  V(NaryOperation)              \
  V(Call)                       \
  V(CallNew)                    \
  V(CallRuntime)                \
  V(ClassLiteral)               \
  V(CompareOperation)           \
  V(CompoundAssignment)         \
  V(Conditional)                \
  V(CountOperation)             \
  V(EmptyParentheses)           \
  V(FunctionLiteral)            \
  V(GetTemplateObject)          \
  V(ImportCallExpression)       \
  V(Literal)                    \
  V(NativeFunctionLiteral)      \
  V(OptionalChain)              \
  V(Property)                   \
  V(Spread)                     \
  V(SuperCallReference)         \
  V(SuperPropertyReference)     \
  V(TemplateLiteral)            \
  V(ThisExpression)             \
  V(Throw)                      \
  V(UnaryOperation)             \
  V(VariableProxyExpression)    \
  V(Yield)                      \
  V(YieldStar)



#define BINAST_AST_NODE_LIST(V)                        \
  BINAST_STATEMENT_NODE_LIST(V)                        \
  BINAST_EXPRESSION_NODE_LIST(V)

#define BINAST_FAILURE_NODE_LIST(V) V(FailureExpression)

class BinAstNode;
class BinAstNodeFactory;
class BinAstBreakableStatement;
class BinAstExpression;
class BinAstIterationStatement;
class BinAstStatement;

// class BinAstRawString;
#define DEF_FORWARD_DECLARATION(type) class BinAst##type;
BINAST_AST_NODE_LIST(DEF_FORWARD_DECLARATION)
BINAST_FAILURE_NODE_LIST(DEF_FORWARD_DECLARATION)
#undef DEF_FORWARD_DECLARATION

class BinAstNode : public ZoneObject {
 public:
#define DECLARE_TYPE_ENUM(type) k##type,
  enum NodeType : uint8_t {
    BINAST_AST_NODE_LIST(DECLARE_TYPE_ENUM) /* , */
    BINAST_FAILURE_NODE_LIST(DECLARE_TYPE_ENUM)
  };
#undef DECLARE_TYPE_ENUM

#define DECLARE_NODE_FUNCTIONS(type) \
  V8_INLINE bool Is##type() const;   \
  V8_INLINE BinAst##type* As##type();        \
  V8_INLINE const BinAst##type* As##type() const;
  BINAST_AST_NODE_LIST(DECLARE_NODE_FUNCTIONS)
  BINAST_FAILURE_NODE_LIST(DECLARE_NODE_FUNCTIONS)
#undef DECLARE_NODE_FUNCTIONS

  NodeType node_type() const { return NodeTypeField::decode(bit_field_); }
  const char* node_type_name() const {
    switch (node_type()) {
#define NODE_TYPE_NAME(type) case k##type: return #type;
      BINAST_AST_NODE_LIST(NODE_TYPE_NAME)
      BINAST_FAILURE_NODE_LIST(NODE_TYPE_NAME)
#undef NODE_TYPE_NAME
    }
  }
  int position() const { return position_; }

  BinAstIterationStatement* AsIterationStatement();
  // MaterializedLiteral* AsMaterializedLiteral();

 private:
  int position_;
  using NodeTypeField = base::BitField<NodeType, 0, 6>;

protected:
  uint32_t bit_field_;

  template <class T, int size>
  using NextBitField = NodeTypeField::Next<T, size>;

  BinAstNode(int position, NodeType type)
      : position_(position), bit_field_(NodeTypeField::encode(type)) {}
};


class BinAstStatement : public BinAstNode {
 protected:
    BinAstStatement(int position, NodeType type) : BinAstNode(position, type) {}
};


class BinAstExpression : public BinAstNode {
 public:
  enum Context {
    // Not assigned a context yet, or else will not be visited during
    // code generation.
    kUninitialized,
    // Evaluated for its side effects.
    kEffect,
    // Evaluated for its value (and side effects).
    kValue,
    // Evaluated for control flow (and side effects).
    kTest
  };

  // True iff the expression is a valid reference expression.
  bool IsValidReferenceExpression() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True iff the expression is a private name.
  bool IsPrivateName() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // Helpers for ToBoolean conversion.
  bool ToBooleanIsTrue() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }
  bool ToBooleanIsFalse() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // Symbols that cannot be parsed as array indices are considered property
  // names.  We do not treat symbols that can be array indexes as property
  // names because [] for string objects is handled only by keyed ICs.
  bool IsPropertyName() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True iff the expression is a class or function expression without
  // a syntactic name.
  bool IsAnonymousFunctionDefinition() const;

  // True iff the expression is a concise method definition.
  bool IsConciseMethodDefinition() const;

  // True iff the expression is an accessor function definition.
  bool IsAccessorFunctionDefinition() const;

  // True iff the expression is a literal represented as a smi.
  bool IsSmiLiteral() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True iff the expression is a literal represented as a number.
  V8_EXPORT_PRIVATE bool IsNumberLiteral() const;

  // True iff the expression is a string literal.
  bool IsStringLiteral() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True iff the expression is the null literal.
  bool IsNullLiteral() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True iff the expression is the hole literal.
  bool IsTheHoleLiteral() const;

  // True if we can prove that the expression is the undefined literal. Note
  // that this also checks for loads of the global "undefined" variable.
  bool IsUndefinedLiteral() const {
    // TODO(binast)
    DCHECK(false);
    return false;
  }

  // True if either null literal or undefined literal.
  inline bool IsNullOrUndefinedLiteral() const {
    return IsNullLiteral() || IsUndefinedLiteral();
  }

  // True if a literal and not null or undefined.
  bool IsLiteralButNotNullOrUndefined() const;

  bool IsCompileTimeValue();

  bool IsPattern() {
    STATIC_ASSERT(kObjectLiteral + 1 == kArrayLiteral);
    return base::IsInRange(node_type(), kObjectLiteral, kArrayLiteral);
  }

  bool is_parenthesized() const {
    return IsParenthesizedField::decode(bit_field_);
  }

  void mark_parenthesized() {
    bit_field_ = IsParenthesizedField::update(bit_field_, true);
  }

  void clear_parenthesized() {
    bit_field_ = IsParenthesizedField::update(bit_field_, false);
  }

 private:
  using IsParenthesizedField = BinAstNode::NextBitField<bool, 1>;

 protected:
  BinAstExpression(int pos, NodeType type) : BinAstNode(pos, type) {
    DCHECK(!is_parenthesized());
  }

  template <class T, int size>
  using NextBitField = IsParenthesizedField::Next<T, size>;
};


class BinAstThrow final : public BinAstExpression {
 public:
  BinAstExpression* exception() const { return exception_; }

 private:
  friend class BinAstNodeFactory;

  BinAstThrow(BinAstExpression* exception, int pos)
      : BinAstExpression(pos, kThrow), exception_(exception) {}

  BinAstExpression* exception_;
};


class BinAstFunctionLiteral final : public BinAstExpression {
 public:
  enum ParameterFlag : uint8_t {
    kNoDuplicateParameters,
    kHasDuplicateParameters
  };
  enum EagerCompileHint : uint8_t { kShouldEagerCompile, kShouldLazyCompile };

  bool has_shared_name() const { return raw_name_ != nullptr; }
  const AstConsString* raw_name() const { return raw_name_; }
  void set_raw_name(const AstConsString* name) { raw_name_ = name; }
  DeclarationScope* scope() const { return scope_; }
  ZonePtrList<BinAstStatement>* body() { return &body_; }
  void set_function_token_position(int pos) { function_token_position_ = pos; }
  int function_token_position() const { return function_token_position_; }
  int start_position() const;
  int end_position() const;
  bool is_anonymous_expression() const {
    return syntax_kind() == FunctionSyntaxKind::kAnonymousExpression;
  }

  void mark_as_oneshot_iife() {
    bit_field_ = OneshotIIFEBit::update(bit_field_, true);
  }
  bool is_oneshot_iife() const { return OneshotIIFEBit::decode(bit_field_); }
  bool is_toplevel() const {
    return function_literal_id() == kFunctionLiteralIdTopLevel;
  }
  V8_EXPORT_PRIVATE LanguageMode language_mode() const;

  static bool NeedsHomeObject(Expression* expr);

  void add_expected_properties(int number_properties) {
    expected_property_count_ += number_properties;
  }
  int expected_property_count() { return expected_property_count_; }
  int parameter_count() { return parameter_count_; }
  int function_length() { return function_length_; }

  bool AllowsLazyCompilation();

  bool CanSuspend() {
    if (suspend_count() > 0) {
      DCHECK(IsResumableFunction(kind()));
      return true;
    }
    return false;
  }

  // We can safely skip the arguments adaptor frame setup even
  // in case of arguments mismatches for strict mode functions,
  // as long as there's
  //
  //   1. no use of the arguments object (either explicitly or
  //      potentially implicitly via a direct eval() call), and
  //   2. rest parameters aren't being used in the function.
  //
  // See http://bit.ly/v8-faster-calls-with-arguments-mismatch
  // for the details here (https://crbug.com/v8/8895).
  bool SafeToSkipArgumentsAdaptor() const;

  // Returns either name or inferred name as a cstring.
  std::unique_ptr<char[]> GetDebugName() const;

  const AstConsString* raw_inferred_name() { return raw_inferred_name_; }

  // Only one of {set_inferred_name, set_raw_inferred_name} should be called.
  // TODO(binast): I don't think we need the Handle version of this.
  // void set_inferred_name(Handle<String> inferred_name);
  void set_raw_inferred_name(AstConsString* raw_inferred_name);

  bool pretenure() const { return Pretenure::decode(bit_field_); }
  void set_pretenure() { bit_field_ = Pretenure::update(bit_field_, true); }

  bool has_duplicate_parameters() const {
    // Not valid for lazy functions.
    DCHECK(ShouldEagerCompile());
    return HasDuplicateParameters::decode(bit_field_);
  }

  // This is used as a heuristic on when to eagerly compile a function
  // literal. We consider the following constructs as hints that the
  // function will be called immediately:
  // - (function() { ... })();
  // - var x = function() { ... }();
  V8_EXPORT_PRIVATE bool ShouldEagerCompile() const;
  V8_EXPORT_PRIVATE void SetShouldEagerCompile();

  FunctionSyntaxKind syntax_kind() const {
    return FunctionSyntaxKindBits::decode(bit_field_);
  }
  FunctionKind kind() const;

  bool dont_optimize() {
    return dont_optimize_reason() != BailoutReason::kNoReason;
  }
  BailoutReason dont_optimize_reason() {
    return DontOptimizeReasonField::decode(bit_field_);
  }
  void set_dont_optimize_reason(BailoutReason reason) {
    bit_field_ = DontOptimizeReasonField::update(bit_field_, reason);
  }

  bool IsAnonymousFunctionDefinition() const {
    return is_anonymous_expression();
  }

  int suspend_count() { return suspend_count_; }
  void set_suspend_count(int suspend_count) { suspend_count_ = suspend_count; }

  int return_position() {
    return std::max(
        start_position(),
        end_position() - (HasBracesField::decode(bit_field_) ? 1 : 0));
  }

  int function_literal_id() const { return function_literal_id_; }
  void set_function_literal_id(int function_literal_id) {
    function_literal_id_ = function_literal_id;
  }

  void set_requires_instance_members_initializer(bool value) {
    bit_field_ = RequiresInstanceMembersInitializer::update(bit_field_, value);
  }
  bool requires_instance_members_initializer() const {
    return RequiresInstanceMembersInitializer::decode(bit_field_);
  }

  void set_has_static_private_methods_or_accessors(bool value) {
    bit_field_ =
        HasStaticPrivateMethodsOrAccessorsField::update(bit_field_, value);
  }
  bool has_static_private_methods_or_accessors() const {
    return HasStaticPrivateMethodsOrAccessorsField::decode(bit_field_);
  }

  void set_class_scope_has_private_brand(bool value) {
    bit_field_ = ClassScopeHasPrivateBrandField::update(bit_field_, value);
  }
  bool class_scope_has_private_brand() const {
    return ClassScopeHasPrivateBrandField::decode(bit_field_);
  }

  bool private_name_lookup_skips_outer_class() const;

  ProducedPreparseData* produced_preparse_data() const {
    return produced_preparse_data_;
  }

 private:
  friend class BinAstNodeFactory;
  friend class BinAstPrintVisitor;
  friend Zone;

  BinAstFunctionLiteral(Zone* zone, const AstConsString* name,
                  BinAstValueFactory* ast_value_factory, DeclarationScope* scope,
                  const ScopedPtrList<BinAstStatement>& body,
                  int expected_property_count, int parameter_count,
                  int function_length, FunctionSyntaxKind function_syntax_kind,
                  ParameterFlag has_duplicate_parameters,
                  EagerCompileHint eager_compile_hint, int position,
                  bool has_braces, int function_literal_id,
                  ProducedPreparseData* produced_preparse_data = nullptr)
      : BinAstExpression(position, kFunctionLiteral),
        expected_property_count_(expected_property_count),
        parameter_count_(parameter_count),
        function_length_(function_length),
        function_token_position_(kNoSourcePosition),
        suspend_count_(0),
        function_literal_id_(function_literal_id),
        raw_name_(name),
        scope_(scope),
        body_(0, nullptr),
        raw_inferred_name_(ast_value_factory->empty_cons_string()),
        produced_preparse_data_(produced_preparse_data) {
    bit_field_ |= FunctionSyntaxKindBits::encode(function_syntax_kind) |
                  Pretenure::encode(false) |
                  HasDuplicateParameters::encode(has_duplicate_parameters ==
                                                 kHasDuplicateParameters) |
                  DontOptimizeReasonField::encode(BailoutReason::kNoReason) |
                  RequiresInstanceMembersInitializer::encode(false) |
                  HasBracesField::encode(has_braces) |
                  OneshotIIFEBit::encode(false);
    if (eager_compile_hint == kShouldEagerCompile) SetShouldEagerCompile();
  }

  using FunctionSyntaxKindBits =
      BinAstExpression::NextBitField<FunctionSyntaxKind, 3>;
  using Pretenure = FunctionSyntaxKindBits::Next<bool, 1>;
  using HasDuplicateParameters = Pretenure::Next<bool, 1>;
  using DontOptimizeReasonField =
      HasDuplicateParameters::Next<BailoutReason, 8>;
  using RequiresInstanceMembersInitializer =
      DontOptimizeReasonField::Next<bool, 1>;
  using ClassScopeHasPrivateBrandField =
      RequiresInstanceMembersInitializer::Next<bool, 1>;
  using HasStaticPrivateMethodsOrAccessorsField =
      ClassScopeHasPrivateBrandField::Next<bool, 1>;
  using HasBracesField = HasStaticPrivateMethodsOrAccessorsField::Next<bool, 1>;
  using OneshotIIFEBit = HasBracesField::Next<bool, 1>;

  // expected_property_count_ is the sum of instance fields and properties.
  // It can vary depending on whether a function is lazily or eagerly parsed.
  int expected_property_count_;
  int parameter_count_;
  int function_length_;
  int function_token_position_;
  int suspend_count_;
  int function_literal_id_;

  const AstConsString* raw_name_;
  DeclarationScope* scope_;
  ZonePtrList<BinAstStatement> body_;
  AstConsString* raw_inferred_name_;
  ProducedPreparseData* produced_preparse_data_;
};

// There are several types of Suspend node:
//
// Yield
// YieldStar
// Await
//
// Our Yield is different from the JS yield in that it "returns" its argument as
// is, without wrapping it in an iterator result object.  Such wrapping, if
// desired, must be done beforehand (see the parser).
class BinAstSuspend : public BinAstExpression {
 public:
  // With {kNoControl}, the {Suspend} behaves like yield, except that it never
  // throws and never causes the current generator to return. This is used to
  // desugar yield*.
  // TODO(caitp): remove once yield* desugaring for async generators is handled
  // in BytecodeGenerator.
  enum OnAbruptResume { kOnExceptionThrow, kNoControl };

  BinAstExpression* expression() const { return expression_; }
  OnAbruptResume on_abrupt_resume() const {
    return OnAbruptResumeField::decode(bit_field_);
  }

 private:
  friend class BinAstNodeFactory;
  friend class BinAstYield;
  friend class BinAstYieldStar;
  friend class BinAstAwait;

  BinAstSuspend(NodeType node_type, BinAstExpression* expression, int pos,
          OnAbruptResume on_abrupt_resume)
      : BinAstExpression(pos, node_type), expression_(expression) {
    bit_field_ |= OnAbruptResumeField::encode(on_abrupt_resume);
  }

  BinAstExpression* expression_;

  using OnAbruptResumeField = BinAstExpression::NextBitField<OnAbruptResume, 1>;
};


class BinAstYield final : public BinAstSuspend {
 private:
  friend class BinAstNodeFactory;
  BinAstYield(BinAstExpression* expression, int pos, OnAbruptResume on_abrupt_resume)
      : BinAstSuspend(kYield, expression, pos, on_abrupt_resume) {}
};

class BinAstYieldStar final : public BinAstSuspend {
 private:
  friend class BinAstNodeFactory;
  BinAstYieldStar(BinAstExpression* expression, int pos)
      : BinAstSuspend(kYieldStar, expression, pos,
                BinAstSuspend::OnAbruptResume::kNoControl) {}
};

class BinAstAwait final : public BinAstSuspend {
 private:
  friend class BinAstNodeFactory;

  BinAstAwait(BinAstExpression* expression, int pos)
      : BinAstSuspend(kAwait, expression, pos, BinAstSuspend::kOnExceptionThrow) {}
};


class BinAstUnaryOperation final : public BinAstExpression {
 public:
  Token::Value op() const { return OperatorField::decode(bit_field_); }
  BinAstExpression* expression() const { return expression_; }

 private:
  friend class BinAstNodeFactory;

  BinAstUnaryOperation(Token::Value op, BinAstExpression* expression, int pos)
      : BinAstExpression(pos, kUnaryOperation), expression_(expression) {
    bit_field_ |= OperatorField::encode(op);
    DCHECK(Token::IsUnaryOp(op));
  }

  BinAstExpression* expression_;

  using OperatorField = BinAstExpression::NextBitField<Token::Value, 7>;
};


class BinAstBinaryOperation final : public BinAstExpression {
 public:
  Token::Value op() const { return OperatorField::decode(bit_field_); }
  BinAstExpression* left() const { return left_; }
  BinAstExpression* right() const { return right_; }

  // Returns true if one side is a Smi literal, returning the other side's
  // sub-expression in |subexpr| and the literal Smi in |literal|.
  bool IsSmiLiteralOperation(BinAstExpression** subexpr, Smi* literal);

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstBinaryOperation(Token::Value op, BinAstExpression* left, BinAstExpression* right, int pos)
      : BinAstExpression(pos, kBinaryOperation), left_(left), right_(right) {
    bit_field_ |= OperatorField::encode(op);
    DCHECK(Token::IsBinaryOp(op));
  }

  BinAstExpression* left_;
  BinAstExpression* right_;

  using OperatorField = BinAstExpression::NextBitField<Token::Value, 7>;
};

class BinAstNaryOperation final : public BinAstExpression {
 public:
  Token::Value op() const { return OperatorField::decode(bit_field_); }
  BinAstExpression* first() const { return first_; }
  BinAstExpression* subsequent(size_t index) const {
    return subsequent_[index].expression;
  }

  size_t subsequent_length() const { return subsequent_.size(); }
  int subsequent_op_position(size_t index) const {
    return subsequent_[index].op_position;
  }

  void AddSubsequent(BinAstExpression* expr, int pos) {
    subsequent_.emplace_back(expr, pos);
  }

 private:
  friend class BinAstNodeFactory;

  BinAstNaryOperation(Zone* zone, Token::Value op, BinAstExpression* first,
                size_t initial_subsequent_size)
      : BinAstExpression(first->position(), kNaryOperation),
        first_(first),
        subsequent_(zone) {
    bit_field_ |= OperatorField::encode(op);
    DCHECK(Token::IsBinaryOp(op));
    DCHECK_NE(op, Token::EXP);
    subsequent_.reserve(initial_subsequent_size);
  }

  // Nary operations store the first (lhs) child expression inline, and the
  // child expressions (rhs of each op) are stored out-of-line, along with
  // their operation's position. Note that the Nary operation expression's
  // position has no meaning.
  //
  // So an nary add:
  //
  //    expr + expr + expr + ...
  //
  // is stored as:
  //
  //    (expr) [(+ expr), (+ expr), ...]
  //    '-.--' '-----------.-----------'
  //    first    subsequent entry list

  BinAstExpression* first_;

  struct NaryOperationEntry {
    BinAstExpression* expression;
    int op_position;
    NaryOperationEntry(BinAstExpression* e, int pos)
        : expression(e), op_position(pos) {}
  };
  ZoneVector<NaryOperationEntry> subsequent_;

  using OperatorField = BinAstExpression::NextBitField<Token::Value, 7>;
};


class BinAstCountOperation final : public BinAstExpression {
 public:
  bool is_prefix() const { return IsPrefixField::decode(bit_field_); }
  bool is_postfix() const { return !is_prefix(); }

  Token::Value op() const { return TokenField::decode(bit_field_); }

  BinAstExpression* expression() const { return expression_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstCountOperation(Token::Value op, bool is_prefix, BinAstExpression* expr, int pos)
      : BinAstExpression(pos, kCountOperation), expression_(expr) {
    bit_field_ |= IsPrefixField::encode(is_prefix) | TokenField::encode(op);
  }

  using IsPrefixField = BinAstExpression::NextBitField<bool, 1>;
  using TokenField = IsPrefixField::Next<Token::Value, 7>;

  BinAstExpression* expression_;
};


class BinAstCompareOperation final : public BinAstExpression {
 public:
  Token::Value op() const { return OperatorField::decode(bit_field_); }
  BinAstExpression* left() const { return left_; }
  BinAstExpression* right() const { return right_; }

  // Match special cases.
  bool IsLiteralCompareTypeof(BinAstExpression** expr, Literal** literal);
  bool IsLiteralCompareUndefined(BinAstExpression** expr);
  bool IsLiteralCompareNull(BinAstExpression** expr);

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstCompareOperation(Token::Value op, BinAstExpression* left, BinAstExpression* right,
                   int pos)
      : BinAstExpression(pos, kCompareOperation), left_(left), right_(right) {
    bit_field_ |= OperatorField::encode(op);
    DCHECK(Token::IsCompareOp(op));
  }

  BinAstExpression* left_;
  BinAstExpression* right_;

  using OperatorField = BinAstExpression::NextBitField<Token::Value, 7>;
};


class BinAstSpread final : public BinAstExpression {
 public:
  BinAstExpression* expression() const { return expression_; }

  int expression_position() const { return expr_pos_; }

 private:
  friend class BinAstNodeFactory;

  BinAstSpread(BinAstExpression* expression, int pos, int expr_pos)
      : BinAstExpression(pos, kSpread),
        expr_pos_(expr_pos),
        expression_(expression) {}

  int expr_pos_;
  BinAstExpression* expression_;
};


class BinAstConditional final : public BinAstExpression {
 public:
  BinAstExpression* condition() const { return condition_; }
  BinAstExpression* then_expression() const { return then_expression_; }
  BinAstExpression* else_expression() const { return else_expression_; }

 private:
  friend class BinAstNodeFactory;

  BinAstConditional(BinAstExpression* condition, BinAstExpression* then_expression,
              BinAstExpression* else_expression, int position)
      : BinAstExpression(position, kConditional),
        condition_(condition),
        then_expression_(then_expression),
        else_expression_(else_expression) {}

  BinAstExpression* condition_;
  BinAstExpression* then_expression_;
  BinAstExpression* else_expression_;
};


class BinAstAssignment : public BinAstExpression {
 public:
  Token::Value op() const { return TokenField::decode(bit_field_); }
  BinAstExpression* target() const { return target_; }
  BinAstExpression* value() const { return value_; }

  // The assignment was generated as part of block-scoped sloppy-mode
  // function hoisting, see
  // ES#sec-block-level-function-declarations-web-legacy-compatibility-semantics
  LookupHoistingMode lookup_hoisting_mode() const {
    return static_cast<LookupHoistingMode>(
        LookupHoistingModeField::decode(bit_field_));
  }
  void set_lookup_hoisting_mode(LookupHoistingMode mode) {
    bit_field_ =
        LookupHoistingModeField::update(bit_field_, static_cast<bool>(mode));
  }

 protected:
  BinAstAssignment(NodeType node_type, Token::Value op, BinAstExpression* target,
             BinAstExpression* value, int pos)
    : BinAstExpression(pos, node_type), target_(target), value_(value)
  {
    bit_field_ |= TokenField::encode(op);
  }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  using TokenField = BinAstExpression::NextBitField<Token::Value, 7>;
  using LookupHoistingModeField = TokenField::Next<bool, 1>;

  BinAstExpression* target_;
  BinAstExpression* value_;
};

class BinAstCompoundAssignment final : public BinAstAssignment {
 public:
  BinAstBinaryOperation* binary_operation() const { return binary_operation_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstCompoundAssignment(Token::Value op, BinAstExpression* target, BinAstExpression* value,
                     int pos, BinAstBinaryOperation* binary_operation)
      : BinAstAssignment(kCompoundAssignment, op, target, value, pos),
        binary_operation_(binary_operation) {}

  BinAstBinaryOperation* binary_operation_;
};


// This AST Node is used to represent a dynamic import call --
// import(argument).
class BinAstImportCallExpression final : public BinAstExpression {
 public:
  BinAstExpression* argument() const { return argument_; }

 private:
  friend class BinAstNodeFactory;

  BinAstImportCallExpression(BinAstExpression* argument, int pos)
      : BinAstExpression(pos, kImportCallExpression), argument_(argument) {}

  BinAstExpression* argument_;
};



// This class is produced when parsing the () in arrow functions without any
// arguments and is not actually a valid expression.
class BinAstEmptyParentheses final : public BinAstExpression {
 private:
  friend class BinAstNodeFactory;

  explicit BinAstEmptyParentheses(int pos) : BinAstExpression(pos, kEmptyParentheses) {
    // TODO(binast)
    // mark_parenthesized();
    DCHECK(false);
  }
};


class BinAstCall final : public BinAstExpression {
 public:
  BinAstExpression* expression() const { return expression_; }
  const ZonePtrList<BinAstExpression>* arguments() const { return &arguments_; }

  bool is_possibly_eval() const {
    return IsPossiblyEvalField::decode(bit_field_);
  }

  bool is_tagged_template() const {
    return IsTaggedTemplateField::decode(bit_field_);
  }

  bool is_optional_chain_link() const {
    return IsOptionalChainLinkField::decode(bit_field_);
  }

  bool only_last_arg_is_spread() {
    return !arguments_.is_empty() && arguments_.last()->IsSpread();
  }

  enum CallType {
    GLOBAL_CALL,
    WITH_CALL,
    NAMED_PROPERTY_CALL,
    KEYED_PROPERTY_CALL,
    NAMED_OPTIONAL_CHAIN_PROPERTY_CALL,
    KEYED_OPTIONAL_CHAIN_PROPERTY_CALL,
    NAMED_SUPER_PROPERTY_CALL,
    KEYED_SUPER_PROPERTY_CALL,
    PRIVATE_CALL,
    PRIVATE_OPTIONAL_CHAIN_CALL,
    SUPER_CALL,
    OTHER_CALL,
  };

  enum PossiblyEval {
    IS_POSSIBLY_EVAL,
    NOT_EVAL,
  };

  // Helpers to determine how to handle the call.
  CallType GetCallType() const;

  enum class TaggedTemplateTag { kTrue };

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstCall(Zone* zone, BinAstExpression* expression,
       const ScopedPtrList<BinAstExpression>& arguments, int pos, bool has_spread,
       PossiblyEval possibly_eval, bool optional_chain)
      : BinAstExpression(pos, kCall),
        expression_(expression),
        arguments_(0, nullptr) {
    bit_field_ |=
        IsPossiblyEvalField::encode(possibly_eval == IS_POSSIBLY_EVAL) |
        IsTaggedTemplateField::encode(false) |
        IsOptionalChainLinkField::encode(optional_chain);
  }

  BinAstCall(Zone* zone, BinAstExpression* expression,
       const ScopedPtrList<BinAstExpression>& arguments, int pos,
       TaggedTemplateTag tag)
      : BinAstExpression(pos, kCall),
        expression_(expression),
        arguments_(0, nullptr) {
    bit_field_ |= IsPossiblyEvalField::encode(false) |
                  IsTaggedTemplateField::encode(true) |
                  IsOptionalChainLinkField::encode(false);
  }

  using IsPossiblyEvalField = BinAstExpression::NextBitField<bool, 1>;
  using IsTaggedTemplateField = IsPossiblyEvalField::Next<bool, 1>;
  using IsOptionalChainLinkField = IsTaggedTemplateField::Next<bool, 1>;

  BinAstExpression* expression_;
  ZonePtrList<BinAstExpression> arguments_;
};


class BinAstCallNew final : public BinAstExpression {
 public:
  BinAstExpression* expression() const { return expression_; }
  const ZonePtrList<BinAstExpression>* arguments() const { return &arguments_; }

  bool only_last_arg_is_spread() {
    // return !arguments_.is_empty() && arguments_.last()->IsSpread();
    // TODO(binast)
    DCHECK(false);
    return false;
  }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstCallNew(Zone* zone, BinAstExpression* expression,
          const ScopedPtrList<BinAstExpression>& arguments, int pos)
      : BinAstExpression(pos, kCallNew),
        expression_(expression),
        arguments_(0, nullptr) {
  }

  BinAstExpression* expression_;
  ZonePtrList<BinAstExpression> arguments_;
};


class BinAstFailureExpression : public BinAstExpression {
 private:
  friend class BinAstNodeFactory;
  friend Zone;
  BinAstFailureExpression()
    : BinAstExpression(kNoSourcePosition, kFailureExpression)
  {
  }
};

// V8's notion of BreakableStatement does not correspond to the notion of
// BreakableStatement in ECMAScript. In V8, the idea is that a
// BreakableStatement is a statement that can be the target of a break
// statement.
//
// Since we don't want to track a list of labels for all kinds of statements, we
// only declare switchs, loops, and blocks as BreakableStatements.  This means
// that we implement breaks targeting other statement forms as breaks targeting
// a substatement thereof. For instance, in "foo: if (b) { f(); break foo; }" we
// pretend that foo is the label of the inner block. That's okay because one
// can't observe the difference.
// TODO(verwaest): Reconsider this optimization now that the tracking of labels
// is done at runtime.
class BinAstBreakableStatement : public BinAstStatement {
 protected:
  BinAstBreakableStatement(int position, NodeType type) : BinAstStatement(position, type) {}
};

class BinAstBlock final : public BinAstBreakableStatement {
 public:
  ZonePtrList<BinAstStatement>* statements() { return &statements_; }
  bool ignore_completion_value() const {
    return IgnoreCompletionField::decode(bit_field_);
  }
  bool is_breakable() const { return IsBreakableField::decode(bit_field_); }

  Scope* scope() const { return scope_; }
  void set_scope(Scope* scope) { scope_ = scope; }

  void InitializeStatements(const ScopedPtrList<BinAstStatement>& statements,
                            Zone* zone) {
    DCHECK_EQ(0, statements_.length());
  }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  ZonePtrList<BinAstStatement> statements_;
  Scope* scope_;

  using IgnoreCompletionField = BinAstBreakableStatement::NextBitField<bool, 1>;
  using IsBreakableField = IgnoreCompletionField::Next<bool, 1>;

 protected:
  BinAstBlock(Zone* zone, int capacity, bool ignore_completion_value,
        bool is_breakable)
      : BinAstBreakableStatement(kNoSourcePosition, kBlock),
        statements_(capacity, zone),
        scope_(nullptr) {
    bit_field_ |= IgnoreCompletionField::encode(ignore_completion_value) |
                  IsBreakableField::encode(is_breakable);
  }

  BinAstBlock(bool ignore_completion_value, bool is_breakable)
      : BinAstBlock(nullptr, 0, ignore_completion_value, is_breakable) {}
};


class BinAstLiteral final : public BinAstExpression {
 public:
   enum Type {
    kSmi,
    kHeapNumber,
    kBigInt,
    kString,
    kSymbol,
    kBoolean,
    kUndefined,
    kNull,
    kTheHole,
  };

  Type type() const { return TypeField::decode(bit_field_); }

  // TODO(binast)
  // Returns true if literal represents a property name (i.e. cannot be parsed
  // as array indices).
  // bool IsPropertyName() const;

  // TODO(binast)
  // Returns true if literal represents an array index.
  // Note, that in general the following statement is not true:
  //   key->IsPropertyName() != key->AsArrayIndex(...)
  // but for non-computed LiteralProperty properties the following is true:
  //   property->key()->IsPropertyName() != property->key()->AsArrayIndex(...)
  // bool AsArrayIndex(uint32_t* index) const;

  const AstRawString* AsRawPropertyName() {
    // TODO(binast)
    // DCHECK(IsPropertyName());
    return string_;
  }

  Smi AsSmiLiteral() const {
    DCHECK_EQ(kSmi, type());
    return Smi::FromInt(smi_);
  }

  // Returns true if literal represents a Number.
  bool IsNumber() const { return type() == kHeapNumber || type() == kSmi; }
  double AsNumber() const {
    DCHECK(IsNumber());
    switch (type()) {
      case kSmi:
        return smi_;
      case kHeapNumber:
        return number_;
      default:
        UNREACHABLE();
    }
  }

  // TODO(binast)
  // AstBigInt AsBigInt() const {
  //   DCHECK_EQ(type(), kBigInt);
  //   return bigint_;
  // }

  bool IsString() const { return type() == kString; }
  const AstRawString* AsRawString() {
    DCHECK_EQ(type(), kString);
    return string_;
  }

  // TODO(binast)
  // AstSymbol AsSymbol() {
  //   DCHECK_EQ(type(), kSymbol);
  //   return symbol_;
  // }

  V8_EXPORT_PRIVATE bool ToBooleanIsTrue() const;
  bool ToBooleanIsFalse() const { return !ToBooleanIsTrue(); }

  // TODO(binast)
  // bool ToUint32(uint32_t* value) const;

    // TODO(binast)
  // Returns an appropriate Object representing this Literal, allocating
  // a heap object if needed.
  // template <typename LocalIsolate>
  // Handle<Object> BuildValue(LocalIsolate* isolate) const;

  // TODO(binast)
  // Support for using Literal as a HashMap key. NOTE: Currently, this works
  // only for string and number literals!
  // uint32_t Hash();
  // static bool Match(void* literal1, void* literal2);


 private:
  friend class BinAstNodeFactory;
  friend class BinAstPrintVisitor;
  friend Zone;

  using TypeField = BinAstExpression::NextBitField<Type, 4>;

  BinAstLiteral(int smi, int position) 
      : BinAstExpression(position, kLiteral), smi_(smi) {
    bit_field_ = TypeField::update(bit_field_, kSmi);
  }

  BinAstLiteral(double number, int position)
      : BinAstExpression(position, kLiteral), number_(number) {
    bit_field_ = TypeField::update(bit_field_, kHeapNumber);
  }

  // BinAstLiteral(AstBigInt bigint, int position)
  //     : BinAstExpression(position, kLiteral), bigint_(bigint) {
  //   bit_field_ = TypeField::update(bit_field_, kBigInt);
  // }

  BinAstLiteral(const AstRawString* string, int position)
      : BinAstExpression(position, kLiteral), string_(string) {
    bit_field_ = TypeField::update(bit_field_, kString);
  }

  // BinAstLiteral(AstSymbol symbol /*, int position */)
  //     : BinAstExpression(/* position, */ kLiteral), symbol_(symbol) {
  //   bit_field_ = TypeField::update(bit_field_, kSymbol);
  // }

  BinAstLiteral(bool boolean, int position)
      : BinAstExpression(position, kLiteral), boolean_(boolean) {
    bit_field_ = TypeField::update(bit_field_, kBoolean);
  }

  BinAstLiteral(Type type, int position) 
      : BinAstExpression(position, kLiteral) {
    DCHECK(type == kNull || type == kUndefined || type == kTheHole);
    bit_field_ = TypeField::update(bit_field_, type);
  }

  union {
    const AstRawString* string_;
    int smi_;
    double number_;
    // AstSymbol symbol_;
    // AstBigInt bigint_;
    bool boolean_;
  };
};


// NB: I believe we don't need this for binary AST hierarchy because the type feedback vector
//     stuff only applies to the "real" AST during compilation for a particular Isolate.
//
// // Base class for literals that need space in the type feedback vector.
// class BinAstMaterializedLiteral : public BinAstExpression {
//  public:
//   // A Materializedliteral is simple if the values consist of only
//   // constants and simple object and array literals.
//   bool IsSimple() const;

//  protected:
//   BinAstMaterializedLiteral(int pos, NodeType type) : BinAstExpression(pos, type) {}

//   friend class BinAstArrayLiteral;
//   friend class BinAstObjectLiteral;

//   // Populate the depth field and any flags the literal has, returns the depth.
//   int InitDepthAndFlags();

//   bool NeedsInitialAllocationSite();

//   // Populate the constant properties/elements fixed array.
//   template <typename LocalIsolate>
//   void BuildConstants(LocalIsolate* isolate);

//   // If the expression is a literal, return the literal value;
//   // if the expression is a materialized literal and is_simple
//   // then return an Array or Object Boilerplate Description
//   // Otherwise, return undefined literal as the placeholder
//   // in the object literal boilerplate.
//   template <typename LocalIsolate>
//   Handle<Object> GetBoilerplateValue(BinAstExpression* expression,
//                                      LocalIsolate* isolate);
// };

// Node for capturing a regexp literal.
class BinAstRegExpLiteral final : public BinAstExpression {
 public:
  // Handle<String> pattern() const { return pattern_->string(); }
  const AstRawString* raw_pattern() const { return pattern_; }
  int flags() const { return flags_; }

 private:
  friend class BinAstNodeFactory;

  BinAstRegExpLiteral(const AstRawString* pattern, int flags, int pos)
      : BinAstExpression(pos, kRegExpLiteral),
        flags_(flags),
        pattern_(pattern) {}

  int const flags_;
  const AstRawString* const pattern_;
};

// Base class for Array and Object literals, providing common code for handling
// nested subliterals.
class BinAstAggregateLiteral : public BinAstExpression {
 public:
  enum Flags {
    kNoFlags = 0,
    kIsShallow = 1,
    kDisableMementos = 1 << 1,
    kNeedsInitialAllocationSite = 1 << 2,
  };

  bool is_initialized() const { return 0 < depth_; }
  int depth() const {
    DCHECK(is_initialized());
    return depth_;
  }

  bool is_shallow() const { return depth() == 1; }
  bool needs_initial_allocation_site() const {
    return NeedsInitialAllocationSiteField::decode(bit_field_);
  }

  int ComputeFlags(bool disable_mementos = false) const {
    int flags = kNoFlags;
    if (is_shallow()) flags |= kIsShallow;
    if (disable_mementos) flags |= kDisableMementos;
    if (needs_initial_allocation_site()) flags |= kNeedsInitialAllocationSite;
    return flags;
  }

  // An AggregateLiteral is simple if the values consist of only
  // constants and simple object and array literals.
  bool is_simple() const { return IsSimpleField::decode(bit_field_); }

  ElementsKind boilerplate_descriptor_kind() const {
    return BoilerplateDescriptorKindField::decode(bit_field_);
  }

 private:
  int depth_ : 31;
  using NeedsInitialAllocationSiteField =
      BinAstExpression::NextBitField<bool, 1>;
  using IsSimpleField = NeedsInitialAllocationSiteField::Next<bool, 1>;
  using BoilerplateDescriptorKindField =
      IsSimpleField::Next<ElementsKind, kFastElementsKindBits>;

 protected:
  friend class BinAstNodeFactory;
  BinAstAggregateLiteral(int pos, NodeType type)
      : BinAstExpression(pos, type), depth_(0) {
    bit_field_ |=
        NeedsInitialAllocationSiteField::encode(false) |
        IsSimpleField::encode(false) |
        BoilerplateDescriptorKindField::encode(FIRST_FAST_ELEMENTS_KIND);
  }

  void set_is_simple(bool is_simple) {
    bit_field_ = IsSimpleField::update(bit_field_, is_simple);
  }

  void set_boilerplate_descriptor_kind(ElementsKind kind) {
    DCHECK(IsFastElementsKind(kind));
    bit_field_ = BoilerplateDescriptorKindField::update(bit_field_, kind);
  }

  void set_depth(int depth) {
    DCHECK(!is_initialized());
    depth_ = depth;
  }

  void set_needs_initial_allocation_site(bool required) {
    bit_field_ = NeedsInitialAllocationSiteField::update(bit_field_, required);
  }

  template <class T, int size>
  using NextBitField = BoilerplateDescriptorKindField::Next<T, size>;
};


class BinAstThisExpression final : public BinAstExpression {
 private:
  friend class BinAstNodeFactory;
  friend Zone;
  BinAstThisExpression() : BinAstExpression(kNoSourcePosition, kThisExpression) {}
};


// An object literal has a boilerplate object that is used
// for minimizing the work when constructing it at runtime.
class BinAstObjectLiteral final : public BinAstAggregateLiteral {
 public:
  using Property = ObjectLiteralProperty;

  // Handle<ObjectBoilerplateDescription> boilerplate_description() const {
  //   DCHECK(!boilerplate_description_.is_null());
  //   return boilerplate_description_;
  // }
  int properties_count() const { return boilerplate_properties_; }
  const ZonePtrList<Property>* properties() const { return &properties_; }
  bool has_elements() const { return HasElementsField::decode(bit_field_); }
  bool has_rest_property() const {
    return HasRestPropertyField::decode(bit_field_);
  }
  bool fast_elements() const { return FastElementsField::decode(bit_field_); }
  bool has_null_prototype() const {
    return HasNullPrototypeField::decode(bit_field_);
  }

  bool is_empty() const {
    DCHECK(is_initialized());
    return !has_elements() && properties_count() == 0 &&
           properties()->length() == 0;
  }

  bool IsEmptyObjectLiteral() const {
    return is_empty() && !has_null_prototype();
  }

  // Populate the depth field and flags, returns the depth.
  int InitDepthAndFlags();

  // Get the boilerplate description, populating it if necessary.
  // template <typename LocalIsolate>
  // Handle<ObjectBoilerplateDescription> GetOrBuildBoilerplateDescription(
  //     LocalIsolate* isolate) {
  //   if (boilerplate_description_.is_null()) {
  //     BuildBoilerplateDescription(isolate);
  //   }
  //   return boilerplate_description();
  // }

  // Populate the boilerplate description.
  // template <typename LocalIsolate>
  // void BuildBoilerplateDescription(LocalIsolate* isolate);

  // Mark all computed expressions that are bound to a key that
  // is shadowed by a later occurrence of the same key. For the
  // marked expressions, no store code is emitted.
  void CalculateEmitStore(Zone* zone);

  // Determines whether the {CreateShallowObjectLiteratal} builtin can be used.
  bool IsFastCloningSupported() const;

  // Assemble bitfield of flags for the CreateObjectLiteral helper.
  int ComputeFlags(bool disable_mementos = false) const {
    int flags = BinAstAggregateLiteral::ComputeFlags(disable_mementos);
    if (fast_elements()) flags |= kFastElements;
    if (has_null_prototype()) flags |= kHasNullPrototype;
    return flags;
  }

  int EncodeLiteralType() {
    int flags = kNoFlags;
    if (fast_elements()) flags |= kFastElements;
    if (has_null_prototype()) flags |= kHasNullPrototype;
    return flags;
  }

  enum Flags {
    kFastElements = 1 << 3,
    kHasNullPrototype = 1 << 4,
  };
  STATIC_ASSERT(
      static_cast<int>(BinAstAggregateLiteral::kNeedsInitialAllocationSite) <
      static_cast<int>(kFastElements));

 private:
  friend class BinAstNodeFactory;

  BinAstObjectLiteral(Zone* zone, const ScopedPtrList<Property>& properties,
                uint32_t boilerplate_properties, int pos,
                bool has_rest_property)
      : BinAstAggregateLiteral(pos, kObjectLiteral),
        boilerplate_properties_(boilerplate_properties),
        properties_(0, nullptr) {
    bit_field_ |= HasElementsField::encode(false) |
                  HasRestPropertyField::encode(has_rest_property) |
                  FastElementsField::encode(false) |
                  HasNullPrototypeField::encode(false);
  }

  void InitFlagsForPendingNullPrototype(int i);

  void set_has_elements(bool has_elements) {
    bit_field_ = HasElementsField::update(bit_field_, has_elements);
  }
  void set_fast_elements(bool fast_elements) {
    bit_field_ = FastElementsField::update(bit_field_, fast_elements);
  }
  void set_has_null_protoype(bool has_null_prototype) {
    bit_field_ = HasNullPrototypeField::update(bit_field_, has_null_prototype);
  }
  uint32_t boilerplate_properties_;
  // Handle<ObjectBoilerplateDescription> boilerplate_description_;
  ZoneList<Property*> properties_;

  using HasElementsField = BinAstAggregateLiteral::NextBitField<bool, 1>;
  using HasRestPropertyField = HasElementsField::Next<bool, 1>;
  using FastElementsField = HasRestPropertyField::Next<bool, 1>;
  using HasNullPrototypeField = FastElementsField::Next<bool, 1>;
};

// An array literal has a literals object that is used
// for minimizing the work when constructing it at runtime.
class BinAstArrayLiteral final : public BinAstAggregateLiteral {
 public:
  // Handle<ArrayBoilerplateDescription> boilerplate_description() const {
  //   return boilerplate_description_;
  // }

  const ZonePtrList<BinAstExpression>* values() const { return &values_; }

  int first_spread_index() const { return first_spread_index_; }

  // Populate the depth field and flags, returns the depth.
  int InitDepthAndFlags();

  // Get the boilerplate description, populating it if necessary.
  // template <typename LocalIsolate>
  // Handle<ArrayBoilerplateDescription> GetOrBuildBoilerplateDescription(
  //     LocalIsolate* isolate) {
  //   if (boilerplate_description_.is_null()) {
  //     BuildBoilerplateDescription(isolate);
  //   }
  //   return boilerplate_description_;
  // }

  // Populate the boilerplate description.
  // template <typename LocalIsolate>
  // void BuildBoilerplateDescription(LocalIsolate* isolate);

  // Determines whether the {CreateShallowArrayLiteral} builtin can be used.
  bool IsFastCloningSupported() const;

  // Assemble bitfield of flags for the CreateArrayLiteral helper.
  int ComputeFlags(bool disable_mementos = false) const {
    return BinAstAggregateLiteral::ComputeFlags(disable_mementos);
  }

 private:
  friend class BinAstNodeFactory;

  BinAstArrayLiteral(Zone* zone, const ScopedPtrList<BinAstExpression>& values,
               int first_spread_index, int pos)
      : BinAstAggregateLiteral(pos, kArrayLiteral),
        first_spread_index_(first_spread_index),
        values_(0, nullptr) {
  }

  int first_spread_index_;
  // Handle<ArrayBoilerplateDescription> boilerplate_description_;
  ZonePtrList<BinAstExpression> values_;
};


class BinAstVariableProxyExpression final : public BinAstExpression {
 public:
  bool IsValidReferenceExpression() const { return !proxy_->is_new_target(); }

  Handle<String> name() const { return proxy_->name(); }
  const AstRawString* raw_name() const { return proxy_->raw_name(); }

  Variable* var() const { return proxy_->var(); }
  void set_var(Variable* v) { proxy_->set_var(v); }

  Scanner::Location location() {
    return Scanner::Location(position(), position() + raw_name()->length());
  }

  bool is_assigned() const { return proxy_->is_assigned(); }
  void set_is_assigned() { proxy_->set_is_assigned(); }
  void clear_is_assigned() { proxy_->clear_is_assigned(); }

  bool is_resolved() const { return proxy_->is_resolved(); }
  void set_is_resolved() { proxy_->set_is_resolved(); }

  bool is_new_target() const { return proxy_->is_new_target(); }
  void set_is_new_target() { proxy_->set_is_new_target(); }

  HoleCheckMode hole_check_mode() const { return proxy_->hole_check_mode(); }
  void set_needs_hole_check() { proxy_->set_needs_hole_check(); }

  bool IsPrivateName() const { return proxy_->IsPrivateName(); }

  // Bind this proxy to the variable var.
  void BindTo(Variable* var);

  void mark_removed_from_unresolved() { proxy_->mark_removed_from_unresolved(); }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstVariableProxyExpression(VariableProxy* proxy)
      : BinAstExpression(proxy->position(), kVariableProxyExpression),
        proxy_(proxy) {
  }

  VariableProxy* proxy_;
};

// Wraps an optional chain to provide a wrapper for jump labels.
class BinAstOptionalChain final : public BinAstExpression {
 public:
  BinAstExpression* expression() const { return expression_; }

 private:
  friend class BinAstNodeFactory;

  explicit BinAstOptionalChain(BinAstExpression* expression)
      : BinAstExpression(0, kOptionalChain), expression_(expression) {}

  BinAstExpression* expression_;
};

class BinAstProperty final : public BinAstExpression {
 public:
  bool is_optional_chain_link() const {
    return IsOptionalChainLinkField::decode(bit_field_);
  }

  bool IsValidReferenceExpression() const { return true; }

  BinAstExpression* obj() const { return obj_; }
  BinAstExpression* key() const { return key_; }

  bool IsSuperAccess() { return obj()->IsSuperPropertyReference(); }
  bool IsPrivateReference() const { return key()->IsPrivateName(); }

  // Returns the properties assign type.
  static AssignType GetAssignType(BinAstProperty* property) {
    if (property == nullptr) return NON_PROPERTY;
    if (property->IsPrivateReference()) {
      DCHECK(!property->IsSuperAccess());
      BinAstVariableProxyExpression* proxy = property->key()->AsVariableProxyExpression();
      DCHECK_NOT_NULL(proxy);
      Variable* var = proxy->var();

      switch (var->mode()) {
        case VariableMode::kPrivateMethod:
          return PRIVATE_METHOD;
        case VariableMode::kConst:
          return KEYED_PROPERTY;  // Use KEYED_PROPERTY for private fields.
        case VariableMode::kPrivateGetterOnly:
          return PRIVATE_GETTER_ONLY;
        case VariableMode::kPrivateSetterOnly:
          return PRIVATE_SETTER_ONLY;
        case VariableMode::kPrivateGetterAndSetter:
          return PRIVATE_GETTER_AND_SETTER;
        default:
          UNREACHABLE();
      }
    }
    bool super_access = property->IsSuperAccess();
    return (property->key()->IsPropertyName())
               ? (super_access ? NAMED_SUPER_PROPERTY : NAMED_PROPERTY)
               : (super_access ? KEYED_SUPER_PROPERTY : KEYED_PROPERTY);
  }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstProperty(BinAstExpression* obj, BinAstExpression* key, int pos, bool optional_chain)
      : BinAstExpression(pos, kProperty), obj_(obj), key_(key) {
    bit_field_ |= IsOptionalChainLinkField::encode(optional_chain);
  }

  using IsOptionalChainLinkField = BinAstExpression::NextBitField<bool, 1>;

  BinAstExpression* obj_;
  BinAstExpression* key_;
};


class BinAstExpressionStatement final : public BinAstStatement {
 public:
  void set_expression(BinAstExpression* e) { expression_ = e; }
  BinAstExpression* expression() const { return expression_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstExpressionStatement(BinAstExpression* expression, int pos)
      : BinAstStatement(pos, kExpressionStatement), expression_(expression) {}

  
  BinAstExpression* expression_;
};

class BinAstJumpStatement : public BinAstStatement {
 protected:
  BinAstJumpStatement(int pos, NodeType type) : BinAstStatement(pos, type) {}
};


class BinAstContinueStatement final : public BinAstJumpStatement {
 public:
  BinAstIterationStatement* target() const { return target_; }

 private:
  friend class BinAstNodeFactory;

  BinAstContinueStatement(BinAstIterationStatement* target, int pos)
      : BinAstJumpStatement(pos, kContinueStatement), target_(target) {}

  BinAstIterationStatement* target_;
};


class BinAstBreakStatement final : public BinAstJumpStatement {
 public:
  BinAstBreakableStatement* target() const { return target_; }

 private:
  friend class BinAstNodeFactory;

  BinAstBreakStatement(BinAstBreakableStatement* target, int pos)
      : BinAstJumpStatement(pos, kBreakStatement), target_(target) {}

  BinAstBreakableStatement* target_;
};


class BinAstReturnStatement final : public BinAstJumpStatement {
 public:
  enum Type { kNormal, kAsyncReturn, kSyntheticAsyncReturn };
  BinAstExpression* expression() const { return expression_; }

  Type type() const { return TypeField::decode(bit_field_); }
  bool is_async_return() const { return type() != kNormal; }
  bool is_synthetic_async_return() const {
    return type() == kSyntheticAsyncReturn;
  }

  int end_position() const { return end_position_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstReturnStatement(BinAstExpression* expression, Type type, int pos, int end_position)
      : BinAstJumpStatement(pos, kReturnStatement),
        expression_(expression),
        end_position_(end_position) {
    bit_field_ |= TypeField::encode(type);
  }

  BinAstExpression* expression_;
  int end_position_;

  using TypeField = BinAstJumpStatement::NextBitField<Type, 2>;
};


class BinAstWithStatement final : public BinAstStatement {
 public:
  Scope* scope() { return scope_; }
  BinAstExpression* expression() const { return expression_; }
  BinAstStatement* statement() const { return statement_; }
  void set_statement(BinAstStatement* s) { statement_ = s; }

 private:
  friend class BinAstNodeFactory;

  BinAstWithStatement(Scope* scope, BinAstExpression* expression, BinAstStatement* statement,
                int pos)
      : BinAstStatement(pos, kWithStatement),
        scope_(scope),
        expression_(expression),
        statement_(statement) {}

  Scope* scope_;
  BinAstExpression* expression_;
  BinAstStatement* statement_;
};


class BinAstCaseClause final : public ZoneObject {
 public:
  bool is_default() const { return label_ == nullptr; }
  BinAstExpression* label() const {
    DCHECK(!is_default());
    return label_;
  }
  ZonePtrList<BinAstStatement>* statements() { return &statements_; }

 private:
  friend class BinAstNodeFactory;

  BinAstCaseClause(Zone* zone, BinAstExpression* label,
             const ScopedPtrList<BinAstStatement>& statements);

  BinAstExpression* label_;
  ZonePtrList<BinAstStatement> statements_;
};


class BinAstSwitchStatement final : public BinAstBreakableStatement {
 public:
  BinAstExpression* tag() const { return tag_; }
  void set_tag(BinAstExpression* t) { tag_ = t; }

  ZonePtrList<BinAstCaseClause>* cases() { return &cases_; }

 private:
  friend class BinAstNodeFactory;

  BinAstSwitchStatement(Zone* zone, BinAstExpression* tag, int pos)
      : BinAstBreakableStatement(pos, kSwitchStatement), tag_(tag), cases_(4, zone) {}

  BinAstExpression* tag_;
  ZonePtrList<BinAstCaseClause> cases_;
};


// If-statements always have non-null references to their then- and
// else-parts. When parsing if-statements with no explicit else-part,
// the parser implicitly creates an empty statement. Use the
// HasThenStatement() and HasElseStatement() functions to check if a
// given if-statement has a then- or an else-part containing code.
class BinAstIfStatement final : public BinAstStatement {
 public:
  bool HasThenStatement() const { return !then_statement_->IsEmptyStatement(); }
  bool HasElseStatement() const { return !else_statement_->IsEmptyStatement(); }

  BinAstExpression* condition() const { return condition_; }
  BinAstStatement* then_statement() const { return then_statement_; }
  BinAstStatement* else_statement() const { return else_statement_; }

  void set_then_statement(BinAstStatement* s) { then_statement_ = s; }
  void set_else_statement(BinAstStatement* s) { else_statement_ = s; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  BinAstIfStatement(BinAstExpression* condition, BinAstStatement* then_statement,
              BinAstStatement* else_statement, int pos)
      : BinAstStatement(pos, kIfStatement),
        condition_(condition),
        then_statement_(then_statement),
        else_statement_(else_statement) {}

  BinAstExpression* condition_;
  BinAstStatement* then_statement_;
  BinAstStatement* else_statement_;
};


class BinAstDebuggerStatement final : public BinAstStatement {
 private:
  friend class BinAstNodeFactory;

  explicit BinAstDebuggerStatement(int pos) : BinAstStatement(pos, kDebuggerStatement) {}
};


class BinAstEmptyStatement final : public BinAstStatement {
 private:
  friend class BinAstNodeFactory;
  friend Zone;
  BinAstEmptyStatement() : BinAstStatement(kNoSourcePosition, kEmptyStatement) {}
};


class BinAstIterationStatement : public BinAstBreakableStatement {
 public:
  BinAstStatement* body() const { return body_; }
  void set_body(BinAstStatement* s) { body_ = s; }

 protected:
  BinAstIterationStatement(int pos, NodeType type)
      : BinAstBreakableStatement(pos, type), body_(nullptr) {}
  void Initialize(BinAstStatement* body) { body_ = body; }

 private:
  BinAstStatement* body_;
};

class BinAstDoWhileStatement final : public BinAstIterationStatement {
 public:
  void Initialize(BinAstExpression* cond, BinAstStatement* body) {
    BinAstIterationStatement::Initialize(body);
    cond_ = cond;
  }

  BinAstExpression* cond() const { return cond_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  explicit BinAstDoWhileStatement(int pos)
      : BinAstIterationStatement(pos, kDoWhileStatement), cond_(nullptr) {}

  BinAstExpression* cond_;
};


class BinAstWhileStatement final : public BinAstIterationStatement {
 public:
  void Initialize(BinAstExpression* cond, BinAstStatement* body) {
    BinAstIterationStatement::Initialize(body);
    cond_ = cond;
  }

  BinAstExpression* cond() const { return cond_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  explicit BinAstWhileStatement(int pos)
      : BinAstIterationStatement(pos, kWhileStatement), cond_(nullptr) {}

  BinAstExpression* cond_;
};


class BinAstForStatement final : public BinAstIterationStatement {
 public:
  void Initialize(BinAstStatement* init, BinAstExpression* cond, BinAstStatement* next,
                  BinAstStatement* body) {
    BinAstIterationStatement::Initialize(body);
    init_ = init;
    cond_ = cond;
    next_ = next;
  }

  BinAstStatement* init() const { return init_; }
  BinAstExpression* cond() const { return cond_; }
  BinAstStatement* next() const { return next_; }

 private:
  friend class BinAstNodeFactory;
  friend Zone;

  explicit BinAstForStatement(int pos)
      : BinAstIterationStatement(pos, kForStatement),
        init_(nullptr),
        cond_(nullptr),
        next_(nullptr) {}

  BinAstStatement* init_;
  BinAstExpression* cond_;
  BinAstStatement* next_;
};

// Shared class for for-in and for-of statements.
class BinAstForEachStatement : public BinAstIterationStatement {
 public:
  enum VisitMode {
    ENUMERATE,   // for (each in subject) body;
    ITERATE      // for (each of subject) body;
  };

  using BinAstIterationStatement::Initialize;

  static const char* VisitModeString(VisitMode mode) {
    return mode == ITERATE ? "for-of" : "for-in";
  }

  void Initialize(BinAstExpression* each, BinAstExpression* subject, BinAstStatement* body) {
    BinAstIterationStatement::Initialize(body);
    each_ = each;
    subject_ = subject;
  }

  BinAstExpression* each() const { return each_; }
  BinAstExpression* subject() const { return subject_; }

 protected:
  friend class BinAstNodeFactory;

  BinAstForEachStatement(int pos, NodeType type)
      : BinAstIterationStatement(pos, type), each_(nullptr), subject_(nullptr) {}

  BinAstExpression* each_;
  BinAstExpression* subject_;
};

class BinAstForInStatement final : public BinAstForEachStatement {
 private:
  friend class BinAstNodeFactory;

  explicit BinAstForInStatement(int pos) : BinAstForEachStatement(pos, kForInStatement) {}
};

class BinAstForOfStatement final : public BinAstForEachStatement {
 public:
  IteratorType type() const { return type_; }

 private:
  friend class BinAstNodeFactory;

  BinAstForOfStatement(int pos, IteratorType type)
      : BinAstForEachStatement(pos, kForOfStatement), type_(type) {}

  IteratorType type_;
};


class BinAstParseInfo {
 public:
  BinAstParseInfo(Zone* zone, const UnoptimizedCompileFlags flags, const BinAstStringConstants* ast_string_constants);

  BinAstValueFactory* GetOrCreateAstValueFactory();
  Zone* zone() const { return zone_.get(); }
  const UnoptimizedCompileFlags& flags() const { return flags_; }
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  uintptr_t stack_limit() const { return stack_limit_; }

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

  BinAstValueFactory* ast_value_factory() const {
    DCHECK(ast_value_factory_.get());
    return ast_value_factory_.get();
  }
  
  const BinAstStringConstants* ast_string_constants() const {
    return ast_string_constants_;
  }

  const AstRawString* function_name() const { return function_name_; }
  void set_function_name(const AstRawString* function_name) {
    function_name_ = function_name;
  }

  BinAstFunctionLiteral* literal() const { return literal_; }
  void set_literal(BinAstFunctionLiteral* literal) { literal_ = literal; }

  DeclarationScope* scope() const;

  int parameters_end_pos() const { return parameters_end_pos_; }
  void set_parameters_end_pos(int parameters_end_pos) {
    parameters_end_pos_ = parameters_end_pos;
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
  std::unique_ptr<BinAstValueFactory> ast_value_factory_;
  const AstRawString* function_name_;
  RuntimeCallStats* runtime_call_stats_;
  SourceRangeMap* source_range_map_;  // Used when block coverage is enabled.
  PendingCompilationErrorHandler pending_error_handler_;
  Logger* logger_;
  const BinAstStringConstants* ast_string_constants_;

  //----------- Output of parsing and scope analysis ------------------------
  BinAstFunctionLiteral* literal_;
  bool allow_eval_cache_ : 1;
  bool contains_asm_module_ : 1;
  LanguageMode language_mode_ : 1;
};

class BinAstNodeFactory final {
 public:
  BinAstNodeFactory(BinAstValueFactory* ast_value_factory, Zone* zone)
    : zone_(zone),
      ast_value_factory_(ast_value_factory),
      empty_statement_(zone->New<BinAstEmptyStatement>()),
      this_expression_(zone->New<BinAstThisExpression>()),
      failure_expression_(zone->New<BinAstFailureExpression>())
  {
  }

  BinAstNodeFactory* ast_node_factory() { return this; }
  BinAstValueFactory* ast_value_factory() const { return ast_value_factory_; }

  VariableDeclaration* NewVariableDeclaration(int pos) {
    return zone_->New<VariableDeclaration>(pos);
  }

  NestedVariableDeclaration* NewNestedVariableDeclaration(Scope* scope,
                                                          int pos) {
    return zone_->New<NestedVariableDeclaration>(scope, pos);
  }

  FunctionDeclaration* NewFunctionDeclaration(BinAstFunctionLiteral* fun, int pos) {
    return zone_->New<FunctionDeclaration>(fun, pos);
  }

  BinAstBlock* NewBlock(int capacity, bool ignore_completion_value) {
    return zone_->New<BinAstBlock>(zone_, capacity, ignore_completion_value, false);
  }

  BinAstBlock* NewBlock(bool ignore_completion_value, bool is_breakable) {
    return zone_->New<BinAstBlock>(ignore_completion_value, is_breakable);
  }

  BinAstBlock* NewBlock(bool ignore_completion_value,
                  const ScopedPtrList<BinAstStatement>& statements) {
    BinAstBlock* result = NewBlock(ignore_completion_value, false);
    result->InitializeStatements(statements, zone_);
    return result;
  }

#define STATEMENT_WITH_POSITION(NodeType) \
  BinAst##NodeType* New##NodeType(int pos) { return zone_->New<BinAst##NodeType>(pos); }
  STATEMENT_WITH_POSITION(DoWhileStatement)
  STATEMENT_WITH_POSITION(WhileStatement)
  STATEMENT_WITH_POSITION(ForStatement)
#undef STATEMENT_WITH_POSITION

  BinAstSwitchStatement* NewSwitchStatement(BinAstExpression* tag, int pos) {
    // return new (zone_) SwitchStatement(zone_, tag, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstForEachStatement* NewForEachStatement(ForEachStatement::VisitMode visit_mode,
                                        int pos) {
    // switch (visit_mode) {
    //   case ForEachStatement::ENUMERATE: {
    //     return new (zone_) ForInStatement(pos);
    //   }
    //   case ForEachStatement::ITERATE: {
    //     return new (zone_) ForOfStatement(pos, IteratorType::kNormal);
    //   }
    // }
    // UNREACHABLE();
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstForOfStatement* NewForOfStatement(int pos, IteratorType type) {
    // return new (zone_) ForOfStatement(pos, type);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstExpressionStatement* NewExpressionStatement(BinAstExpression* expression, int pos) {
    return zone_->New<BinAstExpressionStatement>(expression, pos);
  }

  BinAstContinueStatement* NewContinueStatement(BinAstIterationStatement* target, int pos) {
    // return new (zone_) ContinueStatement(target, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstBreakStatement* NewBreakStatement(BinAstBreakableStatement* target, int pos) {
    // return new (zone_) BreakStatement(target, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstReturnStatement* NewReturnStatement(BinAstExpression* expression, int pos,
                                      int end_position = kNoSourcePosition) {
    return zone_->New<BinAstReturnStatement>(expression, BinAstReturnStatement::kNormal,
                                       pos, end_position);
  }

  BinAstReturnStatement* NewAsyncReturnStatement(
      BinAstExpression* expression, int pos, int end_position = kNoSourcePosition) {
    // return new (zone_) ReturnStatement(
    //     expression, ReturnStatement::kAsyncReturn, pos, end_position);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstReturnStatement* NewSyntheticAsyncReturnStatement(
      BinAstExpression* expression, int pos, int end_position = kNoSourcePosition) {
    // return new (zone_) ReturnStatement(
    //     expression, ReturnStatement::kSyntheticAsyncReturn, pos, end_position);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstWithStatement* NewWithStatement(Scope* scope,
                                  BinAstExpression* expression,
                                  BinAstStatement* statement,
                                  int pos) {
    // return new (zone_) WithStatement(scope, expression, statement, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstIfStatement* NewIfStatement(BinAstExpression* condition, BinAstStatement* then_statement,
                              BinAstStatement* else_statement, int pos) {
    return zone_->New<BinAstIfStatement>(condition, then_statement, else_statement, pos);
  }

  BinAstTryCatchStatement* NewTryCatchStatement(BinAstBlock* try_block, Scope* scope,
                                          BinAstBlock* catch_block, int pos) {
    // return new (zone_) TryCatchStatement(try_block, scope, catch_block,
    //                                      HandlerTable::CAUGHT, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstTryCatchStatement* NewTryCatchStatementForReThrow(BinAstBlock* try_block,
                                                    Scope* scope,
                                                    BinAstBlock* catch_block,
                                                    int pos) {
    // return new (zone_) TryCatchStatement(try_block, scope, catch_block,
    //                                      HandlerTable::UNCAUGHT, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstTryCatchStatement* NewTryCatchStatementForDesugaring(BinAstBlock* try_block,
                                                       Scope* scope,
                                                       BinAstBlock* catch_block,
                                                       int pos) {
    // return new (zone_) TryCatchStatement(try_block, scope, catch_block,
    //                                      HandlerTable::DESUGARING, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstTryCatchStatement* NewTryCatchStatementForAsyncAwait(BinAstBlock* try_block,
                                                       Scope* scope,
                                                       BinAstBlock* catch_block,
                                                       int pos) {
    // return new (zone_) TryCatchStatement(try_block, scope, catch_block,
    //                                      HandlerTable::ASYNC_AWAIT, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstTryCatchStatement* NewTryCatchStatementForReplAsyncAwait(BinAstBlock* try_block,
                                                           Scope* scope,
                                                           BinAstBlock* catch_block,
                                                           int pos) {
    // return new (zone_) TryCatchStatement(
    //     try_block, scope, catch_block, HandlerTable::UNCAUGHT_ASYNC_AWAIT, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstTryFinallyStatement* NewTryFinallyStatement(BinAstBlock* try_block,
                                              BinAstBlock* finally_block, int pos) {
    // return new (zone_) TryFinallyStatement(try_block, finally_block, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstDebuggerStatement* NewDebuggerStatement(int pos) {
    // return new (zone_) DebuggerStatement(pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  class BinAstEmptyStatement* EmptyStatement() {
    return empty_statement_;
  }

  class BinAstThisExpression* ThisExpression() {
    // Clear any previously set "parenthesized" flag on this_expression_ so this
    // particular token does not inherit the it. The flag is used to check
    // during arrow function head parsing whether we came from parenthesized
    // exprssion parsing, since additional arrow function verification was done
    // there. It does not matter whether a flag is unset after arrow head
    // verification, so clearing at this point is fine.
    this_expression_->clear_parenthesized();
    return this_expression_;
  }


  BinAstExpression* FailureExpression() { return failure_expression_; }

  SloppyBlockFunctionStatement* NewSloppyBlockFunctionStatement(
      int pos, Variable* var, Token::Value init) {
    // return new (zone_)
    //     SloppyBlockFunctionStatement(pos, var, init, EmptyStatement());
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstCaseClause* NewCaseClause(BinAstExpression* label,
                            const ScopedPtrList<BinAstStatement>& statements) {
    // return new (zone_) CaseClause(zone_, label, statements);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstLiteral* NewStringLiteral(const AstRawString* string, int pos) {
    DCHECK_NOT_NULL(string);
    return zone_->New<BinAstLiteral>(string, pos);
  }

  BinAstLiteral* NewNumberLiteral(double number, int pos) {
    return zone_->New<BinAstLiteral>(number, pos);
  }

  BinAstLiteral* NewSmiLiteral(int number, int pos) {
    return zone_->New<BinAstLiteral>(number, pos);
  }

  BinAstLiteral* NewBigIntLiteral(AstBigInt bigint, int pos) {
    // return new (zone_) Literal(bigint, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstLiteral* NewBooleanLiteral(bool b, int pos) {
    return zone_->New<BinAstLiteral>(b, pos);
  }

  BinAstLiteral* NewNullLiteral(int pos) {
    return zone_->New<BinAstLiteral>(BinAstLiteral::kNull, pos);
  }

  BinAstLiteral* NewUndefinedLiteral(int pos) {
    return zone_->New<BinAstLiteral>(BinAstLiteral::kUndefined, pos);
  }

  BinAstLiteral* NewTheHoleLiteral() {
    return zone_->New<BinAstLiteral>(BinAstLiteral::kTheHole, kNoSourcePosition);
  }

  BinAstObjectLiteral* NewObjectLiteral(
      const ScopedPtrList<ObjectLiteral::Property>& properties,
      uint32_t boilerplate_properties, int pos, bool has_rest_property,
      Variable* home_object = nullptr) {
    // return new (zone_) ObjectLiteral(zone_, properties, boilerplate_properties,
    //                                  pos, has_rest_property);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  ObjectLiteral::Property* NewObjectLiteralProperty(
      BinAstExpression* key, BinAstExpression* value, ObjectLiteralProperty::Kind kind,
      bool is_computed_name) {
    // return new (zone_)
    //     ObjectLiteral::Property(key, value, kind, is_computed_name);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  ObjectLiteral::Property* NewObjectLiteralProperty(BinAstExpression* key,
                                                    BinAstExpression* value,
                                                    bool is_computed_name) {
    // return new (zone_) ObjectLiteral::Property(ast_value_factory_, key, value,
    //                                            is_computed_name);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstRegExpLiteral* NewRegExpLiteral(const AstRawString* pattern, int flags,
                                  int pos) {
    // return new (zone_) RegExpLiteral(pattern, flags, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstArrayLiteral* NewArrayLiteral(const ScopedPtrList<BinAstExpression>& values,
                                int pos) {
    // return new (zone_) ArrayLiteral(zone_, values, -1, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstArrayLiteral* NewArrayLiteral(const ScopedPtrList<BinAstExpression>& values,
                                int first_spread_index, int pos) {
    // return new (zone_) ArrayLiteral(zone_, values, first_spread_index, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstVariableProxyExpression* NewVariableProxyExpression(VariableProxy* proxy) {
    return zone_->New<BinAstVariableProxyExpression>(proxy);
  }

  VariableProxy* NewVariableProxy(Variable* var,
                                  int start_position = kNoSourcePosition) {
    return zone_->New<VariableProxy>(var, start_position);
  }

  VariableProxy* NewVariableProxy(const AstRawString* name,
                                  VariableKind variable_kind,
                                  int start_position = kNoSourcePosition) {
    DCHECK_NOT_NULL(name);
    return zone_->New<VariableProxy>(name, variable_kind, start_position);
  }

  // Recreates the VariableProxy in this Zone.
  VariableProxy* CopyVariableProxy(VariableProxy* proxy) {
    return zone_->New<VariableProxy>(proxy);
  }

  Variable* CopyVariable(Variable* variable) {
    return zone_->New<Variable>(variable);
  }

  BinAstOptionalChain* NewOptionalChain(BinAstExpression* expression) {
    // return new (zone_) OptionalChain(expression);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstProperty* NewProperty(BinAstExpression* obj, BinAstExpression* key, int pos,
                        bool optional_chain = false) {
    return zone_->New<BinAstProperty>(obj, key, pos, optional_chain);
  }


  BinAstCallNew* NewCallNew(BinAstExpression* expression,
                      const ScopedPtrList<BinAstExpression>& arguments, int pos,
                      bool has_spread) {
    return zone_->New<BinAstCallNew>(zone_, expression, arguments, pos);
  }

  BinAstCall* NewCall(BinAstExpression* expression,
                const ScopedPtrList<BinAstExpression>& arguments, int pos,
                bool has_spread,
                Call::PossiblyEval possibly_eval = Call::NOT_EVAL,
                bool optional_chain = false) {
    // TODO(binast): Fix this hack
    BinAstCall::PossiblyEval converted_possibly_eval;
    switch (possibly_eval) {
      case Call::NOT_EVAL: {
        converted_possibly_eval = BinAstCall::NOT_EVAL;
        break;
      }
      case Call::IS_POSSIBLY_EVAL: {
        converted_possibly_eval = BinAstCall::IS_POSSIBLY_EVAL;
        break;
      }
      default: {
        UNREACHABLE();
      }
    }
    return zone_->New<BinAstCall>(zone_, expression, arguments, pos, has_spread, converted_possibly_eval, optional_chain);
  }

  BinAstCall* NewTaggedTemplate(BinAstExpression* expression,
                          const ScopedPtrList<BinAstExpression>& arguments, int pos) {
    // return new (zone_)
    //     Call(zone_, expression, arguments, pos, Call::TaggedTemplateTag::kTrue);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstCallRuntime* NewCallRuntime(Runtime::FunctionId id,
                              const ScopedPtrList<BinAstExpression>& arguments,
                              int pos) {
    // return new (zone_)
    //     CallRuntime(zone_, Runtime::FunctionForId(id), arguments, pos);
        // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstCallRuntime* NewCallRuntime(const Runtime::Function* function,
                              const ScopedPtrList<BinAstExpression>& arguments,
                              int pos) {
    // return new (zone_) CallRuntime(zone_, function, arguments, pos);
        // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstCallRuntime* NewCallRuntime(int context_index,
                              const ScopedPtrList<BinAstExpression>& arguments,
                              int pos) {
    // return new (zone_) CallRuntime(zone_, context_index, arguments, pos);
        // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstUnaryOperation* NewUnaryOperation(Token::Value op,
                                    BinAstExpression* expression,
                                    int pos) {
    // return new (zone_) UnaryOperation(op, expression, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstBinaryOperation* NewBinaryOperation(Token::Value op,
                                      BinAstExpression* left,
                                      BinAstExpression* right,
                                      int pos) {
    return zone_->New<BinAstBinaryOperation>(op, left, right, pos);
  }

  BinAstNaryOperation* NewNaryOperation(Token::Value op, BinAstExpression* first,
                                  size_t initial_subsequent_size) {
    // return new (zone_) NaryOperation(zone_, op, first, initial_subsequent_size);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstCountOperation* NewCountOperation(Token::Value op,
                                    bool is_prefix,
                                    BinAstExpression* expr,
                                    int pos) {
    return zone_->New<BinAstCountOperation>(op, is_prefix, expr, pos);
  }

  BinAstCompareOperation* NewCompareOperation(Token::Value op,
                                        BinAstExpression* left,
                                        BinAstExpression* right,
                                        int pos) {
    return zone_->New<BinAstCompareOperation>(op, left, right, pos);
  }

  BinAstSpread* NewSpread(BinAstExpression* expression, int pos, int expr_pos) {
    // return new (zone_) Spread(expression, pos, expr_pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstConditional* NewConditional(BinAstExpression* condition,
                              BinAstExpression* then_expression,
                              BinAstExpression* else_expression,
                              int position) {
    // return new (zone_)
    //     Conditional(condition, then_expression, else_expression, position);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstAssignment* NewAssignment(Token::Value op,
                            BinAstExpression* target,
                            BinAstExpression* value,
                            int pos) {
    DCHECK(Token::IsAssignmentOp(op));
    DCHECK_NOT_NULL(target);
    DCHECK_NOT_NULL(value);

    if (op != Token::INIT && target->IsVariableProxyExpression()) {
      target->AsVariableProxyExpression()->set_is_assigned();
    }

    if (op == Token::ASSIGN || op == Token::INIT) {
      return zone_->New<BinAstAssignment>(BinAstNode::kAssignment, op, target, value, pos);
    } else {
      return zone_->New<BinAstCompoundAssignment>(
          op, target, value, pos,
          NewBinaryOperation(Token::BinaryOpForAssignment(op), target, value,
                             pos + 1));
    }
        // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstSuspend* NewYield(BinAstExpression* expression, int pos,
                    Suspend::OnAbruptResume on_abrupt_resume) {
    // if (!expression) expression = NewUndefinedLiteral(pos);
    // return new (zone_) Yield(expression, pos, on_abrupt_resume);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstYieldStar* NewYieldStar(BinAstExpression* expression, int pos) {
    // return new (zone_) YieldStar(expression, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstAwait* NewAwait(BinAstExpression* expression, int pos) {
    // if (!expression) expression = NewUndefinedLiteral(pos);
    // return new (zone_) Await(expression, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstThrow* NewThrow(BinAstExpression* exception, int pos) {
    // return new (zone_) Throw(exception, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstFunctionLiteral* NewFunctionLiteral(
      const AstRawString* name, DeclarationScope* scope,
      const ScopedPtrList<BinAstStatement>& body, int expected_property_count,
      int parameter_count, int function_length,
      FunctionLiteral::ParameterFlag has_duplicate_parameters,
      FunctionSyntaxKind function_syntax_kind,
      FunctionLiteral::EagerCompileHint eager_compile_hint, int position,
      bool has_braces, int function_literal_id,
      ProducedPreparseData* produced_preparse_data = nullptr) {
    // TODO(binast): Undo this hack around parser-base hard-coding the various enum params from FunctionLiteral
    BinAstFunctionLiteral::ParameterFlag converted_has_duplicate_parameters;
    switch(has_duplicate_parameters) {
      case FunctionLiteral::kHasDuplicateParameters:
        converted_has_duplicate_parameters = BinAstFunctionLiteral::kHasDuplicateParameters;
        break;
      case FunctionLiteral::kNoDuplicateParameters:
        converted_has_duplicate_parameters = BinAstFunctionLiteral::kNoDuplicateParameters;
        break;
      default:
        DCHECK(false);
        break;
    }

    // TODO(binast): Undo this hack around parser-base hard-coding the various enum params from FunctionLiteral
    BinAstFunctionLiteral::EagerCompileHint converted_eager_compile_hint;
    switch(eager_compile_hint) {
      case FunctionLiteral::kShouldEagerCompile:
        converted_eager_compile_hint = BinAstFunctionLiteral::kShouldEagerCompile;
        break;
      case FunctionLiteral::kShouldLazyCompile:
        converted_eager_compile_hint = BinAstFunctionLiteral::kShouldLazyCompile;
        break;
      default:
        DCHECK(false);
        break;
    }
    return zone_->New<BinAstFunctionLiteral>(
        zone_, name ? ast_value_factory_->NewConsString(name) : nullptr,
        ast_value_factory_, scope, body, expected_property_count,
        parameter_count, function_length, function_syntax_kind,
        converted_has_duplicate_parameters, converted_eager_compile_hint, position, has_braces,
        function_literal_id, produced_preparse_data);
  }

  // Creates a FunctionLiteral representing a top-level script, the
  // result of an eval (top-level or otherwise), or the result of calling
  // the Function constructor.
  BinAstFunctionLiteral* NewScriptOrEvalFunctionLiteral(
      DeclarationScope* scope, const ScopedPtrList<BinAstStatement>& body,
      int expected_property_count, int parameter_count) {
    return zone_->New<BinAstFunctionLiteral>(
        zone_, ast_value_factory_->empty_cons_string(), ast_value_factory_,
        scope, body, expected_property_count, parameter_count, parameter_count,
        FunctionSyntaxKind::kAnonymousExpression,
        BinAstFunctionLiteral::kNoDuplicateParameters,
        BinAstFunctionLiteral::kShouldLazyCompile, 0, /* has_braces */ false,
        kFunctionLiteralIdTopLevel);
  }

  ClassLiteral::Property* NewClassLiteralProperty(
      BinAstExpression* key, BinAstExpression* value, ClassLiteralProperty::Kind kind,
      bool is_static, bool is_computed_name, bool is_private) {
    // return new (zone_) ClassLiteral::Property(key, value, kind, is_static,
    //                                           is_computed_name, is_private);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  ClassLiteral* NewClassLiteral(
      ClassScope* scope, BinAstExpression* extends, BinAstFunctionLiteral* constructor,
      ZonePtrList<ClassLiteral::Property>* public_members,
      ZonePtrList<ClassLiteral::Property>* private_members,
      BinAstFunctionLiteral* static_fields_initializer,
      BinAstFunctionLiteral* instance_members_initializer_function,
      int start_position, int end_position, bool has_name_static_property,
      bool has_static_computed_names, bool is_anonymous,
      bool has_private_methods) {
    // return new (zone_) ClassLiteral(
    //     scope, extends, constructor, public_members, private_members,
    //     static_fields_initializer, instance_members_initializer_function,
    //     start_position, end_position, has_name_static_property,
    //     has_static_computed_names, is_anonymous, has_private_methods);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  NativeFunctionLiteral* NewNativeFunctionLiteral(const AstRawString* name,
                                                  v8::Extension* extension,
                                                  int pos) {
    // return new (zone_) NativeFunctionLiteral(name, extension, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  SuperPropertyReference* NewSuperPropertyReference(BinAstExpression* home_object,
                                                    int pos) {
    // return new (zone_) SuperPropertyReference(home_object, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  SuperCallReference* NewSuperCallReference(VariableProxyExpression* new_target_var,
                                            VariableProxyExpression* this_function_var,
                                            int pos) {
    // return new (zone_)
    //     SuperCallReference(new_target_var, this_function_var, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstEmptyParentheses* NewEmptyParentheses(int pos) {
    // return new (zone_) EmptyParentheses(pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  GetTemplateObject* NewGetTemplateObject(
      const ZonePtrList<const AstRawString>* cooked_strings,
      const ZonePtrList<const AstRawString>* raw_strings, int pos) {
    // return new (zone_) GetTemplateObject(cooked_strings, raw_strings, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  TemplateLiteral* NewTemplateLiteral(
      const ZonePtrList<const AstRawString>* string_parts,
      const ZonePtrList<BinAstExpression>* substitutions, int pos) {
    // return new (zone_) TemplateLiteral(string_parts, substitutions, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstImportCallExpression* NewImportCallExpression(BinAstExpression* args, int pos) {
    // return new (zone_) ImportCallExpression(args, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  BinAstImportCallExpression* NewImportCallExpression(BinAstExpression* specifier,
                                                BinAstExpression* import_assertions,
                                                int pos) {
    //return zone_->New<ImportCallExpression>(specifier, import_assertions, pos);
    DCHECK(false);
    return nullptr;
  }


  InitializeClassMembersStatement* NewInitializeClassMembersStatement(
      ZonePtrList<ClassLiteral::Property>* args, int pos) {
    // return new (zone_) InitializeClassMembersStatement(args, pos);
    // TODO(binast)
    DCHECK(false);
    return nullptr;
  }

  Zone* zone() const {
    DCHECK_NOT_NULL(zone_);
    return zone_;
  }

 private:
   Zone* zone_;
   BinAstValueFactory* ast_value_factory_;
   class BinAstEmptyStatement* empty_statement_;
   class BinAstThisExpression* this_expression_;
   BinAstFailureExpression* failure_expression_;
};

// Type testing & conversion functions overridden by concrete subclasses.
// Inline functions for AstNode.

#define DECLARE_NODE_FUNCTIONS(type)                                         \
  bool BinAstNode::Is##type() const { return node_type() == BinAstNode::k##type; } \
  BinAst##type* BinAstNode::As##type() {                                                \
    return node_type() == BinAstNode::k##type ? reinterpret_cast<BinAst##type*>(this)   \
                                           : nullptr;                        \
  }                                                                          \
  const BinAst##type* BinAstNode::As##type() const {                                    \
    return node_type() == BinAstNode::k##type                                   \
               ? reinterpret_cast<const BinAst##type*>(this)                         \
               : nullptr;                                                    \
  }
BINAST_AST_NODE_LIST(DECLARE_NODE_FUNCTIONS)
BINAST_FAILURE_NODE_LIST(DECLARE_NODE_FUNCTIONS)
#undef DECLARE_NODE_FUNCTIONS

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_H_

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_PRINT_VISITOR_H_
#define V8_PARSING_BINAST_PRINT_VISITOR_H_

#include <stdio.h>
#include "src/parsing/binast-visitor.h"

namespace v8 {
namespace internal {

class BinAstPrintVisitor final : public BinAstVisitor {
 public:
  BinAstPrintVisitor()
    : BinAstVisitor(),
      indent_level_(0)
  {
  }

  virtual void VisitFunctionLiteral(BinAstFunctionLiteral* function_literal) override {
    PrintIndentLevel();
    printf("BinAstFunctionLiteral(\"");
    PrintConsString(function_literal->raw_name());
    printf("\", \n");
    indent_level_++;
    auto body = function_literal->body();
    bool first = true;
    for (auto i = 0; i < body->length(); ++i) {
      if (!first) {
        printf(",\n");
      }
      first = false;
      this->VisitNode(body->at(i));
    }
    printf("\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitBlock(BinAstBlock* block) override {
    PrintIndentLevel();
    printf("BinAstBlock(\n");
    indent_level_++;
    auto statements = block->statements();
    for (auto i = 0; i < statements->length(); ++i) {
      this->VisitNode(statements->at(i));
      printf(",\n");
    }
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitIfStatement(BinAstIfStatement* if_statement) override {
    PrintIndentLevel();
    printf("IfStatement(\n");
    indent_level_++;
    this->VisitNode(if_statement->condition());
    printf(",\n");
    this->VisitNode(if_statement->then_statement());
    printf(",\n");
    this->VisitNode(if_statement->else_statement());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")\n");
  }

  virtual void VisitExpressionStatement(BinAstExpressionStatement* statement) override {
    PrintIndentLevel();
    printf("ExpressionStatement(\n");
    indent_level_++;
    this->VisitNode(statement->expression());
    printf("\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitLiteral(BinAstLiteral* literal) override {
    PrintIndentLevel();
    printf("Literal(");
    switch(literal->type()) {
      case BinAstLiteral::kSmi:
        printf("Smi:%f", literal->AsNumber());
        break;
      case BinAstLiteral::kHeapNumber:
        printf("HeapNumber:%f", literal->AsNumber());
        break;
      case BinAstLiteral::kBigInt:
        // TODO(binast)
        DCHECK(false);
        break;
      case BinAstLiteral::kString:
        printf("\"%.*s\"", literal->string_->byte_length(), literal->string_->raw_data());
        break;
      case BinAstLiteral::kSymbol:
        // TODO(binast)
        DCHECK(false);
        break;
      case BinAstLiteral::kBoolean:
        printf("%s", literal->boolean_ ? "true" : "false");
        break;
      case BinAstLiteral::kUndefined:
        printf("undefined");
        break;
      case BinAstLiteral::kNull:
        printf("null");
        break;
      case BinAstLiteral::kTheHole:
        printf("hole");
        break;
      default:
        UNREACHABLE();
    }
    printf(")");
  }

  virtual void VisitEmptyStatement(BinAstEmptyStatement* empty_statement) override {
    PrintIndentLevel();
    printf("EmptyStatement()");
  }

  virtual void VisitAssignment(BinAstAssignment* assignment) override {
    PrintIndentLevel();
    printf("Assignment(\n");
    indent_level_++;
    PrintIndentLevel();
    printf("op:%s,\n", Token::Name(assignment->op()));
    this->VisitNode(assignment->target());
    printf("\n");
    this->VisitNode(assignment->value());
    printf("\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitVariableProxyExpression(BinAstVariableProxyExpression* var_proxy) override {
    PrintIndentLevel();
    printf("VariableProxyExpression(%.*s)", var_proxy->raw_name()->byte_length(), var_proxy->raw_name()->raw_data());
  }

  virtual void VisitForStatement(BinAstForStatement* for_statement) override {
    PrintIndentLevel();
    printf("ForStatement(\n");
    indent_level_++;
    this->VisitNode(for_statement->init());
    printf(",\n");
    this->VisitNode(for_statement->cond());
    printf(",\n");
    this->VisitNode(for_statement->next());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitCompareOperation(BinAstCompareOperation* compare) override {
    PrintIndentLevel();
    printf("CompareOperation(\n");
    indent_level_++;
    PrintIndentLevel();
    printf("op:%s,\n", Token::Name(compare->op()));
    this->VisitNode(compare->left());
    printf(",\n");
    this->VisitNode(compare->right());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitCountOperation(BinAstCountOperation* operation) override {
    PrintIndentLevel();
    printf("CountOperation(\n");
    indent_level_++;
    PrintIndentLevel();
    printf("op:%s,\n", Token::Name(operation->op()));
    const char* fix = operation->is_prefix() ? "pre" : (operation->is_postfix() ? "post" : "none");
    PrintIndentLevel(); 
    printf("fix:%s,\n", fix);
    this->VisitNode(operation->expression());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitCall(BinAstCall* call) override {
    PrintIndentLevel();
    printf("Call(\n");
    indent_level_++;
    this->VisitNode(call->expression());
    printf(",\n");
    bool first = true;
    for (BinAstExpression* expr : *call->arguments()) {
      if (!first) {
        printf(",\n");
      }
      first = false;
      this->VisitNode(expr);
    }
    printf("\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitProperty(BinAstProperty* property) override {
    PrintIndentLevel();
    printf("Property(\n");
    indent_level_++;
    this->VisitNode(property->obj());
    printf(",\n");
    this->VisitNode(property->key());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitReturnStatement(BinAstReturnStatement* return_statement) override {
    PrintIndentLevel();
    printf("ReturnStatement(\n");
    indent_level_++;
    this->VisitNode(return_statement->expression());
    printf("\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }

  virtual void VisitBinaryOperation(BinAstBinaryOperation* binary_op) override {
    PrintIndentLevel();
    printf("BinaryOperation(\n");
    indent_level_++;
    PrintIndentLevel();
    printf("op:%s,\n", Token::Name(binary_op->op()));
    this->VisitNode(binary_op->left());
    printf(",\n");
    this->VisitNode(binary_op->right());
    printf(",\n");
    indent_level_--;
    PrintIndentLevel();
    printf(")");
  }


 private:
  void PrintConsString(const AstConsString* s) {
    std::forward_list<const AstRawString*> raw_strings = s->ToRawStrings();
    for (const AstRawString* raw_string : raw_strings) {
      printf("%.*s", raw_string->byte_length(), raw_string->raw_data());
    }
  }

  void PrintIndentLevel() {
    for (size_t i = 0; i < indent_level_; ++i) {
      printf("  ");
    }
  }

  size_t indent_level_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_PRINT_VISITOR_H_
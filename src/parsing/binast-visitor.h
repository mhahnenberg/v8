// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_VISITOR_H_
#define V8_PARSING_BINAST_VISITOR_H_

#include "src/ast/ast.h"

namespace v8{
namespace internal {

class BinAstVisitor {
public:
  virtual void VisitFunctionLiteral(FunctionLiteral* function_literal) = 0;
  virtual void VisitBlock(Block* block) = 0;
  virtual void VisitIfStatement(IfStatement* if_statement) = 0;
  virtual void VisitExpressionStatement(ExpressionStatement* statement) = 0;
  virtual void VisitLiteral(Literal* literal) = 0;
  virtual void VisitEmptyStatement(EmptyStatement* empty_statement) = 0;
  virtual void VisitAssignment(Assignment* assignment) = 0;
  virtual void VisitVariableProxyExpression(VariableProxyExpression* var_proxy) = 0;
  virtual void VisitForStatement(ForStatement* for_statement) = 0;
  virtual void VisitForInStatement(ForInStatement* for_in_statement) = 0;
  virtual void VisitWhileStatement(WhileStatement* while_statement) = 0;
  virtual void VisitDoWhileStatement(DoWhileStatement* do_while_statement) = 0;
  virtual void VisitCompareOperation(CompareOperation* compare) = 0;
  virtual void VisitCountOperation(CountOperation* operation) = 0;
  virtual void VisitCall(Call* call) = 0;
  virtual void VisitCallNew(CallNew* call) = 0;
  virtual void VisitProperty(Property* property) = 0;
  virtual void VisitReturnStatement(ReturnStatement* return_statement) = 0;
  virtual void VisitBinaryOperation(BinaryOperation* binary_op) = 0;
  virtual void VisitObjectLiteral(ObjectLiteral* object_literal) = 0;
  virtual void VisitArrayLiteral(ArrayLiteral* array_literal) = 0;
  virtual void VisitCompoundAssignment(CompoundAssignment* compound_assignment) = 0;

  void VisitNode(AstNode* node) {
    switch (node->node_type()) {
      case AstNode::kFunctionLiteral: {
        VisitFunctionLiteral(static_cast<FunctionLiteral*>(node));
        break;
      }

      case AstNode::kBlock: {
        VisitBlock(static_cast<Block*>(node));
        break;
      }

      case AstNode::kIfStatement: {
        VisitIfStatement(static_cast<IfStatement*>(node));
        break;
      }

      case AstNode::kExpressionStatement: {
        VisitExpressionStatement(static_cast<ExpressionStatement*>(node));
        break;
      }

      case AstNode::kLiteral: {
        VisitLiteral(static_cast<Literal*>(node));
        break;
      }

      case AstNode::kEmptyStatement: {
        VisitEmptyStatement(static_cast<EmptyStatement*>(node));
        break;
      }

      case AstNode::kAssignment: {
        VisitAssignment(static_cast<Assignment*>(node));
        break;
      }

      case AstNode::kVariableProxyExpression: {
        VisitVariableProxyExpression(static_cast<VariableProxyExpression*>(node));
        break;
      }

      case AstNode::kForStatement: {
        VisitForStatement(static_cast<ForStatement*>(node));
        break;
      }

      case AstNode::kForInStatement: {
        VisitForInStatement(static_cast<ForInStatement*>(node));
        break;
      }

      case AstNode::kWhileStatement: {
        VisitWhileStatement(static_cast<WhileStatement*>(node));
        break;
      }

      case AstNode::kDoWhileStatement: {
        VisitDoWhileStatement(static_cast<DoWhileStatement*>(node));
        break;
      }

      case AstNode::kCompareOperation: {
        VisitCompareOperation(static_cast<CompareOperation*>(node));
        break;
      }

      case AstNode::kCountOperation: {
        VisitCountOperation(static_cast<CountOperation*>(node));
        break;
      }

      case AstNode::kCall: {
        VisitCall(static_cast<Call*>(node));
        break;
      }

      case AstNode::kCallNew: {
        VisitCallNew(static_cast<CallNew*>(node));
        break;
      }

      case AstNode::kProperty: {
        VisitProperty(static_cast<Property*>(node));
        break;
      }

      case AstNode::kReturnStatement: {
        VisitReturnStatement(static_cast<ReturnStatement*>(node));
        break;
      }

      case AstNode::kBinaryOperation: {
        VisitBinaryOperation(static_cast<BinaryOperation*>(node));
        break;
      }

      case AstNode::kObjectLiteral: {
        VisitObjectLiteral(static_cast<ObjectLiteral*>(node));
        break;
      }

      case AstNode::kArrayLiteral: {
        VisitArrayLiteral(static_cast<ArrayLiteral*>(node));
        break;
      }

      case AstNode::kCompoundAssignment: {
        VisitCompoundAssignment(static_cast<CompoundAssignment*>(node));
        break;
      }

      default: {
        printf("Unimplemented node type: %s\n", node->node_type_name());
        DCHECK(false);
        break;
      }
    }
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_BINAST_VISITOR_H_
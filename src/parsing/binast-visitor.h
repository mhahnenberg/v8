// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_BINAST_VISITOR_H_
#define V8_PARSING_BINAST_VISITOR_H_

#include "binast.h"

namespace v8{
namespace internal {

class BinAstVisitor {
public:
  virtual void VisitFunctionLiteral(BinAstFunctionLiteral* function_literal) = 0;
  virtual void VisitBlock(BinAstBlock* block) = 0;
  virtual void VisitIfStatement(BinAstIfStatement* if_statement) = 0;
  virtual void VisitExpressionStatement(BinAstExpressionStatement* statement) = 0;
  virtual void VisitLiteral(BinAstLiteral* literal) = 0;
  virtual void VisitEmptyStatement(BinAstEmptyStatement* empty_statement) = 0;
  virtual void VisitAssignment(BinAstAssignment* assignment) = 0;
  virtual void VisitVariableProxyExpression(BinAstVariableProxyExpression* var_proxy) = 0;
  virtual void VisitForStatement(BinAstForStatement* for_statement) = 0;
  virtual void VisitCompareOperation(BinAstCompareOperation* compare) = 0;
  virtual void VisitCountOperation(BinAstCountOperation* operation) = 0;
  virtual void VisitCall(BinAstCall* call) = 0;
  virtual void VisitProperty(BinAstProperty* property) = 0;
  virtual void VisitReturnStatement(BinAstReturnStatement* return_statement) = 0;
  virtual void VisitBinaryOperation(BinAstBinaryOperation* binary_op) = 0;

  void VisitNode(BinAstNode* node) {
    switch (node->node_type()) {
      case BinAstNode::kFunctionLiteral: {
        VisitFunctionLiteral(static_cast<BinAstFunctionLiteral*>(node));
        break;
      }

      case BinAstNode::kBlock: {
        VisitBlock(static_cast<BinAstBlock*>(node));
        break;
      }

      case BinAstNode::kIfStatement: {
        VisitIfStatement(static_cast<BinAstIfStatement*>(node));
        break;
      }

      case BinAstNode::kExpressionStatement: {
        VisitExpressionStatement(static_cast<BinAstExpressionStatement*>(node));
        break;
      }

      case BinAstNode::kLiteral: {
        VisitLiteral(static_cast<BinAstLiteral*>(node));
        break;
      }

      case BinAstNode::kEmptyStatement: {
        VisitEmptyStatement(static_cast<BinAstEmptyStatement*>(node));
        break;
      }

      case BinAstNode::kAssignment: {
        VisitAssignment(static_cast<BinAstAssignment*>(node));
        break;
      }

      case BinAstNode::kVariableProxyExpression: {
        VisitVariableProxyExpression(static_cast<BinAstVariableProxyExpression*>(node));
        break;
      }

      case BinAstNode::kForStatement: {
        VisitForStatement(static_cast<BinAstForStatement*>(node));
        break;
      }

      case BinAstNode::kCompareOperation: {
        VisitCompareOperation(static_cast<BinAstCompareOperation*>(node));
        break;
      }

      case BinAstNode::kCountOperation: {
        VisitCountOperation(static_cast<BinAstCountOperation*>(node));
        break;
      }

      case BinAstNode::kCall: {
        VisitCall(static_cast<BinAstCall*>(node));
        break;
      }

      case BinAstNode::kProperty: {
        VisitProperty(static_cast<BinAstProperty*>(node));
        break;
      }

      case BinAstNode::kReturnStatement: {
        VisitReturnStatement(static_cast<BinAstReturnStatement*>(node));
        break;
      }

      case BinAstNode::kBinaryOperation: {
        VisitBinaryOperation(static_cast<BinAstBinaryOperation*>(node));
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
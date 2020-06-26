#include <stdio.h>
#include "src/libplatform/default-platform.h"
#include "src/parsing/scanner-character-streams.h"
#include "binast-parser.h"

static const char* test_scripts[] = {
  // TODO(binast): (sloppy mode) "42",
  "'use strict';\n42",
  "'use strict';\n'foo'",
  "'use strict';\nif (true) { 'foo'; }",
  "'use strict';\nif (true) { 'foo'; } else { 'bar'; }",
  "'use strict';\nvar sum = 0;\nfor (var i = 0; i < 10; ++i) {\n  sum += i;\n}\nconsole.log(sum);",
  "'use strict';\nvar square = function(x) { return x * x; };\nconsole.log('square(3) =', square(3));",
  "'use strict';\nfunction square(x) { return x * x; }\nconsole.log('square(5) =', square(5));",
};

static const size_t num_test_scripts = sizeof(test_scripts) / sizeof(char*);

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
    for (auto i = 0; i < body->length(); ++i) {
      this->VisitNode(body->at(i));
      printf(",\n");
    }
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
        printf("\"%s\"", literal->string_->raw_data());
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
    printf("VariableProxyExpression(%s)", var_proxy->raw_name()->raw_data());
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
    for (BinAstExpression* expr : *call->arguments()) {
      this->VisitNode(expr);
      printf(",\n");
    }
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
      printf("%s", raw_string->raw_data());
    }
  }

  void PrintIndentLevel() {
    for (size_t i = 0; i < indent_level_; ++i) {
      printf("  ");
    }
  }

  size_t indent_level_;
};

}
}


static void ParseScript(const char* script) {
  // Create allocator/zone
  v8::internal::AccountingAllocator allocator;
  v8::internal::BinAstStringConstants astStringConstants(&allocator);
  std::unique_ptr<v8::internal::Zone> parseInfoZone = std::make_unique<v8::internal::Zone>(&allocator, ZONE_NAME);

  // Create compile flags to pass to parser
  v8::internal::Isolate* isolate = v8::internal::Isolate::New();
  v8::internal::UnoptimizedCompileFlags flags = v8::internal::UnoptimizedCompileFlags::ForTest(isolate);

  // Are these right?
  flags.set_is_toplevel(true);
  flags.set_is_eager(true);

  // Create ParseInfo
  printf("Creating BinAstParseInfo...");
  v8::internal::BinAstParseInfo parseInfo(parseInfoZone.release(), flags, &astStringConstants);
  parseInfo.set_character_stream(v8::internal::ScannerStream::ForTesting(script, strlen(script)));
  printf("Done!\n");

  // Create BinAstParser
  printf("Creating BinAstParser...");
  v8::internal::BinAstParser parser(&parseInfo);
  printf("Done!\n");

  // Parse program
  printf("Parsing program...");
  parser.ParseProgram(&parseInfo);
  printf("Done!\n");

  // Examine result.
  printf("Dumping AST...\n");
  v8::internal::BinAstPrintVisitor visitor;
  visitor.VisitNode(parseInfo.literal());
  printf("\n");
}

int main() {
  printf("Hello, v8!\n");
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
  printf("v8 initialized\n");

  for (size_t i = 0; i < num_test_scripts; ++i) {
    printf("\nScript #%zu\n", i);
    printf("%s\n\n", test_scripts[i]);
    ParseScript(test_scripts[i]);
  }

  return 0;
}
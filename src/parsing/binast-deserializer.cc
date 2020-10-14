// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/binast-deserializer.h"
#include "src/parsing/binast-deserializer-inl.h"
#include "src/ast/ast.h"
#include "src/parsing/binast-serialize-visitor.h"
#include "src/parsing/parser.h"
#include "src/objects/fixed-array-inl.h"
#include "src/zone/zone-list-inl.h"
#include "third_party/zlib/zlib.h"

namespace v8 {
namespace internal {

BinAstDeserializer::BinAstDeserializer(Isolate* isolate, Parser* parser,
                                       Handle<ByteArray> parse_data)
    : isolate_(isolate),
      parser_(parser),
      parse_data_(parse_data) {}

AstNode* BinAstDeserializer::DeserializeAst(
    base::Optional<uint32_t> start_offset, base::Optional<uint32_t> length) {
  ByteArray serialized_ast = *parse_data_;

  DCHECK(UseCompression() == BinAstSerializeVisitor::UseCompression());
  std::unique_ptr<uint8_t[]> compressed_byte_array_with_size_header = std::make_unique<uint8_t[]>(serialized_ast.length());
  serialized_ast.copy_out(0, compressed_byte_array_with_size_header.get(), serialized_ast.length());

  std::unique_ptr<uint8_t[]> uncompressed_byte_array;
  size_t original_size = 0;
  if (UseCompression()) {
    original_size = *reinterpret_cast<size_t*>(compressed_byte_array_with_size_header.get());
    uint8_t* compressed_data = compressed_byte_array_with_size_header.get() + sizeof(size_t);

    uncompressed_byte_array = std::make_unique<uint8_t[]>(original_size);
    int uncompress_result = uncompress(uncompressed_byte_array.get(), &original_size, compressed_data, serialized_ast.length() - sizeof(size_t));
    if (uncompress_result != Z_OK) {
      printf("Error decompressing serialized AST: %s\n", zError(uncompress_result));
      UNREACHABLE();
    }
  } else {
    original_size = serialized_ast.length();
    uncompressed_byte_array = std::move(compressed_byte_array_with_size_header);
  }

  int offset = 0;
  auto string_table_result = DeserializeStringTable(uncompressed_byte_array.get(), offset);
  offset = string_table_result.new_offset;
  bool is_toplevel = true;
  if (start_offset.has_value()) {
    is_toplevel = false;
    offset = start_offset.value();
  }

  auto result = DeserializeAstNode(uncompressed_byte_array.get(), offset, is_toplevel);
  // Check that we consumed all the bytes that were serialized.
  DCHECK(static_cast<size_t>(result.new_offset) ==
         (length.value_or(original_size) + start_offset.value_or(0)));
  return result.value;
}

BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeAstNode(uint8_t* serialized_binast, int offset, bool is_toplevel) {
  auto bit_field = DeserializeUint32(serialized_binast, offset);
  offset = bit_field.new_offset;

  auto position = DeserializeInt32(serialized_binast, offset);
  offset = position.new_offset;

  AstNode::NodeType nodeType = AstNode::NodeTypeField::decode(bit_field.value);
  switch (nodeType) {
  case AstNode::kFunctionLiteral: {
    BinAstDeserializer::DeserializeResult<uint32_t> start_offset =
        DeserializeUint32(serialized_binast, offset);
    offset = start_offset.new_offset;

    BinAstDeserializer::DeserializeResult<uint32_t> length =
        DeserializeUint32(serialized_binast, offset);
    offset = length.new_offset;

    auto result = DeserializeFunctionLiteral(serialized_binast, bit_field.value,
                                             position.value, offset);

    if (!is_toplevel) {
      Handle<UncompiledDataWithInnerBinAstParseData> data =
          isolate_->factory()->NewUncompiledDataWithInnerBinAstParseData(
              result.value->GetInferredName(isolate_),
              result.value->start_position(), result.value->end_position(),
              parse_data_, start_offset.value, length.value);

      result.value->set_uncompiled_data_with_inner_bin_ast_parse_data(data);
    }

    return {result.value, result.new_offset};
  }
  case AstNode::kReturnStatement: {
    auto result = DeserializeReturnStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBinaryOperation: {
    auto result = DeserializeBinaryOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kProperty: {
    auto result = DeserializeProperty(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kExpressionStatement: {
    auto result = DeserializeExpressionStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kVariableProxyExpression: {
    auto result = DeserializeVariableProxyExpression(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kLiteral: {
    auto result = DeserializeLiteral(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCall: {
    auto result = DeserializeCall(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCallNew: {
    auto result = DeserializeCallNew(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kIfStatement: {
    auto result = DeserializeIfStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kBlock: {
    auto result = DeserializeBlock(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kAssignment: {
    auto result = DeserializeAssignment(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompareOperation: {
    auto result = DeserializeCompareOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kEmptyStatement: {
    auto result = DeserializeEmptyStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForStatement: {
    auto result = DeserializeForStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForInStatement: {
    auto result = DeserializeForInStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCountOperation: {
    auto result = DeserializeCountOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kCompoundAssignment: {
    auto result = DeserializeCompoundAssignment(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kWhileStatement: {
    auto result = DeserializeWhileStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kDoWhileStatement: {
    auto result = DeserializeDoWhileStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kThisExpression: {
    auto result = DeserializeThisExpression(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kUnaryOperation: {
    auto result = DeserializeUnaryOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kObjectLiteral: {
    auto result = DeserializeObjectLiteral(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kArrayLiteral: {
    auto result = DeserializeArrayLiteral(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kNaryOperation: {
    auto result = DeserializeNaryOperation(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kConditional: {
    auto result = DeserializeConditional(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kTryCatchStatement: {
    auto result = DeserializeTryCatchStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kRegExpLiteral: {
    auto result = DeserializeRegExpLiteral(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kSwitchStatement: {
    auto result = DeserializeSwitchStatement(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kThrow: {
    auto result = DeserializeThrow(serialized_binast, bit_field.value, position.value, offset);
    return {result.value, result.new_offset};
  }
  case AstNode::kForOfStatement:
  case AstNode::kSloppyBlockFunctionStatement:
  case AstNode::kContinueStatement:
  case AstNode::kBreakStatement:
  case AstNode::kTryFinallyStatement:
  case AstNode::kDebuggerStatement:
  case AstNode::kInitializeClassMembersStatement:
  case AstNode::kAwait:
  case AstNode::kCallRuntime:
  case AstNode::kClassLiteral:
  case AstNode::kEmptyParentheses:
  case AstNode::kGetTemplateObject:
  case AstNode::kImportCallExpression:
  case AstNode::kNativeFunctionLiteral:
  case AstNode::kOptionalChain:
  case AstNode::kSpread:
  case AstNode::kSuperCallReference:
  case AstNode::kSuperPropertyReference:
  case AstNode::kTemplateLiteral:
  case AstNode::kYield:
  case AstNode::kYieldStar:
  case AstNode::kWithStatement:
  case AstNode::kFailureExpression: {
    // We should never get here because the serializer should give up if it encounters these node types.
    AstNode temp(0, nodeType);
    printf("Got unhandled node type: %s\n", temp.node_type_name());
    UNREACHABLE();
  }
  }
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeVariableMap(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto total_local_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_local_variables.new_offset;

  for (uint32_t i = 0; i < total_local_variables.value; ++i) {
    auto start_offset = offset;
    auto new_variable =
        DeserializeLocalVariable(serialized_binast, offset, scope);
    variables_by_id_.insert({start_offset, new_variable.value});
    offset = new_variable.new_offset;
  }

  auto total_nonlocal_variables = DeserializeUint32(serialized_binast, offset);
  offset = total_nonlocal_variables.new_offset;

  for (uint32_t i = 0; i < total_nonlocal_variables.value; ++i) {
    auto start_offset = offset;
    auto new_variable =
        DeserializeNonLocalVariable(serialized_binast, offset, scope);
    variables_by_id_.insert({start_offset, new_variable.value});
    offset = new_variable.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<Declaration*> BinAstDeserializer::DeserializeDeclaration(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto start_pos = DeserializeInt32(serialized_binast, offset);
  offset = start_pos.new_offset;

  auto decl_type = DeserializeUint8(serialized_binast, offset);
  offset = decl_type.new_offset;

  auto variable = DeserializeVariableReference(serialized_binast, offset, scope);
  offset = variable.new_offset;

  Declaration* result;
  switch (decl_type.value) {
    case Declaration::DeclType::VariableDecl: {
      auto is_nested = DeserializeUint8(serialized_binast, offset);
      offset = is_nested.new_offset;

      // TODO(binast): Add support for nested var decls.
      DCHECK(!is_nested.value);
      result = new (zone()) VariableDeclaration(start_pos.value, is_nested.value);
      break;
    }
    case Declaration::DeclType::FunctionDecl: {
      // We need to push the Scope being currently deserialized onto the Scope stack while deserializing
      // the FunctionLiterals inside FunctionDeclarations, otherwise their scope chain will end up skipping it.
      Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope->AsDeclarationScope());
      auto func = DeserializeAstNode(serialized_binast, offset);
      offset = func.new_offset;

      result = new (zone()) FunctionDeclaration((void*)func.value, start_pos.value);
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  result->set_var(variable.value);
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeDeclarations(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto num_decls = DeserializeUint32(serialized_binast, offset);
  offset = num_decls.new_offset;

  for (uint32_t i = 0; i < num_decls.value; ++i) {
    auto decl_result = DeserializeDeclaration(serialized_binast, offset, scope);
    scope->decls_.Add(decl_result.value);
    offset = decl_result.new_offset;
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeScopeParameters(uint8_t* serialized_binast, int offset, DeclarationScope* scope) {
  auto num_parameters_result = DeserializeInt32(serialized_binast, offset);
  offset = num_parameters_result.new_offset;
  scope->num_parameters_ = num_parameters_result.value;

  for (int i = 0; i < num_parameters_result.value; ++i) {
    auto param_result = DeserializeVariableReference(serialized_binast, offset, scope);
    offset = param_result.new_offset;
    scope->params_.Add(param_result.value, zone());
  }

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeCommonScopeFields(uint8_t* serialized_binast, int offset, Scope* scope) {
  auto variable_map_result = DeserializeScopeVariableMap(serialized_binast, offset, scope);
  offset = variable_map_result.new_offset;
  // unresolved_list_
  auto declarations_result = DeserializeScopeDeclarations(serialized_binast, offset, scope);
  offset = declarations_result.new_offset;

  // scope_info_ TODO(binast): do we need this?
#ifdef DEBUG
  auto has_scope_name = DeserializeUint8(serialized_binast, offset);
  offset = has_scope_name.new_offset;

  if (has_scope_name.value) {
    auto scope_name_result = DeserializeRawStringReference(serialized_binast, offset);
    offset = scope_name_result.new_offset;
    scope->SetScopeName(scope_name_result.value);
  }

  auto already_resolved_result = DeserializeUint8(serialized_binast, offset);
  offset = already_resolved_result.new_offset;
  scope->already_resolved_ = already_resolved_result.value;

  auto needs_migration_result = DeserializeUint8(serialized_binast, offset);
  offset = needs_migration_result.new_offset;
  scope->needs_migration_ = needs_migration_result.value;
#endif
  auto start_position_result = DeserializeInt32(serialized_binast, offset);
  offset = start_position_result.new_offset;
  scope->set_start_position(start_position_result.value);

  auto end_position_result = DeserializeInt32(serialized_binast, offset);
  offset = end_position_result.new_offset;
  scope->set_end_position(end_position_result.value);

  auto num_stack_slots_result = DeserializeInt32(serialized_binast, offset);
  offset = num_stack_slots_result.new_offset;
  scope->num_stack_slots_ = num_stack_slots_result.value;

  auto num_heap_slots_result = DeserializeInt32(serialized_binast, offset);
  offset = num_heap_slots_result.new_offset;
  scope->num_heap_slots_ = num_heap_slots_result.value;

  auto encoded_boolean_flags_result = DeserializeUint16Flags(serialized_binast, offset);
  offset = encoded_boolean_flags_result.new_offset;

  scope->is_strict_ = encoded_boolean_flags_result.value[0];
  scope->calls_eval_ = encoded_boolean_flags_result.value[1];
  scope->sloppy_eval_can_extend_vars_ = encoded_boolean_flags_result.value[2];
  scope->scope_nonlinear_ = encoded_boolean_flags_result.value[3];
  scope->is_hidden_ = encoded_boolean_flags_result.value[4];
  scope->is_debug_evaluate_scope_ = encoded_boolean_flags_result.value[5];
  scope->inner_scope_calls_eval_ = encoded_boolean_flags_result.value[6];
  scope->force_context_allocation_for_parameters_ = encoded_boolean_flags_result.value[7];
  scope->is_declaration_scope_ = encoded_boolean_flags_result.value[8];
  scope->private_name_lookup_skips_outer_class_ = encoded_boolean_flags_result.value[9];
  scope->must_use_preparsed_scope_data_ = encoded_boolean_flags_result.value[10];
  scope->is_repl_mode_scope_ = encoded_boolean_flags_result.value[11];
  scope->deserialized_scope_uses_external_cache_ = encoded_boolean_flags_result.value[12];

  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<Scope*> BinAstDeserializer::DeserializeScope(uint8_t* serialized_binast, int offset) {
  Scope* scope = nullptr;
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  switch (scope_type.value) {
    case FUNCTION_SCOPE:
    case SCRIPT_SCOPE:
    case MODULE_SCOPE: {
      // Client should use the more specific version of this function.
      UNREACHABLE();
    }
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case WITH_SCOPE: {
      printf("scope_type.value = %d\n", scope_type.value);
      // TODO(binast): Implement
      DCHECK(false);
      break;
    }
    case CATCH_SCOPE: {
      scope = parser_->NewScope(CATCH_SCOPE);
      break;
    }
    case BLOCK_SCOPE: {
      scope = parser_->NewVarblockScope();
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  DCHECK(scope_type.value == scope->scope_type());

  auto common_fields_result = DeserializeCommonScopeFields(serialized_binast, offset, scope);
  offset = common_fields_result.new_offset;

  return {scope, offset};
}


BinAstDeserializer::DeserializeResult<DeclarationScope*> BinAstDeserializer::DeserializeDeclarationScope(uint8_t* serialized_binast, int offset) {
  DeclarationScope* scope = nullptr;
  auto scope_type = DeserializeUint8(serialized_binast, offset);
  offset = scope_type.new_offset;

  switch (scope_type.value) {
    case CLASS_SCOPE:
    case EVAL_SCOPE:
    case MODULE_SCOPE:
    case SCRIPT_SCOPE:
    case CATCH_SCOPE:
    case WITH_SCOPE: {
      UNREACHABLE();
    }
    case FUNCTION_SCOPE: {
      auto function_kind = DeserializeUint8(serialized_binast, offset);
      offset = function_kind.new_offset;

      scope = parser_->NewFunctionScope(static_cast<FunctionKind>(function_kind.value));
      break;
    }
    case BLOCK_SCOPE: {
      scope = parser_->NewVarblockScope();
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  DCHECK(scope_type.value == scope->scope_type());

  auto common_fields_result = DeserializeCommonScopeFields(serialized_binast, offset, scope);
  offset = common_fields_result.new_offset;

  // DeclarationScope data:
  auto encoded_decl_scope_bool_flags_result = DeserializeUint16Flags(serialized_binast, offset);
  offset = encoded_decl_scope_bool_flags_result.new_offset;

  scope->has_simple_parameters_ = encoded_decl_scope_bool_flags_result.value[0];
  scope->is_asm_module_ = encoded_decl_scope_bool_flags_result.value[1];
  scope->force_eager_compilation_ = encoded_decl_scope_bool_flags_result.value[2];
  scope->has_rest_ = encoded_decl_scope_bool_flags_result.value[3];
  scope->has_arguments_parameter_ = encoded_decl_scope_bool_flags_result.value[4];
  scope->scope_uses_super_property_ = encoded_decl_scope_bool_flags_result.value[5];
  scope->should_eager_compile_ = encoded_decl_scope_bool_flags_result.value[6];
  scope->was_lazily_parsed_ = encoded_decl_scope_bool_flags_result.value[7];
  scope->is_skipped_function_ = encoded_decl_scope_bool_flags_result.value[8];
  scope->has_inferred_function_name_ = encoded_decl_scope_bool_flags_result.value[9];
  scope->has_checked_syntax_ = encoded_decl_scope_bool_flags_result.value[10];
  scope->has_this_reference_ = encoded_decl_scope_bool_flags_result.value[11];
  scope->has_this_declaration_ = encoded_decl_scope_bool_flags_result.value[12];
  scope->needs_private_name_context_chain_recalc_ = encoded_decl_scope_bool_flags_result.value[13];

  auto params_result = DeserializeScopeParameters(serialized_binast, offset, scope);
  offset = params_result.new_offset;

  // TODO(binast): sloppy_block_functions_ (needed for non-strict mode support)

  auto receiver_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = receiver_result.new_offset;
  scope->receiver_ = receiver_result.value;

  auto function_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = function_result.new_offset;
  scope->function_ = function_result.value;

  auto new_target_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = new_target_result.new_offset;
  scope->new_target_ = new_target_result.value;

  auto arguments_result = DeserializeScopeVariableOrReference(serialized_binast, offset, scope);
  offset = arguments_result.new_offset;
  scope->arguments_ = arguments_result.value;

  // TODO(binast): rare_data_ (needed for > ES5.1 features)
  return {scope, offset};
}

BinAstDeserializer::DeserializeResult<FunctionLiteral*> BinAstDeserializer::DeserializeFunctionLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  // TODO(binast): Kind of silly that we serialize a cons string only to deserialized into a raw string
  auto name = DeserializeConsString(serialized_binast, offset);
  offset = name.new_offset;

  const AstRawString* raw_name = nullptr;
  if (name.value == nullptr) {
    raw_name = nullptr;
  } else {
    for (const AstRawString* s : name.value->ToRawStrings()) {
      DCHECK(raw_name == nullptr);
      DCHECK(s != nullptr);
      raw_name = s;
    }
    DCHECK(raw_name != nullptr);
  }

  auto scope = DeserializeDeclarationScope(serialized_binast, offset);
  offset = scope.new_offset;
  
  auto expected_property_count = DeserializeInt32(serialized_binast, offset);
  offset = expected_property_count.new_offset;

  auto parameter_count = DeserializeInt32(serialized_binast, offset);
  offset = parameter_count.new_offset;

  auto function_length = DeserializeInt32(serialized_binast, offset);
  offset = function_length.new_offset;

  auto function_token_position = DeserializeInt32(serialized_binast, offset);
  offset = function_token_position.new_offset;

  auto suspend_count = DeserializeInt32(serialized_binast, offset);
  offset = suspend_count.new_offset;

  auto function_literal_id = DeserializeInt32(serialized_binast, offset);
  offset = function_literal_id.new_offset;

  FunctionLiteral::ParameterFlag has_duplicate_parameters = FunctionLiteral::HasDuplicateParameters::decode(bit_field) ? FunctionLiteral::ParameterFlag::kHasDuplicateParameters : FunctionLiteral::ParameterFlag::kNoDuplicateParameters;
  FunctionSyntaxKind function_syntax_kind = FunctionLiteral::FunctionSyntaxKindBits::decode(bit_field);
  FunctionLiteral::EagerCompileHint eager_compile_hint = scope.value->ShouldEagerCompile() ? FunctionLiteral::kShouldEagerCompile : FunctionLiteral::kShouldLazyCompile;
  bool has_braces = FunctionLiteral::HasBracesField::decode(bit_field);

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Statement> body(&pointer_buffer);
  auto num_statements = DeserializeInt32(serialized_binast, offset);
  offset = num_statements.new_offset;

  {
    Parser::FunctionState function_state(&parser_->function_state_, &parser_->scope_, scope.value);

    for (int i = 0; i < num_statements.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;
      DCHECK(statement.value != nullptr);
      DCHECK(statement.value->AsStatement() != nullptr);
      body.Add(static_cast<Statement*>(statement.value));
    }
  }

  FunctionLiteral* result = parser_->factory()->NewFunctionLiteral(
    raw_name, scope.value, body, expected_property_count.value, parameter_count.value,
    function_length.value, has_duplicate_parameters, function_syntax_kind,
    eager_compile_hint, position, has_braces, function_literal_id.value);

  result->function_token_position_ = function_token_position.value;
  result->suspend_count_ = suspend_count.value;
  result->bit_field_ = bit_field;

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ReturnStatement*> BinAstDeserializer::DeserializeReturnStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto end_position = DeserializeInt32(serialized_binast, offset);
  offset = end_position.new_offset;

  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ReturnStatement* result = parser_->factory()->NewReturnStatement(static_cast<Expression*>(expression.value), position, end_position.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<BinaryOperation*> BinAstDeserializer::DeserializeBinaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(serialized_binast, offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(serialized_binast, offset);
  offset = right.new_offset;

  Token::Value op = BinaryOperation::OperatorField::decode(bit_field);

  BinaryOperation* result = parser_->factory()->NewBinaryOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Property*> BinAstDeserializer::DeserializeProperty(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto obj = DeserializeAstNode(serialized_binast, offset);
  offset = obj.new_offset;

  auto key = DeserializeAstNode(serialized_binast, offset);
  offset = key.new_offset;

  Property* result = parser_->factory()->NewProperty(static_cast<Expression*>(obj.value), static_cast<Expression*>(key.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ExpressionStatement*> BinAstDeserializer::DeserializeExpressionStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  ExpressionStatement* result = parser_->factory()->NewExpressionStatement(static_cast<Expression*>(expression.value), offset);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<VariableProxy*> BinAstDeserializer::DeserializeVariableProxy(uint8_t* serialized_binast, int offset) {
  auto position = DeserializeInt32(serialized_binast, offset);
  offset = position.new_offset;

  auto bit_field = DeserializeUint32(serialized_binast, offset);
  offset = bit_field.new_offset;

  bool is_resolved = VariableProxy::IsResolvedField::decode(bit_field.value);

  VariableProxy* result;
  if (is_resolved) {
    // The resolved Variable should either be a reference (i.e. currently visible in scope) or should be a 
    // NonScope Variable definition (i.e. it's a Variable that is outside the current Scope boundaries, 
    // e.g. inside an eval).
    auto variable = DeserializeNonScopeVariableOrReference(serialized_binast, offset);
    offset = variable.new_offset;
    result = parser_->factory()->NewVariableProxy(variable.value, position.value);
  } else {
    auto raw_name = DeserializeRawStringReference(serialized_binast, offset);
    offset = raw_name.new_offset;
    // We use NORMAL_VARIABLE as a placeholder here.
    result = parser_->factory()->NewVariableProxy(raw_name.value, VariableKind::NORMAL_VARIABLE, position.value);

    parser_->scope()->AddUnresolved(result);
  }
  result->bit_field_ = bit_field.value;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<VariableProxyExpression*> BinAstDeserializer::DeserializeVariableProxyExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto variable_proxy = DeserializeVariableProxy(serialized_binast, offset);
  offset = variable_proxy.new_offset;

  VariableProxyExpression* result = parser_->factory()->NewVariableProxyExpression(variable_proxy.value);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Call*> BinAstDeserializer::DeserializeCall(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(serialized_binast, offset);
  offset = params_count.new_offset;

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Expression> params(&pointer_buffer);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(serialized_binast, offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  Call* result = parser_->factory()->NewCall(static_cast<Expression*>(expression.value), params, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<CallNew*> BinAstDeserializer::DeserializeCallNew(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  auto params_count = DeserializeInt32(serialized_binast, offset);
  offset = params_count.new_offset;

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Expression> params(&pointer_buffer);
  for (int i = 0; i < params_count.value; ++i) {
    auto param = DeserializeAstNode(serialized_binast, offset);
    offset = param.new_offset;
    params.Add(static_cast<Expression*>(param.value));
  }

  CallNew* result = parser_->factory()->NewCallNew(static_cast<Expression*>(expression.value), params, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<IfStatement*> BinAstDeserializer::DeserializeIfStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto condition = DeserializeAstNode(serialized_binast, offset);
  offset = condition.new_offset;

  auto then_statement = DeserializeAstNode(serialized_binast, offset);
  offset = then_statement.new_offset;

  auto else_statement = DeserializeAstNode(serialized_binast, offset);
  offset = else_statement.new_offset;

  IfStatement* result = parser_->factory()->NewIfStatement(static_cast<Expression*>(condition.value), static_cast<Statement*>(then_statement.value), static_cast<Statement*>(else_statement.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Block*> BinAstDeserializer::DeserializeBlock(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto has_scope = DeserializeUint8(serialized_binast, offset);
  offset = has_scope.new_offset;

  Scope* scope = nullptr;
  if (has_scope.value) {
    auto scope_result = DeserializeScope(serialized_binast, offset);
    offset = scope_result.new_offset;
    scope = scope_result.value;
  }

  auto statement_count = DeserializeInt32(serialized_binast, offset);
  offset = statement_count.new_offset;

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Statement> statements(&pointer_buffer);
  if (scope != nullptr) {
    Parser::BlockState block_state(&parser_->scope_, scope);
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;
      statements.Add(static_cast<Statement*>(statement.value));
    }
  } else {
    for (int i = 0; i < statement_count.value; ++i) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;
      statements.Add(static_cast<Statement*>(statement.value));
    }
  }

  bool ignore_completion_value = false; // Just a filler value.
  Block* result = parser_->factory()->NewBlock(ignore_completion_value, statements);
  result->bit_field_ = bit_field;
  result->set_scope(scope);

  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Assignment*> BinAstDeserializer::DeserializeAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(serialized_binast, offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(serialized_binast, offset);
  offset = value.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<CompareOperation*> BinAstDeserializer::DeserializeCompareOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto left = DeserializeAstNode(serialized_binast, offset);
  offset = left.new_offset;

  auto right = DeserializeAstNode(serialized_binast, offset);
  offset = right.new_offset;

  Token::Value op = CompareOperation::OperatorField::decode(bit_field);
  CompareOperation* result = parser_->factory()->NewCompareOperation(op, static_cast<Expression*>(left.value), static_cast<Expression*>(right.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<EmptyStatement*> BinAstDeserializer::DeserializeEmptyStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  EmptyStatement* result = parser_->factory()->EmptyStatement();
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<AstNode*> BinAstDeserializer::DeserializeMaybeAstNode(uint8_t* serialized_binast, int offset) {
  auto has_node = DeserializeUint8(serialized_binast, offset);
  offset = has_node.new_offset;

  if (has_node.value) {
    auto node = DeserializeAstNode(serialized_binast, offset);
    offset = node.new_offset;
    return {node.value, offset};
  }
  return {nullptr, offset};
}

BinAstDeserializer::DeserializeResult<ForStatement*> BinAstDeserializer::DeserializeForStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto init_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = init_node.new_offset;

  auto cond_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = cond_node.new_offset;

  auto next_node = DeserializeMaybeAstNode(serialized_binast, offset);
  offset = next_node.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  ForStatement* result = parser_->factory()->NewForStatement(position);
  result->Initialize(
    static_cast<Statement*>(init_node.value),
    static_cast<Expression*>(cond_node.value),
    static_cast<Statement*>(next_node.value),
    static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ForInStatement*> BinAstDeserializer::DeserializeForInStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto each = DeserializeAstNode(serialized_binast, offset);
  offset = each.new_offset;

  auto subject = DeserializeAstNode(serialized_binast, offset);
  offset = subject.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  ForEachStatement* result = parser_->factory()->NewForEachStatement(ForEachStatement::ENUMERATE, position);
  result->Initialize(static_cast<Expression*>(each.value), static_cast<Expression*>(subject.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  DCHECK(result->IsForInStatement());
  return {static_cast<ForInStatement*>(result), offset};
}

BinAstDeserializer::DeserializeResult<WhileStatement*> BinAstDeserializer::DeserializeWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(serialized_binast, offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  WhileStatement* result = parser_->factory()->NewWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<DoWhileStatement*> BinAstDeserializer::DeserializeDoWhileStatement(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto cond = DeserializeAstNode(serialized_binast, offset);
  offset = cond.new_offset;

  auto body = DeserializeAstNode(serialized_binast, offset);
  offset = body.new_offset;

  DoWhileStatement* result = parser_->factory()->NewDoWhileStatement(position);
  result->Initialize(static_cast<Expression*>(cond.value), static_cast<Statement*>(body.value));
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<CountOperation*> BinAstDeserializer::DeserializeCountOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  Token::Value op = CountOperation::TokenField::decode(bit_field);
  bool is_prefix = CountOperation::IsPrefixField::decode(bit_field);

  CountOperation* result = parser_->factory()->NewCountOperation(op, is_prefix, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<CompoundAssignment*> BinAstDeserializer::DeserializeCompoundAssignment(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto target = DeserializeAstNode(serialized_binast, offset);
  offset = target.new_offset;

  auto value = DeserializeAstNode(serialized_binast, offset);
  offset = value.new_offset;

  auto binary_operation = DeserializeAstNode(serialized_binast, offset);
  offset = binary_operation.new_offset;

  Token::Value op = Assignment::TokenField::decode(bit_field);
  Assignment* result = parser_->factory()->NewAssignment(op, static_cast<Expression*>(target.value), static_cast<Expression*>(value.value), position);
  result->bit_field_ = bit_field;
  return {result->AsCompoundAssignment(), offset};
}

BinAstDeserializer::DeserializeResult<UnaryOperation*> BinAstDeserializer::DeserializeUnaryOperation(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto expression = DeserializeAstNode(serialized_binast, offset);
  offset = expression.new_offset;

  Token::Value op = UnaryOperation::OperatorField::decode(bit_field);
  UnaryOperation* result = parser_->factory()->NewUnaryOperation(op, static_cast<Expression*>(expression.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ThisExpression*> BinAstDeserializer::DeserializeThisExpression(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  ThisExpression* result = parser_->factory()->ThisExpression();
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ObjectLiteral*> BinAstDeserializer::DeserializeObjectLiteral(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  auto properties_length = DeserializeInt32(serialized_binast, offset);
  offset = properties_length.new_offset;

  std::vector<void*> pointer_buffer;
  ScopedPtrList<ObjectLiteral::Property> properties(&pointer_buffer);

  int number_of_boilerplate_properties = 0;  // add increments for this

  for (int i = 0; i < properties_length.value; i++) {
    auto key = DeserializeAstNode(serialized_binast, offset);
    offset = key.new_offset;

    auto value = DeserializeAstNode(serialized_binast, offset);
    offset = value.new_offset;

    auto kind = DeserializeUint8(serialized_binast, offset);
    offset = kind.new_offset;

    auto is_computed_name = DeserializeUint8(serialized_binast, offset);
    offset = is_computed_name.new_offset;

    auto property = parser_->factory()->NewObjectLiteralProperty(
        static_cast<Expression*>(key.value),
        static_cast<Expression*>(value.value),
        static_cast<ObjectLiteral::Property::Kind>(kind.value),
        is_computed_name.value);

    if (parser_->IsBoilerplateProperty(property) &&
        !is_computed_name.value) {
      number_of_boilerplate_properties++;
    }

    properties.Add(property);
  }

  bool has_rest_property = false;
  ObjectLiteral* result = parser_->factory()->NewObjectLiteral(
      properties, number_of_boilerplate_properties, position,
      has_rest_property);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<ArrayLiteral*>
BinAstDeserializer::DeserializeArrayLiteral(uint8_t* serialized_binast,
                                            uint32_t bit_field,
                                            int32_t position, int offset) {
  auto array_length = DeserializeInt32(serialized_binast, offset);
  offset = array_length.new_offset;

  auto first_spread_index = DeserializeInt32(serialized_binast, offset);
  offset = first_spread_index.new_offset;

  DCHECK(first_spread_index.value == -1);

  std::vector<void*> pointer_buffer;
  ScopedPtrList<Expression> values(&pointer_buffer);

  for (int i = 0; i < array_length.value; i++) {
    auto value = DeserializeAstNode(serialized_binast, offset);
    offset = value.new_offset;

    values.Add(static_cast<Expression*>(value.value));
  }

  ArrayLiteral* result = parser_->factory()->NewArrayLiteral(
      values, first_spread_index.value, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<NaryOperation*>
BinAstDeserializer::DeserializeNaryOperation(uint8_t* serialized_binast,
                                             uint32_t bit_field,
                                             int32_t position, int offset) {
  Token::Value op = NaryOperation::OperatorField::decode(bit_field);

  auto first = DeserializeAstNode(serialized_binast, offset);
  offset = first.new_offset;

  auto subsequent_length = DeserializeUint32(serialized_binast, offset);
  offset = subsequent_length.new_offset;

  NaryOperation* result = parser_->factory()->NewNaryOperation(
      op, static_cast<Expression*>(first.value), subsequent_length.value);

  for (uint32_t i = 0; i < subsequent_length.value; i++) {
    auto expr = DeserializeAstNode(serialized_binast, offset);
    offset = expr.new_offset;

    auto pos = DeserializeInt32(serialized_binast, offset);
    offset = pos.new_offset;

    result->AddSubsequent(static_cast<Expression*>(expr.value), pos.value);
  }

  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Conditional*>
BinAstDeserializer::DeserializeConditional(uint8_t* serialized_binast,
                                           uint32_t bit_field, int32_t position,
                                           int offset) {
  auto condition = DeserializeAstNode(serialized_binast, offset);
  offset = condition.new_offset;

  auto then_expression = DeserializeAstNode(serialized_binast, offset);
  offset = then_expression.new_offset;

  auto else_expression = DeserializeAstNode(serialized_binast, offset);
  offset = else_expression.new_offset;

  Conditional* result = parser_->factory()->NewConditional(
      static_cast<Expression*>(condition.value),
      static_cast<Expression*>(then_expression.value),
      static_cast<Expression*>(else_expression.value),
      position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<TryCatchStatement*>
BinAstDeserializer::DeserializeTryCatchStatement(uint8_t* serialized_binast,
                                                 uint32_t bit_field,
                                                 int32_t position, int offset) {
  auto try_block = DeserializeAstNode(serialized_binast, offset);
  offset = try_block.new_offset;

  auto scope = DeserializeScope(serialized_binast, offset);
  offset = scope.new_offset;

  auto catch_block = DeserializeAstNode(serialized_binast, offset);
  offset = catch_block.new_offset;

  TryCatchStatement* result = parser_->factory()->NewTryCatchStatement(
      static_cast<Block*>(try_block.value), scope.value,
      static_cast<Block*>(catch_block.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<RegExpLiteral*>
BinAstDeserializer::DeserializeRegExpLiteral(uint8_t* serialized_binast,
                                                 uint32_t bit_field,
                                                 int32_t position, int offset) {
  auto raw_pattern = DeserializeRawStringReference(serialized_binast, offset);
  offset = raw_pattern.new_offset;

  auto flags = DeserializeInt32(serialized_binast, offset);
  offset = flags.new_offset;

  RegExpLiteral* result = parser_->factory()->NewRegExpLiteral(
      raw_pattern.value, flags.value, position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<SwitchStatement*>
BinAstDeserializer::DeserializeSwitchStatement(uint8_t* serialized_binast,
                                               uint32_t bit_field,
                                               int32_t position, int offset) {
  auto tag = DeserializeAstNode(serialized_binast, offset);
  offset = tag.new_offset;

  SwitchStatement* result = parser_->factory()->NewSwitchStatement(
      static_cast<Expression*>(tag.value), position);

  auto num_cases = DeserializeInt32(serialized_binast, offset);
  offset = num_cases.new_offset;

  for (int i = 0; i < num_cases.value; i++) {
    auto is_default = DeserializeUint8(serialized_binast, offset);
    offset = is_default.new_offset;

    Expression* label;

    if (is_default.value == 0) {
      auto deserialized_label = DeserializeAstNode(serialized_binast, offset);
      offset = deserialized_label.new_offset;

      label = static_cast<Expression*>(deserialized_label.value);
    } else {
      label = nullptr;
    }

    auto statements_length = DeserializeInt32(serialized_binast, offset);
    offset = statements_length.new_offset;

    std::vector<void*> pointer_buffer;
    ScopedPtrList<Statement> statements(&pointer_buffer);

    for (int i = 0; i < statements_length.value; i++) {
      auto statement = DeserializeAstNode(serialized_binast, offset);
      offset = statement.new_offset;

      statements.Add(static_cast<Statement*>(statement.value));
    }

    result->cases()->Add(parser_->factory()->NewCaseClause(label, statements),
                         zone());
  }

  result->bit_field_ = bit_field;
  return {result, offset};
}

BinAstDeserializer::DeserializeResult<Throw*>
BinAstDeserializer::DeserializeThrow(uint8_t* serialized_binast,
                                     uint32_t bit_field, int32_t position,
                                     int offset) {
  auto exception = DeserializeAstNode(serialized_binast, offset);
  offset = exception.new_offset;

  Throw* result = parser_->factory()->NewThrow(
      static_cast<Expression*>(exception.value), position);
  result->bit_field_ = bit_field;
  return {result, offset};
}

// This is just a placeholder while we implement the various nodes that we'll support.
BinAstDeserializer::DeserializeResult<std::nullptr_t> BinAstDeserializer::DeserializeNodeStub(uint8_t* serialized_binast, uint32_t bit_field, int32_t position, int offset) {
  UNREACHABLE();
  return {nullptr, offset};
}

}  // namespace internal
}  // namespace v8

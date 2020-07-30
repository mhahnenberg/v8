#include <stdio.h>
#include "src/libplatform/default-platform.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/binast-parser.h"
#include "src/parsing/binast-visitor.h"
#include "include/v8.h"
#include "src/d8/d8.h"
#include "src/ast/prettyprinter.h"

static const char* test_scripts[] = {
  // TODO(binast): (sloppy mode) "42",
  "'use strict';\n42",
  "'use strict';\n'foo'",
  "'use strict';\nif (true) { 'foo'; }",
  "'use strict';\nif (true) { 'foo'; } else { 'bar'; }",
  "'use strict';\nvar sum = 0;\nfor (var i = 0; i < 10; ++i) {\n  sum += i;\n}\nconsole.log(sum);",
  "'use strict';\nvar square = function(x) { return x * x; };\nconsole.log('square(3) =', square(3));",
  "'use strict';\nfunction square(x) { return x * x; }\nconsole.log('square(5) =', square(5));",
  "'use strict';\nvar obj = {};",
  "'use strict';\nvar obj = {foo: 'bar', bar: 1, baz: function(x) { return x; }};",
  "'use strict';\nvar obj = {}; obj.foo = 'bar';",
  "'use strict';\nvar arr = [];",
};

static const size_t num_test_scripts = sizeof(test_scripts) / sizeof(char*);

static void ParseScript(v8::Isolate* isolate, const char* script) {
  // Create allocator/zone
  v8::internal::AccountingAllocator allocator;
  std::unique_ptr<v8::internal::Zone> parseInfoZone = std::make_unique<v8::internal::Zone>(&allocator, ZONE_NAME);

  v8::Isolate::Scope isolate_scope(isolate);
  v8::internal::Isolate* i_isolate = reinterpret_cast<v8::internal::Isolate*>(isolate);
  v8::internal::UnoptimizedCompileFlags flags = v8::internal::UnoptimizedCompileFlags::ForTest(i_isolate);
  v8::internal::UnoptimizedCompileState state(i_isolate);

  // Are these right?
  flags.set_is_toplevel(true);
  flags.set_is_eager(true);

  // Create ParseInfo
  printf("Creating ParseInfo...");
  v8::internal::ParseInfo parseInfo(i_isolate, flags, &state);
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
  v8::internal::AstPrinter ast_printer(i_isolate->stack_guard()->real_climit());
  printf("%s\n", ast_printer.PrintProgram(parseInfo.literal()));
}

int main(int argc, char* argv[]) {
  printf("Hello, v8!\n");
  // Initialize V8.
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
  // Create compile flags to pass to parser
  // Create a new Isolate and make it the current one.
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  printf("v8 initialized\n");

  for (size_t i = 0; i < num_test_scripts; ++i) {
    printf("\nScript #%zu\n", i);
    printf("%s\n\n", test_scripts[i]);
    ParseScript(isolate, test_scripts[i]);
  }


  isolate->Dispose();
  delete create_params.array_buffer_allocator;

  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();

  return 0;
}
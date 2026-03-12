#include "lexer/lexer.h"
#include "parser/parser.h"
#include "runtime/runtime.h"
#include "codegen/codegen.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static const char* VERSION = "1.0.0";

static void printUsage() {
  std::cout << "Vibe Programming Language v" << VERSION << "\n\n";
  std::cout << "Usage:\n";
  std::cout << "  vibe <file.vibe>          Run a .vibe file\n";
  std::cout << "  vibe run <file.vibe>      Run a .vibe file\n";
  std::cout << "  vibe build <file.vibe>    Transpile to C++ and compile\n";
  std::cout << "  vibe check <file.vibe>    Check for syntax errors\n";
  std::cout << "  vibe repl                 Start interactive REPL\n";
  std::cout << "  vibe version              Show version\n";
  std::cout << "  vibe help                 Show this help\n";
}

static std::string readFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::cerr << "Error: Cannot open file: " << path << std::endl;
    std::exit(1);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void runFile(const std::string& path) {
  std::string source = readFile(path);
  try {
    nova::Lexer lexer(path, source);
    nova::Parser parser(std::move(lexer));
    nova::Program program = parser.parseProgram();
    nova::Interpreter interpreter(path, source);
    interpreter.exec(program);
  } catch (const nova::RuntimeError& e) {
    std::cerr << e.what() << std::endl;
    std::exit(1);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    std::exit(1);
  }
}

static void buildFile(const std::string& path) {
  std::string source = readFile(path);
  try {
    nova::Lexer lexer(path, source);
    nova::Parser parser(std::move(lexer));
    nova::Program program = parser.parseProgram();
    nova::Codegen codegen;
    std::string cpp = codegen.generate(program);

    // Determine output names
    fs::path srcPath(path);
    std::string baseName = srcPath.stem().string();
    std::string cppFile = baseName + ".cpp";
    std::string outFile = baseName;
#ifdef _WIN32
    outFile += ".exe";
#endif

    // Write C++ file
    {
      std::ofstream out(cppFile);
      out << cpp;
    }
    std::cout << "Generated: " << cppFile << std::endl;

    // Compile
    std::string compiler = "g++";
    // Try clang++ first
    if (std::system("which clang++ > /dev/null 2>&1") == 0)
      compiler = "clang++";

    std::string cmd = compiler + " -std=c++17 -O2 -o " + outFile + " " + cppFile;
    std::cout << "Compiling: " << cmd << std::endl;
    int result = std::system(cmd.c_str());
    if (result == 0) {
      std::cout << "Built: " << outFile << std::endl;
    } else {
      std::cerr << "Compilation failed (exit code " << result << ")\n";
      std::cerr << "The generated C++ file is preserved at: " << cppFile << "\n";
      std::exit(1);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    std::exit(1);
  }
}

static void checkFile(const std::string& path) {
  std::string source = readFile(path);
  try {
    nova::Lexer lexer(path, source);
    nova::Parser parser(std::move(lexer));
    nova::Program program = parser.parseProgram();
    std::cout << "✓ " << path << " - No errors found (" << program.stmts.size() << " statements)\n";
  } catch (const std::exception& e) {
    std::cerr << "✗ " << e.what() << std::endl;
    std::exit(1);
  }
}

static void printReplWelcome() {
  std::cout << "\033[1;35m";
  std::cout << R"(
 ██╗   ██╗██╗██████╗ ███████╗
 ██║   ██║██║██╔══██╗██╔════╝
 ██║   ██║██║██████╔╝█████╗  
 ╚██╗ ██╔╝██║██╔══██╗██╔══╝  
  ╚████╔╝ ██║██████╔╝███████╗
   ╚═══╝  ╚═╝╚═════╝ ╚══════╝
)";
  std::cout << "\033[0m";
  std::cout << "\033[1;36m  Vibe Programming Language v" << VERSION << "\033[0m\n";
  std::cout << "\033[90m  A modern, expressive language that feels right.\033[0m\n\n";
  std::cout << "\033[33m  Commands:\033[0m\n";
  std::cout << "    \033[32m.help\033[0m      Show help and available commands\n";
  std::cout << "    \033[32m.clear\033[0m     Clear the screen\n";
  std::cout << "    \033[32m.reset\033[0m     Reset the REPL environment\n";
  std::cout << "    \033[32m.exit\033[0m      Exit the REPL (or type 'exit' / Ctrl+D)\n";
  std::cout << "\n";
}

static void printReplHelp() {
  std::cout << "\033[1;36m  Vibe REPL Help\033[0m\n\n";
  std::cout << "\033[33m  Commands:\033[0m\n";
  std::cout << "    \033[32m.help\033[0m       Show this help message\n";
  std::cout << "    \033[32m.clear\033[0m      Clear the screen\n";
  std::cout << "    \033[32m.reset\033[0m      Reset the REPL environment (clears all variables)\n";
  std::cout << "    \033[32m.exit\033[0m       Exit the REPL\n";
  std::cout << "    \033[32m.vars\033[0m       Show defined variables (coming soon)\n\n";
  std::cout << "\033[33m  Syntax Quick Reference:\033[0m\n";
  std::cout << "    \033[36mvar\033[0m x = 10           Variable declaration\n";
  std::cout << "    \033[36mlet\033[0m y = 20           Variable declaration (alias for var)\n";
  std::cout << "    \033[36mconst\033[0m PI = 3.14      Constant declaration\n";
  std::cout << "    \033[36mfn\033[0m add(a, b) { }     Function definition\n";
  std::cout << "    \033[36mprint\033[0m(\"hello\")        Print output\n";
  std::cout << "    \033[36mif\033[0m / \033[36melse\033[0m / \033[36mfor\033[0m      Control flow\n";
  std::cout << "    \033[36mclass\033[0m Dog { }         Class definition\n";
  std::cout << "    \033[36mimport\033[0m math           Import modules\n\n";
  std::cout << "\033[33m  Tips:\033[0m\n";
  std::cout << "    • Semicolons are auto-added in the REPL\n";
  std::cout << "    • Expression results are printed automatically\n";
  std::cout << "    • Multi-line input: open a { and press Enter\n\n";
}

static void repl() {
  printReplWelcome();

  std::string buffer;
  nova::Interpreter interpreter("<repl>", "");
  // Keep parsed programs alive so function bodies (raw Stmt pointers) remain valid
  std::vector<nova::Program> programs;
  int lineNum = 1;

  while (true) {
    if (buffer.empty())
      std::cout << "\033[1;32mvibe\033[0m \033[90m" << lineNum << "\033[0m\033[1;33m ❯ \033[0m";
    else
      std::cout << "\033[1;33m ... \033[0m";
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cout << "\n\033[90mGoodbye! 👋\033[0m\n";
      break;
    }

    // Trim whitespace for command detection
    std::string trimmedLine = line;
    {
      auto start = trimmedLine.find_first_not_of(" \t");
      if (start != std::string::npos)
        trimmedLine = trimmedLine.substr(start);
      else
        trimmedLine = "";
    }

    // Handle REPL commands
    if (buffer.empty()) {
      if (trimmedLine == "exit" || trimmedLine == ".exit" || trimmedLine == "quit") {
        std::cout << "\033[90mGoodbye! 👋\033[0m\n";
        break;
      }
      if (trimmedLine == ".clear" || trimmedLine == "clear") {
        std::cout << "\033[2J\033[H"; // ANSI clear screen
        std::cout.flush();
        continue;
      }
      if (trimmedLine == ".help" || trimmedLine == "help") {
        printReplHelp();
        continue;
      }
      if (trimmedLine == ".reset") {
        interpreter = nova::Interpreter("<repl>", "");
        programs.clear();
        lineNum = 1;
        std::cout << "\033[90m  Environment reset.\033[0m\n";
        continue;
      }
      if (trimmedLine.empty()) continue;
    }

    buffer += line + "\n";

    // Check if we have a complete statement (braces balanced)
    int braces = 0;
    bool inString = false;
    char strChar = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
      char c = buffer[i];
      if (inString) {
        if (c == '\\' && i + 1 < buffer.size()) { i++; continue; }
        if (c == strChar) inString = false;
        continue;
      }
      if (c == '"' || c == '\'') { inString = true; strChar = c; continue; }
      if (c == '{') braces++;
      else if (c == '}') braces--;
    }
    if (braces > 0) continue;

    // Auto-add semicolons to lines that need them (REPL convenience)
    {
      std::string adjusted;
      std::istringstream stream(buffer);
      std::string ln;
      while (std::getline(stream, ln)) {
        // Trim trailing whitespace
        auto end = ln.find_last_not_of(" \t\r");
        std::string trimmed = (end != std::string::npos) ? ln.substr(0, end + 1) : ln;
        // Add semicolon if line doesn't end with ; { } or is empty
        if (!trimmed.empty() && trimmed.back() != ';' && trimmed.back() != '{' && trimmed.back() != '}') {
          // Don't add to control flow keywords
          bool isBlock = false;
          // Strip leading whitespace for keyword detection
          std::string stripped = trimmed;
          auto spos = stripped.find_first_not_of(" \t");
          if (spos != std::string::npos) stripped = stripped.substr(spos);
          for (auto kw : {"fn ", "if ", "if(", "else", "while ", "while(", "for ", "for(",
                          "match ", "class ", "interface ", "enum ", "try", "catch",
                          "import ", "export "})
            if (stripped.find(kw) == 0) { isBlock = true; break; }
          if (!isBlock) trimmed += ";";
        }
        adjusted += trimmed + "\n";
      }
      buffer = adjusted;
    }

    // Try to parse and execute
    try {
      nova::Lexer lexer("<repl>", buffer);
      nova::Parser parser(std::move(lexer));
      programs.push_back(parser.parseProgram());
      auto& program = programs.back();

      for (const auto& st : program.stmts) {
        // If it's a bare expression, print the result
        if (auto* es = std::get_if<nova::ExprStmt>(&st->node)) {
          nova::Value result = interpreter.eval(*es->expr);
          if (!std::holds_alternative<std::monostate>(result)) {
            // Color output based on type
            if (std::holds_alternative<std::string>(result))
              std::cout << "\033[32m\"" << nova::toString(result) << "\"\033[0m" << std::endl;
            else if (std::holds_alternative<int64_t>(result) || std::holds_alternative<double>(result))
              std::cout << "\033[33m" << nova::toString(result) << "\033[0m" << std::endl;
            else if (std::holds_alternative<bool>(result))
              std::cout << "\033[35m" << nova::toString(result) << "\033[0m" << std::endl;
            else
              std::cout << "\033[36m" << nova::toString(result) << "\033[0m" << std::endl;
          }
        } else {
          interpreter.execStmt(*st);
        }
      }
    } catch (const nova::ReturnSignal&) {
      // Ignore return in REPL
    } catch (const nova::ThrowSignal& ts) {
      std::cerr << "\033[1;31m  Error: \033[0m\033[31m" << nova::toString(ts.value) << "\033[0m" << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "\033[1;31m  Error: \033[0m\033[31m" << e.what() << "\033[0m" << std::endl;
    }

    buffer.clear();
    lineNum++;
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    // No arguments → start REPL
    repl();
    return 0;
  }

  std::string cmd = argv[1];

  // Direct file execution: vibe file.vibe
  if (cmd.size() > 5 && cmd.substr(cmd.size() - 5) == ".vibe") {
    runFile(cmd);
    return 0;
  }

  if (cmd == "run") {
    if (argc < 3) {
      std::cerr << "Usage: vibe run <file.vibe>\n";
      return 1;
    }
    runFile(argv[2]);
  } else if (cmd == "build") {
    if (argc < 3) {
      std::cerr << "Usage: vibe build <file.vibe>\n";
      return 1;
    }
    buildFile(argv[2]);
  } else if (cmd == "check") {
    if (argc < 3) {
      std::cerr << "Usage: vibe check <file.vibe>\n";
      return 1;
    }
    checkFile(argv[2]);
  } else if (cmd == "repl") {
    repl();
  } else if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    std::cout << "Vibe " << VERSION << std::endl;
  } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    printUsage();
  } else {
    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
  }

  return 0;
}

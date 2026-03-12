#include <iostream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "runtime/runtime.h"

int main(){
  try {
    std::string filename = "examples/fib.nv";
    std::string source = R"NOVA_SRC(fn fib(n: int) -> int {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn main() {
    var n = 10;
    var i = 0;
    while (i <= n) {
        print("fib(" + i + ") = " + fib(i));
        i += 1;
    }
}

)NOVA_SRC";
    nova::Lexer lx(filename, source);
    nova::Parser ps(std::move(lx));
    nova::Program prog = ps.parseProgram();
    nova::Interpreter it(filename, source);
    it.exec(prog);
  } catch(const nova::ParseError& e){
    std::cerr << e.tok.pos.filename << ':' << e.tok.pos.line << ':' << e.tok.pos.column
              << " Parse error: " << e.what() << std::endl;
    return 2;
  } catch(const nova::RuntimeError& e){
    std::cerr << e.pos.filename << ':' << e.pos.line << ':' << e.pos.column
              << " Runtime error: " << e.what() << std::endl;
    return 3;
  } catch(const std::exception& e){
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

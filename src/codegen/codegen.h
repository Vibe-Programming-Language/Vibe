#pragma once
#include "parser/ast.h"
#include <sstream>
#include <string>
#include <unordered_set>

namespace nova {

// ─── C++ Transpiler ────────────────────────────────────────────────
// Converts a Vibe AST into compilable C++ source code.
// Usage:
//   Codegen gen;
//   std::string cpp = gen.generate(program);
//   // Write cpp to file, then compile with g++/clang++

class Codegen {
public:
  std::string generate(const Program& program);

private:
  std::ostringstream out_;
  int indent_ = 0;
  std::unordered_set<std::string> declaredFunctions_;
  bool inClass_ = false;
  std::string currentClass_;

  void emit(const std::string& s);
  void emitLine(const std::string& s);
  void emitIndent();

  void genStmt(const Stmt& s);
  void genBlock(const BlockStmt& b);
  void genVarDecl(const VarDeclStmt& v);
  void genExprStmt(const ExprStmt& e);
  void genIf(const IfStmt& i);
  void genWhile(const WhileStmt& w);
  void genDoWhile(const DoWhileStmt& d);
  void genFor(const ForStmt& f);
  void genForIn(const ForInStmt& f);
  void genMatch(const MatchStmt& m);
  void genReturn(const ReturnStmt& r);
  void genThrow(const ThrowStmt& t);
  void genTryCatch(const TryCatchStmt& tc);
  void genFnDecl(const FnDeclStmt& f);
  void genClassDecl(const ClassDeclStmt& c);
  void genEnumDecl(const EnumDeclStmt& e);
  void genImport(const ImportStmt& imp);
  void genExport(const ExportStmt& exp);

  std::string genExpr(const Expr& e);
  std::string genBinaryOp(const Token& op);
};

} // namespace nova

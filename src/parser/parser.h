#pragma once

#include "lexer/lexer.h"
#include "parser/ast.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace nova {

struct ParseError : std::runtime_error {
  Token tok;
  explicit ParseError(Token t, const std::string& msg)
      : std::runtime_error(msg), tok(std::move(t)) {}
};

class Parser {
public:
  explicit Parser(Lexer lexer);

  Program parseProgram();

private:
  Token peek();
  Token next();
  bool check(TokenType t);
  bool match(TokenType t);
  Token expect(TokenType t, const std::string& message);

  StmtPtr parseDeclOrStmt();
  StmtPtr parseStatement();
  StmtPtr parseBlock();
  StmtPtr parseVarOrConst(bool isConst);
  StmtPtr parseFnDecl(bool isAsync = false, bool isStatic = false,
                       bool isOverride = false);
  StmtPtr parseIf();
  StmtPtr parseWhile();
  StmtPtr parseDoWhile();
  StmtPtr parseFor();
  StmtPtr parseMatch();
  StmtPtr parseReturn();
  StmtPtr parseBreak();
  StmtPtr parseContinue();
  StmtPtr parseThrow();
  StmtPtr parseTryCatch();
  StmtPtr parseClass();
  StmtPtr parseInterface();
  StmtPtr parseEnum();
  StmtPtr parseImport();
  StmtPtr parseExport();
  StmtPtr parseSpawn();
  StmtPtr parseUnsafe();
  StmtPtr parseExprStmt();

  std::optional<TypeAnn> parseOptionalTypeAnn();
  TypeAnn parseTypeAnn();

  ExprPtr parseExpression();
  ExprPtr parseAssignment();
  ExprPtr parseTernary();
  ExprPtr parseLogicalOr();
  ExprPtr parseLogicalAnd();
  ExprPtr parseBitwiseOr();
  ExprPtr parseBitwiseXor();
  ExprPtr parseBitwiseAnd();
  ExprPtr parseEquality();
  ExprPtr parseComparison();
  ExprPtr parseShift();
  ExprPtr parseTerm();
  ExprPtr parseFactor();
  ExprPtr parsePower();
  ExprPtr parseUnary();
  ExprPtr parsePostfix();
  ExprPtr parsePrimary();

  ExprPtr makeExpr(SourcePos pos, Expr e);
  StmtPtr makeStmt(SourcePos pos, Stmt s);

  Lexer lexer_;
  Token current_;
  bool hasCurrent_ = false;
};

} // namespace nova

#pragma once

#include "lexer/tokens.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace nova {

struct Expr;
struct Stmt;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ── Type annotation ─────────────────────────────
struct TypeAnn {
  std::string text;  // e.g. "int", "str", "List<int>", "int?", "int[]"
};

// ── Expression nodes ────────────────────────────

struct LiteralExpr {
  Token tok;
};

struct IdentExpr {
  Token name;
};

struct UnaryExpr {
  Token op;
  ExprPtr rhs;
};

struct BinaryExpr {
  ExprPtr lhs;
  Token op;
  ExprPtr rhs;
};

struct AssignExpr {
  ExprPtr target;   // IdentExpr, MemberExpr, or IndexExpr
  Token op;         // =, +=, -=, *=, /=, %=
  ExprPtr value;
};

struct CallExpr {
  ExprPtr callee;
  Token lparen;
  std::vector<ExprPtr> args;
};

struct IndexExpr {
  ExprPtr object;
  Token lbracket;
  ExprPtr index;
};

struct MemberExpr {
  ExprPtr object;
  Token dot;
  Token member;
};

struct GroupExpr {
  ExprPtr inner;
};

struct ArrayExpr {
  Token lbracket;
  std::vector<ExprPtr> elements;
};

struct MapEntry {
  ExprPtr key;
  ExprPtr value;
};

struct MapExpr {
  Token lbrace;
  std::vector<MapEntry> entries;
};

struct LambdaExpr {
  std::vector<Token> params;
  std::vector<std::optional<TypeAnn>> paramTypes;
  std::optional<TypeAnn> retType;
  // body is either a single expression (arrow) or a block
  ExprPtr bodyExpr;   // for => expr
  StmtPtr bodyBlock;  // for => { ... }
};

struct NewExpr {
  Token className;
  std::vector<ExprPtr> args;
};

struct SelfExpr {
  Token kw;
};

struct SuperExpr {
  Token kw;
};

struct AwaitExpr {
  Token kw;
  ExprPtr expr;
};

struct SpreadExpr {
  Token kw;
  ExprPtr expr;
};

struct TernaryExpr {
  ExprPtr cond;
  ExprPtr thenExpr;
  ExprPtr elseExpr;
};

struct Expr {
  SourcePos pos;
  std::variant<LiteralExpr, IdentExpr, UnaryExpr, BinaryExpr, AssignExpr,
               CallExpr, IndexExpr, MemberExpr, GroupExpr, ArrayExpr, MapExpr,
               LambdaExpr, NewExpr, SelfExpr, SuperExpr, AwaitExpr,
               SpreadExpr, TernaryExpr>
      node;
};

// ── Statement nodes ─────────────────────────────

struct BlockStmt {
  std::vector<StmtPtr> stmts;
};

struct VarDeclStmt {
  bool isConst = false;
  Token name;
  std::optional<TypeAnn> type;
  ExprPtr init; // may be null
};

struct ExprStmt {
  ExprPtr expr;
};

struct IfStmt {
  ExprPtr cond;
  StmtPtr thenBranch;
  std::optional<StmtPtr> elseBranch;
};

struct WhileStmt {
  ExprPtr cond;
  StmtPtr body;
};

struct DoWhileStmt {
  StmtPtr body;
  ExprPtr cond;
};

struct ForStmt {
  std::optional<StmtPtr> init;
  std::optional<ExprPtr> cond;
  std::optional<ExprPtr> post;
  StmtPtr body;
};

struct ForInStmt {
  Token name;
  ExprPtr iterable;
  StmtPtr body;
};

struct MatchArm {
  std::optional<ExprPtr> pattern;  // nullopt = default (_)
  StmtPtr action;
};

struct MatchStmt {
  ExprPtr target;
  std::vector<MatchArm> arms;
};

struct ReturnStmt {
  Token kw;
  std::optional<ExprPtr> value;
};

struct BreakStmt {
  Token kw;
};

struct ContinueStmt {
  Token kw;
};

struct ThrowStmt {
  Token kw;
  ExprPtr value;
};

struct CatchClause {
  std::optional<Token> typeName;  // optional: catch (TypeName ...)
  std::optional<Token> varName;   // optional: ... as varName
  StmtPtr body;
};

struct TryCatchStmt {
  StmtPtr tryBody;
  std::vector<CatchClause> catches;
  std::optional<StmtPtr> finallyBody;
};

struct Param {
  Token name;
  std::optional<TypeAnn> type;
  std::optional<ExprPtr> defaultValue;
};

struct FnDeclStmt {
  bool isAsync = false;
  bool isStatic = false;
  bool isOverride = false;
  Token name;
  std::vector<Param> params;
  std::optional<TypeAnn> retType;
  StmtPtr body;
};

struct ClassMember {
  enum class Access { Public, Private, Protected };
  Access access = Access::Public;
};

struct ClassFieldDecl {
  ClassMember::Access access = ClassMember::Access::Public;
  bool isStatic = false;
  Token name;
  std::optional<TypeAnn> type;
  ExprPtr init;  // may be null
};

struct ClassMethodDecl {
  ClassMember::Access access = ClassMember::Access::Public;
  FnDeclStmt method;
};

struct ClassInitDecl {
  std::vector<Param> params;
  StmtPtr body;
};

struct ClassDeclStmt {
  Token name;
  std::optional<Token> superClass;
  std::vector<Token> interfaces;
  std::vector<ClassFieldDecl> fields;
  std::vector<ClassMethodDecl> methods;
  std::optional<ClassInitDecl> initDecl;
};

struct InterfaceMethodDecl {
  Token name;
  std::vector<Param> params;
  std::optional<TypeAnn> retType;
};

struct InterfaceDeclStmt {
  Token name;
  std::vector<InterfaceMethodDecl> methods;
};

struct EnumVariant {
  Token name;
  std::optional<ExprPtr> value;
};

struct EnumDeclStmt {
  Token name;
  std::vector<EnumVariant> variants;
};

struct ImportStmt {
  Token kw;
  std::vector<Token> path;  // e.g. [math] or [net, http]
  std::optional<Token> alias;
  std::vector<Token> names;  // from X import a, b, c
};

struct ExportStmt {
  Token kw;
  StmtPtr decl;  // the fn/class/var being exported
};

struct SpawnStmt {
  Token kw;
  StmtPtr body;
};

struct UnsafeBlock {
  Token kw;
  StmtPtr body;
};

struct Stmt {
  SourcePos pos;
  std::variant<BlockStmt, VarDeclStmt, ExprStmt, IfStmt, WhileStmt,
               DoWhileStmt, ForStmt, ForInStmt, MatchStmt, ReturnStmt,
               BreakStmt, ContinueStmt, ThrowStmt, TryCatchStmt, FnDeclStmt,
               ClassDeclStmt, InterfaceDeclStmt, EnumDeclStmt, ImportStmt,
               ExportStmt, SpawnStmt, UnsafeBlock>
      node;
};

struct Program {
  std::vector<StmtPtr> stmts;
};

} // namespace nova

#pragma once

#include "parser/ast.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace nova {

struct RuntimeError : std::runtime_error {
  SourcePos pos;
  explicit RuntimeError(SourcePos p, const std::string& msg)
      : std::runtime_error(msg), pos(std::move(p)) {}
};

struct BreakSignal {};
struct ContinueSignal {};

struct RangeValue {
  int64_t start = 0;
  int64_t end = 0;
};

// Forward declarations
class Env;
class Interpreter;
struct ClassDef;

// ── Value types ─────────────────────────────────
struct ListValue {
  std::vector<std::shared_ptr<struct ValueBox>> elements;
};

struct MapValue {
  std::vector<std::pair<std::string, std::shared_ptr<struct ValueBox>>> entries;
};

struct FuncValue {
  std::string name;
  std::vector<Token> params;
  std::vector<const Expr*> defaults;
  const Stmt* body = nullptr;
  // For lambdas: captures is a snapshot of the defining scope
  std::shared_ptr<Env> closure;
  // Lambda expression body
  const Expr* lambdaBodyExpr = nullptr;
};

struct ObjectValue {
  std::string className;
  std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<struct ValueBox>>> fields;
  std::shared_ptr<ClassDef> classDef;
};

using Value = std::variant<std::monostate, bool, int64_t, double, std::string,
                           RangeValue, ListValue, MapValue, FuncValue, ObjectValue>;

struct ValueBox {
  Value val;
  ValueBox() : val(std::monostate{}) {}
  explicit ValueBox(Value v) : val(std::move(v)) {}
};

struct ReturnSignal {
  Value value;
};

struct ThrowSignal {
  Value value;
  SourcePos pos;
};

std::string typeName(const Value& v);
std::string toString(const Value& v);
bool isTruthy(const Value& v);
bool valuesEqual(const Value& a, const Value& b);

// ── Class definition ────────────────────────────
struct ClassDef {
  std::string name;
  std::shared_ptr<ClassDef> parent;
  // Methods stored as FuncValue
  std::unordered_map<std::string, FuncValue> methods;
  // Field defaults
  std::vector<std::pair<std::string, Value>> fieldDefaults;
  // Constructor
  std::optional<FuncValue> constructor;
};

// ── Environment (scope chain) ───────────────────
class Env : public std::enable_shared_from_this<Env> {
public:
  Env();
  explicit Env(std::shared_ptr<Env> parent);

  void define(const std::string& name, Value v, bool isConst = false);
  bool assign(const std::string& name, Value v);
  Value get(const std::string& name) const;
  Value* getRef(const std::string& name);
  bool has(const std::string& name) const;

  std::shared_ptr<Env> parent() const { return parent_; }

  // Class registry (global)
  void defineClass(const std::string& name, std::shared_ptr<ClassDef> def);
  std::shared_ptr<ClassDef> getClass(const std::string& name) const;

private:
  struct Slot {
    Value value;
    bool isConst = false;
  };
  std::unordered_map<std::string, Slot> vars_;
  std::shared_ptr<Env> parent_;

  // Class registry — typically stored at global scope
  std::unordered_map<std::string, std::shared_ptr<ClassDef>> classes_;
};

// ── Built-in function type ──────────────────────
using BuiltinFn = std::function<Value(Interpreter&, const SourcePos&,
                                       const std::vector<Value>&)>;

// ── Interpreter ─────────────────────────────────
class Interpreter {
public:
  explicit Interpreter(std::string filename, std::string source);

  void exec(const Program& program);
  Value eval(const Expr& e);
  void execStmt(const Stmt& s);

  // Public for builtins / REPL
  std::shared_ptr<Env> globalEnv() { return env_; }
  void registerBuiltin(const std::string& name, BuiltinFn fn);
  void registerStdlib();

private:
  void execBlock(const BlockStmt& b, std::shared_ptr<Env> scope);
  void execVarDecl(const VarDeclStmt& v);
  void execFnDecl(const FnDeclStmt& f);
  void execClassDecl(const ClassDeclStmt& c);
  void execIf(const IfStmt& i);
  void execWhile(const WhileStmt& w);
  void execDoWhile(const DoWhileStmt& d);
  void execFor(const ForStmt& f);
  void execForIn(const ForInStmt& f);
  void execMatch(const MatchStmt& m);
  void execReturn(const ReturnStmt& r);
  void execThrow(const ThrowStmt& t);
  void execTryCatch(const TryCatchStmt& tc);
  void execImport(const ImportStmt& imp);
  void execExport(const ExportStmt& exp);
  void execSpawn(const SpawnStmt& sp);

  Value callFunction(const SourcePos& pos, const FuncValue& fn,
                     const std::vector<Value>& args,
                     ObjectValue* self = nullptr);
  Value callBuiltin(const SourcePos& pos, const std::string& name,
                    const std::vector<Value>& args);
  Value callValue(const SourcePos& pos, const Value& callee,
                  const std::vector<Value>& args);
  Value instantiate(const SourcePos& pos, const std::string& className,
                    const std::vector<Value>& args);

  Value memberGet(const SourcePos& pos, const Value& obj, const std::string& member);
  Value memberCall(const SourcePos& pos, Value& obj,
                   const std::string& member, const std::vector<Value>& args);

  Value binaryOp(const SourcePos& pos, const Token& op, const Value& a,
                 const Value& b);
  Value unaryOp(const SourcePos& pos, const Token& op, const Value& a);

  std::string filename_;
  std::string source_;
  std::shared_ptr<Env> env_;             // current scope
  std::shared_ptr<Env> globalEnv_;       // global scope
  std::unordered_map<std::string, BuiltinFn> builtins_;
  std::unordered_map<std::string, bool> importedModules_;
};

} // namespace nova

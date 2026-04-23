#include "codegen/codegen.h"
#include <iostream>

namespace nova {

void Codegen::emit(const std::string& s) { out_ << s; }
void Codegen::emitLine(const std::string& s) {
  emitIndent();
  out_ << s << "\n";
}
void Codegen::emitIndent() {
  for (int i = 0; i < indent_; i++) out_ << "    ";
}

std::string Codegen::generate(const Program& program) {
  out_.str("");
  out_.clear();

  // Preamble
  out_ << "#include <iostream>\n";
  out_ << "#include <string>\n";
  out_ << "#include <vector>\n";
  out_ << "#include <map>\n";
  out_ << "#include <cmath>\n";
  out_ << "#include <algorithm>\n";
  out_ << "#include <sstream>\n";
  out_ << "#include <functional>\n";
  out_ << "#include <stdexcept>\n";
  out_ << "#include <cstdlib>\n";
  out_ << "#include <fstream>\n";
  out_ << "\n";
  out_ << "using namespace std;\n\n";

  // Helper: Vibe's + operator concatenates anything with strings
  out_ << "// Vibe string concatenation helper\n";
  out_ << "template<typename A, typename B>\n";
  out_ << "auto _vibe_add(A&& a, B&& b) {\n";
  out_ << "    if constexpr (is_same_v<decay_t<A>, string> || is_same_v<decay_t<B>, string>) {\n";
  out_ << "        ostringstream _s; _s << a << b; return _s.str();\n";
  out_ << "    } else { return a + b; }\n";
  out_ << "}\n\n";

  // Forward declarations for classes
  for (const auto& st : program.stmts) {
    if (auto* cls = std::get_if<ClassDeclStmt>(&st->node)) {
      emitLine("class " + cls->name.lexeme + ";");
    }
  }
  out_ << "\n";

  // Separate top-level declarations (functions, classes, enums) from body statements
  std::vector<const Stmt*> topLevel;
  std::vector<const Stmt*> bodyStmts;
  bool hasMain = false;
  for (const auto& st : program.stmts) {
    if (auto* fn = std::get_if<FnDeclStmt>(&st->node)) {
      topLevel.push_back(st.get());
      if (fn->name.lexeme == "main") hasMain = true;
    } else if (std::get_if<ClassDeclStmt>(&st->node) ||
               std::get_if<EnumDeclStmt>(&st->node) ||
               std::get_if<InterfaceDeclStmt>(&st->node)) {
      topLevel.push_back(st.get());
    } else {
      bodyStmts.push_back(st.get());
    }
  }

  // Emit top-level declarations (outside main)
  for (const auto* st : topLevel) {
    genStmt(*st);
    out_ << "\n";
  }

  // Emit body statements inside main() — unless user already defined fn main()
  if (!hasMain || !bodyStmts.empty()) {
    if (!hasMain) {
      emitLine("int main() {");
      indent_++;
      for (const auto* st : bodyStmts) {
        genStmt(*st);
        out_ << "\n";
      }
      emitLine("return 0;");
      indent_--;
      emitLine("}");
    } else if (!bodyStmts.empty()) {
      // User has fn main() + loose statements — wrap loose stmts in __vibe_init
      emitLine("// Top-level statements (called from main)");
      emitLine("void __vibe_init() {");
      indent_++;
      for (const auto* st : bodyStmts) {
        genStmt(*st);
        out_ << "\n";
      }
      indent_--;
      emitLine("}");
    }
  }
  return out_.str();
}

void Codegen::genStmt(const Stmt& s) {
  auto visitor = [this](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, BlockStmt>) genBlock(node);
    else if constexpr (std::is_same_v<T, VarDeclStmt>) genVarDecl(node);
    else if constexpr (std::is_same_v<T, ExprStmt>) genExprStmt(node);
    else if constexpr (std::is_same_v<T, IfStmt>) genIf(node);
    else if constexpr (std::is_same_v<T, WhileStmt>) genWhile(node);
    else if constexpr (std::is_same_v<T, DoWhileStmt>) genDoWhile(node);
    else if constexpr (std::is_same_v<T, ForStmt>) genFor(node);
    else if constexpr (std::is_same_v<T, ForInStmt>) genForIn(node);
    else if constexpr (std::is_same_v<T, MatchStmt>) genMatch(node);
    else if constexpr (std::is_same_v<T, ReturnStmt>) genReturn(node);
    else if constexpr (std::is_same_v<T, BreakStmt>) emitLine("break;");
    else if constexpr (std::is_same_v<T, ContinueStmt>) emitLine("continue;");
    else if constexpr (std::is_same_v<T, ThrowStmt>) genThrow(node);
    else if constexpr (std::is_same_v<T, TryCatchStmt>) genTryCatch(node);
    else if constexpr (std::is_same_v<T, FnDeclStmt>) genFnDecl(node);
    else if constexpr (std::is_same_v<T, ClassDeclStmt>) genClassDecl(node);
    else if constexpr (std::is_same_v<T, EnumDeclStmt>) genEnumDecl(node);
    else if constexpr (std::is_same_v<T, ImportStmt>) genImport(node);
    else if constexpr (std::is_same_v<T, PyImportStmt>) genPyImport(node);
    else if constexpr (std::is_same_v<T, PythonBlockStmt>) genPythonBlock(node);
    else if constexpr (std::is_same_v<T, ExportStmt>) genExport(node);
    else if constexpr (std::is_same_v<T, InterfaceDeclStmt>) {
      // Interfaces map to abstract classes in C++
      emitLine("class " + node.name.lexeme + " {");
      emitLine("public:");
      indent_++;
      for (auto& m : node.methods)
        emitLine("virtual auto " + m.name.lexeme + "() -> void = 0;");
      indent_--;
      emitLine("};");
    }
    else if constexpr (std::is_same_v<T, SpawnStmt>) {
      emitLine("// spawn (runs synchronously in compiled mode)");
      genStmt(*node.body);
    }
    else if constexpr (std::is_same_v<T, UnsafeBlock>) {
      emitLine("// unsafe block");
      genStmt(*node.body);
    }
  };
  std::visit(visitor, s.node);
}

void Codegen::genBlock(const BlockStmt& b) {
  emitLine("{");
  indent_++;
  for (const auto& st : b.stmts) genStmt(*st);
  indent_--;
  emitLine("}");
}

void Codegen::genVarDecl(const VarDeclStmt& v) {
  std::string keyword = v.isConst ? "const auto" : "auto";
  if (v.init) {
    emitLine(keyword + " " + v.name.lexeme + " = " + genExpr(*v.init) + ";");
  } else {
    // Without init, we can't deduce type; use a variant or default
    emitLine("auto " + v.name.lexeme + " = 0;");
  }
}

void Codegen::genExprStmt(const ExprStmt& e) {
  emitLine(genExpr(*e.expr) + ";");
}

void Codegen::genIf(const IfStmt& i) {
  emitLine("if (" + genExpr(*i.cond) + ")");
  genStmt(*i.thenBranch);
  if (i.elseBranch) {
    emitLine("else");
    genStmt(*(*i.elseBranch));
  }
}

void Codegen::genWhile(const WhileStmt& w) {
  emitLine("while (" + genExpr(*w.cond) + ")");
  genStmt(*w.body);
}

void Codegen::genDoWhile(const DoWhileStmt& d) {
  emitLine("do");
  genStmt(*d.body);
  emitLine("while (" + genExpr(*d.cond) + ");");
}

void Codegen::genFor(const ForStmt& f) {
  std::string init_str, cond_str, post_str;
  if (f.init) {
    // Extract the inner statement as a string (simplified)
    std::ostringstream tmp;
    auto saved = out_.str();
    out_.str("");
    genStmt(*(*f.init));
    init_str = out_.str();
    out_.str(saved);
    // Trim trailing newline and leading spaces
    while (!init_str.empty() && init_str.back() == '\n') init_str.pop_back();
    while (!init_str.empty() && init_str.front() == ' ') init_str = init_str.substr(1);
    // Remove trailing semicolon for for-loop syntax
    if (!init_str.empty() && init_str.back() == ';') init_str.pop_back();
  }
  if (f.cond) cond_str = genExpr(*(*f.cond));
  if (f.post) post_str = genExpr(*(*f.post));
  emitLine("for (" + init_str + "; " + cond_str + "; " + post_str + ")");
  genStmt(*f.body);
}

void Codegen::genForIn(const ForInStmt& f) {
  emitLine("for (auto " + f.name.lexeme + " : " + genExpr(*f.iterable) + ")");
  genStmt(*f.body);
}

void Codegen::genMatch(const MatchStmt& m) {
  std::string target = genExpr(*m.target);
  emitLine("{");
  indent_++;
  emitLine("auto _match_val_ = " + target + ";");
  bool first = true;
  const MatchArm* def = nullptr;
  for (const auto& arm : m.arms) {
    if (!arm.pattern) { def = &arm; continue; }
    std::string cond = first ? "if" : "else if";
    emitLine(cond + " (_match_val_ == " + genExpr(*(*arm.pattern)) + ")");
    genStmt(*arm.action);
    first = false;
  }
  if (def) {
    emitLine("else");
    genStmt(*def->action);
  }
  indent_--;
  emitLine("}");
}

void Codegen::genReturn(const ReturnStmt& r) {
  if (r.value)
    emitLine("return " + genExpr(*(*r.value)) + ";");
  else
    emitLine("return;");
}

void Codegen::genThrow(const ThrowStmt& t) {
  emitLine("throw std::runtime_error(" + genExpr(*t.value) + ");");
}

void Codegen::genTryCatch(const TryCatchStmt& tc) {
  emitLine("try");
  genStmt(*tc.tryBody);
  for (const auto& cc : tc.catches) {
    if (cc.varName)
      emitLine("catch (const std::exception& " + cc.varName->lexeme + ")");
    else
      emitLine("catch (...)");
    genStmt(*cc.body);
  }
  if (tc.finallyBody) {
    emitLine("// finally");
    genStmt(*(*tc.finallyBody));
  }
}

void Codegen::genFnDecl(const FnDeclStmt& f) {
  // Special-case 'main' → int main()
  if (f.name.lexeme == "main") {
    emitLine("int main()");
    genStmt(*f.body);
    // Add return 0 after main body (ensure int return)
    return;
  }

  // For functions with params, emit a template declaration for C++17 compat
  if (!f.params.empty()) {
    std::string tmpl = "template<";
    for (size_t i = 0; i < f.params.size(); i++) {
      if (i) tmpl += ", ";
      tmpl += "typename _T" + std::to_string(i);
    }
    tmpl += ">";
    emitLine(tmpl);
  }

  std::string sig = "auto " + f.name.lexeme + "(";
  for (size_t i = 0; i < f.params.size(); i++) {
    if (i) sig += ", ";
    sig += "_T" + std::to_string(i) + " " + f.params[i].name.lexeme;
  }
  sig += ")";
  emitLine(sig);
  genStmt(*f.body);
}

void Codegen::genClassDecl(const ClassDeclStmt& c) {
  std::string decl = "class " + c.name.lexeme;
  if (c.superClass) decl += " : public " + c.superClass->lexeme;
  emitLine(decl + " {");
  emitLine("public:");
  indent_++;

  // Fields
  for (auto& field : c.fields) {
    if (field.init)
      emitLine("auto " + field.name.lexeme + " = " + genExpr(*field.init) + ";");
    else
      emitLine("int " + field.name.lexeme + " = 0;");
  }

  // Constructor
  if (c.initDecl) {
    std::string ctor = c.name.lexeme + "(";
    for (size_t i = 0; i < c.initDecl->params.size(); i++) {
      if (i) ctor += ", ";
      ctor += "auto " + c.initDecl->params[i].name.lexeme;
    }
    ctor += ")";
    emitLine(ctor);
    genStmt(*c.initDecl->body);
  }

  // Methods
  for (auto& md : c.methods) {
    std::string sig = "auto " + md.method.name.lexeme + "(";
    for (size_t i = 0; i < md.method.params.size(); i++) {
      if (i) sig += ", ";
      sig += "auto " + md.method.params[i].name.lexeme;
    }
    sig += ")";
    emitLine(sig);
    genStmt(*md.method.body);
  }

  indent_--;
  emitLine("};");
}

void Codegen::genEnumDecl(const EnumDeclStmt& e) {
  emitLine("enum class " + e.name.lexeme + " {");
  indent_++;
  for (size_t i = 0; i < e.variants.size(); i++) {
    std::string line = e.variants[i].name.lexeme;
    if (e.variants[i].value)
      line += " = " + genExpr(*e.variants[i].value.value());
    if (i + 1 < e.variants.size()) line += ",";
    emitLine(line);
  }
  indent_--;
  emitLine("};");
}

void Codegen::genImport(const ImportStmt& imp) {
  std::string mod;
  for (size_t i = 0; i < imp.path.size(); i++) {
    if (i) mod += "/";
    mod += imp.path[i].lexeme;
  }
  emitLine("// import " + mod);
  // Map known modules
  if (mod == "math") emitLine("#include <cmath>");
  else if (mod == "io") emitLine("#include <fstream>");
  else if (mod == "os") emitLine("#include <cstdlib>");
}
void Codegen::genPyImport(const PyImportStmt& imp) {
  emitLine("// Python interop import");
  std::string name = imp.moduleName.lexeme;
  std::string alias = imp.alias ? imp.alias->lexeme : name;
  emitLine("nova::python::import_module(\"" + name + "\", \"" + alias + "\");");
}

void Codegen::genPythonBlock(const PythonBlockStmt& pyb) {
  emitLine("// Embedded Python execution");
  // escape quotes
  std::string code = pyb.codeString.lexeme;
  emitLine("nova::python::exec_string(" + code + ");");
}
void Codegen::genExport(const ExportStmt& exp) {
  genStmt(*exp.decl);
}

std::string Codegen::genBinaryOp(const Token& op) {
  switch (op.type) {
  case TokenType::Plus: return "+";
  case TokenType::Minus: return "-";
  case TokenType::Star: return "*";
  case TokenType::Slash: return "/";
  case TokenType::TildeSlash: return "/"; // integer division in C++ is already truncating for ints
  case TokenType::Percent: return "%";
  case TokenType::StarStar: return "/* pow */"; // handled in genExpr
  case TokenType::EqualEqual: return "==";
  case TokenType::BangEqual: return "!=";
  case TokenType::Less: return "<";
  case TokenType::LessEqual: return "<=";
  case TokenType::Greater: return ">";
  case TokenType::GreaterEqual: return ">=";
  case TokenType::AmpAmp: return "&&";
  case TokenType::PipePipe: return "||";
  case TokenType::Amp: return "&";
  case TokenType::Pipe: return "|";
  case TokenType::Caret: return "^";
  case TokenType::LessLess: return "<<";
  case TokenType::GreaterGreater: return ">>";
  default: return "?";
  }
}

std::string Codegen::genExpr(const Expr& e) {
  auto visitor = [this, &e](const auto& node) -> std::string {
    using T = std::decay_t<decltype(node)>;

    if constexpr (std::is_same_v<T, LiteralExpr>) {
      const Token& t = node.tok;
      if (t.type == TokenType::KwTrue) return "true";
      if (t.type == TokenType::KwFalse) return "false";
      if (t.type == TokenType::KwNull) return "nullptr";
      if (t.type == TokenType::StringLiteral) {
        // Convert to std::string
        return "string(" + t.lexeme + ")";
      }
      return t.lexeme;
    }

    else if constexpr (std::is_same_v<T, IdentExpr>)
      return node.name.lexeme;

    else if constexpr (std::is_same_v<T, SelfExpr>)
      return "(*this)";

    else if constexpr (std::is_same_v<T, SuperExpr>)
      return "/* super */";

    else if constexpr (std::is_same_v<T, GroupExpr>)
      return "(" + genExpr(*node.inner) + ")";

    else if constexpr (std::is_same_v<T, UnaryExpr>) {
      std::string op;
      if (node.op.type == TokenType::Bang) op = "!";
      else if (node.op.type == TokenType::Minus) op = "-";
      else if (node.op.type == TokenType::Plus) op = "+";
      else if (node.op.type == TokenType::Tilde) op = "~";
      return op + genExpr(*node.rhs);
    }

    else if constexpr (std::is_same_v<T, BinaryExpr>) {
      if (node.op.type == TokenType::StarStar)
        return "pow(" + genExpr(*node.lhs) + ", " + genExpr(*node.rhs) + ")";
      if (node.op.type == TokenType::DotDot)
        return "/* range " + genExpr(*node.lhs) + ".." + genExpr(*node.rhs) + " */";
      // Use _vibe_add for + to support string concatenation with non-strings
      if (node.op.type == TokenType::Plus)
        return "_vibe_add(" + genExpr(*node.lhs) + ", " + genExpr(*node.rhs) + ")";
      return genExpr(*node.lhs) + " " + genBinaryOp(node.op) + " " + genExpr(*node.rhs);
    }

    else if constexpr (std::is_same_v<T, TernaryExpr>)
      return genExpr(*node.cond) + " ? " + genExpr(*node.thenExpr) + " : " + genExpr(*node.elseExpr);

    else if constexpr (std::is_same_v<T, AssignExpr>) {
      std::string target = genExpr(*node.target);
      std::string op = "=";
      if (node.op.type == TokenType::PlusEqual) op = "+=";
      else if (node.op.type == TokenType::MinusEqual) op = "-=";
      else if (node.op.type == TokenType::StarEqual) op = "*=";
      else if (node.op.type == TokenType::SlashEqual) op = "/=";
      else if (node.op.type == TokenType::PercentEqual) op = "%=";
      return target + " " + op + " " + genExpr(*node.value);
    }

    else if constexpr (std::is_same_v<T, CallExpr>) {
      std::string callee = genExpr(*node.callee);
      // Map print → cout with newline (matches interpreter)
      if (callee == "print") {
        std::string out = "[&]{ ";
        for (size_t i = 0; i < node.args.size(); i++) {
          if (i) out += " cout << \" \"; ";
          out += "cout << " + genExpr(*node.args[i]) + "; ";
        }
        out += "cout << endl; }()";
        return out;
      }
      // Map println → cout with just newline (no args)
      if (callee == "println") {
        std::string out = "[&]{ ";
        for (size_t i = 0; i < node.args.size(); i++) {
          if (i) out += " cout << \" \"; ";
          out += "cout << " + genExpr(*node.args[i]) + "; ";
        }
        out += "cout << endl; }()";
        return out;
      }
      // Map len → .size()
      if (callee == "len" && node.args.size() == 1)
        return "(int)" + genExpr(*node.args[0]) + ".size()";
      // Map str → to_string
      if (callee == "str" && node.args.size() == 1)
        return "to_string(" + genExpr(*node.args[0]) + ")";
      // Map type/typeof → typeid
      if ((callee == "type" || callee == "typeof") && node.args.size() == 1)
        return "string(typeid(" + genExpr(*node.args[0]) + ").name())";
      // Map exit
      if (callee == "exit" && node.args.size() == 1)
        return "[&]{ ::exit(" + genExpr(*node.args[0]) + "); return 0; }()";
      // Map range → a simple iota vector
      if (callee == "range") {
        if (node.args.size() == 1)
          return "[&]{ vector<int> _r; for(int _i=0; _i<" + genExpr(*node.args[0]) + "; _i++) _r.push_back(_i); return _r; }()";
        if (node.args.size() == 2)
          return "[&]{ vector<int> _r; for(int _i=" + genExpr(*node.args[0]) + "; _i<" + genExpr(*node.args[1]) + "; _i++) _r.push_back(_i); return _r; }()";
        if (node.args.size() == 3)
          return "[&]{ vector<int> _r; for(int _i=" + genExpr(*node.args[0]) + "; _i<" + genExpr(*node.args[1]) + "; _i+=" + genExpr(*node.args[2]) + ") _r.push_back(_i); return _r; }()";
      }
      if (callee == "input") {
        if (!node.args.empty())
          return "[&]{ cout << " + genExpr(*node.args[0]) + "; string _l; getline(cin, _l); return _l; }()";
        return "[&]{ string _l; getline(cin, _l); return _l; }()";
      }
      std::string out = callee + "(";
      for (size_t i = 0; i < node.args.size(); i++) {
        if (i) out += ", ";
        out += genExpr(*node.args[i]);
      }
      return out + ")";
    }

    else if constexpr (std::is_same_v<T, NewExpr>) {
      std::string out = node.className.lexeme + "(";
      for (size_t i = 0; i < node.args.size(); i++) {
        if (i) out += ", ";
        out += genExpr(*node.args[i]);
      }
      return out + ")";
    }

    else if constexpr (std::is_same_v<T, IndexExpr>)
      return genExpr(*node.object) + "[" + genExpr(*node.index) + "]";

    else if constexpr (std::is_same_v<T, MemberExpr>)
      return genExpr(*node.object) + "." + node.member.lexeme;

    else if constexpr (std::is_same_v<T, ArrayExpr>) {
      // Use initializer list (deduced via auto)
      std::string out = "{";
      for (size_t i = 0; i < node.elements.size(); i++) {
        if (i) out += ", ";
        out += genExpr(*node.elements[i]);
      }
      return out + "}";
    }

    else if constexpr (std::is_same_v<T, MapExpr>) {
      std::string out = "map<string, string>{";
      for (size_t i = 0; i < node.entries.size(); i++) {
        if (i) out += ", ";
        out += "{" + genExpr(*node.entries[i].key) + ", " + genExpr(*node.entries[i].value) + "}";
      }
      return out + "}";
    }

    else if constexpr (std::is_same_v<T, LambdaExpr>) {
      std::string out = "[&](";
      for (size_t i = 0; i < node.params.size(); i++) {
        if (i) out += ", ";
        out += "auto " + node.params[i].lexeme;
      }
      out += ")";
      if (node.bodyExpr) {
        out += " { return " + genExpr(*node.bodyExpr) + "; }";
      } else {
        out += " { /* block */ }";
      }
      return out;
    }

    else if constexpr (std::is_same_v<T, AwaitExpr>)
      return genExpr(*node.expr);

    else if constexpr (std::is_same_v<T, SpreadExpr>)
      return "/* spread */ " + genExpr(*node.expr);

    return std::string("/* unknown expr */");
  };

  return std::visit(visitor, e.node);
}

} // namespace nova

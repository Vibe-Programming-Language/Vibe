#include "runtime/runtime.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

namespace nova {

// ═══════════════════════════════════════════════════
//  Value helpers
// ═══════════════════════════════════════════════════

static int64_t asInt(const SourcePos& pos, const Value& v) {
  if (auto p = std::get_if<int64_t>(&v)) return *p;
  if (auto p = std::get_if<double>(&v)) return static_cast<int64_t>(*p);
  if (auto p = std::get_if<bool>(&v)) return *p ? 1 : 0;
  throw RuntimeError(pos, "Expected number, got " + typeName(v));
}

static double asDouble(const SourcePos& pos, const Value& v) {
  if (auto p = std::get_if<double>(&v)) return *p;
  if (auto p = std::get_if<int64_t>(&v)) return static_cast<double>(*p);
  if (auto p = std::get_if<bool>(&v)) return *p ? 1.0 : 0.0;
  throw RuntimeError(pos, "Expected number, got " + typeName(v));
}

std::string typeName(const Value& v) {
  if (std::holds_alternative<std::monostate>(v)) return "null";
  if (std::holds_alternative<bool>(v)) return "bool";
  if (std::holds_alternative<int64_t>(v)) return "int";
  if (std::holds_alternative<double>(v)) return "float";
  if (std::holds_alternative<std::string>(v)) return "str";
  if (std::holds_alternative<RangeValue>(v)) return "range";
  if (std::holds_alternative<ListValue>(v)) return "list";
  if (std::holds_alternative<MapValue>(v)) return "map";
  if (std::holds_alternative<FuncValue>(v)) return "function";
  if (std::holds_alternative<ObjectValue>(v)) {
    return std::get<ObjectValue>(v).className;
  }
  return "unknown";
}

std::string toString(const Value& v) {
  if (std::holds_alternative<std::monostate>(v)) return "null";
  if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
  if (auto i = std::get_if<int64_t>(&v)) return std::to_string(*i);
  if (auto d = std::get_if<double>(&v)) {
    std::ostringstream oss;
    oss << *d;
    return oss.str();
  }
  if (auto s = std::get_if<std::string>(&v)) return *s;
  if (auto r = std::get_if<RangeValue>(&v)) {
    return "range(" + std::to_string(r->start) + ", " + std::to_string(r->end) + ")";
  }
  if (auto l = std::get_if<ListValue>(&v)) {
    std::string out = "[";
    for (size_t i = 0; i < l->elements.size(); i++) {
      if (i) out += ", ";
      const Value& el = l->elements[i]->val;
      if (std::holds_alternative<std::string>(el))
        out += "\"" + toString(el) + "\"";
      else
        out += toString(el);
    }
    return out + "]";
  }
  if (auto m = std::get_if<MapValue>(&v)) {
    std::string out = "{";
    for (size_t i = 0; i < m->entries.size(); i++) {
      if (i) out += ", ";
      out += "\"" + m->entries[i].first + "\": ";
      const Value& val = m->entries[i].second->val;
      if (std::holds_alternative<std::string>(val))
        out += "\"" + toString(val) + "\"";
      else
        out += toString(val);
    }
    return out + "}";
  }
  if (auto f = std::get_if<FuncValue>(&v)) {
    return "<fn " + f->name + ">";
  }
  if (auto o = std::get_if<ObjectValue>(&v)) {
    return "<" + o->className + " instance>";
  }
  return "<value>";
}

bool isTruthy(const Value& v) {
  if (std::holds_alternative<std::monostate>(v)) return false;
  if (auto b = std::get_if<bool>(&v)) return *b;
  if (auto i = std::get_if<int64_t>(&v)) return *i != 0;
  if (auto d = std::get_if<double>(&v)) return *d != 0.0;
  if (auto s = std::get_if<std::string>(&v)) return !s->empty();
  if (auto l = std::get_if<ListValue>(&v)) return !l->elements.empty();
  if (auto m = std::get_if<MapValue>(&v)) return !m->entries.empty();
  return true;
}

bool valuesEqual(const Value& a, const Value& b) {
  if (a.index() != b.index()) {
    // int == double promotion
    if ((std::holds_alternative<int64_t>(a) && std::holds_alternative<double>(b)) ||
        (std::holds_alternative<double>(a) && std::holds_alternative<int64_t>(b))) {
      double da = std::holds_alternative<int64_t>(a) ? static_cast<double>(std::get<int64_t>(a)) : std::get<double>(a);
      double db = std::holds_alternative<int64_t>(b) ? static_cast<double>(std::get<int64_t>(b)) : std::get<double>(b);
      return da == db;
    }
    return false;
  }
  if (std::holds_alternative<std::monostate>(a)) return true;
  if (auto ba = std::get_if<bool>(&a)) return *ba == std::get<bool>(b);
  if (auto ia = std::get_if<int64_t>(&a)) return *ia == std::get<int64_t>(b);
  if (auto da = std::get_if<double>(&a)) return *da == std::get<double>(b);
  if (auto sa = std::get_if<std::string>(&a)) return *sa == std::get<std::string>(b);
  return toString(a) == toString(b);
}

// ═══════════════════════════════════════════════════
//  Environment
// ═══════════════════════════════════════════════════

Env::Env() : parent_(nullptr) {}
Env::Env(std::shared_ptr<Env> parent) : parent_(std::move(parent)) {}

void Env::define(const std::string& name, Value v, bool isConst) {
  vars_[name] = Slot{std::move(v), isConst};
}

bool Env::assign(const std::string& name, Value v) {
  auto it = vars_.find(name);
  if (it != vars_.end()) {
    if (it->second.isConst) return false;
    it->second.value = std::move(v);
    return true;
  }
  if (parent_) return parent_->assign(name, v);
  return false;
}

Value Env::get(const std::string& name) const {
  auto it = vars_.find(name);
  if (it != vars_.end()) return it->second.value;
  if (parent_) return parent_->get(name);
  throw std::runtime_error("Undefined variable: " + name);
}

Value* Env::getRef(const std::string& name) {
  auto it = vars_.find(name);
  if (it != vars_.end()) return &it->second.value;
  if (parent_) return parent_->getRef(name);
  return nullptr;
}

bool Env::has(const std::string& name) const {
  auto it = vars_.find(name);
  if (it != vars_.end()) return true;
  if (parent_) return parent_->has(name);
  return false;
}

void Env::defineClass(const std::string& name, std::shared_ptr<ClassDef> def) {
  classes_[name] = std::move(def);
}

std::shared_ptr<ClassDef> Env::getClass(const std::string& name) const {
  auto it = classes_.find(name);
  if (it != classes_.end()) return it->second;
  if (parent_) return parent_->getClass(name);
  return nullptr;
}

// ═══════════════════════════════════════════════════
//  Interpreter
// ═══════════════════════════════════════════════════

Interpreter::Interpreter(std::string filename, std::string source)
    : filename_(std::move(filename)), source_(std::move(source)) {
  globalEnv_ = std::make_shared<Env>();
  env_ = globalEnv_;
  registerStdlib();
}

void Interpreter::registerBuiltin(const std::string& name, BuiltinFn fn) {
  builtins_[name] = std::move(fn);
}

// ── Standard library registration ──────────────────
void Interpreter::registerStdlib() {
  // print(...)
  registerBuiltin("print", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    for (size_t i = 0; i < args.size(); i++) {
      if (i) std::cout << " ";
      std::cout << toString(args[i]);
    }
    std::cout << std::endl;
    return std::monostate{};
  });

  // println()
  registerBuiltin("println", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
    std::cout << std::endl;
    return std::monostate{};
  });

  // input(prompt)
  registerBuiltin("input", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (!args.empty()) {
      std::cout << toString(args[0]);
      std::cout.flush();
    }
    std::string line;
    std::getline(std::cin, line);
    return line;
  });

  // printfmt("Hello {name}") — string interpolation
  registerBuiltin("printfmt", [this](Interpreter& interp, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::monostate{};
    std::string fmt = toString(args[0]);
    std::string result;
    size_t i2 = 0;
    while (i2 < fmt.size()) {
      if (fmt[i2] == '{') {
        size_t end = fmt.find('}', i2);
        if (end != std::string::npos) {
          std::string varName = fmt.substr(i2 + 1, end - i2 - 1);
          try {
            Value v = env_->get(varName);
            result += toString(v);
          } catch (...) {
            result += "{" + varName + "}";
          }
          i2 = end + 1;
          continue;
        }
      }
      result += fmt[i2++];
    }
    std::cout << result << std::endl;
    return std::monostate{};
  });

  // range(start, end)
  registerBuiltin("range", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 2)
      throw RuntimeError(pos, "range(start, end) expects 2 arguments");
    return RangeValue{asInt(pos, args[0]), asInt(pos, args[1])};
  });

  // len(val)
  registerBuiltin("len", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "len() expects 1 argument");
    const auto& v = args[0];
    if (auto s = std::get_if<std::string>(&v)) return static_cast<int64_t>(s->size());
    if (auto l = std::get_if<ListValue>(&v)) return static_cast<int64_t>(l->elements.size());
    if (auto m = std::get_if<MapValue>(&v)) return static_cast<int64_t>(m->entries.size());
    throw RuntimeError(pos, "len() expects string, list, or map");
  });

  // type(val)
  registerBuiltin("type", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::string("null");
    return typeName(args[0]);
  });

  // str(val)
  registerBuiltin("str", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::string("");
    return toString(args[0]);
  });

  // int(val)
  registerBuiltin("int", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.empty()) return int64_t(0);
    const auto& v = args[0];
    if (auto i = std::get_if<int64_t>(&v)) return *i;
    if (auto d = std::get_if<double>(&v)) return static_cast<int64_t>(*d);
    if (auto b = std::get_if<bool>(&v)) return *b ? int64_t(1) : int64_t(0);
    if (auto s = std::get_if<std::string>(&v)) {
      try { return static_cast<int64_t>(std::stoll(*s)); }
      catch (...) { throw RuntimeError(pos, "Cannot convert '" + *s + "' to int"); }
    }
    throw RuntimeError(pos, "Cannot convert " + typeName(v) + " to int");
  });

  // float(val)
  registerBuiltin("float", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.empty()) return 0.0;
    const auto& v = args[0];
    if (auto d = std::get_if<double>(&v)) return *d;
    if (auto i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
    if (auto b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
    if (auto s = std::get_if<std::string>(&v)) {
      try { return std::stod(*s); }
      catch (...) { throw RuntimeError(pos, "Cannot convert '" + *s + "' to float"); }
    }
    throw RuntimeError(pos, "Cannot convert " + typeName(v) + " to float");
  });

  // abs(val)
  registerBuiltin("abs", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "abs() expects 1 argument");
    if (auto i = std::get_if<int64_t>(&args[0])) return std::abs(*i);
    if (auto d = std::get_if<double>(&args[0])) return std::abs(*d);
    throw RuntimeError(pos, "abs() expects a number");
  });

  // min(a, b) or min(list)
  registerBuiltin("min", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() == 2) return asDouble(pos, args[0]) < asDouble(pos, args[1]) ? args[0] : args[1];
    if (args.size() == 1) {
      if (auto l = std::get_if<ListValue>(&args[0])) {
        if (l->elements.empty()) throw RuntimeError(pos, "min() of empty list");
        Value best = l->elements[0]->val;
        for (size_t i2 = 1; i2 < l->elements.size(); i2++) {
          if (asDouble(pos, l->elements[i2]->val) < asDouble(pos, best))
            best = l->elements[i2]->val;
        }
        return best;
      }
    }
    throw RuntimeError(pos, "min() expects 2 args or a list");
  });

  // max(a, b) or max(list)
  registerBuiltin("max", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() == 2) return asDouble(pos, args[0]) > asDouble(pos, args[1]) ? args[0] : args[1];
    if (args.size() == 1) {
      if (auto l = std::get_if<ListValue>(&args[0])) {
        if (l->elements.empty()) throw RuntimeError(pos, "max() of empty list");
        Value best = l->elements[0]->val;
        for (size_t i2 = 1; i2 < l->elements.size(); i2++) {
          if (asDouble(pos, l->elements[i2]->val) > asDouble(pos, best))
            best = l->elements[i2]->val;
        }
        return best;
      }
    }
    throw RuntimeError(pos, "max() expects 2 args or a list");
  });

  // sum(list)
  registerBuiltin("sum", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "sum() expects a list");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "sum() expects a list");
    double total = 0;
    bool allInt = true;
    for (auto& el : l->elements) {
      if (std::holds_alternative<double>(el->val)) allInt = false;
      total += asDouble(pos, el->val);
    }
    if (allInt) return static_cast<int64_t>(total);
    return total;
  });

  // push(list, val) — alias for list.add
  registerBuiltin("push", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 2) throw RuntimeError(pos, "push() expects (list, value)");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "push() first arg must be a list");
    // Note: we can't modify the variant directly via const ref. This is best-effort.
    // The proper way is for the user to use list.add() method syntax.
    throw RuntimeError(pos, "Use list.add(value) instead of push(list, value)");
  });

  // typeof(val)
  registerBuiltin("typeof", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::string("null");
    return typeName(args[0]);
  });

  // exit(code)
  registerBuiltin("exit", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    int code = 0;
    if (!args.empty()) code = static_cast<int>(asInt(SourcePos{}, args[0]));
    std::exit(code);
    return std::monostate{}; // unreachable
  });

  // ── Additional string functions ──────────────────
  registerBuiltin("chr", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "chr() expects 1 argument");
    int64_t code = asInt(pos, args[0]);
    return std::string(1, static_cast<char>(code));
  });

  registerBuiltin("ord", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "ord() expects 1 argument");
    auto s = std::get_if<std::string>(&args[0]);
    if (!s || s->empty()) throw RuntimeError(pos, "ord() expects a non-empty string");
    return static_cast<int64_t>(static_cast<unsigned char>((*s)[0]));
  });

  registerBuiltin("format", [this](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::string("");
    std::string fmt = toString(args[0]);
    std::string result;
    size_t argIdx = 1;
    for (size_t i2 = 0; i2 < fmt.size(); i2++) {
      if (fmt[i2] == '{' && i2 + 1 < fmt.size() && fmt[i2 + 1] == '}') {
        if (argIdx < args.size())
          result += toString(args[argIdx++]);
        else
          result += "{}";
        i2++;
      } else if (fmt[i2] == '{') {
        size_t end = fmt.find('}', i2);
        if (end != std::string::npos) {
          std::string varName = fmt.substr(i2 + 1, end - i2 - 1);
          try { result += toString(env_->get(varName)); }
          catch (...) { result += "{" + varName + "}"; }
          i2 = end;
        } else {
          result += fmt[i2];
        }
      } else {
        result += fmt[i2];
      }
    }
    return result;
  });

  // ── Collection utilities ─────────────────────────
  registerBuiltin("keys", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "keys() expects 1 argument");
    auto m = std::get_if<MapValue>(&args[0]);
    if (!m) throw RuntimeError(pos, "keys() expects a map");
    ListValue lv;
    for (auto& entry : m->entries)
      lv.elements.push_back(std::make_shared<ValueBox>(Value(entry.first)));
    return Value(std::move(lv));
  });

  registerBuiltin("values", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "values() expects 1 argument");
    auto m = std::get_if<MapValue>(&args[0]);
    if (!m) throw RuntimeError(pos, "values() expects a map");
    ListValue lv;
    for (auto& entry : m->entries)
      lv.elements.push_back(std::make_shared<ValueBox>(entry.second->val));
    return Value(std::move(lv));
  });

  registerBuiltin("zip", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 2) throw RuntimeError(pos, "zip() expects 2 lists");
    auto a = std::get_if<ListValue>(&args[0]);
    auto b = std::get_if<ListValue>(&args[1]);
    if (!a || !b) throw RuntimeError(pos, "zip() expects two lists");
    ListValue result;
    size_t len = std::min(a->elements.size(), b->elements.size());
    for (size_t i = 0; i < len; i++) {
      ListValue pair;
      pair.elements.push_back(std::make_shared<ValueBox>(a->elements[i]->val));
      pair.elements.push_back(std::make_shared<ValueBox>(b->elements[i]->val));
      result.elements.push_back(std::make_shared<ValueBox>(Value(std::move(pair))));
    }
    return Value(std::move(result));
  });

  registerBuiltin("enumerate", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "enumerate() expects 1 list");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "enumerate() expects a list");
    ListValue result;
    for (size_t i = 0; i < l->elements.size(); i++) {
      ListValue pair;
      pair.elements.push_back(std::make_shared<ValueBox>(Value(static_cast<int64_t>(i))));
      pair.elements.push_back(std::make_shared<ValueBox>(l->elements[i]->val));
      result.elements.push_back(std::make_shared<ValueBox>(Value(std::move(pair))));
    }
    return Value(std::move(result));
  });

  registerBuiltin("sorted", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "sorted() expects 1 list");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "sorted() expects a list");
    ListValue result = *l; // copy
    std::sort(result.elements.begin(), result.elements.end(),
      [&pos](const std::shared_ptr<ValueBox>& a, const std::shared_ptr<ValueBox>& b) {
        return asDouble(pos, a->val) < asDouble(pos, b->val);
      });
    return Value(std::move(result));
  });

  registerBuiltin("reversed", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "reversed() expects 1 arg");
    if (auto l = std::get_if<ListValue>(&args[0])) {
      ListValue result = *l;
      std::reverse(result.elements.begin(), result.elements.end());
      return Value(std::move(result));
    }
    if (auto s = std::get_if<std::string>(&args[0])) {
      std::string r = *s;
      std::reverse(r.begin(), r.end());
      return r;
    }
    throw RuntimeError(pos, "reversed() expects list or string");
  });

  registerBuiltin("flatten", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "flatten() expects 1 list");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "flatten() expects a list");
    ListValue result;
    for (auto& el : l->elements) {
      if (auto inner = std::get_if<ListValue>(&el->val)) {
        for (auto& inner_el : inner->elements)
          result.elements.push_back(std::make_shared<ValueBox>(inner_el->val));
      } else {
        result.elements.push_back(std::make_shared<ValueBox>(el->val));
      }
    }
    return Value(std::move(result));
  });

  registerBuiltin("unique", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 1) throw RuntimeError(pos, "unique() expects 1 list");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "unique() expects a list");
    ListValue result;
    for (auto& el : l->elements) {
      bool found = false;
      for (auto& existing : result.elements) {
        if (valuesEqual(existing->val, el->val)) { found = true; break; }
      }
      if (!found) result.elements.push_back(std::make_shared<ValueBox>(el->val));
    }
    return Value(std::move(result));
  });

  // ── Type checking builtins ───────────────────────
  registerBuiltin("isNull", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<std::monostate>(args[0]);
  });
  registerBuiltin("isInt", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<int64_t>(args[0]);
  });
  registerBuiltin("isFloat", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<double>(args[0]);
  });
  registerBuiltin("isStr", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<std::string>(args[0]);
  });
  registerBuiltin("isBool", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<bool>(args[0]);
  });
  registerBuiltin("isList", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<ListValue>(args[0]);
  });
  registerBuiltin("isMap", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<MapValue>(args[0]);
  });
  registerBuiltin("isFunction", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    return !args.empty() && std::holds_alternative<FuncValue>(args[0]);
  });

  // ── Assertion ────────────────────────────────────
  registerBuiltin("assert", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.empty()) throw RuntimeError(pos, "assert() expects at least 1 argument");
    if (!isTruthy(args[0])) {
      std::string msg = "Assertion failed";
      if (args.size() > 1) msg += ": " + toString(args[1]);
      throw RuntimeError(pos, msg);
    }
    return std::monostate{};
  });

  // ── Time functions ───────────────────────────────
  registerBuiltin("clock", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return static_cast<double>(ms) / 1000.0;
  });

  registerBuiltin("timestamp", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(epoch).count());
  });

  // ── JSON-like stringifier ────────────────────────
  registerBuiltin("toJSON", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (args.empty()) return std::string("null");
    std::function<std::string(const Value&)> jsonify;
    jsonify = [&jsonify](const Value& v) -> std::string {
      if (std::holds_alternative<std::monostate>(v)) return "null";
      if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
      if (auto i = std::get_if<int64_t>(&v)) return std::to_string(*i);
      if (auto d = std::get_if<double>(&v)) { std::ostringstream oss; oss << *d; return oss.str(); }
      if (auto s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
      if (auto l = std::get_if<ListValue>(&v)) {
        std::string out = "[";
        for (size_t i2 = 0; i2 < l->elements.size(); i2++) {
          if (i2) out += ", ";
          out += jsonify(l->elements[i2]->val);
        }
        return out + "]";
      }
      if (auto m = std::get_if<MapValue>(&v)) {
        std::string out = "{";
        for (size_t i2 = 0; i2 < m->entries.size(); i2++) {
          if (i2) out += ", ";
          out += "\"" + m->entries[i2].first + "\": " + jsonify(m->entries[i2].second->val);
        }
        return out + "}";
      }
      return "null";
    };
    return jsonify(args[0]);
  });

  // ── List creation helpers ────────────────────────
  registerBuiltin("list", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    ListValue lv;
    for (auto& a : args)
      lv.elements.push_back(std::make_shared<ValueBox>(a));
    return Value(std::move(lv));
  });

  registerBuiltin("repeat", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() != 2) throw RuntimeError(pos, "repeat() expects (value, count)");
    int64_t count = asInt(pos, args[1]);
    ListValue lv;
    for (int64_t i = 0; i < count; i++)
      lv.elements.push_back(std::make_shared<ValueBox>(args[0]));
    return Value(std::move(lv));
  });

  // ── String join helper ───────────────────────────
  registerBuiltin("join", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() < 1) throw RuntimeError(pos, "join() expects (list) or (list, separator)");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "join() first arg must be a list");
    std::string sep = args.size() > 1 ? toString(args[1]) : "";
    std::string result;
    for (size_t i = 0; i < l->elements.size(); i++) {
      if (i) result += sep;
      result += toString(l->elements[i]->val);
    }
    return result;
  });

  // ── Map creation helper ──────────────────────────
  registerBuiltin("map", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
    return Value(MapValue{});
  });

  // ── Functional programming ───────────────────────
  registerBuiltin("reduce", [this](Interpreter& interp, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    if (args.size() < 2) throw RuntimeError(pos, "reduce() expects (list, fn) or (list, fn, initial)");
    auto l = std::get_if<ListValue>(&args[0]);
    if (!l) throw RuntimeError(pos, "reduce() first arg must be a list");
    if (l->elements.empty()) {
      if (args.size() > 2) return args[2];
      throw RuntimeError(pos, "reduce() of empty list with no initial value");
    }
    Value acc = args.size() > 2 ? args[2] : l->elements[0]->val;
    size_t start = args.size() > 2 ? 0 : 1;
    for (size_t i = start; i < l->elements.size(); i++) {
      acc = callValue(pos, args[1], {acc, l->elements[i]->val});
    }
    return acc;
  });

  // ── Printing without newline ─────────────────────
  registerBuiltin("write", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    for (size_t i = 0; i < args.size(); i++) {
      if (i) std::cout << " ";
      std::cout << toString(args[i]);
    }
    std::cout.flush();
    return std::monostate{};
  });

  // ── Random number shortcut ───────────────────────
  registerBuiltin("random", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
    static std::mt19937 rng(std::random_device{}());
    if (args.empty()) {
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      return dist(rng);
    }
    if (args.size() == 1) {
      int64_t hi = asInt(pos, args[0]);
      std::uniform_int_distribution<int64_t> dist(0, hi - 1);
      return dist(rng);
    }
    int64_t lo = asInt(pos, args[0]);
    int64_t hi = asInt(pos, args[1]);
    std::uniform_int_distribution<int64_t> dist(lo, hi);
    return dist(rng);
  });

  // ── Hash/toString ────────────────────────────────
  registerBuiltin("hash", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
    if (args.empty()) return int64_t(0);
    return static_cast<int64_t>(std::hash<std::string>{}(toString(args[0])));
  });
}

// ═══════════════════════════════════════════════════
//  Program execution
// ═══════════════════════════════════════════════════

void Interpreter::exec(const Program& program) {
  // Phase 1: register all top-level functions and classes
  for (const auto& st : program.stmts) {
    if (auto fn = std::get_if<FnDeclStmt>(&st->node)) {
      execFnDecl(*fn);
    } else if (auto cls = std::get_if<ClassDeclStmt>(&st->node)) {
      execClassDecl(*cls);
    }
  }

  // Phase 2: execute all other statements
  for (const auto& st : program.stmts) {
    if (std::holds_alternative<FnDeclStmt>(st->node)) continue;
    if (std::holds_alternative<ClassDeclStmt>(st->node)) continue;
    execStmt(*st);
  }

  // Phase 3: call main() if exists
  if (env_->has("main")) {
    try {
      Value mainFn = env_->get("main");
      if (auto fv = std::get_if<FuncValue>(&mainFn)) {
        callFunction(SourcePos{filename_, 1, 1, 0}, *fv, {});
      }
    } catch (const ReturnSignal&) {}
  }
}

// ═══════════════════════════════════════════════════
//  Statement execution
// ═══════════════════════════════════════════════════

void Interpreter::execStmt(const Stmt& s) {
  auto visitor = [this, &s](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, BlockStmt>) {
      auto child = std::make_shared<Env>(env_);
      auto old = env_;
      env_ = child;
      try {
        for (const auto& st : node.stmts) execStmt(*st);
      } catch (...) { env_ = old; throw; }
      env_ = old;
    } else if constexpr (std::is_same_v<T, VarDeclStmt>) {
      execVarDecl(node);
    } else if constexpr (std::is_same_v<T, ExprStmt>) {
      (void)eval(*node.expr);
    } else if constexpr (std::is_same_v<T, IfStmt>) {
      execIf(node);
    } else if constexpr (std::is_same_v<T, WhileStmt>) {
      execWhile(node);
    } else if constexpr (std::is_same_v<T, DoWhileStmt>) {
      execDoWhile(node);
    } else if constexpr (std::is_same_v<T, ForStmt>) {
      execFor(node);
    } else if constexpr (std::is_same_v<T, ForInStmt>) {
      execForIn(node);
    } else if constexpr (std::is_same_v<T, MatchStmt>) {
      execMatch(node);
    } else if constexpr (std::is_same_v<T, ReturnStmt>) {
      execReturn(node);
    } else if constexpr (std::is_same_v<T, BreakStmt>) {
      throw BreakSignal{};
    } else if constexpr (std::is_same_v<T, ContinueStmt>) {
      throw ContinueSignal{};
    } else if constexpr (std::is_same_v<T, ThrowStmt>) {
      execThrow(node);
    } else if constexpr (std::is_same_v<T, TryCatchStmt>) {
      execTryCatch(node);
    } else if constexpr (std::is_same_v<T, FnDeclStmt>) {
      execFnDecl(node);
    } else if constexpr (std::is_same_v<T, ClassDeclStmt>) {
      execClassDecl(node);
    } else if constexpr (std::is_same_v<T, InterfaceDeclStmt>) {
      // Interfaces are type-checking only; no runtime behavior
    } else if constexpr (std::is_same_v<T, EnumDeclStmt>) {
      // Register enum values as constants
      for (size_t i2 = 0; i2 < node.variants.size(); i2++) {
        Value val;
        if (node.variants[i2].value)
          val = eval(*node.variants[i2].value.value());
        else
          val = static_cast<int64_t>(i2);
        env_->define(node.variants[i2].name.lexeme, std::move(val), true);
      }
    } else if constexpr (std::is_same_v<T, ImportStmt>) {
      execImport(node);
    } else if constexpr (std::is_same_v<T, ExportStmt>) {
      execExport(node);
    } else if constexpr (std::is_same_v<T, SpawnStmt>) {
      execSpawn(node);
    } else if constexpr (std::is_same_v<T, UnsafeBlock>) {
      // Execute the body normally (unsafe is a marker for the transpiler)
      execStmt(*node.body);
    }
  };
  try {
    std::visit(visitor, s.node);
  } catch (const RuntimeError&) { throw; }
    catch (const BreakSignal&) { throw; }
    catch (const ContinueSignal&) { throw; }
    catch (const ReturnSignal&) { throw; }
    catch (const ThrowSignal&) { throw; }
    catch (const std::exception& e) {
      throw RuntimeError(s.pos, e.what());
    }
}

void Interpreter::execBlock(const BlockStmt& b, std::shared_ptr<Env> scope) {
  auto old = env_;
  env_ = scope;
  try {
    for (const auto& st : b.stmts) execStmt(*st);
  } catch (...) { env_ = old; throw; }
  env_ = old;
}

void Interpreter::execVarDecl(const VarDeclStmt& v) {
  Value init = std::monostate{};
  if (v.init) init = eval(*v.init);
  env_->define(v.name.lexeme, std::move(init), v.isConst);
}

void Interpreter::execFnDecl(const FnDeclStmt& f) {
  FuncValue fv;
  fv.name = f.name.lexeme;
  for (const auto& p : f.params) {
    fv.params.push_back(p.name);
    fv.defaults.push_back(p.defaultValue ? p.defaultValue->get() : nullptr);
  }
  fv.body = f.body.get();
  fv.closure = env_;
  env_->define(f.name.lexeme, Value(std::move(fv)), false);
}

void Interpreter::execClassDecl(const ClassDeclStmt& c) {
  auto def = std::make_shared<ClassDef>();
  def->name = c.name.lexeme;

  // Resolve parent class
  if (c.superClass) {
    def->parent = env_->getClass(c.superClass->lexeme);
    if (!def->parent)
      throw RuntimeError(c.superClass->pos, "Unknown superclass: " + c.superClass->lexeme);
  }

  // Register fields with default values
  for (const auto& field : c.fields) {
    Value def_val = std::monostate{};
    if (field.init) def_val = eval(*field.init);
    def->fieldDefaults.push_back({field.name.lexeme, std::move(def_val)});
  }

  // Register methods
  for (const auto& md : c.methods) {
    FuncValue fv;
    fv.name = md.method.name.lexeme;
    for (const auto& p : md.method.params) {
      fv.params.push_back(p.name);
      fv.defaults.push_back(p.defaultValue ? p.defaultValue->get() : nullptr);
    }
    fv.body = md.method.body.get();
    fv.closure = env_;
    def->methods[fv.name] = std::move(fv);
  }

  // Constructor
  if (c.initDecl) {
    FuncValue ctor;
    ctor.name = "init";
    for (const auto& p : c.initDecl->params) {
      ctor.params.push_back(p.name);
      ctor.defaults.push_back(p.defaultValue ? p.defaultValue->get() : nullptr);
    }
    ctor.body = c.initDecl->body.get();
    ctor.closure = env_;
    def->constructor = std::move(ctor);
  }

  env_->defineClass(c.name.lexeme, def);
  // Also define the class name as a callable (constructor shorthand)
  // ClassName(args) => instantiate
  // We register a function that calls the constructor
  FuncValue ctorFn;
  ctorFn.name = c.name.lexeme;
  env_->define(c.name.lexeme, Value(std::move(ctorFn)), true);
}

void Interpreter::execIf(const IfStmt& i) {
  if (isTruthy(eval(*i.cond))) {
    execStmt(*i.thenBranch);
  } else if (i.elseBranch) {
    execStmt(*(*i.elseBranch));
  }
}

void Interpreter::execWhile(const WhileStmt& w) {
  while (isTruthy(eval(*w.cond))) {
    try { execStmt(*w.body); }
    catch (const ContinueSignal&) { continue; }
    catch (const BreakSignal&) { break; }
  }
}

void Interpreter::execDoWhile(const DoWhileStmt& d) {
  do {
    try { execStmt(*d.body); }
    catch (const ContinueSignal&) { continue; }
    catch (const BreakSignal&) { break; }
  } while (isTruthy(eval(*d.cond)));
}

void Interpreter::execFor(const ForStmt& f) {
  auto scope = std::make_shared<Env>(env_);
  auto old = env_;
  env_ = scope;
  try {
    if (f.init) execStmt(*(*f.init));
    while (true) {
      if (f.cond && !isTruthy(eval(*(*f.cond)))) break;
      try { execStmt(*f.body); }
      catch (const ContinueSignal&) { /* fallthrough to post */ }
      catch (const BreakSignal&) { break; }
      if (f.post) (void)eval(*(*f.post));
    }
  } catch (...) { env_ = old; throw; }
  env_ = old;
}

void Interpreter::execForIn(const ForInStmt& f) {
  Value iterable = eval(*f.iterable);
  auto scope = std::make_shared<Env>(env_);
  auto old = env_;
  env_ = scope;
  try {
    if (auto r = std::get_if<RangeValue>(&iterable)) {
      for (int64_t i2 = r->start; i2 < r->end; i2++) {
        env_->define(f.name.lexeme, i2, false);
        try { execStmt(*f.body); }
        catch (const ContinueSignal&) { continue; }
        catch (const BreakSignal&) { break; }
      }
    } else if (auto l = std::get_if<ListValue>(&iterable)) {
      for (auto& el : l->elements) {
        env_->define(f.name.lexeme, el->val, false);
        try { execStmt(*f.body); }
        catch (const ContinueSignal&) { continue; }
        catch (const BreakSignal&) { break; }
      }
    } else if (auto s = std::get_if<std::string>(&iterable)) {
      for (char ch : *s) {
        env_->define(f.name.lexeme, std::string(1, ch), false);
        try { execStmt(*f.body); }
        catch (const ContinueSignal&) { continue; }
        catch (const BreakSignal&) { break; }
      }
    } else if (auto m = std::get_if<MapValue>(&iterable)) {
      for (auto& entry : m->entries) {
        env_->define(f.name.lexeme, entry.first, false);
        try { execStmt(*f.body); }
        catch (const ContinueSignal&) { continue; }
        catch (const BreakSignal&) { break; }
      }
    } else {
      throw RuntimeError(f.iterable->pos, "for-in expects iterable (range, list, string, map), got " + typeName(iterable));
    }
  } catch (...) { env_ = old; throw; }
  env_ = old;
}

void Interpreter::execMatch(const MatchStmt& m) {
  Value target = eval(*m.target);
  const MatchArm* def = nullptr;
  for (const auto& arm : m.arms) {
    if (!arm.pattern) { def = &arm; continue; }
    Value pat = eval(*(*arm.pattern));
    if (valuesEqual(pat, target)) {
      execStmt(*arm.action);
      return;
    }
  }
  if (def) execStmt(*def->action);
}

void Interpreter::execReturn(const ReturnStmt& r) {
  Value v = std::monostate{};
  if (r.value) v = eval(*(*r.value));
  throw ReturnSignal{v};
}

void Interpreter::execThrow(const ThrowStmt& t) {
  Value v = eval(*t.value);
  throw ThrowSignal{v, t.kw.pos};
}

void Interpreter::execTryCatch(const TryCatchStmt& tc) {
  try {
    execStmt(*tc.tryBody);
  } catch (const ThrowSignal& ts) {
    bool handled = false;
    for (const auto& cc : tc.catches) {
      auto scope = std::make_shared<Env>(env_);
      if (cc.varName) {
        scope->define(cc.varName->lexeme, ts.value, false);
      }
      auto old = env_;
      env_ = scope;
      try { execStmt(*cc.body); }
      catch (...) { env_ = old; throw; }
      env_ = old;
      handled = true;
      break;
    }
    if (!handled) {
      if (tc.finallyBody) execStmt(*(*tc.finallyBody));
      throw;
    }
  } catch (const RuntimeError& re) {
    bool handled = false;
    for (const auto& cc : tc.catches) {
      auto scope = std::make_shared<Env>(env_);
      if (cc.varName)
        scope->define(cc.varName->lexeme, std::string(re.what()), false);
      auto old = env_;
      env_ = scope;
      try { execStmt(*cc.body); }
      catch (...) { env_ = old; throw; }
      env_ = old;
      handled = true;
      break;
    }
    if (!handled) {
      if (tc.finallyBody) execStmt(*(*tc.finallyBody));
      throw;
    }
  }
  if (tc.finallyBody) execStmt(*(*tc.finallyBody));
}

void Interpreter::execImport(const ImportStmt& imp) {
  // Build module name from path
  std::string moduleName;
  for (size_t i2 = 0; i2 < imp.path.size(); i2++) {
    if (i2) moduleName += ".";
    moduleName += imp.path[i2].lexeme;
  }

  if (importedModules_.count(moduleName)) return;
  importedModules_[moduleName] = true;

  // Built-in math module
  if (moduleName == "math") {
    auto mathEnv = std::make_shared<Env>(env_);
    env_->define("math", std::monostate{}, true); // placeholder
    // Register math functions as global builtins with math. prefix
    BuiltinFn mathBuiltin;

    registerBuiltin("math.sqrt", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.sqrt expects 1 arg");
      return std::sqrt(asDouble(pos, args[0]));
    });
    registerBuiltin("math.pow", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "math.pow expects 2 args");
      return std::pow(asDouble(pos, args[0]), asDouble(pos, args[1]));
    });
    registerBuiltin("math.abs", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.abs expects 1 arg");
      return std::abs(asDouble(pos, args[0]));
    });
    registerBuiltin("math.floor", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.floor expects 1 arg");
      return std::floor(asDouble(pos, args[0]));
    });
    registerBuiltin("math.ceil", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.ceil expects 1 arg");
      return std::ceil(asDouble(pos, args[0]));
    });
    registerBuiltin("math.round", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.round expects 1 arg");
      return std::round(asDouble(pos, args[0]));
    });
    registerBuiltin("math.sin", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.sin expects 1 arg");
      return std::sin(asDouble(pos, args[0]));
    });
    registerBuiltin("math.cos", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.cos expects 1 arg");
      return std::cos(asDouble(pos, args[0]));
    });
    registerBuiltin("math.tan", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.tan expects 1 arg");
      return std::tan(asDouble(pos, args[0]));
    });
    registerBuiltin("math.log", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() == 1) return std::log(asDouble(pos, args[0]));
      if (args.size() == 2) return std::log(asDouble(pos, args[0])) / std::log(asDouble(pos, args[1]));
      throw RuntimeError(pos, "math.log expects 1-2 args");
    });
    registerBuiltin("math.random", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      static std::mt19937 rng(std::random_device{}());
      static std::uniform_real_distribution<double> dist(0.0, 1.0);
      return dist(rng);
    });
    registerBuiltin("math.randomInt", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "math.randomInt expects 2 args");
      static std::mt19937 rng(std::random_device{}());
      int64_t lo = asInt(pos, args[0]);
      int64_t hi = asInt(pos, args[1]);
      std::uniform_int_distribution<int64_t> dist(lo, hi);
      return dist(rng);
    });
    registerBuiltin("math.factorial", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.factorial expects 1 arg");
      int64_t n = asInt(pos, args[0]);
      if (n < 0) throw RuntimeError(pos, "factorial of negative number");
      int64_t result = 1;
      for (int64_t i2 = 2; i2 <= n; i2++) result *= i2;
      return result;
    });
    registerBuiltin("math.isPrime", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "math.isPrime expects 1 arg");
      int64_t n = asInt(pos, args[0]);
      if (n < 2) return false;
      for (int64_t i2 = 2; i2 * i2 <= n; i2++)
        if (n % i2 == 0) return false;
      return true;
    });
    registerBuiltin("math.gcd", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "math.gcd expects 2 args");
      int64_t a = std::abs(asInt(pos, args[0]));
      int64_t b = std::abs(asInt(pos, args[1]));
      while (b) { int64_t t = b; b = a % b; a = t; }
      return a;
    });
    registerBuiltin("math.lcm", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "math.lcm expects 2 args");
      int64_t a = std::abs(asInt(pos, args[0]));
      int64_t b = std::abs(asInt(pos, args[1]));
      if (a == 0 || b == 0) return int64_t(0);
      int64_t g = a;
      int64_t t = b;
      while (t) { int64_t tmp = t; t = g % t; g = tmp; }
      return a / g * b;
    });
    // Constants
    env_->define("math.PI", 3.14159265358979323846, true);
    env_->define("math.E", 2.71828182845904523536, true);
    return;
  }

  // Built-in io module
  if (moduleName == "io") {
    importedModules_["io"] = true;
    registerBuiltin("io.readFile", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "io.readFile expects 1 arg");
      auto path = std::get_if<std::string>(&args[0]);
      if (!path) throw RuntimeError(pos, "io.readFile expects string path");
      std::ifstream f(*path);
      if (!f) throw RuntimeError(pos, "File not found: " + *path);
      std::ostringstream ss; ss << f.rdbuf();
      return ss.str();
    });
    registerBuiltin("io.writeFile", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "io.writeFile expects 2 args");
      auto path = std::get_if<std::string>(&args[0]);
      if (!path) throw RuntimeError(pos, "io.writeFile expects string path");
      std::ofstream f(*path);
      if (!f) throw RuntimeError(pos, "Cannot write file: " + *path);
      f << toString(args[1]);
      return std::monostate{};
    });
    registerBuiltin("io.appendFile", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "io.appendFile expects 2 args");
      auto path = std::get_if<std::string>(&args[0]);
      if (!path) throw RuntimeError(pos, "io.appendFile expects string path");
      std::ofstream f(*path, std::ios::app);
      if (!f) throw RuntimeError(pos, "Cannot append file: " + *path);
      f << toString(args[1]);
      return std::monostate{};
    });
    registerBuiltin("io.exists", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "io.exists expects 1 arg");
      auto path = std::get_if<std::string>(&args[0]);
      if (!path) throw RuntimeError(pos, "io.exists expects string path");
      return std::filesystem::exists(*path);
    });
    registerBuiltin("io.readLines", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "io.readLines expects 1 arg");
      auto path = std::get_if<std::string>(&args[0]);
      if (!path) throw RuntimeError(pos, "io.readLines expects string path");
      std::ifstream f(*path);
      if (!f) throw RuntimeError(pos, "File not found: " + *path);
      ListValue lv;
      std::string line;
      while (std::getline(f, line))
        lv.elements.push_back(std::make_shared<ValueBox>(Value(line)));
      return Value(std::move(lv));
    });
    return;
  }

  // Built-in os module
  if (moduleName == "os") {
    importedModules_["os"] = true;
    registerBuiltin("os.exec", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "os.exec expects 1 arg");
      auto cmd = std::get_if<std::string>(&args[0]);
      if (!cmd) throw RuntimeError(pos, "os.exec expects string cmd");
      return static_cast<int64_t>(std::system(cmd->c_str()));
    });
    registerBuiltin("os.env", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "os.env expects 1 arg");
      auto name = std::get_if<std::string>(&args[0]);
      if (!name) throw RuntimeError(pos, "os.env expects string name");
      const char* val = std::getenv(name->c_str());
      return val ? std::string(val) : Value(std::monostate{});
    });
    registerBuiltin("os.platform", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
#ifdef _WIN32
      return std::string("windows");
#elif __APPLE__
      return std::string("macos");
#else
      return std::string("linux");
#endif
    });
    registerBuiltin("os.cwd", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return std::filesystem::current_path().string();
    });
    registerBuiltin("os.sleep", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "os.sleep expects 1 arg (ms)");
      int64_t ms = asInt(pos, args[0]);
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      return std::monostate{};
    });
    return;
  }

  // Built-in time module
  if (moduleName == "time") {
    importedModules_["time"] = true;
    registerBuiltin("time.now", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      auto now = std::chrono::system_clock::now();
      auto epoch = now.time_since_epoch();
      return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count()) / 1000.0;
    });
    registerBuiltin("time.millis", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      auto now = std::chrono::high_resolution_clock::now();
      return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    });
    registerBuiltin("time.sleep", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 1) throw RuntimeError(pos, "time.sleep expects 1 arg (ms)");
      int64_t ms = asInt(pos, args[0]);
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      return std::monostate{};
    });
    registerBuiltin("time.measure", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      auto now = std::chrono::high_resolution_clock::now();
      return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()) / 1000000.0;
    });
    return;
  }

  // Built-in string module  
  if (moduleName == "string") {
    importedModules_["string"] = true;
    registerBuiltin("string.ascii_letters", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return std::string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    });
    registerBuiltin("string.digits", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return std::string("0123456789");
    });
    registerBuiltin("string.repeat", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "string.repeat expects 2 args");
      std::string s = toString(args[0]);
      int64_t n = asInt(pos, args[1]);
      std::string result;
      for (int64_t i = 0; i < n; i++) result += s;
      return result;
    });
    registerBuiltin("string.format", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty()) return std::string("");
      std::string fmt = toString(args[0]);
      std::string result;
      size_t argIdx = 1;
      for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
          if (argIdx < args.size()) result += toString(args[argIdx++]);
          else result += "{}";
          i++;
        } else {
          result += fmt[i];
        }
      }
      return result;
    });
    return;
  }

  // Built-in json module
  if (moduleName == "json") {
    importedModules_["json"] = true;
    registerBuiltin("json.stringify", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      if (args.empty()) return std::string("null");
      std::function<std::string(const Value&)> jsonify;
      jsonify = [&jsonify](const Value& v) -> std::string {
        if (std::holds_alternative<std::monostate>(v)) return "null";
        if (auto b = std::get_if<bool>(&v)) return *b ? "true" : "false";
        if (auto i = std::get_if<int64_t>(&v)) return std::to_string(*i);
        if (auto d = std::get_if<double>(&v)) { std::ostringstream oss; oss << *d; return oss.str(); }
        if (auto s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
        if (auto l = std::get_if<ListValue>(&v)) {
          std::string out = "[";
          for (size_t i2 = 0; i2 < l->elements.size(); i2++) {
            if (i2) out += ", ";
            out += jsonify(l->elements[i2]->val);
          }
          return out + "]";
        }
        if (auto m = std::get_if<MapValue>(&v)) {
          std::string out = "{";
          for (size_t i2 = 0; i2 < m->entries.size(); i2++) {
            if (i2) out += ", ";
            out += "\"" + m->entries[i2].first + "\": " + jsonify(m->entries[i2].second->val);
          }
          return out + "}";
        }
        return "null";
      };
      return jsonify(args[0]);
    });
    return;
  }

  // Built-in collections module
  if (moduleName == "collections") {
    importedModules_["collections"] = true;
    registerBuiltin("collections.Stack", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return Value(ListValue{});
    });
    registerBuiltin("collections.Queue", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return Value(ListValue{});
    });
    return;
  }

  // Try to load as file: ./module or module.vibe
  std::string filePath;
  for (auto& part : imp.path) {
    if (!filePath.empty()) filePath += "/";
    filePath += part.lexeme;
  }
  filePath += ".vibe";
  std::ifstream f(filePath);
  if (!f) {
    // Also try without .vibe
    filePath = "";
    for (auto& part : imp.path) {
      if (!filePath.empty()) filePath += "/";
      filePath += part.lexeme;
    }
    f.open(filePath);
    if (!f) {
      // Not a fatal error for unknown modules
      return;
    }
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string src = ss.str();
  Lexer lx(filePath, src);
  Parser ps(std::move(lx));
  Program prog = ps.parseProgram();
  exec(prog);
}

void Interpreter::execExport(const ExportStmt& exp) {
  execStmt(*exp.decl);
}

void Interpreter::execSpawn(const SpawnStmt& sp) {
  // For the interpreter, just execute synchronously
  // (real concurrency would need threading, which is complex for a tree-walker)
  execStmt(*sp.body);
}

// ═══════════════════════════════════════════════════
//  Expression evaluation
// ═══════════════════════════════════════════════════

Value Interpreter::eval(const Expr& e) {
  auto visitor = [this, &e](const auto& node) -> Value {
    using T = std::decay_t<decltype(node)>;

    if constexpr (std::is_same_v<T, LiteralExpr>) {
      const Token& t = node.tok;
      switch (t.type) {
      case TokenType::IntLiteral: return static_cast<int64_t>(std::stoll(t.lexeme));
      case TokenType::FloatLiteral: return std::stod(t.lexeme);
      case TokenType::StringLiteral: {
        if (t.lexeme.size() >= 2) {
          std::string raw = t.lexeme.substr(1, t.lexeme.size() - 2);
          // Process escape sequences
          std::string result;
          for (size_t i2 = 0; i2 < raw.size(); i2++) {
            if (raw[i2] == '\\' && i2 + 1 < raw.size()) {
              switch (raw[i2 + 1]) {
              case 'n': result += '\n'; i2++; break;
              case 't': result += '\t'; i2++; break;
              case 'r': result += '\r'; i2++; break;
              case '\\': result += '\\'; i2++; break;
              case '"': result += '"'; i2++; break;
              case '0': result += '\0'; i2++; break;
              default: result += raw[i2]; break;
              }
            } else {
              result += raw[i2];
            }
          }
          return result;
        }
        return std::string{};
      }
      case TokenType::CharLiteral: {
        if (t.lexeme.size() >= 3) {
          if (t.lexeme[1] == '\\' && t.lexeme.size() >= 4) {
            switch (t.lexeme[2]) {
            case 'n': return std::string(1, '\n');
            case 't': return std::string(1, '\t');
            case 'r': return std::string(1, '\r');
            case '0': return std::string(1, '\0');
            default: return std::string(1, t.lexeme[2]);
            }
          }
          return std::string(1, t.lexeme[1]);
        }
        return std::string{};
      }
      case TokenType::KwTrue: return true;
      case TokenType::KwFalse: return false;
      case TokenType::KwNull: return std::monostate{};
      default: break;
      }
      throw RuntimeError(e.pos, "Invalid literal");
    }

    else if constexpr (std::is_same_v<T, IdentExpr>) {
      return env_->get(node.name.lexeme);
    }

    else if constexpr (std::is_same_v<T, SelfExpr>) {
      return env_->get("self");
    }

    else if constexpr (std::is_same_v<T, SuperExpr>) {
      return env_->get("self"); // super refers to parent methods, handled in call
    }

    else if constexpr (std::is_same_v<T, GroupExpr>) {
      return eval(*node.inner);
    }

    else if constexpr (std::is_same_v<T, UnaryExpr>) {
      Value rhs = eval(*node.rhs);
      return unaryOp(e.pos, node.op, rhs);
    }

    else if constexpr (std::is_same_v<T, BinaryExpr>) {
      Value lhs = eval(*node.lhs);
      Value rhs = eval(*node.rhs);
      return binaryOp(e.pos, node.op, lhs, rhs);
    }

    else if constexpr (std::is_same_v<T, TernaryExpr>) {
      return isTruthy(eval(*node.cond)) ? eval(*node.thenExpr) : eval(*node.elseExpr);
    }

    else if constexpr (std::is_same_v<T, AssignExpr>) {
      Value rhs = eval(*node.value);

      // Determine target
      if (auto* id = std::get_if<IdentExpr>(&node.target->node)) {
        if (node.op.type != TokenType::Equal) {
          Value cur = env_->get(id->name.lexeme);
          Token synth = node.op;
          if (synth.type == TokenType::PlusEqual) synth.type = TokenType::Plus;
          if (synth.type == TokenType::MinusEqual) synth.type = TokenType::Minus;
          if (synth.type == TokenType::StarEqual) synth.type = TokenType::Star;
          if (synth.type == TokenType::SlashEqual) synth.type = TokenType::Slash;
          if (synth.type == TokenType::PercentEqual) synth.type = TokenType::Percent;
          rhs = binaryOp(e.pos, synth, cur, rhs);
        }
        if (!env_->assign(id->name.lexeme, rhs))
          throw RuntimeError(e.pos, "Cannot assign to '" + id->name.lexeme + "' (const or undefined)");
        return rhs;
      }

      // Member assignment: obj.field = val
      if (auto* mem = std::get_if<MemberExpr>(&node.target->node)) {
        Value obj = eval(*mem->object);
        if (auto* ov = std::get_if<ObjectValue>(&obj)) {
          if (node.op.type != TokenType::Equal) {
            Value cur = (*ov->fields)[mem->member.lexeme]->val;
            Token synth = node.op;
            if (synth.type == TokenType::PlusEqual) synth.type = TokenType::Plus;
            if (synth.type == TokenType::MinusEqual) synth.type = TokenType::Minus;
            if (synth.type == TokenType::StarEqual) synth.type = TokenType::Star;
            if (synth.type == TokenType::SlashEqual) synth.type = TokenType::Slash;
            if (synth.type == TokenType::PercentEqual) synth.type = TokenType::Percent;
            rhs = binaryOp(e.pos, synth, cur, rhs);
          }
          (*ov->fields)[mem->member.lexeme] = std::make_shared<ValueBox>(rhs);
          return rhs;
        }
        throw RuntimeError(e.pos, "Cannot set member on " + typeName(obj));
      }

      // Index assignment: list[i] = val or map[key] = val
      if (auto* idx = std::get_if<IndexExpr>(&node.target->node)) {
        Value obj = eval(*idx->object);
        Value index = eval(*idx->index);
        if (auto* lv = std::get_if<ListValue>(&obj)) {
          int64_t i2 = asInt(e.pos, index);
          if (i2 < 0 || i2 >= static_cast<int64_t>(lv->elements.size()))
            throw RuntimeError(e.pos, "Index out of bounds: " + std::to_string(i2));
          if (node.op.type != TokenType::Equal) {
            Value cur = lv->elements[i2]->val;
            Token synth = node.op;
            if (synth.type == TokenType::PlusEqual) synth.type = TokenType::Plus;
            if (synth.type == TokenType::MinusEqual) synth.type = TokenType::Minus;
            if (synth.type == TokenType::StarEqual) synth.type = TokenType::Star;
            if (synth.type == TokenType::SlashEqual) synth.type = TokenType::Slash;
            if (synth.type == TokenType::PercentEqual) synth.type = TokenType::Percent;
            rhs = binaryOp(e.pos, synth, cur, rhs);
          }
          lv->elements[i2]->val = rhs;
          return rhs;
        }
        if (auto* mv = std::get_if<MapValue>(&obj)) {
          std::string key = toString(index);
          for (auto& entry : mv->entries) {
            if (entry.first == key) {
              entry.second->val = rhs;
              return rhs;
            }
          }
          mv->entries.push_back({key, std::make_shared<ValueBox>(rhs)});
          return rhs;
        }
        throw RuntimeError(e.pos, "Cannot index-assign on " + typeName(obj));
      }

      throw RuntimeError(e.pos, "Invalid assignment target");
    }

    else if constexpr (std::is_same_v<T, ArrayExpr>) {
      ListValue lv;
      for (const auto& el : node.elements)
        lv.elements.push_back(std::make_shared<ValueBox>(eval(*el)));
      return Value(std::move(lv));
    }

    else if constexpr (std::is_same_v<T, MapExpr>) {
      MapValue mv;
      for (const auto& entry : node.entries) {
        std::string key = toString(eval(*entry.key));
        Value val = eval(*entry.value);
        mv.entries.push_back({key, std::make_shared<ValueBox>(val)});
      }
      return Value(std::move(mv));
    }

    else if constexpr (std::is_same_v<T, IndexExpr>) {
      Value obj = eval(*node.object);
      Value index = eval(*node.index);
      if (auto* lv = std::get_if<ListValue>(&obj)) {
        int64_t i2 = asInt(e.pos, index);
        if (i2 < 0) i2 += static_cast<int64_t>(lv->elements.size());
        if (i2 < 0 || i2 >= static_cast<int64_t>(lv->elements.size()))
          throw RuntimeError(e.pos, "Index out of bounds: " + std::to_string(i2));
        return lv->elements[i2]->val;
      }
      if (auto* sv = std::get_if<std::string>(&obj)) {
        int64_t i2 = asInt(e.pos, index);
        if (i2 < 0) i2 += static_cast<int64_t>(sv->size());
        if (i2 < 0 || i2 >= static_cast<int64_t>(sv->size()))
          throw RuntimeError(e.pos, "String index out of bounds");
        return std::string(1, (*sv)[i2]);
      }
      if (auto* mv = std::get_if<MapValue>(&obj)) {
        std::string key = toString(index);
        for (auto& entry : mv->entries) {
          if (entry.first == key) return entry.second->val;
        }
        return std::monostate{}; // key not found → null
      }
      throw RuntimeError(e.pos, "Cannot index " + typeName(obj));
    }

    else if constexpr (std::is_same_v<T, MemberExpr>) {
      Value obj = eval(*node.object);
      return memberGet(e.pos, obj, node.member.lexeme);
    }

    else if constexpr (std::is_same_v<T, CallExpr>) {
      // Check if callee is a member expression → method call
      if (auto* mem = std::get_if<MemberExpr>(&node.callee->node)) {
        std::vector<Value> args;
        for (const auto& a : node.args) args.push_back(eval(*a));

        // If the object is a simple variable, get a reference so mutating
        // methods (push, set, sort, etc.) modify the original value.
        if (auto* ident = std::get_if<IdentExpr>(&mem->object->node)) {
          Value* ref = env_->getRef(ident->name.lexeme);
          if (ref) {
            return memberCall(e.pos, *ref, mem->member.lexeme, args);
          }
        }
        // For member chains (e.g. self.field.method()) or temporaries,
        // work on the field pointer if it's an object field access.
        if (auto* outerMem = std::get_if<MemberExpr>(&mem->object->node)) {
          Value outerObj = eval(*outerMem->object);
          if (auto* ov = std::get_if<ObjectValue>(&outerObj)) {
            auto fit = ov->fields->find(outerMem->member.lexeme);
            if (fit != ov->fields->end()) {
              return memberCall(e.pos, fit->second->val, mem->member.lexeme, args);
            }
          }
        }
        // Fallback: eval to local copy (mutations won't persist)
        Value obj = eval(*mem->object);
        return memberCall(e.pos, obj, mem->member.lexeme, args);
      }

      // Check for super() call — invoke parent class constructor
      if (std::get_if<SuperExpr>(&node.callee->node)) {
        // Get the current self
        Value selfVal = env_->get("self");
        auto* ov = std::get_if<ObjectValue>(&selfVal);
        if (!ov) throw RuntimeError(e.pos, "super() called outside of class constructor");
        // Find parent class constructor
        auto cls = ov->classDef;
        if (!cls || !cls->parent) throw RuntimeError(e.pos, "No parent class for super()");
        if (cls->parent->constructor) {
          std::vector<Value> args;
          for (const auto& a : node.args) args.push_back(eval(*a));
          // Call parent constructor with current self to initialize parent fields
          Value* selfRef = env_->getRef("self");
          auto* selfOv = selfRef ? std::get_if<ObjectValue>(selfRef) : nullptr;
          callFunction(e.pos, *cls->parent->constructor, args, selfOv);
        }
        return std::monostate{};
      }

      // Check for ClassName() constructor
      if (auto* id = std::get_if<IdentExpr>(&node.callee->node)) {
        // Check if it's a class name
        auto cls = env_->getClass(id->name.lexeme);
        if (cls) {
          std::vector<Value> args;
          for (const auto& a : node.args) args.push_back(eval(*a));
          return instantiate(e.pos, id->name.lexeme, args);
        }
        // Check if it's a builtin (dotted names handled separately)
        auto bit = builtins_.find(id->name.lexeme);
        if (bit != builtins_.end()) {
          std::vector<Value> args;
          for (const auto& a : node.args) args.push_back(eval(*a));
          return bit->second(*this, e.pos, args);
        }
      }

      // Generic callee evaluation
      Value callee = eval(*node.callee);
      std::vector<Value> args;
      for (const auto& a : node.args) args.push_back(eval(*a));
      return callValue(e.pos, callee, args);
    }

    else if constexpr (std::is_same_v<T, NewExpr>) {
      std::vector<Value> args;
      for (const auto& a : node.args) args.push_back(eval(*a));
      return instantiate(e.pos, node.className.lexeme, args);
    }

    else if constexpr (std::is_same_v<T, LambdaExpr>) {
      FuncValue fv;
      fv.name = "<lambda>";
      fv.params = node.params;
      for (size_t i2 = 0; i2 < node.params.size(); i2++)
        fv.defaults.push_back(nullptr);
      if (node.bodyBlock)
        fv.body = node.bodyBlock.get();
      if (node.bodyExpr)
        fv.lambdaBodyExpr = node.bodyExpr.get();
      fv.closure = env_;
      return Value(std::move(fv));
    }

    else if constexpr (std::is_same_v<T, AwaitExpr>) {
      // In interpreter mode, await just evaluates the expression
      return eval(*node.expr);
    }

    else if constexpr (std::is_same_v<T, SpreadExpr>) {
      throw RuntimeError(e.pos, "Spread operator not supported in this context");
    }

    return Value(std::monostate{});
  };

  try {
    return std::visit(visitor, e.node);
  } catch (const RuntimeError&) { throw; }
    catch (const ReturnSignal&) { throw; }
    catch (const ThrowSignal&) { throw; }
    catch (const std::exception& ex) {
      throw RuntimeError(e.pos, ex.what());
    }
}

// ═══════════════════════════════════════════════════
//  Function calling
// ═══════════════════════════════════════════════════

Value Interpreter::callValue(const SourcePos& pos, const Value& callee,
                             const std::vector<Value>& args) {
  if (auto* fv = std::get_if<FuncValue>(&callee)) {
    return callFunction(pos, *fv, args);
  }
  throw RuntimeError(pos, "Value is not callable: " + typeName(callee));
}

Value Interpreter::callFunction(const SourcePos& pos, const FuncValue& fn,
                                const std::vector<Value>& args,
                                ObjectValue* self) {
  auto scope = std::make_shared<Env>(fn.closure ? fn.closure : env_);

  // Bind self if provided
  if (self) scope->define("self", Value(*self), false);

  // Bind parameters
  for (size_t i2 = 0; i2 < fn.params.size(); i2++) {
    if (i2 < args.size()) {
      scope->define(fn.params[i2].lexeme, args[i2], false);
    } else if (i2 < fn.defaults.size() && fn.defaults[i2]) {
      scope->define(fn.params[i2].lexeme, eval(*fn.defaults[i2]), false);
    } else {
      scope->define(fn.params[i2].lexeme, std::monostate{}, false);
    }
  }

  auto old = env_;
  env_ = scope;

  try {
    // Lambda with expression body
    if (fn.lambdaBodyExpr) {
      Value result = eval(*fn.lambdaBodyExpr);
      env_ = old;
      return result;
    }

    // Regular function with block body
    if (fn.body) {
      execStmt(*fn.body);
    }
  } catch (const ReturnSignal& rs) {
    // If self was modified, update the parent env's copy
    if (self) {
      try {
        Value updatedSelf = scope->get("self");
        if (auto* ov = std::get_if<ObjectValue>(&updatedSelf))
          *self = *ov;
      } catch (...) {}
    }
    env_ = old;
    return rs.value;
  } catch (...) {
    env_ = old;
    throw;
  }

  // If self was modified, update
  if (self) {
    try {
      Value updatedSelf = scope->get("self");
      if (auto* ov = std::get_if<ObjectValue>(&updatedSelf))
        *self = *ov;
    } catch (...) {}
  }

  env_ = old;
  return std::monostate{};
}

Value Interpreter::instantiate(const SourcePos& pos, const std::string& className,
                               const std::vector<Value>& args) {
  auto cls = env_->getClass(className);
  if (!cls) throw RuntimeError(pos, "Unknown class: " + className);

  ObjectValue obj;
  obj.className = className;
  obj.fields = std::make_shared<std::unordered_map<std::string, std::shared_ptr<ValueBox>>>();
  obj.classDef = cls;

  // Initialize fields from parent chain
  std::function<void(const std::shared_ptr<ClassDef>&)> initFields;
  initFields = [&](const std::shared_ptr<ClassDef>& def) {
    if (def->parent) initFields(def->parent);
    for (auto& [name, defaultVal] : def->fieldDefaults) {
      (*obj.fields)[name] = std::make_shared<ValueBox>(defaultVal);
    }
  };
  initFields(cls);

  // Call constructor if exists
  if (cls->constructor) {
    callFunction(pos, *cls->constructor, args, &obj);
  }

  return Value(std::move(obj));
}

// ═══════════════════════════════════════════════════
//  Member access and method calls
// ═══════════════════════════════════════════════════

Value Interpreter::memberGet(const SourcePos& pos, const Value& obj,
                             const std::string& member) {
  // Object field access
  if (auto* ov = std::get_if<ObjectValue>(&obj)) {
    auto it = ov->fields->find(member);
    if (it != ov->fields->end()) return it->second->val;

    // Check methods on class
    auto cls = ov->classDef;
    while (cls) {
      auto mit = cls->methods.find(member);
      if (mit != cls->methods.end()) return Value(mit->second);
      cls = cls->parent;
    }
    throw RuntimeError(pos, "No member '" + member + "' on " + ov->className);
  }

  // String properties
  if (auto* sv = std::get_if<std::string>(&obj)) {
    if (member == "length") return static_cast<int64_t>(sv->size());
    throw RuntimeError(pos, "String has no property '" + member + "' (did you mean to call a method?)");
  }

  // List properties
  if (auto* lv = std::get_if<ListValue>(&obj)) {
    if (member == "length") return static_cast<int64_t>(lv->elements.size());
    if (member == "isEmpty") return lv->elements.empty();
    throw RuntimeError(pos, "List has no property '" + member + "'");
  }

  // Map properties
  if (auto* mv = std::get_if<MapValue>(&obj)) {
    if (member == "length") return static_cast<int64_t>(mv->entries.size());
    // Also check if it's a key access
    for (auto& entry : mv->entries) {
      if (entry.first == member) return entry.second->val;
    }
    return std::monostate{}; // null for missing key
  }

  // Module dotted access (e.g., math.PI resolved as math.PI in env)
  // This is handled via CallExpr for functions, but for constants:
  throw RuntimeError(pos, "Cannot access member '" + member + "' on " + typeName(obj));
}

Value Interpreter::memberCall(const SourcePos& pos, Value& obj,
                              const std::string& member,
                              const std::vector<Value>& args) {
  // ── String methods ─────────────────────────
  if (auto* sv = std::get_if<std::string>(&obj)) {
    if (member == "length") return static_cast<int64_t>(sv->size());
    if (member == "upper") {
      std::string r = *sv;
      std::transform(r.begin(), r.end(), r.begin(), ::toupper);
      return r;
    }
    if (member == "lower") {
      std::string r = *sv;
      std::transform(r.begin(), r.end(), r.begin(), ::tolower);
      return r;
    }
    if (member == "trim") {
      std::string r = *sv;
      r.erase(0, r.find_first_not_of(" \t\n\r"));
      r.erase(r.find_last_not_of(" \t\n\r") + 1);
      return r;
    }
    if (member == "split") {
      std::string delim = " ";
      if (!args.empty()) delim = toString(args[0]);
      ListValue lv;
      size_t start = 0;
      while (start < sv->size()) {
        size_t found = sv->find(delim, start);
        if (found == std::string::npos) {
          lv.elements.push_back(std::make_shared<ValueBox>(Value(sv->substr(start))));
          break;
        }
        lv.elements.push_back(std::make_shared<ValueBox>(Value(sv->substr(start, found - start))));
        start = found + delim.size();
      }
      return Value(std::move(lv));
    }
    if (member == "contains") {
      if (args.empty()) throw RuntimeError(pos, "contains() expects 1 arg");
      return sv->find(toString(args[0])) != std::string::npos;
    }
    if (member == "startsWith") {
      if (args.empty()) throw RuntimeError(pos, "startsWith() expects 1 arg");
      std::string prefix = toString(args[0]);
      return sv->substr(0, prefix.size()) == prefix;
    }
    if (member == "endsWith") {
      if (args.empty()) throw RuntimeError(pos, "endsWith() expects 1 arg");
      std::string suffix = toString(args[0]);
      if (suffix.size() > sv->size()) return false;
      return sv->substr(sv->size() - suffix.size()) == suffix;
    }
    if (member == "replace") {
      if (args.size() < 2) throw RuntimeError(pos, "replace() expects 2 args");
      std::string from = toString(args[0]);
      std::string to = toString(args[1]);
      std::string r = *sv;
      size_t start2 = 0;
      while ((start2 = r.find(from, start2)) != std::string::npos) {
        r.replace(start2, from.size(), to);
        start2 += to.size();
      }
      return r;
    }
    if (member == "slice") {
      if (args.empty()) throw RuntimeError(pos, "slice() expects at least 1 arg");
      int64_t from = asInt(pos, args[0]);
      int64_t to = args.size() > 1 ? asInt(pos, args[1]) : static_cast<int64_t>(sv->size());
      if (from < 0) from += sv->size();
      if (to < 0) to += sv->size();
      if (from < 0) from = 0;
      if (to > static_cast<int64_t>(sv->size())) to = sv->size();
      if (from >= to) return std::string("");
      return sv->substr(from, to - from);
    }
    if (member == "indexOf") {
      if (args.empty()) throw RuntimeError(pos, "indexOf() expects 1 arg");
      auto found = sv->find(toString(args[0]));
      return found == std::string::npos ? int64_t(-1) : static_cast<int64_t>(found);
    }
    if (member == "repeat") {
      if (args.empty()) throw RuntimeError(pos, "repeat() expects 1 arg");
      int64_t count = asInt(pos, args[0]);
      std::string r;
      for (int64_t i2 = 0; i2 < count; i2++) r += *sv;
      return r;
    }
    if (member == "chars") {
      ListValue lv;
      for (char ch : *sv)
        lv.elements.push_back(std::make_shared<ValueBox>(Value(std::string(1, ch))));
      return Value(std::move(lv));
    }
    if (member == "toInt") {
      try { return static_cast<int64_t>(std::stoll(*sv)); }
      catch (...) { throw RuntimeError(pos, "Cannot convert to int: " + *sv); }
    }
    if (member == "toFloat") {
      try { return std::stod(*sv); }
      catch (...) { throw RuntimeError(pos, "Cannot convert to float: " + *sv); }
    }
    if (member == "padStart") {
      if (args.empty()) throw RuntimeError(pos, "padStart() expects at least 1 arg");
      int64_t targetLen = asInt(pos, args[0]);
      std::string pad = args.size() > 1 ? toString(args[1]) : " ";
      std::string r = *sv;
      while (static_cast<int64_t>(r.size()) < targetLen)
        r = pad + r;
      return r;
    }
    if (member == "padEnd") {
      if (args.empty()) throw RuntimeError(pos, "padEnd() expects at least 1 arg");
      int64_t targetLen = asInt(pos, args[0]);
      std::string pad = args.size() > 1 ? toString(args[1]) : " ";
      std::string r = *sv;
      while (static_cast<int64_t>(r.size()) < targetLen)
        r += pad;
      return r;
    }
    if (member == "reverse") {
      std::string r = *sv;
      std::reverse(r.begin(), r.end());
      return r;
    }
    if (member == "charAt") {
      if (args.empty()) throw RuntimeError(pos, "charAt() expects 1 arg");
      int64_t idx = asInt(pos, args[0]);
      if (idx < 0) idx += sv->size();
      if (idx < 0 || idx >= static_cast<int64_t>(sv->size()))
        throw RuntimeError(pos, "charAt() index out of bounds");
      return std::string(1, (*sv)[idx]);
    }
    if (member == "isEmpty") return sv->empty();
    if (member == "isDigit") {
      if (sv->empty()) return false;
      for (char c : *sv) if (!std::isdigit(c)) return false;
      return true;
    }
    if (member == "isAlpha") {
      if (sv->empty()) return false;
      for (char c : *sv) if (!std::isalpha(c)) return false;
      return true;
    }
    if (member == "count") {
      if (args.empty()) throw RuntimeError(pos, "count() expects 1 arg");
      std::string sub = toString(args[0]);
      int64_t cnt = 0;
      size_t pos2 = 0;
      while ((pos2 = sv->find(sub, pos2)) != std::string::npos) { cnt++; pos2 += sub.size(); }
      return cnt;
    }
    if (member == "trimStart") {
      std::string r = *sv;
      r.erase(0, r.find_first_not_of(" \t\n\r"));
      return r;
    }
    if (member == "trimEnd") {
      std::string r = *sv;
      r.erase(r.find_last_not_of(" \t\n\r") + 1);
      return r;
    }
    throw RuntimeError(pos, "String has no method '" + member + "'");
  }

  // ── List methods ─────────────────────────
  if (auto* lv = std::get_if<ListValue>(&obj)) {
    if (member == "add" || member == "push") {
      if (args.empty()) throw RuntimeError(pos, "add() expects 1 arg");
      lv->elements.push_back(std::make_shared<ValueBox>(args[0]));
      return std::monostate{};
    }
    if (member == "pop" || member == "removeLast") {
      if (lv->elements.empty()) throw RuntimeError(pos, "pop() on empty list");
      Value last = lv->elements.back()->val;
      lv->elements.pop_back();
      return last;
    }
    if (member == "length") return static_cast<int64_t>(lv->elements.size());
    if (member == "isEmpty") return lv->elements.empty();
    if (member == "contains") {
      if (args.empty()) throw RuntimeError(pos, "contains() expects 1 arg");
      for (auto& el : lv->elements)
        if (valuesEqual(el->val, args[0])) return true;
      return false;
    }
    if (member == "indexOf") {
      if (args.empty()) throw RuntimeError(pos, "indexOf() expects 1 arg");
      for (size_t i2 = 0; i2 < lv->elements.size(); i2++)
        if (valuesEqual(lv->elements[i2]->val, args[0])) return static_cast<int64_t>(i2);
      return int64_t(-1);
    }
    if (member == "remove") {
      if (args.empty()) throw RuntimeError(pos, "remove() expects 1 arg");
      int64_t idx = asInt(pos, args[0]);
      if (idx < 0 || idx >= static_cast<int64_t>(lv->elements.size()))
        throw RuntimeError(pos, "remove() index out of bounds");
      Value removed = lv->elements[idx]->val;
      lv->elements.erase(lv->elements.begin() + idx);
      return removed;
    }
    if (member == "sort") {
      auto& elems = lv->elements;
      std::sort(elems.begin(), elems.end(),
        [&pos](const std::shared_ptr<ValueBox>& a, const std::shared_ptr<ValueBox>& b) {
          return asDouble(pos, a->val) < asDouble(pos, b->val);
        });
      return std::monostate{};
    }
    if (member == "reverse") {
      auto& elems = lv->elements;
      std::reverse(elems.begin(), elems.end());
      return std::monostate{};
    }
    if (member == "join") {
      std::string delim = "";
      if (!args.empty()) delim = toString(args[0]);
      std::string result;
      for (size_t i2 = 0; i2 < lv->elements.size(); i2++) {
        if (i2) result += delim;
        result += toString(lv->elements[i2]->val);
      }
      return result;
    }
    if (member == "map") {
      if (args.empty()) throw RuntimeError(pos, "map() expects a function");
      ListValue result;
      for (auto& el : lv->elements) {
        result.elements.push_back(std::make_shared<ValueBox>(
          callValue(pos, args[0], {el->val})));
      }
      return Value(std::move(result));
    }
    if (member == "filter") {
      if (args.empty()) throw RuntimeError(pos, "filter() expects a function");
      ListValue result;
      for (auto& el : lv->elements) {
        if (isTruthy(callValue(pos, args[0], {el->val})))
          result.elements.push_back(std::make_shared<ValueBox>(el->val));
      }
      return Value(std::move(result));
    }
    if (member == "forEach") {
      if (args.empty()) throw RuntimeError(pos, "forEach() expects a function");
      for (auto& el : lv->elements)
        callValue(pos, args[0], {el->val});
      return std::monostate{};
    }
    if (member == "slice") {
      int64_t from = args.size() > 0 ? asInt(pos, args[0]) : 0;
      int64_t to = args.size() > 1 ? asInt(pos, args[1]) : static_cast<int64_t>(lv->elements.size());
      if (from < 0) from += lv->elements.size();
      if (to < 0) to += lv->elements.size();
      ListValue result;
      for (int64_t i2 = from; i2 < to && i2 < static_cast<int64_t>(lv->elements.size()); i2++)
        result.elements.push_back(std::make_shared<ValueBox>(lv->elements[i2]->val));
      return Value(std::move(result));
    }
    if (member == "reduce") {
      if (args.empty()) throw RuntimeError(pos, "reduce() expects a function");
      Value acc = args.size() > 1 ? args[1] : (lv->elements.empty() ? Value(std::monostate{}) : lv->elements[0]->val);
      size_t start = args.size() > 1 ? 0 : 1;
      for (size_t i2 = start; i2 < lv->elements.size(); i2++) {
        acc = callValue(pos, args[0], {acc, lv->elements[i2]->val});
      }
      return acc;
    }
    if (member == "find") {
      if (args.empty()) throw RuntimeError(pos, "find() expects a function");
      for (auto& el : lv->elements) {
        if (isTruthy(callValue(pos, args[0], {el->val})))
          return el->val;
      }
      return std::monostate{};
    }
    if (member == "findIndex") {
      if (args.empty()) throw RuntimeError(pos, "findIndex() expects a function");
      for (size_t i2 = 0; i2 < lv->elements.size(); i2++) {
        if (isTruthy(callValue(pos, args[0], {lv->elements[i2]->val})))
          return static_cast<int64_t>(i2);
      }
      return int64_t(-1);
    }
    if (member == "every") {
      if (args.empty()) throw RuntimeError(pos, "every() expects a function");
      for (auto& el : lv->elements) {
        if (!isTruthy(callValue(pos, args[0], {el->val})))
          return false;
      }
      return true;
    }
    if (member == "some") {
      if (args.empty()) throw RuntimeError(pos, "some() expects a function");
      for (auto& el : lv->elements) {
        if (isTruthy(callValue(pos, args[0], {el->val})))
          return true;
      }
      return false;
    }
    if (member == "flat") {
      ListValue result;
      for (auto& el : lv->elements) {
        if (auto inner = std::get_if<ListValue>(&el->val)) {
          for (auto& inner_el : inner->elements)
            result.elements.push_back(std::make_shared<ValueBox>(inner_el->val));
        } else {
          result.elements.push_back(std::make_shared<ValueBox>(el->val));
        }
      }
      return Value(std::move(result));
    }
    if (member == "insert") {
      if (args.size() < 2) throw RuntimeError(pos, "insert() expects (index, value)");
      int64_t idx = asInt(pos, args[0]);
      if (idx < 0) idx += lv->elements.size();
      if (idx < 0 || idx > static_cast<int64_t>(lv->elements.size()))
        throw RuntimeError(pos, "insert() index out of bounds");
      lv->elements.insert(lv->elements.begin() + idx, std::make_shared<ValueBox>(args[1]));
      return std::monostate{};
    }
    if (member == "clear") {
      lv->elements.clear();
      return std::monostate{};
    }
    if (member == "count") {
      if (args.empty()) throw RuntimeError(pos, "count() expects 1 arg");
      int64_t cnt = 0;
      for (auto& el : lv->elements)
        if (valuesEqual(el->val, args[0])) cnt++;
      return cnt;
    }
    if (member == "first") {
      if (lv->elements.empty()) return std::monostate{};
      return lv->elements.front()->val;
    }
    if (member == "last") {
      if (lv->elements.empty()) return std::monostate{};
      return lv->elements.back()->val;
    }
    throw RuntimeError(pos, "List has no method '" + member + "'");
  }

  // ── Map methods ──────────────────────────
  if (auto* mv = std::get_if<MapValue>(&obj)) {
    if (member == "get") {
      if (args.empty()) throw RuntimeError(pos, "get() expects 1 arg");
      std::string key = toString(args[0]);
      for (auto& entry : mv->entries)
        if (entry.first == key) return entry.second->val;
      return args.size() > 1 ? args[1] : Value(std::monostate{});
    }
    if (member == "set") {
      if (args.size() < 2) throw RuntimeError(pos, "set() expects 2 args");
      std::string key = toString(args[0]);
      for (auto& entry : mv->entries) {
        if (entry.first == key) {
          entry.second->val = args[1];
          return std::monostate{};
        }
      }
      mv->entries.push_back({key, std::make_shared<ValueBox>(args[1])});
      return std::monostate{};
    }
    if (member == "has") {
      if (args.empty()) throw RuntimeError(pos, "has() expects 1 arg");
      std::string key = toString(args[0]);
      for (auto& entry : mv->entries)
        if (entry.first == key) return true;
      return false;
    }
    if (member == "keys") {
      ListValue lv2;
      for (auto& entry : mv->entries)
        lv2.elements.push_back(std::make_shared<ValueBox>(Value(entry.first)));
      return Value(std::move(lv2));
    }
    if (member == "values") {
      ListValue lv2;
      for (auto& entry : mv->entries)
        lv2.elements.push_back(std::make_shared<ValueBox>(entry.second->val));
      return Value(std::move(lv2));
    }
    if (member == "delete") {
      if (args.empty()) throw RuntimeError(pos, "delete() expects 1 arg");
      std::string key = toString(args[0]);
      auto& entries = mv->entries;
      entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&key](const auto& e) { return e.first == key; }), entries.end());
      return std::monostate{};
    }
    if (member == "length") return static_cast<int64_t>(mv->entries.size());
    throw RuntimeError(pos, "Map has no method '" + member + "'");
  }

  // ── Object method calls ──────────────────
  if (auto* ov = std::get_if<ObjectValue>(&obj)) {
    // Look up method on class chain
    auto cls = ov->classDef;
    while (cls) {
      auto mit = cls->methods.find(member);
      if (mit != cls->methods.end()) {
        return callFunction(pos, mit->second, args, ov);
      }
      cls = cls->parent;
    }
    throw RuntimeError(pos, "No method '" + member + "' on " + ov->className);
  }

  // ── Dotted builtin calls (math.sqrt, io.readFile, os.exec, etc.) ──
  // If the object is monostate (e.g. 'math' placeholder), check builtins
  if (std::holds_alternative<std::monostate>(obj)) {
    // This case shouldn't normally be reached since we handle dotted calls
    // via MemberExpr → CallExpr path
  }

  throw RuntimeError(pos, "Cannot call method '" + member + "' on " + typeName(obj));
}

// ═══════════════════════════════════════════════════
//  Operators
// ═══════════════════════════════════════════════════

Value Interpreter::unaryOp(const SourcePos& pos, const Token& op,
                           const Value& a) {
  switch (op.type) {
  case TokenType::Bang: return !isTruthy(a);
  case TokenType::Minus:
    if (auto i = std::get_if<int64_t>(&a)) return -(*i);
    if (auto d = std::get_if<double>(&a)) return -(*d);
    return -asDouble(pos, a);
  case TokenType::Plus:
    if (std::holds_alternative<int64_t>(a) || std::holds_alternative<double>(a))
      return a;
    return asDouble(pos, a);
  case TokenType::Tilde:
    return ~asInt(pos, a);
  default: break;
  }
  throw RuntimeError(pos, "Unsupported unary operator " + std::string(toString(op.type)));
}

Value Interpreter::binaryOp(const SourcePos& pos, const Token& op,
                            const Value& a, const Value& b) {
  switch (op.type) {
  case TokenType::Plus:
    if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b))
      return toString(a) + toString(b);
    // List concatenation
    if (std::holds_alternative<ListValue>(a) && std::holds_alternative<ListValue>(b)) {
      ListValue result = std::get<ListValue>(a);
      for (auto& el : std::get<ListValue>(b).elements)
        result.elements.push_back(std::make_shared<ValueBox>(el->val));
      return Value(std::move(result));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
      return asDouble(pos, a) + asDouble(pos, b);
    return asInt(pos, a) + asInt(pos, b);

  case TokenType::Minus:
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
      return asDouble(pos, a) - asDouble(pos, b);
    return asInt(pos, a) - asInt(pos, b);

  case TokenType::Star:
    // string * int → repeat
    if (std::holds_alternative<std::string>(a) && (std::holds_alternative<int64_t>(b) || std::holds_alternative<double>(b))) {
      std::string result;
      int64_t count = asInt(pos, b);
      for (int64_t i2 = 0; i2 < count; i2++) result += std::get<std::string>(a);
      return result;
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
      return asDouble(pos, a) * asDouble(pos, b);
    return asInt(pos, a) * asInt(pos, b);

  case TokenType::Slash: {
    double denom = asDouble(pos, b);
    if (denom == 0.0) throw RuntimeError(pos, "Division by zero");
    return asDouble(pos, a) / denom;
  }

  case TokenType::TildeSlash: {
    int64_t denom = asInt(pos, b);
    if (denom == 0) throw RuntimeError(pos, "Division by zero");
    return asInt(pos, a) / denom;
  }

  case TokenType::Percent: {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
      return std::fmod(asDouble(pos, a), asDouble(pos, b));
    int64_t denom = asInt(pos, b);
    if (denom == 0) throw RuntimeError(pos, "Division by zero");
    return asInt(pos, a) % denom;
  }

  case TokenType::StarStar: {
    double base = asDouble(pos, a);
    double exp = asDouble(pos, b);
    double result = std::pow(base, exp);
    // Return int if both operands were int and result is integral
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b) &&
        result == std::floor(result) && result >= -9223372036854775808.0 &&
        result < 9223372036854775808.0)
      return static_cast<int64_t>(result);
    return result;
  }

  case TokenType::EqualEqual: return valuesEqual(a, b);
  case TokenType::BangEqual: return !valuesEqual(a, b);

  case TokenType::Less:
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
      return std::get<std::string>(a) < std::get<std::string>(b);
    return asDouble(pos, a) < asDouble(pos, b);
  case TokenType::LessEqual:
    return asDouble(pos, a) <= asDouble(pos, b);
  case TokenType::Greater:
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
      return std::get<std::string>(a) > std::get<std::string>(b);
    return asDouble(pos, a) > asDouble(pos, b);
  case TokenType::GreaterEqual:
    return asDouble(pos, a) >= asDouble(pos, b);

  case TokenType::AmpAmp: return isTruthy(a) && isTruthy(b);
  case TokenType::PipePipe: return isTruthy(a) || isTruthy(b);

  // Bitwise
  case TokenType::Amp: return asInt(pos, a) & asInt(pos, b);
  case TokenType::Pipe: return asInt(pos, a) | asInt(pos, b);
  case TokenType::Caret: return asInt(pos, a) ^ asInt(pos, b);
  case TokenType::LessLess: return asInt(pos, a) << asInt(pos, b);
  case TokenType::GreaterGreater: return asInt(pos, a) >> asInt(pos, b);

  default: break;
  }
  throw RuntimeError(pos, "Unsupported binary operator " + std::string(toString(op.type)));
}

Value Interpreter::callBuiltin(const SourcePos& pos, const std::string& name,
                               const std::vector<Value>& args) {
  auto it = builtins_.find(name);
  if (it == builtins_.end())
    throw RuntimeError(pos, "Unknown builtin: " + name);
  return it->second(*this, pos, args);
}

} // namespace nova

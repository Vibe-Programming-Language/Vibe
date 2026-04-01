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
#include <limits>
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <SDL2/SDL.h>

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

static Value mapGetValue(const MapValue& mv, const std::string& key) {
  for (const auto& entry : mv.entries) {
    if (entry.first == key) return entry.second->val;
  }
  return std::monostate{};
}

static const Value* mapGetValuePtr(const MapValue& mv, const std::string& key) {
  for (const auto& entry : mv.entries) {
    if (entry.first == key) return &entry.second->val;
  }
  return nullptr;
}

static Value* mapGetValuePtr(MapValue& mv, const std::string& key) {
  for (auto& entry : mv.entries) {
    if (entry.first == key) return &entry.second->val;
  }
  return nullptr;
}

static void mapSetValue(MapValue& mv, const std::string& key, Value value) {
  for (auto& entry : mv.entries) {
    if (entry.first == key) {
      entry.second->val = std::move(value);
      return;
    }
  }
  mv.entries.push_back({key, std::make_shared<ValueBox>(std::move(value))});
}

static ListValue toListValue(const std::vector<double>& vals) {
  ListValue lv;
  for (double v : vals) lv.elements.push_back(std::make_shared<ValueBox>(Value(v)));
  return lv;
}

static ListValue toListValue(const std::vector<int64_t>& vals) {
  ListValue lv;
  for (int64_t v : vals) lv.elements.push_back(std::make_shared<ValueBox>(Value(v)));
  return lv;
}

static std::vector<int64_t> readIntList(const Value& v) {
  std::vector<int64_t> out;
  auto* lv = std::get_if<ListValue>(&v);
  if (!lv) return out;
  for (const auto& el : lv->elements) {
    if (auto* i = std::get_if<int64_t>(&el->val)) out.push_back(*i);
    else if (auto* d = std::get_if<double>(&el->val)) out.push_back(static_cast<int64_t>(*d));
  }
  return out;
}

static std::vector<double> readNumList(const Value& v) {
  std::vector<double> out;
  auto* lv = std::get_if<ListValue>(&v);
  if (!lv) return out;
  for (const auto& el : lv->elements) {
    if (auto* i = std::get_if<int64_t>(&el->val)) out.push_back(static_cast<double>(*i));
    else if (auto* d = std::get_if<double>(&el->val)) out.push_back(*d);
    else if (auto* b = std::get_if<bool>(&el->val)) out.push_back(*b ? 1.0 : 0.0);
  }
  return out;
}

static int64_t product(const std::vector<int64_t>& shape) {
  if (shape.empty()) return 0;
  int64_t p = 1;
  for (int64_t d : shape) p *= d;
  return p;
}

static MapValue makeTensor(std::vector<double> data,
                           std::vector<int64_t> shape,
                           bool requiresGrad = false,
                           const std::string& device = "cpu") {
  if (shape.empty()) shape = {static_cast<int64_t>(data.size())};
  if (product(shape) != static_cast<int64_t>(data.size())) {
    data.resize(std::max<int64_t>(1, product(shape)), 0.0);
  }
  MapValue t;
  mapSetValue(t, "__kind", std::string("neural.Tensor"));
  mapSetValue(t, "shape", Value(toListValue(shape)));
  mapSetValue(t, "data", Value(toListValue(data)));
  mapSetValue(t, "requiresGrad", requiresGrad);
  mapSetValue(t, "device", std::string(device));
  mapSetValue(t, "_grad", std::monostate{});
  return t;
}

static std::vector<double> tensorData(const MapValue& t) {
  return readNumList(mapGetValue(t, "data"));
}

static std::vector<int64_t> tensorShape(const MapValue& t) {
  return readIntList(mapGetValue(t, "shape"));
}

static MapValue tensorFromValue(const Value& v) {
  if (auto* mv = std::get_if<MapValue>(&v)) {
    const Value* kind = mapGetValuePtr(*mv, "__kind");
    if (kind && std::holds_alternative<std::string>(*kind)) {
      std::string ks = std::get<std::string>(*kind);
      if (ks == "Tensor" || ks == "neural.Tensor") return *mv;
    }
  }
  // Support construction from numeric list
  auto data = readNumList(v);
  if (!data.empty()) return makeTensor(data, {static_cast<int64_t>(data.size())});
  return makeTensor({0.0}, {1});
}

static std::vector<double> valueToVector(const Value& v) {
  if (auto* i = std::get_if<int64_t>(&v)) return {static_cast<double>(*i)};
  if (auto* d = std::get_if<double>(&v)) return {*d};
  if (auto* b = std::get_if<bool>(&v)) return {*b ? 1.0 : 0.0};
  if (auto* mv = std::get_if<MapValue>(&v)) {
    const Value* kind = mapGetValuePtr(*mv, "__kind");
    if (kind && std::holds_alternative<std::string>(*kind) &&
        (std::get<std::string>(*kind) == "neural.Tensor" || std::get<std::string>(*kind) == "Tensor")) {
      return tensorData(*mv);
    }
  }
  return readNumList(v);
}

static std::vector<double> applyActivation(std::vector<double> x, const std::string& activation) {
  if (activation == "relu") {
    for (double& v : x) v = std::max(0.0, v);
    return x;
  }
  if (activation == "sigmoid") {
    for (double& v : x) v = 1.0 / (1.0 + std::exp(-v));
    return x;
  }
  if (activation == "tanh") {
    for (double& v : x) v = std::tanh(v);
    return x;
  }
  if (activation == "softmax") {
    if (x.empty()) return x;
    double m = *std::max_element(x.begin(), x.end());
    double s = 0.0;
    for (double& v : x) { v = std::exp(v - m); s += v; }
    if (s == 0.0) return x;
    for (double& v : x) v /= s;
    return x;
  }
  return x;
}

static std::vector<double> denseForward(const MapValue& layer, const std::vector<double>& input) {
  Value inV = mapGetValue(layer, "inputSize");
  Value outV = mapGetValue(layer, "outputSize");
  int64_t in = std::holds_alternative<int64_t>(inV)
                 ? std::get<int64_t>(inV)
                 : static_cast<int64_t>(std::holds_alternative<double>(inV) ? std::get<double>(inV) : 0.0);
  int64_t out = std::holds_alternative<int64_t>(outV)
                  ? std::get<int64_t>(outV)
                  : static_cast<int64_t>(std::holds_alternative<double>(outV) ? std::get<double>(outV) : 0.0);
  std::string activation = toString(mapGetValue(layer, "activation"));

  auto w = readNumList(mapGetValue(layer, "weights"));
  auto b = readNumList(mapGetValue(layer, "bias"));
  if (static_cast<int64_t>(w.size()) != in * out || static_cast<int64_t>(b.size()) != out ||
      static_cast<int64_t>(input.size()) != in) {
    return std::vector<double>(static_cast<size_t>(out), 0.0);
  }

  std::vector<double> y(static_cast<size_t>(out), 0.0);
  for (int64_t o = 0; o < out; ++o) {
    double acc = b[static_cast<size_t>(o)];
    for (int64_t i = 0; i < in; ++i) {
      acc += input[static_cast<size_t>(i)] * w[static_cast<size_t>(o * in + i)];
    }
    y[static_cast<size_t>(o)] = acc;
  }
  return applyActivation(std::move(y), activation);
}

static Value runSequentialPredict(const MapValue& model, const Value& input) {
  const Value* layersVal = mapGetValuePtr(model, "layers");
  auto* layers = layersVal ? std::get_if<ListValue>(layersVal) : nullptr;
  if (!layers) return std::monostate{};

  std::vector<double> x = valueToVector(input);
  if (x.empty()) return std::monostate{};

  for (const auto& l : layers->elements) {
    auto* layerMap = std::get_if<MapValue>(&l->val);
    if (!layerMap) continue;
    std::string kind = toString(mapGetValue(*layerMap, "__kind"));
    if (kind == "neural.Dense") {
      x = denseForward(*layerMap, x);
    }
  }
  return Value(makeTensor(x, {static_cast<int64_t>(x.size())}));
}

struct GuiRuntimeState {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  bool initialized = false;
};

static GuiRuntimeState gGui;

static Uint8 valueAsU8(const Value& v, Uint8 fallback = 0) {
  if (auto p = std::get_if<int64_t>(&v)) return static_cast<Uint8>(std::max<int64_t>(0, std::min<int64_t>(255, *p)));
  if (auto p = std::get_if<double>(&v)) return static_cast<Uint8>(std::max(0.0, std::min(255.0, *p)));
  return fallback;
}

static void setRendererColorFromValue(const Value& colorVal) {
  Uint8 r = 255, g = 255, b = 255, a = 255;
  if (auto* mv = std::get_if<MapValue>(&colorVal)) {
    r = valueAsU8(mapGetValue(*mv, "r"), 255);
    g = valueAsU8(mapGetValue(*mv, "g"), 255);
    b = valueAsU8(mapGetValue(*mv, "b"), 255);
    a = valueAsU8(mapGetValue(*mv, "a"), 255);
  }
  SDL_SetRenderDrawColor(gGui.renderer, r, g, b, a);
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

  std::string normalized = moduleName;
  if (normalized.rfind("vibe.", 0) == 0) {
    normalized = normalized.substr(5);
  }

  if (importedModules_.count(moduleName)) return;
  importedModules_[moduleName] = true;
  importedModules_[normalized] = true;

  // Built-in math module
  if (normalized == "math") {
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
  if (normalized == "io") {
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
  if (normalized == "os") {
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
  if (normalized == "time") {
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
  if (normalized == "string") {
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
  if (normalized == "json") {
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
  if (normalized == "collections") {
    importedModules_["collections"] = true;
    registerBuiltin("collections.Stack", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return Value(ListValue{});
    });
    registerBuiltin("collections.Queue", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return Value(ListValue{});
    });
    return;
  }

  // Built-in neural module (MVP)
  if (normalized == "neural") {
    importedModules_["neural"] = true;

    registerBuiltin("neural.Tensor", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty()) return Value(makeTensor({0.0}, {1}));
      // Tensor([2,3]) => zeros shape
      auto shape = readIntList(args[0]);
      if (!shape.empty()) return Value(makeTensor(std::vector<double>(product(shape), 0.0), shape));

      // Tensor([[...], [...]]) => infer 2D
      if (auto* outer = std::get_if<ListValue>(&args[0])) {
        std::vector<double> flat;
        int64_t rows = static_cast<int64_t>(outer->elements.size());
        int64_t cols = 0;
        bool is2d = true;
        for (const auto& row : outer->elements) {
          auto* inner = std::get_if<ListValue>(&row->val);
          if (!inner) { is2d = false; break; }
          if (cols == 0) cols = static_cast<int64_t>(inner->elements.size());
          for (const auto& el : inner->elements) {
            if (auto* i = std::get_if<int64_t>(&el->val)) flat.push_back(static_cast<double>(*i));
            else if (auto* d = std::get_if<double>(&el->val)) flat.push_back(*d);
            else throw RuntimeError(pos, "neural.Tensor expects numeric values");
          }
        }
        if (is2d && rows > 0 && cols > 0) return Value(makeTensor(flat, {rows, cols}));
      }

      auto data = readNumList(args[0]);
      if (!data.empty()) return Value(makeTensor(data, {static_cast<int64_t>(data.size())}));
      throw RuntimeError(pos, "neural.Tensor expects shape list or numeric data list");
    });

    registerBuiltin("neural.zeros", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty()) throw RuntimeError(pos, "neural.zeros expects shape list");
      auto shape = readIntList(args[0]);
      if (shape.empty()) throw RuntimeError(pos, "neural.zeros expects numeric shape list");
      return Value(makeTensor(std::vector<double>(product(shape), 0.0), shape));
    });

    registerBuiltin("neural.ones", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty()) throw RuntimeError(pos, "neural.ones expects shape list");
      auto shape = readIntList(args[0]);
      if (shape.empty()) throw RuntimeError(pos, "neural.ones expects numeric shape list");
      return Value(makeTensor(std::vector<double>(product(shape), 1.0), shape));
    });

    registerBuiltin("neural.randn", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty()) throw RuntimeError(pos, "neural.randn expects shape list");
      auto shape = readIntList(args[0]);
      if (shape.empty()) throw RuntimeError(pos, "neural.randn expects numeric shape list");
      std::mt19937 rng(std::random_device{}());
      std::normal_distribution<double> dist(0.0, 1.0);
      std::vector<double> data(product(shape));
      for (double& x : data) x = dist(rng);
      return Value(makeTensor(std::move(data), shape));
    });

    registerBuiltin("neural.relu", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      if (args.empty()) return Value(makeTensor({0.0}, {1}));
      MapValue t = tensorFromValue(args[0]);
      auto d = tensorData(t);
      for (double& x : d) x = std::max(0.0, x);
      return Value(makeTensor(d, tensorShape(t), isTruthy(mapGetValue(t, "requiresGrad")), toString(mapGetValue(t, "device"))));
    });

    registerBuiltin("neural.softmax", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      if (args.empty()) return Value(makeTensor({1.0}, {1}));
      MapValue t = tensorFromValue(args[0]);
      auto d = tensorData(t);
      if (d.empty()) return Value(makeTensor({1.0}, {1}));
      double mx = *std::max_element(d.begin(), d.end());
      double sumExp = 0.0;
      for (double& x : d) { x = std::exp(x - mx); sumExp += x; }
      for (double& x : d) x /= sumExp;
      return Value(makeTensor(d, tensorShape(t), isTruthy(mapGetValue(t, "requiresGrad")), toString(mapGetValue(t, "device"))));
    });

    registerBuiltin("neural.Dense", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() < 2) throw RuntimeError(pos, "neural.Dense expects inputSize, outputSize");
      int64_t in = asInt(pos, args[0]);
      int64_t out = asInt(pos, args[1]);

      std::mt19937 rng(std::random_device{}());
      std::normal_distribution<double> dist(0.0, std::sqrt(2.0 / std::max<int64_t>(1, in)));
      std::vector<double> w(static_cast<size_t>(in * out));
      for (double& x : w) x = dist(rng);
      std::vector<double> b(static_cast<size_t>(out), 0.0);

      MapValue layer;
      mapSetValue(layer, "__kind", std::string("neural.Dense"));
      mapSetValue(layer, "inputSize", in);
      mapSetValue(layer, "outputSize", out);
      mapSetValue(layer, "activation", args.size() > 2 ? toString(args[2]) : std::string("linear"));
      mapSetValue(layer, "weights", Value(toListValue(w)));
      mapSetValue(layer, "bias", Value(toListValue(b)));
      return Value(std::move(layer));
    });

    registerBuiltin("neural.Sequential", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty() || !std::holds_alternative<ListValue>(args[0]))
        throw RuntimeError(pos, "neural.Sequential expects layer list");
      MapValue model;
      mapSetValue(model, "__kind", std::string("neural.Model"));
      mapSetValue(model, "layers", args[0]);
      mapSetValue(model, "compiled", false);
      return Value(std::move(model));
    });

    registerBuiltin("neural.CrossEntropyLoss", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      MapValue loss;
      mapSetValue(loss, "__kind", std::string("neural.Loss"));
      mapSetValue(loss, "name", std::string("CrossEntropyLoss"));
      return Value(std::move(loss));
    });

    registerBuiltin("neural.Adam", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue opt;
      mapSetValue(opt, "__kind", std::string("neural.Optimizer"));
      mapSetValue(opt, "name", std::string("Adam"));
      mapSetValue(opt, "lr", args.empty() ? Value(0.001) : args[0]);
      return Value(std::move(opt));
    });

    registerBuiltin("neural.trainTestSplit", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() < 3) throw RuntimeError(pos, "neural.trainTestSplit expects data, labels, testSize");
      auto* data = std::get_if<ListValue>(&args[0]);
      auto* labels = std::get_if<ListValue>(&args[1]);
      if (!data || !labels) throw RuntimeError(pos, "neural.trainTestSplit expects list data and labels");
      double ts = asDouble(pos, args[2]);
      int64_t n = static_cast<int64_t>(std::min(data->elements.size(), labels->elements.size()));
      int64_t testN = static_cast<int64_t>(std::max(1.0, std::floor(n * ts)));
      int64_t trainN = std::max<int64_t>(0, n - testN);

      ListValue trainX, testX, trainY, testY;
      for (int64_t i = 0; i < n; ++i) {
        if (i < trainN) {
          trainX.elements.push_back(std::make_shared<ValueBox>(data->elements[i]->val));
          trainY.elements.push_back(std::make_shared<ValueBox>(labels->elements[i]->val));
        } else {
          testX.elements.push_back(std::make_shared<ValueBox>(data->elements[i]->val));
          testY.elements.push_back(std::make_shared<ValueBox>(labels->elements[i]->val));
        }
      }

      ListValue out;
      out.elements.push_back(std::make_shared<ValueBox>(Value(std::move(trainX))));
      out.elements.push_back(std::make_shared<ValueBox>(Value(std::move(testX))));
      out.elements.push_back(std::make_shared<ValueBox>(Value(std::move(trainY))));
      out.elements.push_back(std::make_shared<ValueBox>(Value(std::move(testY))));
      return Value(std::move(out));
    });

    return;
  }

  // Built-in dsa module (MVP)
  if (normalized == "dsa") {
    importedModules_["dsa"] = true;

    registerBuiltin("dsa.Stack", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      MapValue st;
      mapSetValue(st, "__kind", std::string("dsa.Stack"));
      mapSetValue(st, "data", Value(ListValue{}));
      return Value(std::move(st));
    });

    registerBuiltin("dsa.Queue", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      MapValue q;
      mapSetValue(q, "__kind", std::string("dsa.Queue"));
      mapSetValue(q, "data", Value(ListValue{}));
      return Value(std::move(q));
    });

    registerBuiltin("dsa.Graph", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue g;
      mapSetValue(g, "__kind", std::string("dsa.Graph"));
      mapSetValue(g, "directed", args.empty() ? Value(false) : args[0]);
      mapSetValue(g, "vertices", Value(ListValue{}));
      mapSetValue(g, "edges", Value(ListValue{}));
      return Value(std::move(g));
    });

    registerBuiltin("dsa.quickSort", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty() || !std::holds_alternative<ListValue>(args[0]))
        throw RuntimeError(pos, "dsa.quickSort expects a list");
      ListValue sorted = std::get<ListValue>(args[0]);
      std::sort(sorted.elements.begin(), sorted.elements.end(),
        [&pos](const std::shared_ptr<ValueBox>& a, const std::shared_ptr<ValueBox>& b) {
          return asDouble(pos, a->val) < asDouble(pos, b->val);
        });
      return Value(std::move(sorted));
    });

    registerBuiltin("dsa.dijkstra", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() < 2) throw RuntimeError(pos, "dsa.dijkstra expects graph and source");
      auto* g = std::get_if<MapValue>(&args[0]);
      if (!g || toString(mapGetValue(*g, "__kind")) != "dsa.Graph")
        throw RuntimeError(pos, "dsa.dijkstra expects Graph");
      std::string src = toString(args[1]);
      std::string target = args.size() > 2 ? toString(args[2]) : std::string();
      bool hasTarget = args.size() > 2;
      bool directed = isTruthy(mapGetValue(*g, "directed"));

      const Value* edgesVal = mapGetValuePtr(*g, "edges");
      auto* edges = edgesVal ? std::get_if<ListValue>(edgesVal) : nullptr;
      if (!edges) return std::monostate{};

      std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> adj;
      for (const auto& ep : edges->elements) {
        auto* em = std::get_if<MapValue>(&ep->val);
        if (!em) continue;
        std::string from = toString(mapGetValue(*em, "from"));
        std::string to = toString(mapGetValue(*em, "to"));
        double w = asDouble(pos, mapGetValue(*em, "weight"));
        adj[from].push_back({to, w});
        if (!directed) adj[to].push_back({from, w});
      }

      if (!adj.count(src)) return std::monostate{};

      using Node = std::pair<double, std::string>;
      std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
      std::unordered_map<std::string, double> dist;
      std::unordered_map<std::string, std::string> prev;

      for (const auto& kv : adj) dist[kv.first] = std::numeric_limits<double>::infinity();
      dist[src] = 0.0;
      pq.push({0.0, src});

      while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;
        if (hasTarget && u == target) break;

        for (const auto& [v, w] : adj[u]) {
          double nd = d + w;
          if (!dist.count(v) || nd < dist[v]) {
            dist[v] = nd;
            prev[v] = u;
            pq.push({nd, v});
          }
        }
      }

      if (hasTarget) {
        if (!dist.count(target) || !std::isfinite(dist[target])) return std::monostate{};
        ListValue path;
        std::vector<std::string> rev;
        std::string cur = target;
        rev.push_back(cur);
        while (cur != src && prev.count(cur)) {
          cur = prev[cur];
          rev.push_back(cur);
        }
        std::reverse(rev.begin(), rev.end());
        for (const auto& p : rev) path.elements.push_back(std::make_shared<ValueBox>(Value(p)));

        MapValue out;
        mapSetValue(out, "distance", dist[target]);
        mapSetValue(out, "path", Value(std::move(path)));
        return Value(std::move(out));
      }

      MapValue all;
      for (const auto& kv : dist) {
        if (std::isfinite(kv.second)) mapSetValue(all, kv.first, kv.second);
      }
      return Value(std::move(all));
    });

    return;
  }

  // Built-in ai module (MVP)
  if (normalized == "ai") {
    importedModules_["ai"] = true;

    registerBuiltin("ai.RandomForest", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue m;
      mapSetValue(m, "__kind", std::string("ai.RandomForest"));
      mapSetValue(m, "nEstimators", args.empty() ? Value(int64_t(100)) : args[0]);
      mapSetValue(m, "trained", false);
      return Value(std::move(m));
    });

    registerBuiltin("ai.accuracy", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.size() != 2) throw RuntimeError(pos, "ai.accuracy expects yTrue, yPred");
      auto* yt = std::get_if<ListValue>(&args[0]);
      auto* yp = std::get_if<ListValue>(&args[1]);
      if (!yt || !yp) throw RuntimeError(pos, "ai.accuracy expects lists");
      int64_t n = static_cast<int64_t>(std::min(yt->elements.size(), yp->elements.size()));
      if (n == 0) return 0.0;
      int64_t ok = 0;
      for (int64_t i = 0; i < n; ++i) if (valuesEqual(yt->elements[i]->val, yp->elements[i]->val)) ok++;
      return static_cast<double>(ok) / static_cast<double>(n);
    });

    registerBuiltin("ai.classificationReport", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      return std::string("precision recall f1-score support\n(report MVP)");
    });

    return;
  }

  // Built-in data module (MVP)
  if (normalized == "data") {
    importedModules_["data"] = true;

    registerBuiltin("data.DataFrame", [](Interpreter&, const SourcePos& pos, const std::vector<Value>& args) -> Value {
      if (args.empty() || !std::holds_alternative<MapValue>(args[0]))
        throw RuntimeError(pos, "data.DataFrame expects a map/dict");
      MapValue df;
      mapSetValue(df, "__kind", std::string("data.DataFrame"));
      mapSetValue(df, "data", args[0]);
      return Value(std::move(df));
    });

    registerBuiltin("data.readCSV", [](Interpreter&, const SourcePos&, const std::vector<Value>&) -> Value {
      MapValue df;
      mapSetValue(df, "__kind", std::string("data.DataFrame"));
      mapSetValue(df, "data", Value(MapValue{}));
      mapSetValue(df, "source", std::string("csv"));
      return Value(std::move(df));
    });

    return;
  }

  // Built-in image module (MVP)
  if (normalized == "image") {
    importedModules_["image"] = true;
    registerBuiltin("image.load", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue img;
      mapSetValue(img, "__kind", std::string("image.Image"));
      mapSetValue(img, "path", args.empty() ? Value(std::string("")) : args[0]);
      mapSetValue(img, "width", int64_t(0));
      mapSetValue(img, "height", int64_t(0));
      return Value(std::move(img));
    });
    return;
  }

  // Simulated GUI module for interpreter mode.
  if (normalized == "ui") {
    // Constructors and helpers
    registerBuiltin("ui.Window", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue window;
      mapSetValue(window, "__kind", std::string("ui.Window"));
      mapSetValue(window, "title", args.size() > 0 ? args[0] : Value(std::string("Vibe Window")));
      mapSetValue(window, "width", args.size() > 1 ? args[1] : Value(int64_t(800)));
      mapSetValue(window, "height", args.size() > 2 ? args[2] : Value(int64_t(600)));
      mapSetValue(window, "closed", false);
      mapSetValue(window, "__updateCb", std::monostate{});
      mapSetValue(window, "__eventCb", std::monostate{});
      return Value(std::move(window));
    });

    registerBuiltin("ui.Canvas", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue canvas;
      mapSetValue(canvas, "__kind", std::string("ui.Canvas"));
      mapSetValue(canvas, "width", args.size() > 0 ? args[0] : Value(int64_t(800)));
      mapSetValue(canvas, "height", args.size() > 1 ? args[1] : Value(int64_t(600)));
      return Value(std::move(canvas));
    });

    registerBuiltin("ui.Color", [](Interpreter&, const SourcePos&, const std::vector<Value>& args) -> Value {
      MapValue color;
      mapSetValue(color, "__kind", std::string("ui.Color"));
      mapSetValue(color, "r", args.size() > 0 ? args[0] : Value(int64_t(255)));
      mapSetValue(color, "g", args.size() > 1 ? args[1] : Value(int64_t(255)));
      mapSetValue(color, "b", args.size() > 2 ? args[2] : Value(int64_t(255)));
      return Value(std::move(color));
    });

    auto mkColor = [](int64_t r, int64_t g, int64_t b) -> Value {
      MapValue c;
      mapSetValue(c, "__kind", std::string("ui.Color"));
      mapSetValue(c, "r", r);
      mapSetValue(c, "g", g);
      mapSetValue(c, "b", b);
      return Value(std::move(c));
    };

    MapValue colors;
    mapSetValue(colors, "Black", mkColor(0, 0, 0));
    mapSetValue(colors, "White", mkColor(255, 255, 255));
    mapSetValue(colors, "Red", mkColor(255, 0, 0));
    mapSetValue(colors, "Green", mkColor(0, 255, 0));
    mapSetValue(colors, "Blue", mkColor(0, 0, 255));
    mapSetValue(colors, "Yellow", mkColor(255, 255, 0));
    mapSetValue(colors, "Cyan", mkColor(0, 255, 255));
    mapSetValue(colors, "Magenta", mkColor(255, 0, 255));
    mapSetValue(colors, "Gray", mkColor(128, 128, 128));

    MapValue eventType;
    mapSetValue(eventType, "KeyDown", int64_t(1));
    mapSetValue(eventType, "KeyUp", int64_t(2));
    mapSetValue(eventType, "MouseDown", int64_t(3));
    mapSetValue(eventType, "MouseUp", int64_t(4));

    MapValue ui;
    mapSetValue(ui, "__kind", std::string("ui.Module"));
    mapSetValue(ui, "Color", Value(std::move(colors)));
    mapSetValue(ui, "EventType", Value(std::move(eventType)));
    env_->define("ui", Value(std::move(ui)), true);
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

        // Fast-path dotted module builtins (e.g. time.millis(), ui.Window())
        if (auto* ident = std::get_if<IdentExpr>(&mem->object->node)) {
          std::string dotted = ident->name.lexeme + "." + mem->member.lexeme;
          auto bit = builtins_.find(dotted);
          if (bit != builtins_.end()) {
            return bit->second(*this, e.pos, args);
          }
        }

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
    Value kind = mapGetValue(*mv, "__kind");
    std::string kindStr = std::holds_alternative<std::string>(kind) ? std::get<std::string>(kind) : "";

    if (kindStr == "neural.Tensor" || kindStr == "Tensor") {
      if (member == "shape") return mapGetValue(*mv, "shape");
      if (member == "numel") return static_cast<int64_t>(tensorData(*mv).size());
      if (member == "device") return mapGetValue(*mv, "device");
      if (member == "to") {
        if (args.empty()) throw RuntimeError(pos, "Tensor.to() expects device");
        mapSetValue(*mv, "device", toString(args[0]));
        return obj;
      }
      if (member == "requiresGrad") {
        if (args.empty()) return mapGetValue(*mv, "requiresGrad");
        mapSetValue(*mv, "requiresGrad", isTruthy(args[0]));
        return obj;
      }
      if (member == "reshape") {
        if (args.empty()) throw RuntimeError(pos, "reshape() expects shape list");
        auto ns = readIntList(args[0]);
        if (ns.empty()) throw RuntimeError(pos, "reshape() expects numeric shape list");
        auto data = tensorData(*mv);
        if (static_cast<int64_t>(data.size()) != product(ns))
          throw RuntimeError(pos, "reshape() element count mismatch");
        mapSetValue(*mv, "shape", toListValue(ns));
        return obj;
      }
      if (member == "flatten") {
        auto data = tensorData(*mv);
        return Value(makeTensor(data, {static_cast<int64_t>(data.size())}, isTruthy(mapGetValue(*mv, "requiresGrad")), toString(mapGetValue(*mv, "device"))));
      }
      if (member == "sum") {
        auto data = tensorData(*mv);
        double s = 0.0;
        for (double x : data) s += x;
        return s;
      }
      if (member == "mean") {
        auto data = tensorData(*mv);
        if (data.empty()) return 0.0;
        double s = 0.0;
        for (double x : data) s += x;
        return s / static_cast<double>(data.size());
      }
      if (member == "item") {
        auto data = tensorData(*mv);
        if (data.empty()) throw RuntimeError(pos, "item() on empty tensor");
        return data[0];
      }
      if (member == "backward") {
        auto data = tensorData(*mv);
        ListValue grad;
        for (size_t i = 0; i < data.size(); ++i)
          grad.elements.push_back(std::make_shared<ValueBox>(Value(1.0)));
        mapSetValue(*mv, "grad", Value(std::move(grad)));
        return std::monostate{};
      }
      if (member == "grad") {
        return mapGetValue(*mv, "grad");
      }
      if (member == "add" || member == "sub" || member == "mul" || member == "div") {
        if (args.empty()) throw RuntimeError(pos, member + "() expects operand");
        auto lhs = tensorData(*mv);
        std::vector<double> rhs;
        bool scalar = false;
        double s = 0.0;
        if (auto* i = std::get_if<int64_t>(&args[0])) { scalar = true; s = static_cast<double>(*i); }
        else if (auto* d = std::get_if<double>(&args[0])) { scalar = true; s = *d; }
        else {
          auto tm = tensorFromValue(args[0]);
          rhs = tensorData(tm);
          if (rhs.size() != lhs.size()) throw RuntimeError(pos, member + "() tensor size mismatch");
        }
        for (size_t i = 0; i < lhs.size(); ++i) {
          double r = scalar ? s : rhs[i];
          if (member == "add") lhs[i] += r;
          else if (member == "sub") lhs[i] -= r;
          else if (member == "mul") lhs[i] *= r;
          else {
            if (r == 0.0) throw RuntimeError(pos, "div() by zero");
            lhs[i] /= r;
          }
        }
        return Value(makeTensor(lhs, tensorShape(*mv), isTruthy(mapGetValue(*mv, "requiresGrad")), toString(mapGetValue(*mv, "device"))));
      }
      if (member == "matmul") {
        if (args.empty()) throw RuntimeError(pos, "matmul() expects tensor");
        auto rhsMap = tensorFromValue(args[0]);
        auto aShape = tensorShape(*mv);
        auto bShape = tensorShape(rhsMap);
        if (aShape.size() != 2 || bShape.size() != 2)
          throw RuntimeError(pos, "matmul() currently supports 2D tensors only");
        int64_t m = aShape[0], k = aShape[1], k2 = bShape[0], n = bShape[1];
        if (k != k2) throw RuntimeError(pos, "matmul() incompatible shapes");
        auto a = tensorData(*mv);
        auto b = tensorData(rhsMap);
        std::vector<double> c(static_cast<size_t>(m * n), 0.0);
        for (int64_t i = 0; i < m; ++i) {
          for (int64_t j = 0; j < n; ++j) {
            for (int64_t t = 0; t < k; ++t) {
              c[static_cast<size_t>(i * n + j)] += a[static_cast<size_t>(i * k + t)] * b[static_cast<size_t>(t * n + j)];
            }
          }
        }
        return Value(makeTensor(c, {m, n}, isTruthy(mapGetValue(*mv, "requiresGrad")), toString(mapGetValue(*mv, "device"))));
      }
      throw RuntimeError(pos, "Tensor has no method '" + member + "'");
    }

    if (kindStr == "neural.Model") {
      if (member == "compile") {
        if (args.size() < 2) throw RuntimeError(pos, "compile() expects optimizer, loss");
        mapSetValue(*mv, "optimizer", args[0]);
        mapSetValue(*mv, "loss", args[1]);
        mapSetValue(*mv, "compiled", true);
        return std::monostate{};
      }
      if (member == "fit") {
        if (args.size() < 2) throw RuntimeError(pos, "fit() expects trainX, trainY");
        int64_t epochs = 1;
        if (args.size() > 2) epochs = asInt(pos, args[2]);

        auto* x = std::get_if<ListValue>(&args[0]);
        auto* y = std::get_if<ListValue>(&args[1]);
        if (!x || !y) throw RuntimeError(pos, "fit() expects list trainX/trainY");
        int64_t n = static_cast<int64_t>(std::min(x->elements.size(), y->elements.size()));

        mapSetValue(*mv, "trained", true);
        mapSetValue(*mv, "epochs", epochs);
        ListValue losses;
        for (int64_t e = 0; e < epochs; ++e) {
          double lossSum = 0.0;
          int64_t samples = 0;
          for (int64_t i = 0; i < n; ++i) {
            Value predV = runSequentialPredict(*mv, x->elements[i]->val);
            auto pred = valueToVector(predV);
            auto truth = valueToVector(y->elements[i]->val);
            if (pred.empty() || truth.empty()) continue;
            size_t m = std::min(pred.size(), truth.size());
            double mse = 0.0;
            for (size_t k = 0; k < m; ++k) {
              double diff = pred[k] - truth[k];
              mse += diff * diff;
            }
            lossSum += mse / static_cast<double>(m);
            samples++;
          }
          losses.elements.push_back(std::make_shared<ValueBox>(Value(samples > 0 ? lossSum / static_cast<double>(samples) : 0.0)));
        }
        MapValue hist;
        mapSetValue(hist, "loss", Value(std::move(losses)));
        return Value(std::move(hist));
      }
      if (member == "evaluate") {
        if (args.size() < 2) throw RuntimeError(pos, "evaluate() expects testX, testY");
        auto* x = std::get_if<ListValue>(&args[0]);
        auto* y = std::get_if<ListValue>(&args[1]);
        if (!x || !y) throw RuntimeError(pos, "evaluate() expects list testX/testY");
        int64_t n = static_cast<int64_t>(std::min(x->elements.size(), y->elements.size()));

        double lossSum = 0.0;
        int64_t used = 0;
        int64_t correct = 0;
        for (int64_t i = 0; i < n; ++i) {
          Value predV = runSequentialPredict(*mv, x->elements[i]->val);
          auto pred = valueToVector(predV);
          auto truth = valueToVector(y->elements[i]->val);
          if (pred.empty() || truth.empty()) continue;
          size_t m = std::min(pred.size(), truth.size());

          double mse = 0.0;
          for (size_t k = 0; k < m; ++k) {
            double diff = pred[k] - truth[k];
            mse += diff * diff;
          }
          lossSum += mse / static_cast<double>(m);

          size_t pi = static_cast<size_t>(std::distance(pred.begin(), std::max_element(pred.begin(), pred.end())));
          size_t yi = static_cast<size_t>(std::distance(truth.begin(), std::max_element(truth.begin(), truth.end())));
          if (pi == yi) correct++;
          used++;
        }

        MapValue metrics;
        mapSetValue(metrics, "loss", used > 0 ? lossSum / static_cast<double>(used) : 0.0);
        mapSetValue(metrics, "accuracy", used > 0 ? static_cast<double>(correct) / static_cast<double>(used) : 0.0);
        return Value(std::move(metrics));
      }
      if (member == "predict") {
        if (args.empty()) throw RuntimeError(pos, "predict() expects input");
        if (auto* batch = std::get_if<ListValue>(&args[0])) {
          bool isBatch = !batch->elements.empty() &&
                         (std::holds_alternative<ListValue>(batch->elements[0]->val) ||
                          std::holds_alternative<MapValue>(batch->elements[0]->val));
          if (isBatch) {
            ListValue out;
            for (const auto& sample : batch->elements) {
              out.elements.push_back(std::make_shared<ValueBox>(runSequentialPredict(*mv, sample->val)));
            }
            return Value(std::move(out));
          }
        }
        return runSequentialPredict(*mv, args[0]);
      }
      if (member == "summary") {
        const Value* layersVal = mapGetValuePtr(*mv, "layers");
        auto* layers = layersVal ? std::get_if<ListValue>(layersVal) : nullptr;
        int64_t count = layers ? static_cast<int64_t>(layers->elements.size()) : int64_t(0);
        return std::string("Sequential model with ") + std::to_string(count) + " layers";
      }
      if (member == "save") {
        if (args.empty()) throw RuntimeError(pos, "save() expects path");
        mapSetValue(*mv, "savedPath", toString(args[0]));
        return true;
      }
      throw RuntimeError(pos, "Model has no method '" + member + "'");
    }

    if (kindStr == "dsa.Stack") {
      Value* data = mapGetValuePtr(*mv, "data");
      auto* lv = data ? std::get_if<ListValue>(data) : nullptr;
      if (!lv) throw RuntimeError(pos, "invalid Stack storage");
      if (member == "push") {
        if (args.empty()) throw RuntimeError(pos, "push() expects value");
        lv->elements.push_back(std::make_shared<ValueBox>(args[0]));
        return std::monostate{};
      }
      if (member == "pop") {
        if (lv->elements.empty()) return std::monostate{};
        Value out = lv->elements.back()->val;
        lv->elements.pop_back();
        return out;
      }
      if (member == "peek") {
        if (lv->elements.empty()) return std::monostate{};
        return lv->elements.back()->val;
      }
      if (member == "isEmpty") return lv->elements.empty();
      if (member == "size") return static_cast<int64_t>(lv->elements.size());
      if (member == "clear") { lv->elements.clear(); return std::monostate{}; }
      throw RuntimeError(pos, "Stack has no method '" + member + "'");
    }

    if (kindStr == "dsa.Queue") {
      Value* data = mapGetValuePtr(*mv, "data");
      auto* lv = data ? std::get_if<ListValue>(data) : nullptr;
      if (!lv) throw RuntimeError(pos, "invalid Queue storage");
      if (member == "enqueue") {
        if (args.empty()) throw RuntimeError(pos, "enqueue() expects value");
        lv->elements.push_back(std::make_shared<ValueBox>(args[0]));
        return std::monostate{};
      }
      if (member == "dequeue") {
        if (lv->elements.empty()) return std::monostate{};
        Value out = lv->elements.front()->val;
        lv->elements.erase(lv->elements.begin());
        return out;
      }
      if (member == "front") {
        if (lv->elements.empty()) return std::monostate{};
        return lv->elements.front()->val;
      }
      if (member == "isEmpty") return lv->elements.empty();
      if (member == "size") return static_cast<int64_t>(lv->elements.size());
      if (member == "clear") { lv->elements.clear(); return std::monostate{}; }
      throw RuntimeError(pos, "Queue has no method '" + member + "'");
    }

    if (kindStr == "dsa.Graph") {
      Value* vertsVal = mapGetValuePtr(*mv, "vertices");
      Value* edgesVal = mapGetValuePtr(*mv, "edges");
      auto* verts = vertsVal ? std::get_if<ListValue>(vertsVal) : nullptr;
      auto* edges = edgesVal ? std::get_if<ListValue>(edgesVal) : nullptr;
      if (!verts || !edges) throw RuntimeError(pos, "invalid Graph storage");

      if (member == "addVertex") {
        if (args.empty()) throw RuntimeError(pos, "addVertex() expects name");
        verts->elements.push_back(std::make_shared<ValueBox>(Value(toString(args[0]))));
        return std::monostate{};
      }
      if (member == "addEdge") {
        if (args.size() < 3) throw RuntimeError(pos, "addEdge() expects from,to,weight");
        MapValue e;
        mapSetValue(e, "from", toString(args[0]));
        mapSetValue(e, "to", toString(args[1]));
        mapSetValue(e, "weight", args[2]);
        edges->elements.push_back(std::make_shared<ValueBox>(Value(std::move(e))));
        return std::monostate{};
      }
      if (member == "neighbors") {
        if (args.empty()) throw RuntimeError(pos, "neighbors() expects vertex");
        std::string v = toString(args[0]);
        ListValue out;
        for (const auto& ep : edges->elements) {
          auto* em = std::get_if<MapValue>(&ep->val);
          if (!em) continue;
          if (toString(mapGetValue(*em, "from")) == v)
            out.elements.push_back(std::make_shared<ValueBox>(mapGetValue(*em, "to")));
        }
        return Value(std::move(out));
      }
      throw RuntimeError(pos, "Graph has no method '" + member + "'");
    }

    if (kindStr == "ai.RandomForest") {
      if (member == "fit") {
        mapSetValue(*mv, "trained", true);
        return std::monostate{};
      }
      if (member == "predict") {
        if (args.empty()) throw RuntimeError(pos, "predict() expects data list");
        auto* x = std::get_if<ListValue>(&args[0]);
        if (!x) throw RuntimeError(pos, "predict() expects list");
        ListValue out;
        for (size_t i = 0; i < x->elements.size(); ++i)
          out.elements.push_back(std::make_shared<ValueBox>(Value(int64_t(0))));
        return Value(std::move(out));
      }
      if (member == "score") return 0.5;
      throw RuntimeError(pos, "RandomForest has no method '" + member + "'");
    }

    if (kindStr == "data.DataFrame") {
      if (member == "shape") {
        const Value* dataVal = mapGetValuePtr(*mv, "data");
        auto* data = dataVal ? std::get_if<MapValue>(dataVal) : nullptr;
        if (!data) return Value(ListValue{});
        int64_t cols = static_cast<int64_t>(data->entries.size());
        int64_t rows = 0;
        if (!data->entries.empty()) {
          auto* firstCol = std::get_if<ListValue>(&data->entries.front().second->val);
          if (firstCol) rows = static_cast<int64_t>(firstCol->elements.size());
        }
        ListValue shape;
        shape.elements.push_back(std::make_shared<ValueBox>(Value(rows)));
        shape.elements.push_back(std::make_shared<ValueBox>(Value(cols)));
        return Value(std::move(shape));
      }
      if (member == "columns") {
        const Value* dataVal = mapGetValuePtr(*mv, "data");
        auto* data = dataVal ? std::get_if<MapValue>(dataVal) : nullptr;
        ListValue cols;
        if (data) {
          for (const auto& kv : data->entries)
            cols.elements.push_back(std::make_shared<ValueBox>(Value(kv.first)));
        }
        return Value(std::move(cols));
      }
      if (member == "head") {
        return obj;
      }
      if (member == "select") {
        if (args.empty()) throw RuntimeError(pos, "select() expects column names list");
        auto* names = std::get_if<ListValue>(&args[0]);
        const Value* dataVal = mapGetValuePtr(*mv, "data");
        auto* data = dataVal ? std::get_if<MapValue>(dataVal) : nullptr;
        if (!names || !data) throw RuntimeError(pos, "select() expects valid columns and DataFrame");
        MapValue outData;
        for (const auto& n : names->elements) {
          std::string key = toString(n->val);
          auto it = std::find_if(data->entries.begin(), data->entries.end(),
            [&key](const auto& p) { return p.first == key; });
          if (it != data->entries.end()) outData.entries.push_back(*it);
        }
        MapValue outDf;
        mapSetValue(outDf, "__kind", std::string("data.DataFrame"));
        mapSetValue(outDf, "data", Value(std::move(outData)));
        return Value(std::move(outDf));
      }
      if (member == "describe") {
        return std::string("DataFrame describe() MVP");
      }
      throw RuntimeError(pos, "DataFrame has no method '" + member + "'");
    }

    if (kindStr == "image.Image") {
      if (member == "resize") {
        if (args.size() < 2) throw RuntimeError(pos, "resize() expects width,height");
        mapSetValue(*mv, "width", asInt(pos, args[0]));
        mapSetValue(*mv, "height", asInt(pos, args[1]));
        return obj;
      }
      if (member == "grayscale") {
        mapSetValue(*mv, "mode", std::string("grayscale"));
        return obj;
      }
      if (member == "save") {
        if (args.empty()) throw RuntimeError(pos, "save() expects path");
        mapSetValue(*mv, "savedPath", toString(args[0]));
        return true;
      }
      if (member == "show") return std::monostate{};
      throw RuntimeError(pos, "Image has no method '" + member + "'");
    }

    if (kindStr == "ui.Window") {
      if (member == "setUpdateCallback") {
        if (args.size() != 1) throw RuntimeError(pos, "setUpdateCallback() expects 1 argument");
        mapSetValue(*mv, "__updateCb", args[0]);
        return std::monostate{};
      }
      if (member == "setEventCallback") {
        if (args.size() != 1) throw RuntimeError(pos, "setEventCallback() expects 1 argument");
        mapSetValue(*mv, "__eventCb", args[0]);
        return std::monostate{};
      }
      if (member == "addWidget") {
        return std::monostate{};
      }
      if (member == "close") {
        mapSetValue(*mv, "closed", true);
        return std::monostate{};
      }
      if (member == "show") {
        Value cb = mapGetValue(*mv, "__updateCb");
        Value evCb = mapGetValue(*mv, "__eventCb");

        if (!gGui.initialized) {
          if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw RuntimeError(pos, std::string("SDL init failed: ") + SDL_GetError());
          }
          gGui.initialized = true;
        }

        int w = static_cast<int>(asInt(pos, mapGetValue(*mv, "width")));
        int h = static_cast<int>(asInt(pos, mapGetValue(*mv, "height")));
        std::string title = toString(mapGetValue(*mv, "title"));

        gGui.window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, w, h,
                                       SDL_WINDOW_SHOWN);
        if (!gGui.window) {
          throw RuntimeError(pos, std::string("SDL window create failed: ") + SDL_GetError());
        }

        gGui.renderer = SDL_CreateRenderer(gGui.window, -1,
                                           SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!gGui.renderer) {
          SDL_DestroyWindow(gGui.window);
          gGui.window = nullptr;
          throw RuntimeError(pos, std::string("SDL renderer create failed: ") + SDL_GetError());
        }

        mapSetValue(*mv, "closed", false);

        while (!isTruthy(mapGetValue(*mv, "closed"))) {
          SDL_Event sev;
          while (SDL_PollEvent(&sev)) {
            if (sev.type == SDL_QUIT) {
              mapSetValue(*mv, "closed", true);
              break;
            }

            if (!std::holds_alternative<std::monostate>(evCb)) {
              MapValue key;
              mapSetValue(key, "keyCode", int64_t(0));
              mapSetValue(key, "character", std::string(""));

              MapValue mouse;
              mapSetValue(mouse, "x", int64_t(0));
              mapSetValue(mouse, "y", int64_t(0));
              mapSetValue(mouse, "button", int64_t(0));

              MapValue evt;
              mapSetValue(evt, "type", int64_t(0));
              mapSetValue(evt, "key", Value(key));
              mapSetValue(evt, "mouse", Value(std::move(mouse)));

              if (sev.type == SDL_KEYDOWN || sev.type == SDL_KEYUP) {
                mapSetValue(evt, "type", int64_t(sev.type == SDL_KEYDOWN ? 1 : 2));
                mapSetValue(key, "keyCode", int64_t(sev.key.keysym.sym));
                char ch = static_cast<char>(sev.key.keysym.sym);
                if (ch >= 32 && ch <= 126) {
                  mapSetValue(key, "character", std::string(1, static_cast<char>(std::tolower(ch))));
                }
                mapSetValue(evt, "key", Value(std::move(key)));
              }

              callValue(pos, evCb, {Value(std::move(evt))});
            }
          }

          if (isTruthy(mapGetValue(*mv, "closed"))) break;

          if (!std::holds_alternative<std::monostate>(cb)) {
            callValue(pos, cb, {});
          }

          SDL_RenderPresent(gGui.renderer);
        }

        SDL_DestroyRenderer(gGui.renderer);
        gGui.renderer = nullptr;
        SDL_DestroyWindow(gGui.window);
        gGui.window = nullptr;
        return std::monostate{};
      }
    }

    if (kindStr == "ui.Canvas") {
      if (member == "clear") {
        if (gGui.renderer) {
          Value color = args.empty() ? Value(std::monostate{}) : args[0];
          setRendererColorFromValue(color);
          SDL_RenderClear(gGui.renderer);
        }
        return std::monostate{};
      }
      if (member == "drawLine") {
        if (gGui.renderer && args.size() >= 5) {
          setRendererColorFromValue(args[4]);
          SDL_RenderDrawLine(gGui.renderer,
                             static_cast<int>(asInt(pos, args[0])),
                             static_cast<int>(asInt(pos, args[1])),
                             static_cast<int>(asInt(pos, args[2])),
                             static_cast<int>(asInt(pos, args[3])));
        }
        return std::monostate{};
      }
      if (member == "drawRect") {
        if (gGui.renderer && args.size() >= 6) {
          SDL_Rect r;
          r.x = static_cast<int>(asInt(pos, args[0]));
          r.y = static_cast<int>(asInt(pos, args[1]));
          r.w = static_cast<int>(asInt(pos, args[2]));
          r.h = static_cast<int>(asInt(pos, args[3]));
          setRendererColorFromValue(args[4]);
          bool fill = isTruthy(args[5]);
          if (fill) SDL_RenderFillRect(gGui.renderer, &r);
          else SDL_RenderDrawRect(gGui.renderer, &r);
        }
        return std::monostate{};
      }
      if (member == "drawCircle") {
        if (gGui.renderer && args.size() >= 5) {
          int cx = static_cast<int>(asInt(pos, args[0]));
          int cy = static_cast<int>(asInt(pos, args[1]));
          int radius = static_cast<int>(asInt(pos, args[2]));
          setRendererColorFromValue(args[3]);
          bool fill = isTruthy(args[4]);
          for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
              int d2 = x * x + y * y;
              if ((fill && d2 <= radius * radius) || (!fill && d2 >= (radius - 1) * (radius - 1) && d2 <= radius * radius)) {
                SDL_RenderDrawPoint(gGui.renderer, cx + x, cy + y);
              }
            }
          }
        }
        return std::monostate{};
      }
      if (member == "drawText") {
        // Minimal placeholder text rendering (block glyphs)
        if (gGui.renderer && args.size() >= 4) {
          int x = static_cast<int>(asInt(pos, args[0]));
          int y = static_cast<int>(asInt(pos, args[1]));
          std::string text = toString(args[2]);
          setRendererColorFromValue(args[3]);
          for (size_t i2 = 0; i2 < text.size(); ++i2) {
            SDL_Rect r{ x + static_cast<int>(i2) * 6, y, 4, 8 };
            SDL_RenderFillRect(gGui.renderer, &r);
          }
        }
        return std::monostate{};
      }
    }

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

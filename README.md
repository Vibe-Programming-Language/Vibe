<p align="center">
  <img src="https://raw.githubusercontent.com/Vibe-Programming-Language/Vibe/main/assets/banner.svg" alt="Vibe" width="600">
</p>

<h1 align="center">Vibe Programming Language</h1>

<p align="center">
  <strong>A modern, expressive language that compiles to C++ — built for clarity, speed, and joy.</strong>
</p>

<p align="center">
  <a href="https://github.com/Vibe-Programming-Language/Vibe/releases"><img src="https://img.shields.io/github/v/release/Vibe-Programming-Language/Vibe?style=flat-square&color=black" alt="Release"></a>
  <a href="https://vibe-lang-docs.vercel.app"><img src="https://img.shields.io/badge/docs-live-black?style=flat-square" alt="Docs"></a>
  <img src="https://img.shields.io/badge/language-C%2B%2B17-black?style=flat-square" alt="C++17">
  <img src="https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-black?style=flat-square" alt="Platforms">
  <a href="https://github.com/Vibe-Programming-Language/Vibe/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Vibe-Programming-Language/Vibe?style=flat-square&color=black" alt="License"></a>
  <a href="https://github.com/Vibe-Programming-Language/Vibe/stargazers"><img src="https://img.shields.io/github/stars/Vibe-Programming-Language/Vibe?style=flat-square&color=black" alt="Stars"></a>
</p>

---

## What is Vibe?

Vibe is a modern programming language featuring clean syntax, powerful OOP, pattern matching, lambdas, a rich standard library, and the ability to **compile to native binaries** via C++ transpilation. It comes with an interactive, colorful REPL and a growing ecosystem.

```vibe
// Hello, Vibe!
fn greet(name) {
  return "Hello, " + name + "!";
}

var names = ["Alice", "Bob", "Charlie"];
for name in names {
  print(greet(name));
}

// Functional programming
var squares = [1, 2, 3, 4, 5].map((x) => x ** 2);
print(squares);  // [1, 4, 9, 16, 25]
```

## Features

- **Clean syntax** — Expressive and readable, feels like pseudocode
- **Dynamic typing** with optional type annotations
- **First-class functions** — closures, lambdas, higher-order functions
- **OOP** — classes, inheritance, interfaces, access modifiers
- **Pattern matching** — `match` expressions with default arms
- **Error handling** — `try`/`catch`/`finally` with `throw`
- **Modules** — `import`/`export` system for code organization
- **Enums** — Named constants with optional values
- **Rich standard library** — 50+ built-in functions, math, IO, OS, time modules
- **Interactive REPL** — Color-coded output, auto-semicolons, persistent state
- **C++ transpilation** — Compile to native binaries via `vibe build`
- **VS Code extension** — Syntax highlighting, IntelliSense, snippets, diagnostics

## 📖 Documentation

Full documentation is available at **[vibe-lang-docs.vercel.app](https://vibe-lang-docs.vercel.app)** — covering installation, language tour, standard library reference, examples, and more.

## Installation

### Prerequisites

- C++17 compiler (`g++ >= 7` or `clang++ >= 5`)
- CMake 3.20+

### Build from Source

```bash
git clone https://github.com/Vibe-Programming-Language/Vibe.git
cd Vibe

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Optional: install system-wide
sudo make install
```

### Download Pre-built Binary

Head to the [**Releases**](https://github.com/Vibe-Programming-Language/Vibe/releases) page and download the latest binary for your platform.

### Verify

```bash
vibe version
# Vibe 1.0.0
```

## Quick Start

Create a file called `hello.vibe`:

```vibe
print("Hello, World!");
```

Run it:

```bash
vibe hello.vibe
```

Or use the REPL:

```bash
vibe repl
```

## CLI Commands

| Command | Description |
|---------|-------------|
| `vibe <file.vibe>` | Run a Vibe source file |
| `vibe run <file>` | Run a file (explicit) |
| `vibe build <file>` | Transpile to C++ and compile to native binary |
| `vibe check <file>` | Syntax check without running |
| `vibe repl` | Start the interactive REPL |
| `vibe version` | Display version |
| `vibe help` | Show help |

## Language Tour

### Variables

```vibe
var name = "Vibe";    // mutable
let count = 42;       // mutable (alias for var)
const PI = 3.14159;   // immutable

// Types: int, float, str, bool, null, list, map, function
var items = [1, "two", true, null];
var config = {"host": "localhost", "port": 8080};
```

### Functions & Lambdas

```vibe
fn fibonacci(n) {
  if n <= 1 { return n; }
  return fibonacci(n - 1) + fibonacci(n - 2);
}

// Lambdas with arrow syntax
var double = (x) => x * 2;
var nums = [1, 2, 3].map((x) => x ** 2);  // [1, 4, 9]

// Closures
fn counter() {
  var n = 0;
  return () => { n = n + 1; return n; };
}
```

### Classes & Inheritance

```vibe
class Animal {
  var name = "";
  var sound = "";

  init(name, sound) {
    self.name = name;
    self.sound = sound;
  }

  fn speak() {
    print(self.name + " says " + self.sound);
  }
}

class Dog extends Animal {
  init(name) { super(name, "Woof!"); }
}

Dog("Rex").speak();  // Rex says Woof!
```

### Collections & Functional Programming

```vibe
var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

var result = numbers
  .filter((x) => x % 2 == 0)
  .map((x) => x ** 2);
// [4, 16, 36, 64, 100]

var sum = numbers.reduce((a, b) => a + b, 0);  // 55
```

### Pattern Matching

```vibe
match status {
  200 => print("OK"),
  404 => print("Not Found"),
  _   => print("Unknown"),
}
```

### Error Handling

```vibe
try {
  var data = io.readFile("config.json");
} catch e {
  print("Error: " + e);
} finally {
  print("Cleanup done");
}
```

### Modules

```vibe
import math;
import io;
import os;

print(math.sqrt(16));   // 4
print(math.PI);         // 3.14159...
print(os.platform());   // "linux"
```

## Standard Library

### Built-in Functions (always available)

`print` · `input` · `len` · `str` · `int` · `float` · `type` · `range` · `abs` · `min` · `max` · `sum` · `sorted` · `reversed` · `zip` · `enumerate` · `flatten` · `unique` · `map` · `reduce` · `join` · `keys` · `values` · `chr` · `ord` · `format` · `assert` · `clock` · `timestamp` · `random` · `hash` · `toJSON` · `exit`

### Modules

| Module | What's Inside |
|--------|---------------|
| **math** | `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `floor`, `ceil`, `round`, `random`, `factorial`, `isPrime`, `gcd`, `lcm`, `PI`, `E` |
| **io** | `readFile`, `writeFile`, `appendFile`, `readLines`, `exists` |
| **os** | `exec`, `env`, `platform`, `cwd`, `sleep` |
| **time** | `now`, `millis`, `sleep`, `measure` |
| **json** | `stringify` |
| **string** | `ascii_letters`, `digits`, `repeat`, `format` |
| **collections** | `Stack`, `Queue` data structures |

> See the full [standard library reference](https://vibe-lang-docs.vercel.app) for detailed documentation.

## Interactive REPL

```
$ vibe repl

  ╦  ╦╦╔╗ ╔═╗
  ╚╗╔╝║╠╩╗║╣
   ╚╝ ╩╚═╝╚═╝  v1.0.0

  Welcome to the Vibe REPL!
  Type .help for commands or start typing code.

vibe 1 ❯ let x = 42
vibe 2 ❯ x ** 2
1764
vibe 3 ❯ [1,2,3].map((n) => n * 10)
[10, 20, 30]
```

## Compile to Native

```bash
vibe build myapp.vibe
# Creates: myapp.cpp + myapp (native executable)
./myapp
```

## VS Code Extension

Get full IDE support for `.vibe` files:

- Syntax highlighting
- IntelliSense & auto-completion
- 40+ code snippets
- Real-time diagnostics
- Hover info for built-ins

Install from the [VS Code Marketplace](https://marketplace.visualstudio.com) or download the `.vsix` from [Vibe-Language-Extension releases](https://github.com/Vibe-Programming-Language/Vibe-Language-Extension/releases).

## Project Structure

```
src/
├── main.cpp          # CLI & REPL entry point
├── lexer/
│   ├── lexer.cpp     # Tokenizer
│   ├── lexer.h
│   └── tokens.h      # Token types
├── parser/
│   ├── parser.cpp    # Recursive descent parser
│   ├── parser.h
│   └── ast.h         # AST node definitions
├── runtime/
│   ├── runtime.cpp   # Tree-walking interpreter
│   └── runtime.h
└── codegen/
    ├── codegen.cpp   # C++ transpiler
    └── codegen.h
```

## Ecosystem

| Repository | Description |
|------------|-------------|
| [**Vibe**](https://github.com/Vibe-Programming-Language/Vibe) | Core language — lexer, parser, runtime, codegen, CLI, REPL |
| [**Vibe-Docs**](https://github.com/Vibe-Programming-Language/Vibe-Docs) | Documentation — [vibe-lang-docs.vercel.app](https://vibe-lang-docs.vercel.app) |
| [**Vibe-Language-Extension**](https://github.com/Vibe-Programming-Language/Vibe-Language-Extension) | VS Code extension |

## Examples

See the [`examples/`](examples/) directory for sample programs:

- `hello.vibe` — Hello World
- `fib.vibe` — Fibonacci sequence with timing
- `functions.vibe` — Functions and closures
- `showcase.vibe` — Full language feature showcase
- `match_demo.vibe` — Pattern matching examples
- `snake.vibe` — Snake game

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Commit your changes: `git commit -m "Add my feature"`
4. Push: `git push origin feature/my-feature`
5. Open a Pull Request

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

<p align="center">
  <a href="https://vibe-lang-docs.vercel.app">Documentation</a> · <a href="https://github.com/Vibe-Programming-Language/Vibe/releases">Releases</a> · <a href="https://github.com/Vibe-Programming-Language/Vibe-Language-Extension">VS Code Extension</a>
</p>

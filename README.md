# Vibe Programming Language

<p align="center">
  <strong>A modern, expressive language that compiles to C++ — built for clarity, speed, and joy.</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.0.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/language-C%2B%2B17-orange" alt="C++17">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-green" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-purple" alt="License">
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
- **VS Code extension** — Syntax highlighting and file icons

## Installation

### Prerequisites

- C++17 compiler (`g++ >= 7` or `clang++ >= 5`)
- CMake 3.20+

### Build from Source

```bash
git clone https://github.com/aspect-dev/vibe-lang.git
cd vibe-lang

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Optional: install system-wide
sudo make install
```

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
```

### Functions

```vibe
fn fibonacci(n) {
  if n <= 1 { return n; }
  return fibonacci(n - 1) + fibonacci(n - 2);
}

print(fibonacci(10));  // 55
```

### Classes

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
  init(name) {
    super(name, "Woof!");
  }
}

var dog = Dog("Rex");
dog.speak();  // Rex says Woof!
```

### Pattern Matching

```vibe
match status {
  200 => print("OK"),
  404 => print("Not Found"),
  500 => print("Server Error"),
  _ => print("Unknown: " + str(status)),
}
```

### Lambdas & Functional

```vibe
var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

var evens = numbers.filter((x) => x % 2 == 0);
var doubled = evens.map((x) => x * 2);
var sum = doubled.reduce((a, b) => a + b, 0);

print(sum);  // 60
```

### Error Handling

```vibe
try {
  var result = riskyOperation();
} catch e {
  print("Error: " + e);
} finally {
  print("Done");
}
```

### Modules

```vibe
import math;
import io;

print(math.sqrt(16));     // 4
print(math.PI);           // 3.14159...

io.writeFile("out.txt", "Hello!");
var data = io.readFile("out.txt");
```

## Standard Library

### Built-in Functions

`print`, `input`, `len`, `str`, `int`, `float`, `type`, `range`, `abs`, `min`, `max`, `sum`, `sorted`, `reversed`, `zip`, `enumerate`, `flatten`, `unique`, `map`, `reduce`, `filter`, `join`, `keys`, `values`, `chr`, `ord`, `format`, `assert`, `clock`, `timestamp`, `random`, `hash`, `toJSON`, `exit`, and more.

### Modules

| Module | Description |
|--------|-------------|
| `math` | sqrt, pow, trig, floor, ceil, round, random, factorial, isPrime, gcd, lcm |
| `io` | readFile, writeFile, appendFile, readLines, exists |
| `os` | exec, env, platform, cwd, sleep |
| `time` | now, millis, sleep, measure |
| `json` | stringify |
| `string` | ascii_letters, digits, repeat, format |
| `collections` | Stack, Queue data structures |

### String Methods

`.upper()`, `.lower()`, `.trim()`, `.split()`, `.contains()`, `.startsWith()`, `.endsWith()`, `.replace()`, `.slice()`, `.indexOf()`, `.repeat()`, `.reverse()`, `.charAt()`, `.chars()`, `.padStart()`, `.padEnd()`, `.count()`, `.isDigit()`, `.isAlpha()`, `.toInt()`, `.toFloat()`

### List Methods

`.push()`, `.pop()`, `.sort()`, `.reverse()`, `.map()`, `.filter()`, `.reduce()`, `.forEach()`, `.find()`, `.findIndex()`, `.some()`, `.every()`, `.flat()`, `.slice()`, `.join()`, `.contains()`, `.indexOf()`, `.insert()`, `.clear()`, `.count()`, `.first()`, `.last()`

## REPL

The Vibe REPL features:

- 🎨 Color-coded output by type
- ⚡ Auto-semicolons — just type expressions
- 📝 Multi-line input with brace matching
- 🔧 Commands: `.help`, `.clear`, `.reset`, `.exit`
- 💾 Persistent state across lines

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
```

## Building Native Binaries

Vibe can transpile your code to C++ and compile it:

```bash
vibe build myapp.vibe
./myapp
```

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

## Examples

See the [`examples/`](examples/) directory for sample programs:

- `hello.vibe` — Hello World
- `fib.vibe` — Fibonacci sequence with timing
- `functions.vibe` — Functions and closures
- `showcase.vibe` — Full language feature showcase
- `match_demo.vibe` — Pattern matching examples
- `snake.vibe` — Snake game 🐍

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
  Made with ❤️ by the Vibe team
</p>

# Vibe Language Specification v2.0

## 1. Scope
This specification defines Vibe v2.0 language syntax, semantics, standard library modules, and naming conventions.

## 2. Naming Conventions
- Module names: camelCase (examples: `web3`, `async`, `neural`)
- Class names: PascalCase
- Function names: camelCase
- Constants: UPPER_SNAKE_CASE

## 3. Core Syntax

### 3.1 Variables and Constants
```vibe
let count = 10
const MAX_RETRIES = 3
let score: Float = 99.5
```

### 3.2 Functions
```vibe
fn add(a: Int, b: Int) -> Int {
    return a + b
}

async fn fetchJson(url: String) {
    let res = await web.get(url)
    return res.json()
}
```

### 3.3 Pattern Matching
```vibe
match value {
    case 0 => show "zero"
    case 1..10 => show "small"
    case n if n > 100 => show "big: {n}"
    case [first, ...rest] => show "head: {first}"
    case { name, age } => show "{name} {age}"
    case _ => show "unknown"
}
```

### 3.4 Pipe Operator
```vibe
let total = [1,2,3,4]
    |> filter(x => x > 1)
    |> map(x => x * 2)
    |> reduce((a, b) => a + b)
```

### 3.5 Destructuring
```vibe
let { name, age, ...rest } = person
let [first, second, ...others] = values
```

### 3.6 Enums
```vibe
enum Color {
    Red,
    Green,
    Blue,
    Custom(r: Int, g: Int, b: Int)
}
```

### 3.7 Generics
```vibe
fn swap<T>(a: T, b: T) -> (T, T) {
    return (b, a)
}

class Container<T> {
    let items: [T] = []
    fn add(item: T) { items.append(item) }
    fn get(index: Int) -> T { return items[index] }
}
```

### 3.8 Interfaces/Protocols
```vibe
interface Drawable {
    fn draw()
    fn area() -> Float
}
```

### 3.9 Error Handling
```vibe
try {
    let text = fs.read("config.json")
} catch FileNotFound as e {
    show e.message
} finally {
    show "done"
}
```

### 3.10 Decorators
```vibe
@memoize
fn fib(n: Int) -> Int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

### 3.11 Macros
```vibe
macro unless(condition, body) {
    if !condition { body }
}
```

### 3.12 Operator Overloading
```vibe
class Vector2D {
    let x: Float
    let y: Float

    operator +(other: Vector2D) -> Vector2D {
        return Vector2D(x + other.x, y + other.y)
    }
}
```

### 3.13 Type Aliases
```vibe
type StringList = [String]
type Callback = fn(Int) -> Bool
```

### 3.14 Null Safety
```vibe
let name: String? = getUserName()
let length = name?.length ?? 0
let definite = name!
```

### 3.15 Strings, Regex, Ranges, Iterators
```vibe
let letter = "v"
let text = "Hello {letter}"
let regex = /^[a-z]+$/

for i in 0..10 { }
for i in 0...10 { }
for i in 10..0 step -2 { }
```

## 4. Built-in Modules

### 4.1 dsa
Data structures: Stack, Queue, PriorityQueue, Deque, LinkedList, DoublyLinkedList, CircularLinkedList, BinaryTree, BinarySearchTree, AVLTree, RedBlackTree, Heap (MinHeap/MaxHeap), Trie, Graph, HashMap, HashSet, BloomFilter, DisjointSet, SegmentTree, FenwickTree, SuffixArray, SuffixTree, Matrix.

Algorithms:
- Sorting: bubbleSort, mergeSort, quickSort, heapSort, radixSort, countingSort, bucketSort, timSort, insertionSort, selectionSort, shellSort
- Searching: binarySearch, linearSearch, ternarySearch, jumpSearch, exponentialSearch, interpolationSearch
- Graph: bfs, dfs, dijkstra, bellmanFord, floydWarshall, kruskal, prim, topologicalSort, tarjan, kosaraju, astar
- Dynamic programming: knapsack, lcs, lis, editDistance, coinChange, matrixChain, fibonacci
- String: kmp, rabinKarp, zAlgorithm, manacher, levenshtein
- Math: gcd, lcm, sieve, isPrime, modPow, nCr, nPr, factorial
- Backtracking: nQueens, sudokuSolver, permutations, combinations, subsets

### 4.2 neural
Tensor ops, autograd, GPU/CPU dispatch, layers (Dense, Conv, LSTM, Transformer), activations, losses, optimizers, model APIs (Sequential/Model), callbacks, dataloaders, pretrained models.

### 4.3 ai
Classical ML, NLP, CV, RL, AutoML, metrics and pipelines.

### 4.4 data
DataFrame/Series/date/time APIs, file I/O, group operations, windows, stats module.

### 4.5 viz
2D/3D charts, themes, dashboards, animation, export.

### 4.6 image
Image I/O, transforms, effects, morphology, drawing, vision ops.

### 4.7 web
HTTP server/client, websocket, templates, REST builders.

### 4.8 db
SQLite/PostgreSQL/MySQL/MongoDB/Redis, CRUD, transactions, migrations, ORM model.

### 4.9 crypto
Hashing, HMAC, encrypt/decrypt, key management, signatures, bcrypt, JWT, TOTP.

### 4.10 fs
File operations, glob/walk/watch, archives, csv/json/yaml/xml helpers.

### 4.11 async
`async fn`, `await`, task combinators, channels, mutex/semaphore, workers/pools.

### 4.12 test
Suites, assertions, hooks, mocks/spies, benchmarks, runner.

### 4.13 math
Constants, arithmetic, trig, random, algebra, calculus, optimization, interpolation, number theory.

### 4.14 audio
Signal analysis, transforms, DSP, STT/TTS/voice clone, classification.

### 4.15 cloud
Deployment, AWS/GCP/Azure, Docker/K8s, CI pipelines.

### 4.16 game
Windowing, sprites, scenes, camera, physics, input, particles, loop.

### 4.17 web3
Wallets, balance/transfer, contracts, NFT, token, IPFS, signing.

### 4.18 iot
Serial/GPIO/I2C/SPI/MQTT/Bluetooth/sensors/motors/displays.

### 4.19 robot
Arm, drone, mobile robot, SLAM, planning, simulation, ROS.

### 4.20 quantum
Qubits, circuits, gates, simulators, Grover, Shor, QFT, VQE, QAOA, noise models.

## 5. Error Model
- Synchronous errors use `try/catch/finally`
- Functional errors use `Result<T, E>` with `Ok` and `Err`
- Null handling uses nullable types and null-safe operators.

## 6. Compatibility
- v2.0 preserves v1.x core syntax and behavior.
- New modules and syntax are additive and do not remove existing constructs.

## 7. Tooling Contract
- `.vibe` extension
- VS Code grammar scope `source.vibe`
- Language config includes comments/brackets/folding/word pattern/on-enter rules.

## 8. Status
This is the normative language spec for Vibe v2.0.

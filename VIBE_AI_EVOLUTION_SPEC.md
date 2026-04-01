# Vibe Evolution Spec: AI-First, High-Performance, Production-Ready

Version: v2.1 proposal
Audience: language/runtime/compiler/tooling engineers

## 1. Language Feature Design

### 1.1 Design principles
- Keep Python-like readability.
- Compile to efficient native code paths (C++ backend + optional GPU runtime).
- Prefer explicit but short syntax.
- Make AI/data primitives first-class, not bolted-on libraries.

### 1.2 Core additions

#### Native tensor type
- New built-in type: Tensor<T, Rank>
- Runtime shape metadata + optional static shape hints.
- Supports CPU and GPU device handles.

Example:

```vibe
let x = tensor([[1.0, 2.0], [3.0, 4.0]], dtype: Float32)
let y = tensor.ones([2, 2])
let z = x.matmul(y)
show z.shape
```

Compiler/runtime notes:
- Lower tensor ops to optimized kernels.
- Use fused ops pass for common patterns (matmul + bias + activation).
- Device-aware allocator (pinned host memory + GPU pools).

#### Autograd engine
- Dynamic tape with optional graph capture mode.
- Tensor has requiresGrad flag.
- backward() computes gradients; grad() retrieves leaf gradients.

```vibe
let w = tensor.randn([128, 64], requiresGrad: true)
let x = tensor.randn([32, 128])
let y = x.matmul(w).mean()
y.backward()
show w.grad().shape
```

Runtime notes:
- Node struct: op, inputs, saved tensors, backward closure.
- Gradient accumulation uses atomic-friendly reduction for parallel backend.

#### Model primitives in language layer
- Built-ins for model composition:
  - module, forward, parameters(), train(), eval()
- Layer construction remains in neural stdlib but parser supports model ergonomics.

```vibe
module MLP {
    let l1 = neural.Dense(784, 256)
    let l2 = neural.Dense(256, 10)

    fn forward(x) {
        return x |> l1 |> neural.relu() |> l2 |> neural.softmax()
    }
}
```

#### Parallel primitives
- Existing async stays; add data-parallel helper constructs:
  - parallel for
  - reduce parallel
  - pipeline workers

```vibe
parallel for i in 0..n {
    out[i] = heavyCompute(inp[i])
}
```

Compiler notes:
- Lower to thread pool with chunked scheduling.
- SIMD vectorization pass for numeric loops.

#### Data pipeline primitives
- First-class pipeline combinators:
  - stream(), map(), filter(), batch(), prefetch(), cache(), shuffle()

```vibe
let ds = data.stream("train.csv")
    |> map(row => preprocess(row))
    |> filter(row => row.valid)
    |> batch(64)
    |> prefetch(4)
```

#### DSA-first utilities
- Standardized container interfaces:
  - Iterable, MutableCollection, OrderedCollection
- Performance contracts documented (expected Big-O behavior).

---

## 2. Library Architecture

### 2.1 Package layout
- neural: tensor + autograd + deep learning layers.
- ai: ML algorithms, metrics, training/eval orchestration.
- data: dataframe, io connectors, transforms.
- image: image io + transforms + CV helpers.
- dsa: containers + algorithms.

### 2.2 neural library

Key namespaces:
- neural.Tensor
- neural.layers
- neural.losses
- neural.optim
- neural.data

Minimal API:

```vibe
let t = neural.Tensor.randn([32, 128])
let model = neural.Sequential([
    neural.Dense(128, 64, activation: "relu"),
    neural.Dense(64, 10, activation: "softmax")
])
let opt = neural.Adam(model.parameters(), lr: 0.001)
```

Performance notes:
- Kernel fusion for activation + bias patterns.
- Mixed precision path (Float16 compute, Float32 master weights).
- Gradient checkpointing toggle for memory-constrained training.

### 2.3 ai library

Minimal API:

```vibe
let clf = ai.RandomForest(nEstimators: 200, maxDepth: 16)
clf.fit(trainX, trainY)
let yhat = clf.predict(testX)
show ai.f1Score(testY, yhat)
```

Modules:
- classical, nlp, vision, rl, automl, metrics, pipeline.

Performance notes:
- Parallel tree growth for ensembles.
- Batched inference for NLP/vision wrappers.

### 2.4 data library

Minimal API:

```vibe
let df = data.readCSV("sales.csv")
let agg = df.filter(r => r.revenue > 1000)
            .groupBy("region")
            .aggregate({ revenue: "sum", orders: "count" })
```

Connectors:
- CSV, JSON, SQL, Parquet.
- lazy scan mode for large datasets.

Performance notes:
- Columnar internal representation.
- Predicate pushdown for SQL/Parquet backends.

### 2.5 image library

Minimal API:

```vibe
let img = image.load("cat.jpg")
let out = img.resize(224, 224)
             .normalize()
             .toTensor()
```

Performance notes:
- SIMD resize/normalize kernels.
- Zero-copy handoff to Tensor when possible.

### 2.6 dsa library

Minimal API:

```vibe
let g = dsa.Graph(directed: true)
g.addEdge("A", "B", weight: 5)
show dsa.dijkstra(g, source: "A", target: "B")
```

Performance notes:
- Cache-friendly heaps and adjacency lists.
- Non-recursive DFS fallback for deep graphs.

---

## 3. Syntax Specification

### 3.1 Tensor syntax

```vibe
let a = tensor([[1, 2], [3, 4]], dtype: Float32, device: "gpu")
let b = tensor.randn([2, 2])
let c = a @ b   // matmul operator sugar
```

### 3.2 Model definition

```vibe
let model = neural.Sequential([
    neural.Conv2D(32, kernelSize: 3, padding: "same", activation: "relu"),
    neural.MaxPool2D(2),
    neural.Flatten(),
    neural.Dense(128, activation: "relu"),
    neural.Dense(10, activation: "softmax")
])
```

### 3.3 Training loop syntax

```vibe
model.compile(
    optimizer: neural.Adam(lr: 0.001),
    loss: neural.CrossEntropyLoss(),
    metrics: ["accuracy"]
)

for epoch in 0..20 {
    for (xb, yb) in trainLoader {
        let preds = model(xb)
        let loss = neural.CrossEntropyLoss()(preds, yb)
        loss.backward()
        model.optimizer.step()
        model.optimizer.zeroGrad()
    }
}
```

### 3.4 Pipeline syntax

```vibe
let ds = data.stream("train.csv")
    |> map(row => row.toFeatures())
    |> batch(128)
    |> prefetch(2)
```

### 3.5 Parallel syntax

```vibe
let result = parallel.reduce(values, init: 0, fn(acc, v) => acc + v)
```

---

## 4. Example Programs

### 4.1 Neural classification

```vibe
import neural
import data

let [trainX, testX, trainY, testY] = neural.trainTestSplit(x, y, testSize: 0.2)

let model = neural.Sequential([
    neural.Dense(784, 256, activation: "relu"),
    neural.Dropout(0.2),
    neural.Dense(256, 10, activation: "softmax")
])

model.compile(
    optimizer: neural.Adam(lr: 0.001),
    loss: neural.CrossEntropyLoss(),
    metrics: ["accuracy"]
)

model.fit(trainX, trainY, epochs: 10, batchSize: 64)
show model.evaluate(testX, testY)
```

### 4.2 Data + ML pipeline

```vibe
import data
import ai

let df = data.readCSV("churn.csv")
let X = df.dropColumn("label")
let y = df.select("label")

let scaler = ai.FeatureScaler()
let Xs = scaler.standardize(X)

let clf = ai.XGBoost(nEstimators: 300, maxDepth: 8, learningRate: 0.05)
clf.fit(Xs, y)
show clf.score(Xs, y)
```

### 4.3 Image classification preprocessing

```vibe
import image
import neural

let img = image.load("sample.jpg")
let x = img.resize(224, 224)
           .normalize()
           .toTensor()
           .reshape([1, 3, 224, 224])

let model = neural.pretrained.ResNet50()
show model.predict(x)
```

### 4.4 DSA shortest path utility

```vibe
import dsa

let g = dsa.Graph(directed: true)
g.addVertex("A")
g.addVertex("B")
g.addVertex("C")
g.addEdge("A", "B", weight: 1)
g.addEdge("B", "C", weight: 2)

show dsa.dijkstra(g, source: "A", target: "C")
```

---

## 5. Documentation (Ready-to-Paste Markdown)

### 5.1 Getting started block

```md
## AI and Deep Learning in Vibe

Vibe provides first-class support for tensors, autograd, neural network layers, optimizers, and data pipelines.

### Quick Start

```vibe
import neural

let model = neural.Sequential([
    neural.Dense(128, 64, activation: "relu"),
    neural.Dense(64, 10, activation: "softmax")
])
```

### Why Vibe vs Python/PyTorch?
- Less boilerplate for model and pipeline setup.
- Native compilation path for high-throughput inference.
- Unified language for data, ML, and systems workflows.
```

### 5.2 Best practices block

```md
## Best Practices

1. Start with Float32 and enable mixed precision only after baseline validation.
2. Use batch/prefetch pipelines for stable GPU utilization.
3. Keep model code declarative (Sequential/Model) and isolate custom kernels.
4. Add unit tests for tensor shape contracts and numerical stability.
5. Profile first, optimize second: focus on matmul hotspots and data loading.
```

### 5.3 Common errors block

```md
## Common Errors

- ShapeMismatchError: Tensor dimensions do not align for matmul/conv.
- DeviceMismatchError: CPU tensor used with GPU-only operation.
- NonFiniteGradientError: NaN/Inf detected during backward pass.
- DataSchemaError: Missing or unexpected dataframe columns.
```

---

## 6. VS Code Extension Changes (JSON + TS)

### 6.1 tmLanguage updates
Add/confirm scopes:
- keyword tensors: tensor, module, forward, backward
- modules: neural, ai, data, image, dsa
- operators: @, |>, ??, ?., .., ...

Example token rule:

```json
{
  "name": "support.module.vibe",
  "match": "\\b(neural|ai|data|image|dsa)\\b"
}
```

### 6.2 snippets additions
- tensor-create
- model-sequential
- train-loop
- data-pipeline
- dsa-graph

Example snippet:

```json
{
  "Tensor Create": {
    "prefix": "tensor",
    "body": [
      "let ${1:x} = tensor(${2:data}, dtype: ${3|Float32,Float64,Int32|}, device: \"${4|cpu,gpu|}\")"
    ],
    "description": "Create a tensor"
  }
}
```

### 6.3 extension.ts capabilities
- completion provider: tensor/neural/ai/data/image/dsa symbols.
- hover docs: key APIs and examples.
- diagnostics: shape hints + style/lint hints.
- code lens: Run for main, Run Tests for test blocks.

TypeScript shape-check hint example:

```ts
if (/matmul\(/.test(line) && /\[.*\]/.test(line) === false) {
  diagnostics.push(new vscode.Diagnostic(range, 'Consider explicit tensor shape checks before matmul.', vscode.DiagnosticSeverity.Information));
}
```

### 6.4 package.json contribution notes
- Keep language id as vibe.
- Include snippets and grammar paths.
- Add commands:
  - vibe.run
  - vibe.runTests
  - vibe.formatDocument
  - vibe.openDocs

---

## Implementation Phasing (Practical)

Phase 1 (2-4 weeks):
- Tensor core + autograd MVP.
- neural.Sequential + Dense + losses + SGD/Adam.
- VS Code grammar/snippets updates.

Phase 2 (4-8 weeks):
- Conv/RNN/Transformer blocks.
- DataFrame engine + connectors.
- GPU backend abstraction and kernel registry.

Phase 3 (8-12 weeks):
- Production optimizer passes, profiler, mixed precision, distributed primitives.
- Expanded AI/image/dsa ecosystems and benchmark suite.

This roadmap keeps Vibe aligned with its philosophy: simple syntax, strong performance, practical built-ins.

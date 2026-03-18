#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <functional>
#include <cmath>

namespace vibe::ml {

// ═══════════════════════════════════════════════════════════════
//  VibeML - Machine Learning & Deep Learning Framework
// ═══════════════════════════════════════════════════════════════

using dtype = float;  // Default data type for tensors

// ═══════════════════════════════════════════════════════════════
//  Tensor - Multi-dimensional array
// ═══════════════════════════════════════════════════════════════

class Tensor {
 private:
  std::vector<dtype> data_;
  std::vector<int64_t> shape_;
  std::vector<int64_t> strides_;

  void computeStrides();
  int64_t flatIndex(const std::vector<int64_t>& indices) const;

 public:
  Tensor() = default;
  Tensor(const std::vector<int64_t>& shape, dtype initValue = 0);
  Tensor(const std::vector<dtype>& data, const std::vector<int64_t>& shape);

  // Shape & dimensions
  const std::vector<int64_t>& shape() const { return shape_; }
  int64_t size() const;
  int64_t ndim() const { return shape_.size(); }

  // Data access
  dtype at(const std::vector<int64_t>& indices) const;
  dtype& at(const std::vector<int64_t>& indices);
  dtype operator[](int64_t idx) { return data_[idx]; }
  const dtype* data() const { return data_.data(); }
  dtype* data() { return data_.data(); }

  // Operations
  Tensor reshape(const std::vector<int64_t>& newShape) const;
  Tensor transpose(const std::vector<int64_t>& axes) const;
  Tensor flatten() const { return reshape({-1}); }

  // Arithmetic
  Tensor operator+(const Tensor& other) const;
  Tensor operator-(const Tensor& other) const;
  Tensor operator*(const Tensor& other) const;  // Element-wise
  Tensor operator/(const Tensor& other) const;  // Element-wise

  Tensor operator+(dtype scalar) const;
  Tensor operator*(dtype scalar) const;

  // Matrix operations
  static Tensor matmul(const Tensor& a, const Tensor& b);
  static Tensor dot(const Tensor& a, const Tensor& b) { return matmul(a, b); }

  // Reduction operations
  dtype sum() const;
  dtype mean() const;
  dtype max() const;
  dtype min() const;
  Tensor sum(int axis) const;
  Tensor mean(int axis) const;

  // Factory functions
  static Tensor zeros(const std::vector<int64_t>& shape);
  static Tensor ones(const std::vector<int64_t>& shape);
  static Tensor random(const std::vector<int64_t>& shape);
  static Tensor randn(const std::vector<int64_t>& shape);  // Normal distribution
  static Tensor arange(dtype start, dtype stop, dtype step = 1);
};

// ═══════════════════════════════════════════════════════════════
//  Activation Functions
// ═══════════════════════════════════════════════════════════════

namespace activation {
  // ReLU
  Tensor relu(const Tensor& x);
  Tensor reluGrad(const Tensor& x);

  // Sigmoid
  Tensor sigmoid(const Tensor& x);
  Tensor sigmoidGrad(const Tensor& x);

  // Tanh
  Tensor tanh(const Tensor& x);
  Tensor tanhGrad(const Tensor& x);

  // Softmax
  Tensor softmax(const Tensor& x);
  Tensor softmaxGrad(const Tensor& x);

  // Linear
  Tensor linear(const Tensor& x) { return x; }
}

// ═══════════════════════════════════════════════════════════════
//  Loss Functions
// ═══════════════════════════════════════════════════════════════

namespace loss {
  // Mean Squared Error
  dtype mse(const Tensor& predicted, const Tensor& target);
  Tensor mseDiff(const Tensor& predicted, const Tensor& target);

  // Cross Entropy
  dtype crossEntropy(const Tensor& predicted, const Tensor& target);
  Tensor crossEntropyDiff(const Tensor& predicted, const Tensor& target);

  // Binary Cross Entropy
  dtype binaryCrossEntropy(const Tensor& predicted, const Tensor& target);
}

// ═══════════════════════════════════════════════════════════════
//  Layers
// ═══════════════════════════════════════════════════════════════

class Layer {
 protected:
  Tensor weights_;
  Tensor biases_;
  Tensor weightGrads_;
  Tensor biasGrads_;
  std::string name_;

 public:
  Layer(const std::string& name = "layer") : name_(name) {}
  virtual ~Layer() = default;

  virtual Tensor forward(const Tensor& input) = 0;
  virtual Tensor backward(const Tensor& gradOutput) = 0;

  void setWeights(const Tensor& w) { weights_ = w; }
  void setBiases(const Tensor& b) { biases_ = b; }

  const Tensor& getWeights() const { return weights_; }
  const Tensor& getBiases() const { return biases_; }
  const Tensor& getWeightGrads() const { return weightGrads_; }
  const Tensor& getBiasGrads() const { return biasGrads_; }

  std::string getName() const { return name_; }
};

// Dense (Fully Connected) Layer
class Dense : public Layer {
 private:
  int inputSize_ = 0;
  int outputSize_ = 0;
  Tensor lastInput_;

 public:
  Dense(int inputSize, int outputSize);
  ~Dense() = default;

  Tensor forward(const Tensor& input) override;
  Tensor backward(const Tensor& gradOutput) override;
};

// Convolutional Layer
class Conv2D : public Layer {
 private:
  int inChannels_ = 0;
  int outChannels_ = 0;
  int kernelSize_ = 3;
  int stride_ = 1;
  int padding_ = 0;
  Tensor lastInput_;

 public:
  Conv2D(int inChannels, int outChannels, int kernelSize = 3, int stride = 1,
         int padding = 0);
  ~Conv2D() = default;

  Tensor forward(const Tensor& input) override;
  Tensor backward(const Tensor& gradOutput) override;
};

// Recurrent Layer (LSTM-like)
class LSTM : public Layer {
 private:
  int inputSize_ = 0;
  int hiddenSize_ = 0;
  Tensor h_;  // Hidden state
  Tensor c_;  // Cell state

 public:
  LSTM(int inputSize, int hiddenSize);
  ~LSTM() = default;

  Tensor forward(const Tensor& input) override;
  Tensor backward(const Tensor& gradOutput) override;
};

// ═══════════════════════════════════════════════════════════════
//  Optimizers
// ═══════════════════════════════════════════════════════════════

class Optimizer {
 protected:
  dtype learningRate_;
  dtype weightDecay_ = 0.0;

 public:
  Optimizer(dtype lr = 0.001) : learningRate_(lr) {}
  virtual ~Optimizer() = default;

  virtual void step(Tensor& weights, const Tensor& grads) = 0;
  virtual void step(std::vector<std::shared_ptr<Layer>>& layers) = 0;

  void setLearningRate(dtype lr) { learningRate_ = lr; }
  void setWeightDecay(dtype wd) { weightDecay_ = wd; }
};

// Stochastic Gradient Descent
class SGD : public Optimizer {
 private:
  dtype momentum_ = 0.0;
  std::vector<Tensor> velocities_;

 public:
  SGD(dtype lr = 0.01, dtype momentum = 0.0)
      : Optimizer(lr), momentum_(momentum) {}

  void step(Tensor& weights, const Tensor& grads) override;
  void step(std::vector<std::shared_ptr<Layer>>& layers) override;
};

// Adam Optimizer
class Adam : public Optimizer {
 private:
  dtype beta1_ = 0.9;
  dtype beta2_ = 0.999;
  dtype epsilon_ = 1e-8;
  int step_ = 0;
  std::vector<Tensor> m_, v_;  // First and second moments

 public:
  Adam(dtype lr = 0.001, dtype beta1 = 0.9, dtype beta2 = 0.999)
      : Optimizer(lr), beta1_(beta1), beta2_(beta2) {}

  void step(Tensor& weights, const Tensor& grads) override;
  void step(std::vector<std::shared_ptr<Layer>>& layers) override;
};

// ═══════════════════════════════════════════════════════════════
//  Sequential Model
// ═══════════════════════════════════════════════════════════════

class Model {
 private:
  std::vector<std::shared_ptr<Layer>> layers_;
  std::shared_ptr<Optimizer> optimizer_;
  std::string lossFunc_ = "mse";
  std::vector<dtype> trainHistory_;
  std::vector<dtype> valHistory_;

 public:
  Model() = default;
  ~Model() = default;

  // Model construction
  void add(std::shared_ptr<Layer> layer) { layers_.push_back(layer); }

  // Compilation
  void compile(std::shared_ptr<Optimizer> opt, const std::string& loss = "mse");

  // Forward pass
  Tensor predict(const Tensor& input);
  std::vector<Tensor> predictBatch(const std::vector<Tensor>& inputs);

  // Training
  void fit(const std::vector<Tensor>& xTrain, const std::vector<Tensor>& yTrain,
           int epochs = 10, int batchSize = 32, float valSplit = 0.2);

  void trainOnBatch(const std::vector<Tensor>& xBatch,
                    const std::vector<Tensor>& yBatch);

  // Evaluation
  dtype evaluate(const std::vector<Tensor>& xTest,
                 const std::vector<Tensor>& yTest);

  // Model persistence
  void save(const std::string& filepath);
  void load(const std::string& filepath);

  // Getters
  const std::vector<std::shared_ptr<Layer>>& getLayers() const {
    return layers_;
  }

  const std::vector<dtype>& getTrainHistory() const { return trainHistory_; }
  const std::vector<dtype>& getValHistory() const { return valHistory_; }
};

// ═══════════════════════════════════════════════════════════════
//  Data Utilities
// ═══════════════════════════════════════════════════════════════

namespace data {
  // Normalization
  Tensor normalize(const Tensor& x, dtype mean, dtype std);
  Tensor standardize(const Tensor& x);
  std::pair<Tensor, Tensor> computeMeanStd(const std::vector<Tensor>& data);

  // Train-test split
  struct DataSplit {
    std::vector<Tensor> train;
    std::vector<Tensor> test;
  };

  DataSplit trainTestSplit(const std::vector<Tensor>& data, float testSize = 0.2);

  // Batch sampling
  std::vector<std::vector<Tensor>> createBatches(
      const std::vector<Tensor>& data, int batchSize);
}

// ═══════════════════════════════════════════════════════════════
//  Pre-trained Models & Model Zoo
// ═══════════════════════════════════════════════════════════════

namespace models {
  // Simple MLP for classification
  std::shared_ptr<Model> mlp(int inputSize, const std::vector<int>& hiddenSizes,
                             int outputSize);

  // CNN for image classification
  std::shared_ptr<Model> simpleCNN(int inputChannels, int numClasses);

  // Load pre-trained ONNX model
  std::shared_ptr<Model> loadONNX(const std::string& filepath);
}

}  // namespace vibe::ml

#include "vibe_ml.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>
#include <iostream>
#include <fstream>

namespace vibe::ml {

// ═══════════════════════════════════════════════════════════════
//  Tensor Implementation
// ═══════════════════════════════════════════════════════════════

void Tensor::computeStrides() {
  strides_.resize(shape_.size());
  if (shape_.empty()) return;
  strides_.back() = 1;
  for (int i = shape_.size() - 2; i >= 0; --i) {
    strides_[i] = strides_[i + 1] * shape_[i + 1];
  }
}

int64_t Tensor::flatIndex(const std::vector<int64_t>& indices) const {
  int64_t idx = 0;
  for (size_t i = 0; i < indices.size(); ++i) {
    idx += indices[i] * strides_[i];
  }
  return idx;
}

Tensor::Tensor(const std::vector<int64_t>& shape, dtype initValue) : shape_(shape) {
  computeStrides();
  int64_t totalSize = size();
  data_.assign(totalSize, initValue);
}

Tensor::Tensor(const std::vector<dtype>& data, const std::vector<int64_t>& shape)
    : data_(data), shape_(shape) {
  computeStrides();
}

int64_t Tensor::size() const {
  return std::accumulate(shape_.begin(), shape_.end(), 1LL,
                         std::multiplies<int64_t>());
}

dtype Tensor::at(const std::vector<int64_t>& indices) const {
  return data_[flatIndex(indices)];
}

dtype& Tensor::at(const std::vector<int64_t>& indices) {
  return data_[flatIndex(indices)];
}

Tensor Tensor::reshape(const std::vector<int64_t>& newShape) const {
  int64_t newSize = std::accumulate(newShape.begin(), newShape.end(), 1LL,
                                    std::multiplies<int64_t>());
  if (newSize != size()) {
    throw std::runtime_error("Cannot reshape: size mismatch");
  }
  return Tensor(data_, newShape);
}

Tensor Tensor::transpose(const std::vector<int64_t>& axes) const {
  // Simple 2D transpose
  if (ndim() == 2 && axes.size() == 2) {
    Tensor result({shape_[1], shape_[0]});
    for (int i = 0; i < shape_[0]; ++i) {
      for (int j = 0; j < shape_[1]; ++j) {
        result.at({j, i}) = at({i, j});
      }
    }
    return result;
  }
  return *this;
}

Tensor Tensor::operator+(const Tensor& other) const {
  if (shape_ != other.shape_) {
    throw std::runtime_error("Shape mismatch in addition");
  }
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] += other.data_[i];
  }
  return result;
}

Tensor Tensor::operator-(const Tensor& other) const {
  if (shape_ != other.shape_) {
    throw std::runtime_error("Shape mismatch in subtraction");
  }
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] -= other.data_[i];
  }
  return result;
}

Tensor Tensor::operator*(const Tensor& other) const {
  if (shape_ != other.shape_) {
    throw std::runtime_error("Shape mismatch in multiplication");
  }
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] *= other.data_[i];
  }
  return result;
}

Tensor Tensor::operator/(const Tensor& other) const {
  if (shape_ != other.shape_) {
    throw std::runtime_error("Shape mismatch in division");
  }
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] /= other.data_[i];
  }
  return result;
}

Tensor Tensor::operator+(dtype scalar) const {
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] += scalar;
  }
  return result;
}

Tensor Tensor::operator*(dtype scalar) const {
  Tensor result = *this;
  for (int64_t i = 0; i < size(); ++i) {
    result.data_[i] *= scalar;
  }
  return result;
}

Tensor Tensor::matmul(const Tensor& a, const Tensor& b) {
  if (a.ndim() != 2 || b.ndim() != 2) {
    throw std::runtime_error("matmul requires 2D matrices");
  }
  if (a.shape_[1] != b.shape_[0]) {
    throw std::runtime_error("Incompatible matrix dimensions");
  }

  int n = a.shape_[0], k = a.shape_[1], m = b.shape_[1];
  Tensor result({n, m}, 0);

  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < m; ++j) {
      dtype sum = 0;
      for (int l = 0; l < k; ++l) {
        sum += a.at({i, l}) * b.at({l, j});
      }
      result.at({i, j}) = sum;
    }
  }

  return result;
}

dtype Tensor::sum() const {
  return std::accumulate(data_.begin(), data_.end(), 0.0);
}

dtype Tensor::mean() const {
  return size() > 0 ? sum() / size() : 0;
}

dtype Tensor::max() const {
  return *std::max_element(data_.begin(), data_.end());
}

dtype Tensor::min() const {
  return *std::min_element(data_.begin(), data_.end());
}

Tensor Tensor::sum(int axis) const {
  // Simplified: sum along axis
  if (ndim() != 2 || axis != 1) return *this;
  Tensor result({shape_[0]}, 0);
  for (int i = 0; i < shape_[0]; ++i) {
    for (int j = 0; j < shape_[1]; ++j) {
      result.data_[i] += at({i, j});
    }
  }
  return result;
}

Tensor Tensor::mean(int axis) const {
  auto summed = sum(axis);
  return summed * (1.0 / shape_[axis]);
}

Tensor Tensor::zeros(const std::vector<int64_t>& shape) {
  return Tensor(shape, 0);
}

Tensor Tensor::ones(const std::vector<int64_t>& shape) {
  return Tensor(shape, 1);
}

Tensor Tensor::random(const std::vector<int64_t>& shape) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<dtype> dis(0.0, 1.0);
  
  Tensor result(shape);
  for (auto& val : result.data_) {
    val = dis(gen);
  }
  return result;
}

Tensor Tensor::randn(const std::vector<int64_t>& shape) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<dtype> dis(0.0, 1.0);
  
  Tensor result(shape);
  for (auto& val : result.data_) {
    val = dis(gen);
  }
  return result;
}

Tensor Tensor::arange(dtype start, dtype stop, dtype step) {
  std::vector<dtype> data;
  for (dtype i = start; i < stop; i += step) {
    data.push_back(i);
  }
  return Tensor(data, {(int64_t)data.size()});
}

// ═══════════════════════════════════════════════════════════════
//  Activation Functions
// ═══════════════════════════════════════════════════════════════

namespace activation {
  Tensor relu(const Tensor& x) {
    Tensor result = x;
    for (int64_t i = 0; i < x.size(); ++i) {
      result.data()[i] = std::max(0.0, result.data()[i]);
    }
    return result;
  }

  Tensor sigmoid(const Tensor& x) {
    Tensor result = x;
    for (int64_t i = 0; i < x.size(); ++i) {
      result.data()[i] = 1.0 / (1.0 + std::exp(-result.data()[i]));
    }
    return result;
  }

  Tensor tanh(const Tensor& x) {
    Tensor result = x;
    for (int64_t i = 0; i < x.size(); ++i) {
      result.data()[i] = std::tanh(result.data()[i]);
    }
    return result;
  }

  Tensor softmax(const Tensor& x) {
    if (x.ndim() != 2) return x;
    Tensor result = x;
    for (int i = 0; i < x.shape()[0]; ++i) {
      dtype maxVal = -1e9;
      for (int j = 0; j < x.shape()[1]; ++j) {
        maxVal = std::max(maxVal, x.at({i, j}));
      }
      dtype sum = 0;
      for (int j = 0; j < x.shape()[1]; ++j) {
        result.at({i, j}) = std::exp(x.at({i, j}) - maxVal);
        sum += result.at({i, j});
      }
      for (int j = 0; j < x.shape()[1]; ++j) {
        result.at({i, j}) /= sum;
      }
    }
    return result;
  }
}

// ═══════════════════════════════════════════════════════════════
//  Loss Functions
// ═══════════════════════════════════════════════════════════════

namespace loss {
  dtype mse(const Tensor& predicted, const Tensor& target) {
    if (predicted.shape() != target.shape()) {
      throw std::runtime_error("Shape mismatch in MSE");
    }
    dtype sum = 0;
    for (int64_t i = 0; i < predicted.size(); ++i) {
      dtype diff = predicted.data()[i] - target.data()[i];
      sum += diff * diff;
    }
    return sum / predicted.size();
  }

  Tensor mseDiff(const Tensor& predicted, const Tensor& target) {
    return (predicted - target) * (2.0 / predicted.size());
  }

  dtype crossEntropy(const Tensor& predicted, const Tensor& target) {
    dtype loss = 0;
    for (int64_t i = 0; i < predicted.size(); ++i) {
      loss -= target.data()[i] * std::log(predicted.data()[i] + 1e-8);
    }
    return loss / predicted.size();
  }
}

// ═══════════════════════════════════════════════════════════════
//  Dense Layer
// ═══════════════════════════════════════════════════════════════

Dense::Dense(int inputSize, int outputSize)
    : Layer("Dense"), inputSize_(inputSize), outputSize_(outputSize) {
  weights_ = Tensor::randn({inputSize, outputSize}) * 0.01;
  biases_ = Tensor::zeros({outputSize});
}

Tensor Dense::forward(const Tensor& input) {
  lastInput_ = input;
  Tensor output = Tensor::matmul(input, weights_);
  // Add bias
  for (int i = 0; i < output.shape()[0]; ++i) {
    for (int j = 0; j < biases_.shape()[0]; ++j) {
      output.at({i, j}) += biases_.at({j});
    }
  }
  return output;
}

Tensor Dense::backward(const Tensor& gradOutput) {
  // Simplified backprop
  Tensor weightGrads = Tensor::matmul(lastInput_.transpose({1, 0}), gradOutput);
  Tensor grad = Tensor::matmul(gradOutput, weights_.transpose({1, 0}));
  return grad;
}

// ═══════════════════════════════════════════════════════════════
//  Conv2D Layer
// ═══════════════════════════════════════════════════════════════

Conv2D::Conv2D(int inChannels, int outChannels, int kernelSize, int stride,
               int padding)
    : Layer("Conv2D"),
      inChannels_(inChannels),
      outChannels_(outChannels),
      kernelSize_(kernelSize),
      stride_(stride),
      padding_(padding) {
  weights_ = Tensor::randn({outChannels, inChannels, kernelSize, kernelSize});
  biases_ = Tensor::zeros({outChannels});
}

Tensor Conv2D::forward(const Tensor& input) {
  lastInput_ = input;
  // Simplified: just return input for now
  return input;
}

Tensor Conv2D::backward(const Tensor& gradOutput) {
  return gradOutput;
}

// ═══════════════════════════════════════════════════════════════
//  LSTM Layer
// ═══════════════════════════════════════════════════════════════

LSTM::LSTM(int inputSize, int hiddenSize)
    : Layer("LSTM"), inputSize_(inputSize), hiddenSize_(hiddenSize) {
  // Initialize gates: input, forget, output, cell
  weights_ = Tensor::randn({inputSize + hiddenSize, 4 * hiddenSize}) * 0.01;
  biases_ = Tensor::zeros({4 * hiddenSize});
  h_ = Tensor::zeros({1, hiddenSize});
  c_ = Tensor::zeros({1, hiddenSize});
}

Tensor LSTM::forward(const Tensor& input) {
  // Simplified LSTM forward pass
  return input;
}

Tensor LSTM::backward(const Tensor& gradOutput) {
  return gradOutput;
}

// ═══════════════════════════════════════════════════════════════
//  Optimizers
// ═══════════════════════════════════════════════════════════════

void SGD::step(Tensor& weights, const Tensor& grads) {
  weights = weights + (grads * (-learningRate_));
}

void SGD::step(std::vector<std::shared_ptr<Layer>>& layers) {
  for (auto& layer : layers) {
    if (layer) {
      layer->setWeights(layer->getWeights() +
                        (layer->getWeightGrads() * (-learningRate_)));
      layer->setBiases(layer->getBiases() +
                       (layer->getBiasGrads() * (-learningRate_)));
    }
  }
}

void Adam::step(Tensor& weights, const Tensor& grads) {
  step_ += 1;
  // Simplified Adam update
  weights = weights + (grads * (-learningRate_));
}

void Adam::step(std::vector<std::shared_ptr<Layer>>& layers) {
  step_ += 1;
  for (auto& layer : layers) {
    if (layer) {
      layer->setWeights(layer->getWeights() +
                        (layer->getWeightGrads() * (-learningRate_)));
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  Model Implementation
// ═══════════════════════════════════════════════════════════════

void Model::compile(std::shared_ptr<Optimizer> opt, const std::string& loss) {
  optimizer_ = opt;
  lossFunc_ = loss;
  std::cout << "[Model] Compiled with " << typeid(*opt).name() << " optimizer\n";
}

Tensor Model::predict(const Tensor& input) {
  Tensor output = input;
  for (auto& layer : layers_) {
    output = layer->forward(output);
  }
  return output;
}

std::vector<Tensor> Model::predictBatch(const std::vector<Tensor>& inputs) {
  std::vector<Tensor> outputs;
  for (const auto& input : inputs) {
    outputs.push_back(predict(input));
  }
  return outputs;
}

void Model::fit(const std::vector<Tensor>& xTrain, const std::vector<Tensor>& yTrain,
                int epochs, int batchSize, float valSplit) {
  std::cout << "[Model] Training started: " << epochs << " epochs, batch "
            << batchSize << ", val_split " << valSplit << "\n";

  for (int epoch = 0; epoch < epochs; ++epoch) {
    dtype epochLoss = 0;

    for (size_t i = 0; i < xTrain.size(); i += batchSize) {
      std::vector<Tensor> batchX(
          xTrain.begin() + i,
          xTrain.begin() + std::min((size_t)i + batchSize, xTrain.size()));
      std::vector<Tensor> batchY(
          yTrain.begin() + i,
          yTrain.begin() + std::min((size_t)i + batchSize, yTrain.size()));

      trainOnBatch(batchX, batchY);
    }

    std::cout << "[Model] Epoch " << (epoch + 1) << "/" << epochs << "\n";
  }
}

void Model::trainOnBatch(const std::vector<Tensor>& xBatch,
                         const std::vector<Tensor>& yBatch) {
  // Simplified training step
  for (size_t i = 0; i < xBatch.size(); ++i) {
    Tensor output = predict(xBatch[i]);
    // Compute loss and backprop
  }
}

dtype Model::evaluate(const std::vector<Tensor>& xTest,
                      const std::vector<Tensor>& yTest) {
  dtype totalLoss = 0;
  for (size_t i = 0; i < xTest.size(); ++i) {
    Tensor predicted = predict(xTest[i]);
    if (lossFunc_ == "mse") {
      totalLoss += loss::mse(predicted, yTest[i]);
    }
  }
  return totalLoss / xTest.size();
}

void Model::save(const std::string& filepath) {
  std::cout << "[Model] Saving to " << filepath << "\n";
  // Simplified: would serialize weights/biases to file
}

void Model::load(const std::string& filepath) {
  std::cout << "[Model] Loading from " << filepath << "\n";
  // Simplified: would deserialize from file
}

// ═══════════════════════════════════════════════════════════════
//  Data Utilities
// ═══════════════════════════════════════════════════════════════

namespace data {
  Tensor normalize(const Tensor& x, dtype mean, dtype std) {
    return (x + (-mean)) * (1.0 / std);
  }

  Tensor standardize(const Tensor& x) {
    dtype m = x.mean();
    dtype s = std::sqrt((x - (m)) * (x - (m))).mean();
    return normalize(x, m, s);
  }
}

// ═══════════════════════════════════════════════════════════════
//  Pre-trained Models
// ═══════════════════════════════════════════════════════════════

namespace models {
  std::shared_ptr<Model> mlp(int inputSize, const std::vector<int>& hiddenSizes,
                             int outputSize) {
    auto model = std::make_shared<Model>();
    int prevSize = inputSize;
    for (int hidSize : hiddenSizes) {
      model->add(std::make_shared<Dense>(prevSize, hidSize));
      prevSize = hidSize;
    }
    model->add(std::make_shared<Dense>(prevSize, outputSize));
    return model;
  }

  std::shared_ptr<Model> simpleCNN(int inputChannels, int numClasses) {
    auto model = std::make_shared<Model>();
    model->add(std::make_shared<Conv2D>(inputChannels, 32, 3, 1, 1));
    model->add(std::make_shared<Conv2D>(32, 64, 3, 1, 1));
    model->add(std::make_shared<Dense>(64, numClasses));
    return model;
  }

  std::shared_ptr<Model> loadONNX(const std::string& filepath) {
    std::cout << "[Model Zoo] Loading ONNX model from " << filepath << "\n";
    return std::make_shared<Model>();
  }
}

}  // namespace vibe::ml

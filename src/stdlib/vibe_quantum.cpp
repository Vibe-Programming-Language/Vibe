#include "vibe_quantum.h"

#include <iostream>
#include <random>

namespace nova {
namespace quantum {

Qubit::Qubit() : alpha(1.0, 0.0), beta(0.0, 0.0) {}

void Qubit::apply_h_gate() {
    double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    std::complex<double> new_alpha = inv_sqrt2 * (alpha + beta);
    std::complex<double> new_beta = inv_sqrt2 * (alpha - beta);
    alpha = new_alpha;
    beta = new_beta;
}

void Qubit::apply_x_gate() {
    std::swap(alpha, beta);
}

void Qubit::apply_z_gate() {
    beta = -beta;
}

int Qubit::measure() {
    double p_zero = std::norm(alpha);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    double rand_val = dis(gen);
    if (rand_val < p_zero) {
        alpha = {1.0, 0.0};
        beta = {0.0, 0.0};
        return 0;      
    } else {
        alpha = {0.0, 0.0};
        beta = {1.0, 0.0};
        return 1;
    }
}

// Simple Circuit
QuantumCircuit::QuantumCircuit(int num_qubits) : qubits_(num_qubits) {}

void QuantumCircuit::h(int q) {
    if (q >= 0 && q < qubits_.size())
        qubits_[q].apply_h_gate();
}

void QuantumCircuit::cx(int control, int target) {
    // In a fully generalized quantum simulator this would be a large matrix tensor product.
    // For standalone qubit mock state: apply NOT to target if control measures 1.
    // This is inaccurate for true entanglement, meant as a minimal transpiler mockup.
    // But we simulate a toy model here.
    if (control >= 0 && target >= 0 && control < qubits_.size() && target < qubits_.size()) {
       std::cout << "[Quantum|cx] Approximated Controlled-NOT applied target " << target << std::endl;
    }
}

std::vector<int> QuantumCircuit::measure_all() {
    std::vector<int> results;
    for (auto& q : qubits_) {
        results.push_back(q.measure());
    }
    return results;
}

} // namespace quantum
} // namespace nova
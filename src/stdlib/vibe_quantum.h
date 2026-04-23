#pragma once

#include <string>
#include <vector>
#include <complex>

namespace nova {
namespace quantum {

class Qubit {
public:
    Qubit();
    // Probabilistic representation: alpha|0> + beta|1>
    std::complex<double> alpha;
    std::complex<double> beta;

    void apply_h_gate(); // Hadamard
    void apply_x_gate(); // Pauli-X (NOT)
    void apply_z_gate(); // Pauli-Z
    int measure();       // Collapses state to 0 or 1
};

class QuantumCircuit {
public:
    QuantumCircuit(int num_qubits);
    void h(int q);
    void cx(int control, int target); // CNOT gate
    std::vector<int> measure_all();
private:
    std::vector<Qubit> qubits_;
};

} // namespace quantum
} // namespace nova
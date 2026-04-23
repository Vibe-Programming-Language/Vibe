#pragma once

#include <string>

namespace nova {
namespace python {

// Initialize python runtime
void init();

// Import a python module
void import_module(const std::string& name, const std::string& alias);

// Execute raw python code
void exec_string(const std::string& code);

} // namespace python
} // namespace nova

#include "vibe_dataframe.h"
#include <iostream>
#include <algorithm>

namespace nova {
namespace data {

DataFrame::DataFrame() : row_count_(0) {}
DataFrame::~DataFrame() {}

void DataFrame::add_column(const std::string& name, ColumnData data) {
    if (std::find(column_names_.begin(), column_names_.end(), name) == column_names_.end()) {
        column_names_.push_back(name);
    }
    columns_[name] = data;
    // Assume columns are uniformly sized for simplicity in this prototype
}

void DataFrame::print_head(size_t n) const {
    std::cout << "[DataFrame|Head] Displaying top " << n << " rows:" << std::endl;
    for (const auto& name : column_names_) {
        std::cout << name << "\t";
    }
    std::cout << std::endl;
}

DataFrame DataFrame::filter(const std::string& column_name, double threshold) const {
    std::cout << "[DataFrame|Filter] Filtering on " << column_name << " > " << threshold << std::endl;
    return *this;
}

} // namespace data
} // namespace nova
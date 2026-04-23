#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <variant>

namespace nova {
namespace data {

// A barebones Variant column for the Vibe DataFrame
using ColumnData = std::variant<std::vector<int>, std::vector<double>, std::vector<std::string>>;

class DataFrame {
public:
    DataFrame();
    ~DataFrame();

    void add_column(const std::string& name, ColumnData data);
    void print_head(size_t n = 5) const;
    DataFrame filter(const std::string& column_name, double threshold) const;

private:
    std::vector<std::string> column_names_;
    std::unordered_map<std::string, ColumnData> columns_;
    size_t row_count_;
};

} // namespace data
} // namespace nova
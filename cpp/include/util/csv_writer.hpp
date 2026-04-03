#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

class CsvWriter {
public:
    CsvWriter(const std::string& path, const std::vector<std::string>& headers)
        : file_(path, std::ios::out | std::ios::trunc) {
        if (!file_.is_open()) throw std::runtime_error("CsvWriter: cannot open: " + path);
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file_ << ',';
            file_ << headers[i];
        }
        file_ << '\n';
    }

    ~CsvWriter() { file_.flush(); file_.close(); }

    void write_row(const std::vector<std::string>& fields) {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) file_ << ',';
            file_ << fields[i];
        }
        file_ << '\n';
    }

    void flush() { file_.flush(); }

private:
    std::ofstream file_;
};

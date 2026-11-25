#include "../../include/util/input_file.hpp"

#include <cstring>
#include <stdexcept>

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

InputFile::InputFile(const std::string& path) : path_(path) {
    if (path == "-") {
        fp_    = stdin;
        owned_ = false;
        return;
    }
    // Open the file itself first: popen succeeds even when the file is
    // missing, and the decompressor's own error is less clear.
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    const bool gz = ends_with(path, ".gz"), zip = ends_with(path, ".zip");
    if (!gz && !zip) {
        fp_ = f;
        return;
    }
    std::fclose(f);
    fp_   = ::popen(((gz ? "gzip -dc " : "unzip -p ") + shell_quote(path)).c_str(), "r");
    pipe_ = true;
    if (!fp_) throw std::runtime_error("cannot start decompressor for " + path);
}

InputFile::~InputFile() {
    if (!fp_ || !owned_) return;
    if (pipe_) ::pclose(fp_);
    else std::fclose(fp_);
}

void InputFile::finish() {
    if (!fp_) return;
    if (std::ferror(fp_)) throw std::runtime_error("read error in " + path_);
    if (!pipe_) return;
    FILE* fp = fp_;
    fp_ = nullptr;
    if (::pclose(fp) != 0)
        throw std::runtime_error("decompression failed (truncated or corrupt archive?): " + path_);
}

size_t InputFile::read(void* dst, size_t n) {
    if (!fp_) return 0;
    const size_t got = std::fread(dst, 1, n, fp_);
    if (got == 0) finish();
    return got;
}

bool InputFile::getline(std::string& line) {
    line.clear();
    if (!fp_) return false;
    char buf[1 << 16];
    while (std::fgets(buf, sizeof(buf), fp_)) {
        size_t n = std::strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') {
            line.append(buf, n - 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line.append(buf, n);
    }
    finish();
    return !line.empty();
}

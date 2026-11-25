#pragma once
#include <cstddef>
#include <cstdio>
#include <string>

// A read-only byte stream over a plain file, a `.gz` / `.zip` file (streamed
// through gzip / unzip, so multi-GB days never need expanding on disk), or
// standard input ("-").
//
// Reaching the end of a compressed stream checks the decompressor's exit
// status: a truncated or corrupt archive throws instead of looking like a
// short but complete day of data.
class InputFile {
public:
    explicit InputFile(const std::string& path);
    ~InputFile();
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;

    // Reads up to n bytes; 0 at end of file. Throws on a failed stream.
    size_t read(void* dst, size_t n);
    // Reads a line (without its newline / CR). False at end of file.
    bool getline(std::string& line);
    bool compressed() const { return pipe_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    FILE* fp_   = nullptr;
    bool  pipe_ = false;
    bool  owned_ = true;

    void finish();   // at end of file: verify the stream ended cleanly
};

bool ends_with(const std::string& s, const std::string& suffix);
// Single-quotes a string for /bin/sh.
std::string shell_quote(const std::string& s);

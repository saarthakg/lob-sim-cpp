#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Run options, shared by the command line and the Python module so both accept
// the same settings and reject the same mistakes. Keys are the command-line
// spellings ("--order-size"). Anything not in the known set is an error: a typo
// must not silently run an experiment with the default value.
class Options {
public:
    static Options parse(int argc, const char* const* argv);

    // Sets one option; flags take an empty value. Throws on an unknown name,
    // a missing value, or a value given to a flag.
    void set(const std::string& key, const std::string& value);
    static bool is_flag(const std::string& key);

    bool has(const std::string& k) const { return kv_.count(k) != 0; }
    std::string str(const std::string& k, const std::string& d = "") const {
        auto it = kv_.find(k);
        return it == kv_.end() ? d : it->second;
    }
    double num(const std::string& k, double d) const;

    // Pick from a fixed set of names, so a typo in a *value* is an error too.
    template <typename T>
    T choose(const std::string& key, const std::string& dflt, const std::map<std::string, T>& options) const {
        auto it = options.find(str(key, dflt));
        if (it != options.end()) return it->second;
        std::string list;
        for (const auto& [name, v] : options) { (void)v; list += (list.empty() ? "" : "|") + name; }
        throw std::runtime_error(key + " must be one of " + list + ", not " + str(key));
    }

    // Everything that was set (flags as "true"), for recording with results.
    std::map<std::string, std::string> all() const { return kv_; }

private:
    std::map<std::string, std::string> kv_;
};

std::vector<std::string> split_list(const std::string& comma_separated);
// HH:MM:SS → nanoseconds since midnight. Throws on a malformed time.
uint64_t time_of_day_ns(const std::string& hms);

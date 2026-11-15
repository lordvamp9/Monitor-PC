#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace telemetry {
inline std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> out;
    std::string part;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { part += '"'; ++i; }
            else quoted = !quoted;
        } else if (c == delimiter && !quoted) { out.push_back(part); part.clear(); }
        else if (c != '\r') part += c;
    }
    out.push_back(part);
    return out;
}
inline double number(const std::string& text) {
    try { size_t n = 0; double v = std::stod(text, &n);
        return n == text.size() && std::isfinite(v) ? v : -1;
    } catch (...) { return -1; }
}
// Processes sharing one physical engine contribute to its load. Independent
// engines must not be added together: adapter utilization is its busiest engine.
inline double busiestEngine(const std::map<std::string, double>& engines) {
    double result = -1;
    for (const auto& item : engines) result = std::max(result, item.second);
    return result < 0 ? -1 : std::clamp(result, 0.0, 100.0);
}
struct FrameSample { double elapsed = 0; unsigned count = 0; unsigned long long seen = 0; };
class Frames {
    std::vector<std::string> header;
    std::map<std::pair<unsigned long, std::string>, FrameSample> samples;
    int column(const char* name) const {
        auto it = std::find(header.begin(), header.end(), name);
        return it == header.end() ? -1 : static_cast<int>(it - header.begin());
    }
public:
    void ingest(const std::string& line, unsigned long long now) {
        auto row = split(line, ',');
        if (std::find(row.begin(), row.end(), "ProcessID") != row.end()) { header = row; return; }
        int pid = column("ProcessID"), chain = column("SwapChainAddress"), ms = column("MsBetweenPresents");
        if (pid < 0 || chain < 0 || ms < 0 || row.size() != header.size()) return;
        double process = number(row[pid]), delta = number(row[ms]);
        if (process <= 0 || process > 4294967295.0 || delta <= 0 || delta > 10000) return;
        auto& s = samples[{static_cast<unsigned long>(process), row[chain]}];
        s.elapsed += delta; ++s.count; s.seen = now;
    }
    double take(unsigned long pid, unsigned long long now) {
        double best = -1;
        for (auto it = samples.begin(); it != samples.end();) {
            auto& s = it->second;
            if (now - s.seen > 2000) { it = samples.erase(it); continue; }
            if (it->first.first == pid && s.count > 0 && s.elapsed > 0)
                best = std::max(best, 1000.0 * s.count / s.elapsed);
            s.count = 0; s.elapsed = 0; ++it;
        }
        return best;
    }
};
}

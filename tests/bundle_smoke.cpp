#include "hardware_monitor.h"
#include "runtime_bundle.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

int main() {
    try {
        for (const auto* name : {L"../escape", L"C:\\escape", L"\\escape", L"sensors/../../escape", L"file:stream"})
            if (IsSafeBundlePath(name)) throw std::runtime_error("Unsafe cabinet path accepted");
        if (!IsSafeBundlePath(L"sensors/MonitorSensors.exe")) throw std::runtime_error("Valid cabinet path rejected");
        const auto outside = GetRuntimeDirectory();
        PrepareRuntime();
        const auto runtime = GetRuntimeDirectory();
        if (runtime == outside || !std::filesystem::exists(runtime / L"sensors/MonitorSensors.exe") ||
            !std::filesystem::exists(runtime / L"tools/PresentMon.exe") ||
            !std::filesystem::exists(runtime / L"licenses/THIRD_PARTY_NOTICES.md"))
            throw std::runtime_error("The executable did not unpack its embedded dependencies");
        StartHardwareMonitor();
        bool measured = false;
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto m = GetLatestMetrics();
            measured = measured || (m.cpu_usage_percent >= 0 && m.ram_used_gb > 0);
        }
        StopHardwareMonitor();
        if (!measured) throw std::runtime_error("Bundled startup did not produce live system metrics");
        CleanupRuntime();
        if (std::filesystem::exists(runtime)) throw std::runtime_error("Collector workspace was not cleaned up");
        std::puts("Embedded collectors: extraction, live telemetry, shutdown and cleanup passed.");
        return 0;
    } catch (const std::exception& e) {
        StopHardwareMonitor(); CleanupRuntime();
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "hardware_monitor.h"
#include "telemetry.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <evntrace.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

namespace {
std::mutex metricsMutex;
HardwareMetrics latest;
SystemSpecs specs;
std::atomic<bool> stopping{false};
std::thread worker;
std::filesystem::path directory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return std::filesystem::path(path).parent_path();
}
// Own both the process and its descendants. Exit cannot leave collectors behind.
class Collector {
    HANDLE process = nullptr, pipe = nullptr, job = nullptr;
    std::string pending;
public:
    Collector() = default;
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;
    ~Collector() { stop(); }
    void stop() {
        if (job) { CloseHandle(job); job = nullptr; }
        if (process) { if (WaitForSingleObject(process, 1500) == WAIT_TIMEOUT) TerminateProcess(process, 0); CloseHandle(process); process = nullptr; }
        if (pipe) { CloseHandle(pipe); pipe = nullptr; }
        pending.clear();
    }
    bool start(const std::filesystem::path& exe, const std::wstring& args) {
        if (!std::filesystem::exists(exe)) return false;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE write = nullptr;
        if (!CreatePipe(&pipe, &write, &sa, 0)) return false;
        SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0);
        HANDLE nullFile = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = write; si.hStdError = nullFile; si.hStdInput = nullFile;
        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + exe.wstring() + L"\" " + args;
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(write); if (nullFile != INVALID_HANDLE_VALUE) CloseHandle(nullFile); stop(); return false;
        }
        BOOL ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, exe.parent_path().c_str(), &si, &pi);
        CloseHandle(write); if (nullFile != INVALID_HANDLE_VALUE) CloseHandle(nullFile);
        if (!ok) { stop(); return false; }
        process = pi.hProcess;
        if (!AssignProcessToJobObject(job, process)) { TerminateProcess(process, 1); CloseHandle(pi.hThread); stop(); return false; }
        ResumeThread(pi.hThread); CloseHandle(pi.hThread); return true;
    }
    template<class F> void poll(F onLine) {
        if (!pipe) return;
        // Bound work so an excessively busy collector cannot starve shutdown.
        for (int batch = 0; batch < 256; ++batch) {
            DWORD available = 0, read = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[8192];
            if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)) break;
            pending.append(buffer, read);
            size_t end;
            while ((end = pending.find('\n')) != std::string::npos) {
                onLine(pending.substr(0, end)); pending.erase(0, end + 1);
            }
            if (pending.size() > 65536) pending.clear();
        }
    }
};
struct Adapter { std::string key, name; };
std::vector<Adapter> adapters() {
    std::vector<Adapter> result;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return result;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) != S_OK) break;
        DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc); adapter->Release();
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        char name[256]{}, key[64]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        std::snprintf(key, sizeof(key), "luid_0x%08x_0x%08x", static_cast<unsigned>(desc.AdapterLuid.HighPart), static_cast<unsigned>(desc.AdapterLuid.LowPart));
        result.push_back({key, name});
    }
    factory->Release(); return result;
}
class GpuCounter {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
public:
    GpuCounter() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) { counter = nullptr; return; }
        PdhCollectQueryData(query);
    }
    ~GpuCounter() { if (query) PdhCloseQuery(query); }
    std::map<std::string, double> read(const std::vector<Adapter>& list) {
        std::map<std::string, double> result;
        if (!counter || PdhCollectQueryData(query) != ERROR_SUCCESS) return result;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA)) return result;
        std::vector<unsigned char> buffer(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(buffer.data());
        if (PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return result;
        std::map<std::string, std::map<std::string, double>> engines;
        for (DWORD i = 0; i < count; ++i) {
            auto& value = items[i].FmtValue;
            if (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) continue;
            std::string name = items[i].szName;
            auto engine = name.find("_eng_");
            if (engine == std::string::npos || !std::isfinite(value.doubleValue) || value.doubleValue < 0) continue;
            for (const auto& a : list) if (name.find(a.key) != std::string::npos)
                engines[a.key][name.substr(engine)] += value.doubleValue;
        }
        for (const auto& item : engines) result[item.first] = telemetry::busiestEngine(item.second);
        return result;
    }
};
unsigned long long ticks(FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
void run() {
    auto list = adapters();
    GpuCounter gpu;
    Collector sensors, presents;
    sensors.start(directory() / L"sensors" / L"MonitorSensors.exe", std::to_wstring(GetCurrentProcessId()));
    presents.start(directory() / L"tools" / L"PresentMon.exe",
        L"--output_stdout --no_console_stats --v1_metrics --no_track_gpu --no_track_display --no_track_input --session_name MonitorPC_" + std::to_wstring(GetCurrentProcessId()));
    telemetry::Frames frames;
    double cpuTemp = -1;
    ULONGLONG sensorSeen = 0;
    std::map<std::string, double> temperatures;
    std::map<std::string, double> sensorLoads;
    std::map<std::string, double> pendingTemperatures, pendingLoads;
    double pendingCpuTemp = -1;
    FILETIME idle{}, kernel{}, user{};
    GetSystemTimes(&idle, &kernel, &user);
    auto lastIdle = ticks(idle), lastTotal = ticks(kernel) + ticks(user);
    ULONGLONG next = GetTickCount64() + 1000;
    while (!stopping) {
        const auto now = GetTickCount64();
        sensors.poll([&](const std::string& line) {
            auto parts = telemetry::split(line, '\t');
            if (parts.size() == 2 && parts[0] == "C") { pendingCpuTemp = telemetry::number(parts[1]); pendingTemperatures.clear(); pendingLoads.clear(); }
            if (parts.size() == 4 && parts[0] == "G") {
                pendingTemperatures[parts[1]] = telemetry::number(parts[2]);
                pendingLoads[parts[1]] = telemetry::number(parts[3]);
            }
            if (parts.size() == 1 && parts[0] == "E") { cpuTemp = pendingCpuTemp; temperatures = pendingTemperatures; sensorLoads = pendingLoads; sensorSeen = now; }
        });
        presents.poll([&](const std::string& line) { frames.ingest(line, now); });
        if (now >= next) {
            next = now + 1000;
            HardwareMetrics m;
            if (GetSystemTimes(&idle, &kernel, &user)) {
                auto total = ticks(kernel) + ticks(user), idleNow = ticks(idle);
                if (total > lastTotal && idleNow >= lastIdle)
                    m.cpu_usage_percent = std::clamp(100.0 * (1.0 - double(idleNow - lastIdle) / double(total - lastTotal)), 0.0, 100.0);
                lastTotal = total; lastIdle = idleNow;
            }
            MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
            if (GlobalMemoryStatusEx(&memory)) {
                constexpr double GiB = 1024.0 * 1024 * 1024;
                m.ram_total_gb = memory.ullTotalPhys / GiB;
                m.ram_used_gb = (memory.ullTotalPhys - memory.ullAvailPhys) / GiB;
                m.ram_usage_percent = 100.0 * m.ram_used_gb / m.ram_total_gb;
            }
            auto loads = gpu.read(list);
            if (sensorSeen && now - sensorSeen < 5000) {
                for (const auto& a : list) {
                    auto fallback = sensorLoads.find(a.name);
                    if (loads.find(a.key) == loads.end() && fallback != sensorLoads.end() && fallback->second >= 0)
                        loads[a.key] = std::clamp(fallback->second, 0.0, 100.0);
                }
            }
            const Adapter* selected = list.empty() ? nullptr : &list.front();
            for (const auto& a : list) {
                auto found = loads.find(a.key);
                if (found != loads.end() && found->second > m.gpu_usage_percent) { selected = &a; m.gpu_usage_percent = found->second; }
            }
            std::snprintf(m.gpu_name, sizeof(m.gpu_name), "%s", selected ? selected->name.c_str() : "No supported graphics adapter");
            if (sensorSeen && now - sensorSeen < 5000) {
                m.cpu_temp = cpuTemp;
                if (selected) { auto t = temperatures.find(selected->name); if (t != temperatures.end()) m.gpu_temp = t->second; }
            }
            DWORD foreground = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
            m.fps = frames.take(foreground, now);
            std::lock_guard<std::mutex> lock(metricsMutex);
            std::copy(std::begin(latest.cpu_history), std::end(latest.cpu_history), m.cpu_history);
            std::copy(std::begin(latest.gpu_history), std::end(latest.gpu_history), m.gpu_history);
            m.history_index = (latest.history_index + 1) % HISTORY_SIZE;
            m.history_count = std::min(HISTORY_SIZE, latest.history_count + 1);
            m.cpu_history[m.history_index] = m.cpu_usage_percent;
            m.gpu_history[m.history_index] = m.gpu_usage_percent;
            latest = m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Stop only our unique ETW session before the job object reaps collectors.
    struct TraceProperties { EVENT_TRACE_PROPERTIES properties; wchar_t name[128]; } trace{};
    trace.properties.Wnode.BufferSize = sizeof(trace);
    trace.properties.LoggerNameOffset = offsetof(TraceProperties, name);
    const auto session = L"MonitorPC_" + std::to_wstring(GetCurrentProcessId());
    ControlTraceW(0, session.c_str(), &trace.properties, EVENT_TRACE_CONTROL_STOP);
}
}
SystemSpecs GetSystemSpecs() { return specs; }
void StartHardwareMonitor() {
    if (worker.joinable()) return;
    DWORD size = sizeof(specs.cpu_name);
    RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, specs.cpu_name, &size);
    stopping = false; worker = std::thread(run);
}
void StopHardwareMonitor() { stopping = true; if (worker.joinable()) worker.join(); }
HardwareMetrics GetLatestMetrics() { std::lock_guard<std::mutex> lock(metricsMutex); return latest; }

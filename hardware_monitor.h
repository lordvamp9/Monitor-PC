#pragma once
#include <windows.h>
constexpr int HISTORY_SIZE = 100;
// Negative values mean unavailable; readings are never simulated.
struct HardwareMetrics {
    double cpu_usage_percent = -1, ram_usage_percent = -1;
    double ram_total_gb = -1, ram_used_gb = -1;
    double gpu_usage_percent = -1, gpu_temp = -1, cpu_temp = -1, fps = -1;
    char gpu_name[128] = "Detecting graphics adapters...";
    double cpu_history[HISTORY_SIZE] = {}, gpu_history[HISTORY_SIZE] = {};
    int history_index = 0, history_count = 0;
};
struct SystemSpecs { char cpu_name[128] = "Unknown CPU"; };
SystemSpecs GetSystemSpecs();
void StartHardwareMonitor();
void StopHardwareMonitor();
HardwareMetrics GetLatestMetrics();

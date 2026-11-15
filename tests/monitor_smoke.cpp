#include "hardware_monitor.h"
#include <chrono>
#include <cstdio>
#include <thread>
int main() {
    StartHardwareMonitor();
    bool observed = false;
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto m = GetLatestMetrics();
        std::printf("CPU=%.1f RAM=%.2f GPU=%.1f CPUtemp=%.1f GPUtemp=%.1f FPS=%.1f adapter=%s\n",
            m.cpu_usage_percent, m.ram_used_gb, m.gpu_usage_percent, m.cpu_temp, m.gpu_temp, m.fps, m.gpu_name);
        observed = observed || (m.cpu_usage_percent >= 0 && m.cpu_usage_percent <= 100 && m.ram_used_gb > 0);
    }
    StopHardwareMonitor();
    return observed ? 0 : 1;
}

#ifndef HARDWARE_MONITOR_H
#define HARDWARE_MONITOR_H

#include <windows.h>

#define HISTORY_SIZE 100

typedef struct {
    double cpu_usage_percent;
    double ram_usage_percent;
    double ram_total_gb;
    double ram_used_gb;
    double gpu_usage_percent;
    
    double gpu_temp;
    double cpu_temp;
    
    int is_gaming_fullscreen;
    double fps;
    
    double cpu_history[HISTORY_SIZE];
    double gpu_history[HISTORY_SIZE];
    int history_index;
    int history_count;
} HardwareMetrics;

typedef struct {
    char cpu_name[128];
    char gpu_name[128];
    char os_name[128];
    char mobo_name[128];
    double total_ram_gb;
} SystemSpecs;

void InitSystemSpecs(void);
SystemSpecs GetSystemSpecs(void);

void StartHardwareMonitor(void);
void StopHardwareMonitor(void);

HardwareMetrics GetLatestMetrics(void);

#endif 

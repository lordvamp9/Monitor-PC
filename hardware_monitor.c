#include "hardware_monitor.h"
#include <process.h>
#include <stdio.h>
#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>

static HANDLE hMonitorThread = NULL;
static HANDLE hStopEvent = NULL;
static CRITICAL_SECTION csMetrics;
static HardwareMetrics currentMetrics = {0};
static SystemSpecs specs = {0};

static PDH_HQUERY cpuQuery;
static PDH_HCOUNTER cpuCounter;
static PDH_HQUERY gpuQuery;
static PDH_HCOUNTER gpuCounter;
static int pdhInitialized = 0;

typedef int* (*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)();
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** handles, unsigned int* count);
typedef int (*NvAPI_GPU_GetThermalSettings_t)(void* handle, unsigned int sensorIndex, void* thermalSettings);

#define NVAPI_QUERY_INTERFACE_OFFSET 0x0150E828
#define NVAPI_ENUM_PHYSICAL_GPUS_OFFSET 0xE5AC921F
#define NVAPI_GET_THERMAL_SETTINGS_OFFSET 0xE3640A56

typedef struct {
    int controller;
    int defaultMinTemp;
    int defaultMaxTemp;
    int currentTemp;
    int target;
} NV_SENSOR;

typedef struct {
    int version;
    int count;
    NV_SENSOR sensor[3];
} NV_GPU_THERMAL_SETTINGS_V1;

typedef struct {
    int version;
    int count;
    NV_SENSOR sensor[4];
} NV_GPU_THERMAL_SETTINGS_V2;

#define NV_GPU_THERMAL_SETTINGS_VER_1 (sizeof(NV_GPU_THERMAL_SETTINGS_V1) | (1 << 16))
#define NV_GPU_THERMAL_SETTINGS_VER_2 (sizeof(NV_GPU_THERMAL_SETTINGS_V2) | (2 << 16))

static HMODULE hNvAPI = NULL;
static NvAPI_QueryInterface_t NvAPI_QueryInterface = NULL;
static NvAPI_Initialize_t NvAPI_Initialize = NULL;
static NvAPI_EnumPhysicalGPUs_t NvAPI_EnumPhysicalGPUs = NULL;
static NvAPI_GPU_GetThermalSettings_t NvAPI_GPU_GetThermalSettings = NULL;
static void* gpuHandles[64] = {0};
static unsigned int gpuCount = 0;

typedef struct {
    int iSize;
    int iTemperature;
} ADLTemperature;

typedef void* (__stdcall *ADL_MEM_ALLOC)(int);
typedef int (*ADL_Main_Control_Create_t)(ADL_MEM_ALLOC, int);
typedef int (*ADL_Main_Control_Destroy_t)();
typedef int (*ADL_Adapter_NumberOfAdapters_Get_t)(int*);
typedef int (*ADL_Overdrive5_Temperature_Get_t)(int, int, ADLTemperature*);

static HMODULE hAdl = NULL;
static ADL_Main_Control_Create_t ADL_Main_Control_Create = NULL;
static ADL_Main_Control_Destroy_t ADL_Main_Control_Destroy = NULL;
static ADL_Adapter_NumberOfAdapters_Get_t ADL_Adapter_NumberOfAdapters_Get = NULL;
static ADL_Overdrive5_Temperature_Get_t ADL_Overdrive5_Temperature_Get = NULL;
static int adlInitialized = 0;

static void* __stdcall ADL_Main_Memory_Alloc(int iSize) {
    return malloc(iSize);
}

void InitGPUAPI() {
    
    hNvAPI = LoadLibraryA("nvapi64.dll");
    if (!hNvAPI) hNvAPI = LoadLibraryA("nvapi.dll");
    if (hNvAPI) {
        NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(hNvAPI, "nvapi_QueryInterface");
        if (NvAPI_QueryInterface) {
            NvAPI_Initialize = (NvAPI_Initialize_t)NvAPI_QueryInterface(NVAPI_QUERY_INTERFACE_OFFSET);
            NvAPI_EnumPhysicalGPUs = (NvAPI_EnumPhysicalGPUs_t)NvAPI_QueryInterface(NVAPI_ENUM_PHYSICAL_GPUS_OFFSET);
            NvAPI_GPU_GetThermalSettings = (NvAPI_GPU_GetThermalSettings_t)NvAPI_QueryInterface(NVAPI_GET_THERMAL_SETTINGS_OFFSET);
            
            if (NvAPI_Initialize && NvAPI_EnumPhysicalGPUs && NvAPI_GPU_GetThermalSettings) {
                if (NvAPI_Initialize() == 0) {
                    NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
                }
            }
        }
    }

    hAdl = LoadLibraryA("atiadlxx.dll");
    if (!hAdl) hAdl = LoadLibraryA("atiadlxy.dll");
    if (hAdl) {
        ADL_Main_Control_Create = (ADL_Main_Control_Create_t)GetProcAddress(hAdl, "ADL_Main_Control_Create");
        ADL_Main_Control_Destroy = (ADL_Main_Control_Destroy_t)GetProcAddress(hAdl, "ADL_Main_Control_Destroy");
        ADL_Adapter_NumberOfAdapters_Get = (ADL_Adapter_NumberOfAdapters_Get_t)GetProcAddress(hAdl, "ADL_Adapter_NumberOfAdapters_Get");
        ADL_Overdrive5_Temperature_Get = (ADL_Overdrive5_Temperature_Get_t)GetProcAddress(hAdl, "ADL_Overdrive5_Temperature_Get");
        
        if (ADL_Main_Control_Create && ADL_Main_Control_Destroy && ADL_Adapter_NumberOfAdapters_Get && ADL_Overdrive5_Temperature_Get) {
            if (ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1) == 0) {
                adlInitialized = 1;
            }
        }
    }
}

int GetNvidiaGPUTemperature() {
    if (gpuCount > 0 && NvAPI_GPU_GetThermalSettings) {
        NV_GPU_THERMAL_SETTINGS_V2 thermalSettings = {0};
        thermalSettings.version = NV_GPU_THERMAL_SETTINGS_VER_2;
        if (NvAPI_GPU_GetThermalSettings(gpuHandles[0], 15, &thermalSettings) == 0) {
            return thermalSettings.sensor[0].currentTemp;
        } else {
            NV_GPU_THERMAL_SETTINGS_V1 thermalSettings1 = {0};
            thermalSettings1.version = NV_GPU_THERMAL_SETTINGS_VER_1;
            if (NvAPI_GPU_GetThermalSettings(gpuHandles[0], 15, &thermalSettings1) == 0) {
                return thermalSettings1.sensor[0].currentTemp;
            }
        }
    }
    return 0;
}

int GetAMDGPUTemperature() {
    if (adlInitialized && ADL_Overdrive5_Temperature_Get && ADL_Adapter_NumberOfAdapters_Get) {
        int numAdapters = 0;
        if (ADL_Adapter_NumberOfAdapters_Get(&numAdapters) == 0 && numAdapters > 0) {
            ADLTemperature adlTemp = {0};
            adlTemp.iSize = sizeof(ADLTemperature);
            if (ADL_Overdrive5_Temperature_Get(0, 0, &adlTemp) == 0) {
                return adlTemp.iTemperature / 1000;
            }
        }
    }
    return 0;
}

static double cpu_base_temp = 40.0;
static double cpu_max_temp = 80.0;
static int cpu_temp_initialized = 0;
static double current_sim_temp = 0.0;

double GetSimulatedFPS(double gpuLoad, double cpuLoad) {
    static double current_fps = 0.0;
    static int initialized = 0;
    
    if (!initialized) {
        current_fps = 144.0;
        initialized = 1;
    }
    
    double target_fps = 60.0;
    
    if (gpuLoad > 80.0) {
        if (cpuLoad < 60.0) {
            target_fps = 180.0 + (100.0 - gpuLoad); 
        } else {
            target_fps = 120.0 + (100.0 - cpuLoad); 
        }
    } else if (gpuLoad > 40.0) {
        target_fps = 80.0 + gpuLoad; 
    } else if (gpuLoad > 10.0) {
        target_fps = 60.0 + (gpuLoad / 2.0); 
    } else {
        target_fps = 60.0; 
    }
    
    current_fps = current_fps + (target_fps - current_fps) * 0.1;
    
    double noise = ((rand() % 50) / 10.0) - 2.5;
    
    double final_fps = current_fps + noise;
    if (final_fps < 30.0) final_fps = 30.0;
    if (final_fps > 500.0) final_fps = 500.0;
    
    return final_fps;
}

void InitCPUTempSim() {
    if (!cpu_temp_initialized) {
        if (strstr(specs.cpu_name, "i9") || strstr(specs.cpu_name, "Ryzen 9")) {
            cpu_base_temp = 45.0; cpu_max_temp = 95.0;
        } else if (strstr(specs.cpu_name, "i7") || strstr(specs.cpu_name, "Ryzen 7")) {
            cpu_base_temp = 40.0; cpu_max_temp = 85.0;
        } else if (strstr(specs.cpu_name, "i5") || strstr(specs.cpu_name, "Ryzen 5")) {
            cpu_base_temp = 35.0; cpu_max_temp = 80.0;
        } else if (strstr(specs.cpu_name, "i3") || strstr(specs.cpu_name, "Ryzen 3")) {
            cpu_base_temp = 35.0; cpu_max_temp = 75.0;
        } else {
            cpu_base_temp = 40.0; cpu_max_temp = 80.0;
        }
        cpu_temp_initialized = 1;
    }
}

int GetSimulatedCPUTemp(double load) {
    InitCPUTempSim();
    if (current_sim_temp == 0.0) current_sim_temp = cpu_base_temp;
    
    double target_temp = cpu_base_temp + (load / 100.0) * (cpu_max_temp - cpu_base_temp);
    current_sim_temp = current_sim_temp + (target_temp - current_sim_temp) * 0.2;
    double noise = ((rand() % 30) / 10.0) - 1.5;
    
    int final_temp = (int)(current_sim_temp + noise);
    if (final_temp < (int)cpu_base_temp) final_temp = (int)cpu_base_temp;
    if (final_temp > (int)cpu_max_temp) final_temp = (int)cpu_max_temp;
    return final_temp;
}

static double gpu_base_temp = 35.0;
static double gpu_max_temp = 80.0;
static int gpu_temp_initialized = 0;
static double gpu_current_sim_temp = 0.0;

void InitGPUTempSim() {
    if (!gpu_temp_initialized) {
        if (strstr(specs.gpu_name, "RTX") || strstr(specs.gpu_name, "GTX") || strstr(specs.gpu_name, "Radeon RX")) {
            gpu_base_temp = 40.0; gpu_max_temp = 85.0;
        } else {
            gpu_base_temp = 35.0; gpu_max_temp = 75.0;
        }
        gpu_temp_initialized = 1;
    }
}

int GetSimulatedGPUTemp(double load) {
    InitGPUTempSim();
    if (gpu_current_sim_temp == 0.0) gpu_current_sim_temp = gpu_base_temp;
    
    double target_temp = gpu_base_temp + (load / 100.0) * (gpu_max_temp - gpu_base_temp);
    gpu_current_sim_temp = gpu_current_sim_temp + (target_temp - gpu_current_sim_temp) * 0.15;
    double noise = ((rand() % 20) / 10.0) - 1.0;
    
    int final_temp = (int)(gpu_current_sim_temp + noise);
    if (final_temp < (int)gpu_base_temp) final_temp = (int)gpu_base_temp;
    if (final_temp > (int)gpu_max_temp) final_temp = (int)gpu_max_temp;
    return final_temp;
}

void InitPDH() {
    if (PdhOpenQuery(NULL, 0, &cpuQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterA(cpuQuery, "\\Processor Information(_Total)\\% Processor Utility", 0, &cpuCounter);
        PdhCollectQueryData(cpuQuery);
    }
    if (PdhOpenQuery(NULL, 0, &gpuQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterA(gpuQuery, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter);
        PdhCollectQueryData(gpuQuery);
    }
    pdhInitialized = 1;
}

void ReadPDH(HardwareMetrics* m) {
    if (!pdhInitialized) return;
    
    PdhCollectQueryData(cpuQuery);
    PDH_FMT_COUNTERVALUE counterVal;
    if (PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS) {
        m->cpu_usage_percent = counterVal.doubleValue;
        if (m->cpu_usage_percent > 100.0) m->cpu_usage_percent = 100.0;
    }

    PdhCollectQueryData(gpuQuery);
    DWORD bufSize = 0;
    DWORD itemCnt = 0;
    PdhGetFormattedCounterArrayA(gpuCounter, PDH_FMT_DOUBLE, &bufSize, &itemCnt, NULL);
    if (bufSize > 0) {
        PDH_FMT_COUNTERVALUE_ITEM_A* items = (PDH_FMT_COUNTERVALUE_ITEM_A*)malloc(bufSize);
        if (items) {
            if (PdhGetFormattedCounterArrayA(gpuCounter, PDH_FMT_DOUBLE, &bufSize, &itemCnt, items) == ERROR_SUCCESS) {
                double totalGpu = 0.0;
                for (DWORD i = 0; i < itemCnt; i++) {
                    
                    if (strstr(items[i].szName, "3D")) {
                        totalGpu += items[i].FmtValue.doubleValue;
                    }
                }
                m->gpu_usage_percent = totalGpu;
                if (m->gpu_usage_percent > 100.0) m->gpu_usage_percent = 100.0;
            }
            free(items);
        }
    }
}

void InitSystemSpecs(void) {
    
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        specs.total_ram_gb = (double)memInfo.ullTotalPhys / (1024 * 1024 * 1024);
    } else {
        specs.total_ram_gb = 0;
    }

    HKEY hKey;
    char buffer[256];
    DWORD bufferSize = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            snprintf(specs.cpu_name, sizeof(specs.cpu_name), "%s", buffer);
        } else {
            snprintf(specs.cpu_name, sizeof(specs.cpu_name), "Unknown CPU");
        }
        RegCloseKey(hKey);
    } else {
        snprintf(specs.cpu_name, sizeof(specs.cpu_name), "Unknown CPU");
    }

    bufferSize = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "DriverDesc", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            snprintf(specs.gpu_name, sizeof(specs.gpu_name), "%s", buffer);
        } else {
            snprintf(specs.gpu_name, sizeof(specs.gpu_name), "Unknown GPU");
        }
        RegCloseKey(hKey);
    } else {
        snprintf(specs.gpu_name, sizeof(specs.gpu_name), "Unknown GPU");
    }

    bufferSize = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            snprintf(specs.os_name, sizeof(specs.os_name), "%s", buffer);
        } else {
            snprintf(specs.os_name, sizeof(specs.os_name), "Windows");
        }
        RegCloseKey(hKey);
    } else {
        snprintf(specs.os_name, sizeof(specs.os_name), "Windows");
    }

    bufferSize = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            snprintf(specs.mobo_name, sizeof(specs.mobo_name), "Mobo: %s", buffer);
        } else {
            snprintf(specs.mobo_name, sizeof(specs.mobo_name), "Unknown Board");
        }
        RegCloseKey(hKey);
    } else {
        snprintf(specs.mobo_name, sizeof(specs.mobo_name), "Unknown Board");
    }
}

SystemSpecs GetSystemSpecs(void) {
    return specs;
}

static unsigned __stdcall MonitorThreadFunc(void* arg) {
    (void)arg;
    InitPDH();
    
    while (WaitForSingleObject(hStopEvent, 1000) == WAIT_TIMEOUT) {
        HardwareMetrics tempMetrics = {0};

        ReadPDH(&tempMetrics);

        int nvTemp = GetNvidiaGPUTemperature();
        if (nvTemp > 0) {
            tempMetrics.gpu_temp = nvTemp;
        } else {
            int amdTemp = GetAMDGPUTemperature();
            if (amdTemp > 0) {
                tempMetrics.gpu_temp = amdTemp;
            } else {
                tempMetrics.gpu_temp = GetSimulatedGPUTemp(tempMetrics.gpu_usage_percent);
            }
        }
        tempMetrics.cpu_temp = GetSimulatedCPUTemp(tempMetrics.cpu_usage_percent);

        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            tempMetrics.ram_usage_percent = (double)memInfo.dwMemoryLoad;
            tempMetrics.ram_total_gb = (double)memInfo.ullTotalPhys / (1024 * 1024 * 1024);
            tempMetrics.ram_used_gb = (double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024 * 1024);
        }

        HWND fgWindow = GetForegroundWindow();
        tempMetrics.is_gaming_fullscreen = 0;
        tempMetrics.fps = 0.0;
        if (fgWindow != NULL && fgWindow != GetDesktopWindow()) {
            RECT rect;
            if (GetWindowRect(fgWindow, &rect)) {
                int screenW = GetSystemMetrics(SM_CXSCREEN);
                int screenH = GetSystemMetrics(SM_CYSCREEN);
                if (rect.left <= 0 && rect.top <= 0 && rect.right >= screenW && rect.bottom >= screenH) {
                    char className[256];
                    GetClassNameA(fgWindow, className, sizeof(className));
                    if (strcmp(className, "Progman") != 0 && strcmp(className, "WorkerW") != 0) {
                        tempMetrics.is_gaming_fullscreen = 1;
                        tempMetrics.fps = GetSimulatedFPS(tempMetrics.gpu_usage_percent, tempMetrics.cpu_usage_percent);
                    }
                }
            }
        }

        EnterCriticalSection(&csMetrics);
        
        tempMetrics.history_index = (currentMetrics.history_index + 1) % HISTORY_SIZE;
        tempMetrics.history_count = currentMetrics.history_count;
        if (tempMetrics.history_count < HISTORY_SIZE) tempMetrics.history_count++;
        
        for(int i=0; i<HISTORY_SIZE; i++) {
            tempMetrics.cpu_history[i] = currentMetrics.cpu_history[i];
            tempMetrics.gpu_history[i] = currentMetrics.gpu_history[i];
        }
        tempMetrics.cpu_history[tempMetrics.history_index] = tempMetrics.cpu_usage_percent;
        tempMetrics.gpu_history[tempMetrics.history_index] = tempMetrics.gpu_usage_percent;

        currentMetrics = tempMetrics;
        LeaveCriticalSection(&csMetrics);
    }
    
    if (pdhInitialized) {
        if (cpuQuery) PdhCloseQuery(cpuQuery);
        if (gpuQuery) PdhCloseQuery(gpuQuery);
    }
    
    return 0;
}

void StartHardwareMonitor(void) {
    InitSystemSpecs();
    InitGPUAPI();
    InitializeCriticalSection(&csMetrics);
    hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hStopEvent) {
        hMonitorThread = (HANDLE)_beginthreadex(NULL, 0, MonitorThreadFunc, NULL, 0, NULL);
    }
}

void StopHardwareMonitor(void) {
    if (hStopEvent) {
        SetEvent(hStopEvent);
        if (hMonitorThread) {
            WaitForSingleObject(hMonitorThread, INFINITE);
            CloseHandle(hMonitorThread);
            hMonitorThread = NULL;
        }
        CloseHandle(hStopEvent);
        hStopEvent = NULL;
    }
    
    if (adlInitialized && ADL_Main_Control_Destroy) {
        ADL_Main_Control_Destroy();
        adlInitialized = 0;
    }
    if (hAdl) {
        FreeLibrary(hAdl);
        hAdl = NULL;
    }
    if (hNvAPI) {
        FreeLibrary(hNvAPI);
        hNvAPI = NULL;
    }
    
    DeleteCriticalSection(&csMetrics);
}

HardwareMetrics GetLatestMetrics(void) {
    HardwareMetrics metrics;
    EnterCriticalSection(&csMetrics);
    metrics = currentMetrics;
    LeaveCriticalSection(&csMetrics);
    return metrics;
}

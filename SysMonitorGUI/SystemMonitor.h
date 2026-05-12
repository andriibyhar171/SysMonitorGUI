#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <fstream>
#include <pdh.h>

#define SystemProcessorPerformanceInformation 8
#define CORESENSE_VERSION "v1.0.3"

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, * PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

typedef NTSTATUS(WINAPI* PNT_QUERY_SYSTEM_INFORMATION)(
    UINT SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
    );

struct SystemInfoData {
    std::string pcName;
    std::string osVersion;
    std::string cpuName;
    unsigned int uptimeHours;
    unsigned int uptimeMinutes;
};

struct PowerData {
    bool hasBattery;
    bool isCharging;
    BYTE batteryPercent;
    DWORD batteryLifeSeconds;
};

struct MemoryData {
    double loadPercent;
    DWORDLONG totalMB;
    DWORDLONG availableMB;
    DWORDLONG pageFileMB;
    DWORDLONG virtualMB;
};

struct DriveData {
    std::string name;
    std::string type;
    DWORDLONG totalGB;
    DWORDLONG freeGB;
    double readSpeedMBps;
    double writeSpeedMBps;
};

struct NetworkData {
    DWORD interfaceIndex;
    DWORDLONG receivedMB;
    DWORDLONG sentMB;
    double downloadSpeedKBps;
    double uploadSpeedKBps;
};

struct GpuData {
    std::string name;
    DWORDLONG totalVRAM_MB;
    DWORDLONG usedVRAM_MB;
    double loadPercent;
};

class SystemMonitor {
private:
    std::ofstream logFile;
    std::ofstream csvFile;
    bool isLoggingCsv;
    ULONGLONG lastCsvLogTime;

    ULARGE_INTEGER prevSysIdle, prevSysKernel, prevSysUser;

    PNT_QUERY_SYSTEM_INFORMATION NtQuerySystemInfo;
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> prevCoreStates;
    int numProcessors;
    ULONGLONG lastCoreQueryTime;
    std::vector<double> cachedCoreLoads;

    ULONGLONG lastOverallCpuTime;
    double cachedOverallCpuLoad;

    std::atomic<double> cachedCpuTemp{ 0.0 };
    std::atomic<bool> stopTempThread{ false };
    std::thread tempThread;

    struct NetState {
        DWORDLONG lastIn = 0;
        DWORDLONG lastOut = 0;
        ULONGLONG lastTime = 0;
    };
    std::map<DWORD, NetState> prevNetStates;
    ULONGLONG lastNetworkQueryTime;
    std::vector<NetworkData> cachedNetworkStats;

    PDH_HQUERY diskQuery;
    std::map<std::string, PDH_HCOUNTER> diskReadCounters;
    std::map<std::string, PDH_HCOUNTER> diskWriteCounters;
    ULONGLONG lastDiskQueryTime;
    std::vector<DriveData> cachedDrives;

    std::vector<float> cpuHistory;
    std::vector<float> ramHistory;
    std::vector<float> gpuHistory;
    std::vector<std::vector<float>> coreHistories;
    ULONGLONG lastHistoryTime;

    SystemInfoData cachedSysInfo;
    bool isSysInfoCached;

    PowerData cachedPowerInfo;
    ULONGLONG lastPowerTime;

    std::vector<GpuData> cachedGpus;
    ULONGLONG lastGpuQueryTime;

    std::string GetDriveTypeString(UINT type);
    void TemperatureWorker();

public:
    DWORD updateInterval;

    SystemMonitor();
    ~SystemMonitor();

    bool IsAdmin();
    void RestartAsAdmin();

    void Log(const std::string& message);
    bool IsLogging() const;
    void ToggleCsvLogging();
    void LogMetricsToCsv(double cpu, double ram, double gpu);

    void UpdateHistory(double currentCpu, double currentRam, const std::vector<double>& coreLoads, double currentGpuVram);
    const float* GetCpuHistory() const;
    const float* GetRamHistory() const;
    const float* GetGpuHistory() const;
    const float* GetCoreHistory(int index) const;

    const SystemInfoData& GetSystemInfo();
    const PowerData& GetPowerInfo();
    double GetCpuTemperature();
    double GetCPULoad();
    const std::vector<double>& GetPerCoreLoad();
    MemoryData GetMemoryInfo();

    const std::vector<DriveData>& GetDrivesInfo();
    const std::vector<NetworkData>& GetNetworkStats();
    const std::vector<GpuData>& GetGpuList();
};
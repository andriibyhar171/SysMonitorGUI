#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ProcessData {
    DWORD pid;
    std::string name;
    double cpuLoad;
    DWORD threads;
    DWORD priority;
    SIZE_T ramUsedMB;
    std::string path;
};

namespace ProcessManager {
    std::vector<ProcessData> GetProcesses(DWORD updateInterval);

    bool KillProcess(DWORD processID);
}
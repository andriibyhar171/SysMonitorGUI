#include "SystemMonitor.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "pdh.lib") 
#pragma comment(lib, "dxgi.lib")

SystemMonitor::SystemMonitor() :
    isLoggingCsv(false), lastCsvLogTime(0),
    numProcessors(0), lastCoreQueryTime(0),
    lastOverallCpuTime(0), cachedOverallCpuLoad(0.0),
    lastNetworkQueryTime(0), lastDiskQueryTime(0),
    lastHistoryTime(0),
    isSysInfoCached(false), lastPowerTime(0),
    lastGpuQueryTime(0), updateInterval(1000)
{
    logFile.open("system_monitor_log.txt", std::ios::app);

    FILETIME idleTime, kernelTime, userTime;
    ::GetSystemTimes(&idleTime, &kernelTime, &userTime);
    prevSysIdle.LowPart = idleTime.dwLowDateTime; prevSysIdle.HighPart = idleTime.dwHighDateTime;
    prevSysKernel.LowPart = kernelTime.dwLowDateTime; prevSysKernel.HighPart = kernelTime.dwHighDateTime;
    prevSysUser.LowPart = userTime.dwLowDateTime; prevSysUser.HighPart = userTime.dwHighDateTime;

    SYSTEM_INFO sysInfo;
    ::GetSystemInfo(&sysInfo);
    numProcessors = sysInfo.dwNumberOfProcessors;

    cpuHistory.resize(100, 0.0f);
    ramHistory.resize(100, 0.0f);
    gpuHistory.resize(100, 0.0f);
    gpuCoreHistory.resize(100, 0.0f);
    coreHistories.resize(numProcessors, std::vector<float>(100, 0.0f));

    NtQuerySystemInfo = (PNT_QUERY_SYSTEM_INFORMATION)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    if (NtQuerySystemInfo) {
        prevCoreStates.resize(numProcessors);
        NtQuerySystemInfo(SystemProcessorPerformanceInformation, prevCoreStates.data(), sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * numProcessors, NULL);
    }

    PdhOpenQuery(NULL, NULL, &diskQuery);
    DWORD logicalDrives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (logicalDrives & (1 << i)) {
            char driveLetter = (char)('A' + i);
            std::string driveName = std::string(1, driveLetter) + ":";
            PDH_HCOUNTER readCounter, writeCounter;
            std::string readPath = "\\LogicalDisk(" + driveName + ")\\Disk Read Bytes/sec";
            std::string writePath = "\\LogicalDisk(" + driveName + ")\\Disk Write Bytes/sec";
            PdhAddEnglishCounterA(diskQuery, readPath.c_str(), 0, &readCounter);
            PdhAddEnglishCounterA(diskQuery, writePath.c_str(), 0, &writeCounter);
            diskReadCounters[driveName] = readCounter;
            diskWriteCounters[driveName] = writeCounter;
        }
    }
    PdhCollectQueryData(diskQuery);

    PdhOpenQuery(NULL, NULL, &gpuQuery);
    PdhAddEnglishCounterA(gpuQuery, "\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpuCounter);
    PdhCollectQueryData(gpuQuery);

    tempThread = std::thread(&SystemMonitor::TemperatureWorker, this);
}

SystemMonitor::~SystemMonitor() {
    stopTempThread = true;
    if (tempThread.joinable()) {
        tempThread.join();
    }

    if (logFile.is_open()) logFile.close();
    if (csvFile.is_open()) csvFile.close();
    PdhCloseQuery(diskQuery);
    PdhCloseQuery(gpuQuery);
}

std::string SystemMonitor::GetDriveTypeString(UINT type) {
    switch (type) {
    case DRIVE_REMOVABLE: return "Removable";
    case DRIVE_FIXED:     return "Fixed";
    case DRIVE_REMOTE:    return "Network";
    case DRIVE_CDROM:     return "CD-ROM";
    case DRIVE_RAMDISK:   return "RAM Disk";
    default:              return "Unknown";
    }
}

void SystemMonitor::TemperatureWorker() {
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hres);

    if (needUninit) {
        CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    }

    IWbemLocator* pLoc = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc))) {

        while (!stopTempThread) {
            IWbemServices* pSvc = NULL;
            if (SUCCEEDED(pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc))) {
                CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

                IEnumWbemClassObject* pEnumerator = NULL;
                if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator))) {
                    IWbemClassObject* pclsObj = NULL;
                    ULONG uReturn = 0;
                    double maxTemp = 0.0;

                    while (pEnumerator) {
                        if (FAILED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) || uReturn == 0) break;
                        VARIANT vtProp;
                        if (SUCCEEDED(pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0))) {
                            if (vtProp.vt == VT_I4) {
                                double currentC = (vtProp.lVal / 10.0) - 273.15;
                                if (currentC > maxTemp && currentC < 150.0) {
                                    maxTemp = currentC;
                                }
                            }
                            VariantClear(&vtProp);
                        }
                        pclsObj->Release();
                    }

                    if (maxTemp > 0.0) {
                        cachedCpuTemp.store(maxTemp);
                    }
                    pEnumerator->Release();
                }
                pSvc->Release();
            }

            DWORD waitTime = updateInterval > 0 ? updateInterval : 1000;
            DWORD waited = 0;
            while (waited < waitTime && !stopTempThread) {
                Sleep(100);
                waited += 100;
            }
        }
        pLoc->Release();
    }

    if (needUninit) CoUninitialize();
}

bool SystemMonitor::IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID administratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroup)) {
        CheckTokenMembership(NULL, administratorsGroup, &isAdmin);
        FreeSid(administratorsGroup);
    }
    return isAdmin == TRUE;
}

void SystemMonitor::RestartAsAdmin() {
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.lpVerb = "runas";
        sei.lpFile = path;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        if (ShellExecuteExA(&sei)) {
            exit(0);
        }
    }
}

void SystemMonitor::Log(const std::string& message) {
    if (logFile.is_open()) {
        logFile << message << "\n";
        logFile.flush();
    }
}

bool SystemMonitor::IsLogging() const { return isLoggingCsv; }

void SystemMonitor::ToggleCsvLogging() {
    if (isLoggingCsv) {
        if (csvFile.is_open()) csvFile.close();
        isLoggingCsv = false;
    }
    else {
        csvFile.open("system_report.csv", std::ios::app);
        csvFile.seekp(0, std::ios::end);
        if (csvFile.tellp() == 0) {
            csvFile << "Time;CPU_Load(%);RAM_Load(%);GPU_Load(%)\n";
        }
        isLoggingCsv = true;
    }
}

void SystemMonitor::LogMetricsToCsv(double cpu, double ram, double gpu) {
    if (!isLoggingCsv) return;
    ULONGLONG currentTime = GetTickCount64();

    if (currentTime - lastCsvLogTime >= updateInterval) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeStr[64];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

        std::string cpuStr = std::to_string(cpu); std::replace(cpuStr.begin(), cpuStr.end(), '.', ',');
        std::string ramStr = std::to_string(ram); std::replace(ramStr.begin(), ramStr.end(), '.', ',');
        std::string gpuStr = std::to_string(gpu); std::replace(gpuStr.begin(), gpuStr.end(), '.', ',');

        csvFile << timeStr << ";" << cpuStr << ";" << ramStr << ";" << gpuStr << "\n";
        csvFile.flush();
        lastCsvLogTime = currentTime;
    }
}

void SystemMonitor::UpdateHistory(double currentCpu, double currentRam, const std::vector<double>& coreLoads, double currentGpuVram, double currentGpuCore) {
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - lastHistoryTime > updateInterval) {
        for (size_t i = 0; i < cpuHistory.size() - 1; i++) {
            cpuHistory[i] = cpuHistory[i + 1];
            ramHistory[i] = ramHistory[i + 1];
            gpuHistory[i] = gpuHistory[i + 1];
            gpuCoreHistory[i] = gpuCoreHistory[i + 1];
            for (int c = 0; c < numProcessors; c++) {
                if (c < coreLoads.size()) {
                    coreHistories[c][i] = coreHistories[c][i + 1];
                }
            }
        }
        cpuHistory.back() = (float)currentCpu;
        ramHistory.back() = (float)currentRam;
        gpuHistory.back() = (float)currentGpuVram;
        gpuCoreHistory.back() = (float)currentGpuCore;
        for (int c = 0; c < numProcessors; c++) {
            if (c < coreLoads.size()) {
                coreHistories[c].back() = (float)coreLoads[c];
            }
        }
        lastHistoryTime = currentTime;
    }
}

const float* SystemMonitor::GetCpuHistory() const { return cpuHistory.data(); }
const float* SystemMonitor::GetRamHistory() const { return ramHistory.data(); }
const float* SystemMonitor::GetGpuHistory() const { return gpuHistory.data(); }
const float* SystemMonitor::GetGpuCoreHistory() const { return gpuCoreHistory.data(); }
const float* SystemMonitor::GetCoreHistory(int index) const {
    if (index >= 0 && index < coreHistories.size()) return coreHistories[index].data();
    return nullptr;
}

const SystemInfoData& SystemMonitor::GetSystemInfo() {
    if (!isSysInfoCached) {
        char compName[256];
        DWORD size = sizeof(compName);
        if (GetComputerNameA(compName, &size)) cachedSysInfo.pcName = compName;

        cachedSysInfo.osVersion = "Unknown OS";
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char productName[256];
            DWORD prodSize = sizeof(productName);
            if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)productName, &prodSize) == ERROR_SUCCESS) {
                cachedSysInfo.osVersion = productName;
            }

            char buildNumber[256];
            DWORD buildSize = sizeof(buildNumber);
            if (RegQueryValueExA(hKey, "CurrentBuildNumber", NULL, NULL, (LPBYTE)buildNumber, &buildSize) == ERROR_SUCCESS) {
                int build = std::stoi(buildNumber);
                if (build >= 22000) {
                    size_t pos = cachedSysInfo.osVersion.find("Windows 10");
                    if (pos != std::string::npos) {
                        cachedSysInfo.osVersion.replace(pos, 10, "Windows 11");
                    }
                }
            }
            RegCloseKey(hKey);
        }

        cachedSysInfo.cpuName = "Unknown CPU";
        HKEY hKeyCpu;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKeyCpu) == ERROR_SUCCESS) {
            char cpuNameBuf[256];
            DWORD cpuSize = sizeof(cpuNameBuf);
            if (RegQueryValueExA(hKeyCpu, "ProcessorNameString", NULL, NULL, (LPBYTE)cpuNameBuf, &cpuSize) == ERROR_SUCCESS) {
                cachedSysInfo.cpuName = cpuNameBuf;
            }
            RegCloseKey(hKeyCpu);
        }
        isSysInfoCached = true;
    }

    ULONGLONG uptimeSec = GetTickCount64() / 1000;
    cachedSysInfo.uptimeHours = (unsigned int)(uptimeSec / 3600);
    cachedSysInfo.uptimeMinutes = (unsigned int)((uptimeSec % 3600) / 60);

    return cachedSysInfo;
}

const PowerData& SystemMonitor::GetPowerInfo() {
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - lastPowerTime > updateInterval || lastPowerTime == 0) {
        SYSTEM_POWER_STATUS status;
        if (GetSystemPowerStatus(&status)) {
            cachedPowerInfo.hasBattery = (status.BatteryFlag != 128);
            cachedPowerInfo.isCharging = (status.ACLineStatus == 1);
            if (status.BatteryLifePercent <= 100) {
                cachedPowerInfo.batteryPercent = status.BatteryLifePercent;
            }
            cachedPowerInfo.batteryLifeSeconds = status.BatteryLifeTime;
        }
        lastPowerTime = currentTime;
    }
    return cachedPowerInfo;
}

double SystemMonitor::GetCpuTemperature() {
    return cachedCpuTemp.load();
}

double SystemMonitor::GetCPULoad() {
    ULONGLONG currentTime = GetTickCount64();

    if (currentTime - lastOverallCpuTime > updateInterval) {
        FILETIME idleTime, kernelTime, userTime;
        ::GetSystemTimes(&idleTime, &kernelTime, &userTime);

        ULARGE_INTEGER sysIdle, sysKernel, sysUser;
        sysIdle.LowPart = idleTime.dwLowDateTime; sysIdle.HighPart = idleTime.dwHighDateTime;
        sysKernel.LowPart = kernelTime.dwLowDateTime; sysKernel.HighPart = kernelTime.dwHighDateTime;
        sysUser.LowPart = userTime.dwLowDateTime; sysUser.HighPart = userTime.dwHighDateTime;

        ULONGLONG idleDiff = sysIdle.QuadPart - prevSysIdle.QuadPart;
        ULONGLONG kernelDiff = sysKernel.QuadPart - prevSysKernel.QuadPart;
        ULONGLONG userDiff = sysUser.QuadPart - prevSysUser.QuadPart;
        ULONGLONG totalSys = kernelDiff + userDiff;

        if (totalSys > 0) {
            cachedOverallCpuLoad = (double)(totalSys - idleDiff) * 100.0 / totalSys;
        }

        prevSysIdle = sysIdle; prevSysKernel = sysKernel; prevSysUser = sysUser;
        lastOverallCpuTime = currentTime;
    }

    return cachedOverallCpuLoad;
}

const std::vector<double>& SystemMonitor::GetPerCoreLoad() {
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - lastCoreQueryTime > updateInterval) {
        if (NtQuerySystemInfo) {
            std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> currentCoreStates(numProcessors);

            if (NtQuerySystemInfo(SystemProcessorPerformanceInformation, currentCoreStates.data(), sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * numProcessors, NULL) == 0) {
                cachedCoreLoads.clear();

                for (int i = 0; i < numProcessors; i++) {
                    ULONGLONG idleDiff = currentCoreStates[i].IdleTime.QuadPart - prevCoreStates[i].IdleTime.QuadPart;
                    ULONGLONG kernelDiff = currentCoreStates[i].KernelTime.QuadPart - prevCoreStates[i].KernelTime.QuadPart;
                    ULONGLONG userDiff = currentCoreStates[i].UserTime.QuadPart - prevCoreStates[i].UserTime.QuadPart;

                    ULONGLONG totalSys = kernelDiff + userDiff;
                    double load = 0.0;
                    if (totalSys > 0) {
                        load = (double)(totalSys - idleDiff) * 100.0 / totalSys;
                        if (load < 0.0) load = 0.0;
                        if (load > 100.0) load = 100.0;
                    }
                    cachedCoreLoads.push_back(load);
                }
                prevCoreStates = currentCoreStates;
            }
        }
        else {
            cachedCoreLoads.assign(numProcessors, 0.0);
        }
        lastCoreQueryTime = currentTime;
    }
    return cachedCoreLoads;
}

MemoryData SystemMonitor::GetMemoryInfo() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    MemoryData data;
    data.loadPercent = memInfo.dwMemoryLoad;
    data.totalMB = memInfo.ullTotalPhys / (1024 * 1024);
    data.availableMB = memInfo.ullAvailPhys / (1024 * 1024);

    DWORDLONG commitLimitMB = memInfo.ullTotalPageFile / (1024 * 1024);
    DWORDLONG commitUsedMB = (memInfo.ullTotalPageFile - memInfo.ullAvailPageFile) / (1024 * 1024);

    data.pageFileMB = commitUsedMB;
    data.virtualMB = commitLimitMB;

    return data;
}

const std::vector<DriveData>& SystemMonitor::GetDrivesInfo() {
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - lastDiskQueryTime > updateInterval || cachedDrives.empty()) {
        PdhCollectQueryData(diskQuery);
        std::vector<DriveData> drivesList;
        DWORD drives = GetLogicalDrives();

        for (int i = 0; i < 26; i++) {
            if (drives & (1 << i)) {
                char driveLetter = (char)('A' + i);
                std::string driveName = std::string(1, driveLetter) + ":";
                std::string pathName = driveName + "\\";

                DriveData d;
                d.name = pathName;
                d.type = GetDriveTypeString(GetDriveTypeA(pathName.c_str()));

                ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
                if (GetDiskFreeSpaceExA(pathName.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) {
                    d.totalGB = totalBytes.QuadPart / (1024 * 1024 * 1024);
                    d.freeGB = totalFreeBytes.QuadPart / (1024 * 1024 * 1024);
                }

                PDH_FMT_COUNTERVALUE readVal, writeVal;
                if (diskReadCounters.count(driveName) && PdhGetFormattedCounterValue(diskReadCounters[driveName], PDH_FMT_DOUBLE, NULL, &readVal) == ERROR_SUCCESS) {
                    d.readSpeedMBps = readVal.doubleValue / (1024.0 * 1024.0);
                }
                if (diskWriteCounters.count(driveName) && PdhGetFormattedCounterValue(diskWriteCounters[driveName], PDH_FMT_DOUBLE, NULL, &writeVal) == ERROR_SUCCESS) {
                    d.writeSpeedMBps = writeVal.doubleValue / (1024.0 * 1024.0);
                }

                drivesList.push_back(d);
            }
        }
        cachedDrives = drivesList;
        lastDiskQueryTime = currentTime;
    }
    return cachedDrives;
}

const std::vector<NetworkData>& SystemMonitor::GetNetworkStats() {
    ULONGLONG currentTime = GetTickCount64();

    if (currentTime - lastNetworkQueryTime > updateInterval || cachedNetworkStats.empty()) {
        std::vector<NetworkData> netList;
        PMIB_IFTABLE ifTable;
        DWORD dwSize = 0;

        if (GetIfTable(NULL, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) {
            ifTable = (PMIB_IFTABLE)malloc(dwSize);
            if (ifTable != nullptr) {
                if (GetIfTable(ifTable, &dwSize, 0) == NO_ERROR) {
                    for (DWORD i = 0; i < ifTable->dwNumEntries; i++) {
                        if (ifTable->table[i].dwInOctets > 0 || ifTable->table[i].dwOutOctets > 0) {
                            NetworkData n;
                            n.interfaceIndex = i;
                            n.receivedMB = ifTable->table[i].dwInOctets / (1024 * 1024);
                            n.sentMB = ifTable->table[i].dwOutOctets / (1024 * 1024);

                            auto& state = prevNetStates[i];
                            if (state.lastTime != 0) {
                                ULONGLONG timeDelta = currentTime - state.lastTime;
                                if (timeDelta > 0) {
                                    DWORDLONG inDelta = ifTable->table[i].dwInOctets - state.lastIn;
                                    DWORDLONG outDelta = ifTable->table[i].dwOutOctets - state.lastOut;

                                    n.downloadSpeedKBps = (double)inDelta * 1000.0 / timeDelta / 1024.0;
                                    n.uploadSpeedKBps = (double)outDelta * 1000.0 / timeDelta / 1024.0;
                                }
                            }

                            state.lastIn = ifTable->table[i].dwInOctets;
                            state.lastOut = ifTable->table[i].dwOutOctets;
                            state.lastTime = currentTime;

                            netList.push_back(n);
                        }
                    }
                }
                free(ifTable);
            }
        }
        cachedNetworkStats = netList;
        lastNetworkQueryTime = currentTime;
    }

    return cachedNetworkStats;
}

const std::vector<GpuData>& SystemMonitor::GetGpuList() {
    ULONGLONG currentTime = GetTickCount64();

    if (currentTime - lastGpuQueryTime > updateInterval || cachedGpus.empty()) {
        cachedGpus.clear();
        IDXGIFactory4* factory = nullptr;

        PdhCollectQueryData(gpuQuery);
        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PdhGetFormattedCounterArrayA(gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, NULL);

        std::vector<BYTE> pdhBuffer(bufferSize);
        PDH_FMT_COUNTERVALUE_ITEM_A* gpuItems = nullptr;
        if (bufferSize > 0) {
            gpuItems = (PDH_FMT_COUNTERVALUE_ITEM_A*)pdhBuffer.data();
            if (PdhGetFormattedCounterArrayA(gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, gpuItems) != ERROR_SUCCESS) {
                itemCount = 0;
            }
        }

        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            IDXGIAdapter1* adapter = nullptr;
            for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    adapter->Release();
                    continue;
                }

                GpuData data;
                std::wstring ws(desc.Description);
                data.name.assign(ws.begin(), ws.end());
                data.totalVRAM_MB = desc.DedicatedVideoMemory / (1024 * 1024);

                IDXGIAdapter3* adapter3 = nullptr;
                if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&adapter3))) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
                    if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                        data.usedVRAM_MB = memInfo.CurrentUsage / (1024 * 1024);
                        if (data.totalVRAM_MB > 0) {
                            data.loadPercent = (double)data.usedVRAM_MB * 100.0 / (double)data.totalVRAM_MB;
                        }
                    }
                    adapter3->Release();
                }

                char luidStr[64];
                snprintf(luidStr, sizeof(luidStr), "luid_0x%08x_0x%08x", (unsigned int)desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);

                double totalCoreLoad = 0.0;
                for (DWORD j = 0; j < itemCount; j++) {
                    if (gpuItems && gpuItems[j].szName != nullptr && strstr(gpuItems[j].szName, luidStr) != nullptr) {
                        totalCoreLoad += gpuItems[j].FmtValue.doubleValue;
                    }
                }

                if (totalCoreLoad > 100.0) totalCoreLoad = 100.0;
                data.coreLoadPercent = totalCoreLoad;

                adapter->Release();
                cachedGpus.push_back(data);
            }
            factory->Release();
        }
        lastGpuQueryTime = currentTime;
    }
    return cachedGpus;
}
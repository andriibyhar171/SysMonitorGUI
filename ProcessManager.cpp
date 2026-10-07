#include "ProcessManager.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <map>

namespace ProcessManager {
    static std::vector<ProcessData> cachedProcesses;
    static ULONGLONG lastProcessQueryTime = 0;
    static std::map<DWORD, ULONGLONG> prevProcessTimes;
    static int numProcessors = 0; 

    std::vector<ProcessData> GetProcesses(DWORD updateInterval) {
        ULONGLONG currentTime = GetTickCount64();

        if (currentTime - lastProcessQueryTime < updateInterval && !cachedProcesses.empty()) {
            return cachedProcesses;
        }

        if (numProcessors == 0) {
            SYSTEM_INFO sysInfo;
            ::GetSystemInfo(&sysInfo);
            numProcessors = sysInfo.dwNumberOfProcessors;
        }

        std::vector<ProcessData> procList;
        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap == INVALID_HANDLE_VALUE) return cachedProcesses;

        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hProcessSnap, &pe32)) {
            ULONGLONG timeDelta = currentTime - lastProcessQueryTime;

            do {
                ProcessData p;
                p.pid = pe32.th32ProcessID;

                int nameSize = WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, NULL, 0, NULL, NULL);
                if (nameSize > 0) {
                    std::string utf8Name(nameSize, 0);
                    WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, &utf8Name[0], nameSize, NULL, NULL);
                    p.name = utf8Name.c_str();
                }

                p.threads = pe32.cntThreads;
                p.priority = pe32.pcPriClassBase;
                p.ramUsedMB = 0;
                p.cpuLoad = 0.0;

                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        p.ramUsedMB = pmc.WorkingSetSize / (1024 * 1024);
                    }

                    WCHAR exePathW[MAX_PATH];
                    DWORD pathSize = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, exePathW, &pathSize)) {
                        int pathUtf8Size = WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, NULL, 0, NULL, NULL);
                        if (pathUtf8Size > 0) {
                            std::string utf8Path(pathUtf8Size, 0);
                            WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, &utf8Path[0], pathUtf8Size, NULL, NULL);
                            p.path = utf8Path.c_str();
                        }
                    }

                    FILETIME ftCreation, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                        ULARGE_INTEGER kernel, user;
                        kernel.LowPart = ftKernel.dwLowDateTime;
                        kernel.HighPart = ftKernel.dwHighDateTime;
                        user.LowPart = ftUser.dwLowDateTime;
                        user.HighPart = ftUser.dwHighDateTime;
                        ULONGLONG totalTime = kernel.QuadPart + user.QuadPart;

                        if (lastProcessQueryTime != 0) {
                            auto it = prevProcessTimes.find(p.pid);
                            if (it != prevProcessTimes.end()) {
                                ULONGLONG procDelta = totalTime - it->second;
                                p.cpuLoad = (double)(procDelta / 10000.0) / (double)timeDelta / numProcessors * 100.0;
                            }
                        }
                        prevProcessTimes[p.pid] = totalTime;
                    }

                    CloseHandle(hProcess);
                }
                procList.push_back(p);
            } while (Process32NextW(hProcessSnap, &pe32));
        }
        CloseHandle(hProcessSnap);

        std::map<DWORD, ULONGLONG> currentProcessTimes;
        for (const auto& p : procList) {
            auto it = prevProcessTimes.find(p.pid);
            if (it != prevProcessTimes.end()) {
                currentProcessTimes[p.pid] = it->second;
            }
        }
        prevProcessTimes = currentProcessTimes;

        cachedProcesses = procList;
        lastProcessQueryTime = currentTime;

        return cachedProcesses;
    }

    bool KillProcess(DWORD processID) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID);
        if (hProcess == NULL) return false;
        bool result = TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        return result;
    }
}
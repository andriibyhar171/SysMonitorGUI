# CoreSense Utility 🖥️

**CoreSense** is a high-performance, native Windows system monitoring application. Built from the ground up in C++, it leverages low-level Windows APIs to deliver real-time system telemetry without the bloat of Electron-based apps.

Designed with a modern, hardware-accelerated user interface using Dear ImGui and DirectX 11, CoreSense offers both deep system insights and a seamless user experience.

## ✨ Key Features

* **Processor (CPU) Telemetry:** Real-time tracking of overall CPU load and per-core utilization utilizing the low-level `NtQuerySystemInformation` API for bulletproof accuracy. Includes ACPI thermal zone temperature readings via WMI.
* **Graphics (GPU) Monitoring:** Comprehensive GPU tracking including VRAM usage via DXGI APIs and **real-time 3D Core Load** dynamically calculated via PDH and unique adapter LUID parsing. Supports multi-GPU systems.
* **Memory & Storage:** Live physical RAM utilization and precise **Committed Memory (Page File + RAM)** tracking. Real-time logical drive statistics (Total/Free space, Read/Write speeds via PDH).
* **Network Activity:** Per-interface network monitoring (Download/Upload speeds, total sent/received bytes).
* **Process Manager:** Built-in task manager to view running processes, their CPU/RAM footprint, thread count, and the ability to safely terminate non-responsive applications.
* **CSV Telemetry Logging:** Automated background logging of system metrics (CPU, RAM, GPU) into a `.csv` file for external analysis (optimized for European Excel formatting).
* **Modern UI:**
    * Hardware-accelerated rendering (DirectX 11).
    * Customizable appearance (Dark, Light, and Classic themes) with custom accent colors.
    * **Mini Mode:** A compact, always-on-top overlay for unobtrusive background monitoring.
    * System tray integration for silent auto-start.

## 🛠️ Technologies Used

* **Language:** C++
* **UI Framework:** Dear ImGui
* **Graphics API:** DirectX 11
* **System APIs:**
    * `NtQuerySystemInformation` (Core Load)
    * `WMI / COM` (Temperatures)
    * `PDH` - Performance Data Helper (Disk I/O & GPU Core Load)
    * `DXGI` (Hardware enumeration & VRAM)
    * `Iphlpapi` (Network Stats)
    * `Toolhelp32` (Process Management)

## ☁️ CI/CD & Automated Releases

This project utilizes a fully automated **GitHub Actions CI/CD pipeline**. 
Upon pushing a new version tag (e.g., `v1.1.1`), the pipeline automatically:
1. Compiles the `Release | x64` build using MSBuild.
2. Signs the executable with a digital certificate (`SignTool`).
3. Packages the application into a portable `.zip` archive.
4. Publishes a new GitHub Release automatically.

## 🚀 How to Build Locally

If you want to compile CoreSense from source:

1. Clone the repository:
   ```bash
   git clone [https://github.com/andriibyhar171/SysMonitorGUI.git](https://github.com/andriibyhar171/SysMonitorGUI.git)

#define NOMINMAX
#include "SystemMonitor.h"
#include <string>
#include <algorithm>
#include <map>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include "resource.h"
#include "IconsFontAwesome5.h"
#include <dwmapi.h> 

#define WM_TRAYICON (WM_USER + 1)

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void DrawColoredProgressBar(float fraction, const ImVec2& size_arg, const char* overlay = "") {
    ImVec4 color;
    if (fraction < 0.5f) color = ImVec4(0.16f, 0.65f, 0.35f, 1.0f);
    else if (fraction < 0.85f) color = ImVec4(0.85f, 0.65f, 0.15f, 1.0f);
    else color = ImVec4(0.90f, 0.25f, 0.25f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(fraction, size_arg, overlay);
    ImGui::PopStyleColor();
}

bool DrawToggle(const char* label, bool* v, ImVec4 accentColor) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float height = ImGui::GetFrameHeight() * 0.85f;
    float width = height * 1.85f;
    float radius = height * 0.5f;

    ImGui::InvisibleButton(label, ImVec2(width, height));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *v = !*v;

    static std::map<ImGuiID, float> animations;
    ImGuiID id = ImGui::GetID(label);

    float target = *v ? 1.0f : 0.0f;
    if (animations.find(id) == animations.end()) animations[id] = target;

    float& t = animations[id];
    t += (target - t) * ImGui::GetIO().DeltaTime * 18.0f;

    ImVec4 frameBg = ImGui::GetStyle().Colors[ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg];

    ImU32 col_bg = ImGui::GetColorU32(ImVec4(
        frameBg.x + (accentColor.x - frameBg.x) * t,
        frameBg.y + (accentColor.y - frameBg.y) * t,
        frameBg.z + (accentColor.z - frameBg.z) * t,
        1.0f
    ));

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetTextLineHeight() - height) * 0.5f);
    ImGui::Text("%s", label);

    return clicked;
}

int main(int argc, char** argv)
{
    bool startInTray = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-tray") startInTray = true;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"CoreSense Utility", WS_OVERLAPPEDWINDOW, 100, 100, (int)(800 * main_scale), (int)(650 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(NOTIFYICONDATAW));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd; nid.uID = 1001; nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    lstrcpyW(nid.szTip, L"CoreSense Utility");
    Shell_NotifyIconW(NIM_ADD, &nid);

    if (!startInTray) ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    else ::ShowWindow(hwnd, SW_HIDE);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2; fontConfig.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * main_scale, &fontConfig);

    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.FontDataOwnedByAtlas = false;

    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    HRSRC hRes = ::FindResourceW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDR_FONT_FA), RT_RCDATA);
    if (hRes) {
        HGLOBAL hData = ::LoadResource(GetModuleHandle(nullptr), hRes);
        DWORD dataSize = ::SizeofResource(GetModuleHandle(nullptr), hRes);
        void* pData = ::LockResource(hData);

        if (pData) {
            io.Fonts->AddFontFromMemoryTTF(pData, dataSize, 14.0f * main_scale, &icons_config, icons_ranges);
        }
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    bool done = false;
    SystemMonitor monitor;

    static bool showCpuGraph = false, showRamGraph = false, showGpuGraph = false, alwaysOnTop = false;
    static bool miniMode = false, lastMiniMode = false;
    static int updateIntervalMs = 1000;
    static DWORD processToKillPID = 0;
    static std::string processToKillName = "";
    static bool openKillPopup = false;
    static int selectedGpuIndex = 0;
    static bool autoStart = monitor.IsAutoStartEnabled();
    static ULONGLONG lastWarningTime = 0;

    static int currentTheme = 0;
    static int lastTheme = -1;
    static ImVec4 currentAccentColor = ImVec4(0.16f, 0.44f, 0.75f, 1.0f);
    static ImVec4 lastAccentColor = ImVec4(0, 0, 0, 0);

    static bool settingsLoaded = false;
    if (!settingsLoaded) {
        monitor.LoadSettings(currentTheme, currentAccentColor.x, currentAccentColor.y, currentAccentColor.z);
        settingsLoaded = true;
    }

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (currentTheme != lastTheme ||
            currentAccentColor.x != lastAccentColor.x ||
            currentAccentColor.y != lastAccentColor.y ||
            currentAccentColor.z != lastAccentColor.z)
        {
            BOOL isDark = (currentTheme == 0 || currentTheme == 2) ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, 20, &isDark, sizeof(isDark));
            DwmSetWindowAttribute(hwnd, 19, &isDark, sizeof(isDark));

            ImGuiStyle& style = ImGui::GetStyle();

            style.WindowRounding = 8.0f;
            style.FrameRounding = 6.0f;
            style.PopupRounding = 6.0f;
            style.ScrollbarRounding = 6.0f;
            style.GrabRounding = 6.0f;
            style.TabRounding = 6.0f;
            style.ChildRounding = 6.0f;

            style.WindowPadding = ImVec2(14.0f, 14.0f);
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.ItemSpacing = ImVec2(10.0f, 10.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);

            if (currentTheme == 0) {
                ImVec4* colors = style.Colors;
                colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
                colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
                colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.16f, 0.95f);
                colors[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
                colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
                colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
                colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
                colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
                colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
                colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
                colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.62f, 1.00f);
                colors[ImGuiCol_TableRowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
                colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
                colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
                colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
                colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
            }
            else if (currentTheme == 1) {
                ImGui::StyleColorsLight();
            }
            else if (currentTheme == 2) {
                ImGui::StyleColorsClassic();
            }

            ImVec4* colors = style.Colors;
            colors[ImGuiCol_Button] = currentAccentColor;
            colors[ImGuiCol_ButtonHovered] = ImVec4(std::min(1.0f, currentAccentColor.x + 0.1f), std::min(1.0f, currentAccentColor.y + 0.1f), std::min(1.0f, currentAccentColor.z + 0.1f), 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(std::max(0.0f, currentAccentColor.x - 0.1f), std::max(0.0f, currentAccentColor.y - 0.1f), std::max(0.0f, currentAccentColor.z - 0.1f), 1.00f);
            colors[ImGuiCol_CheckMark] = currentAccentColor;
            colors[ImGuiCol_SliderGrab] = currentAccentColor;
            colors[ImGuiCol_SliderGrabActive] = ImVec4(std::min(1.0f, currentAccentColor.x + 0.1f), std::min(1.0f, currentAccentColor.y + 0.1f), std::min(1.0f, currentAccentColor.z + 0.1f), 1.00f);
            colors[ImGuiCol_Header] = currentAccentColor;
            colors[ImGuiCol_HeaderHovered] = ImVec4(std::min(1.0f, currentAccentColor.x + 0.1f), std::min(1.0f, currentAccentColor.y + 0.1f), std::min(1.0f, currentAccentColor.z + 0.1f), 0.8f);
            colors[ImGuiCol_HeaderActive] = currentAccentColor;
            colors[ImGuiCol_TabActive] = currentAccentColor;
            colors[ImGuiCol_TabHovered] = ImVec4(std::min(1.0f, currentAccentColor.x + 0.2f), std::min(1.0f, currentAccentColor.y + 0.2f), std::min(1.0f, currentAccentColor.z + 0.2f), 0.8f);

            lastTheme = currentTheme;
            lastAccentColor = currentAccentColor;

            SendMessage(hwnd, WM_NCACTIVATE, FALSE, 0); SendMessage(hwnd, WM_NCACTIVATE, TRUE, 0);
        }

        monitor.updateInterval = (DWORD)updateIntervalMs;
        const SystemInfoData& sysInfo = monitor.GetSystemInfo();
        const PowerData& powerInfo = monitor.GetPowerInfo();
        double cpuTemp = monitor.GetCpuTemperature();
        double cpuLoad = monitor.GetCPULoad();
        MemoryData memInfo = monitor.GetMemoryInfo();
        const auto& coreLoads = monitor.GetPerCoreLoad();
        const auto& gpus = monitor.GetGpuList();
        if (selectedGpuIndex >= gpus.size()) selectedGpuIndex = 0;
        GpuData gpuInfo = gpus.empty() ? GpuData() : gpus[selectedGpuIndex];

        monitor.UpdateHistory(cpuLoad, memInfo.loadPercent, coreLoads, gpuInfo.loadPercent);
        monitor.LogMetricsToCsv(cpuLoad, memInfo.loadPercent, gpuInfo.loadPercent);

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { ::Sleep(10); continue; }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0; CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);

        ImGui::Begin("System Monitor GUI", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (miniMode != lastMiniMode) {
            if (miniMode) SetWindowPos(hwnd, NULL, 0, 0, (int)(340 * main_scale), (int)(290 * main_scale), SWP_NOMOVE | SWP_NOZORDER);
            else SetWindowPos(hwnd, NULL, 0, 0, (int)(820 * main_scale), (int)(670 * main_scale), SWP_NOMOVE | SWP_NOZORDER);
            lastMiniMode = miniMode;
        }

        if (miniMode) {
            ImGui::TextColored(currentAccentColor, "CoreSense Mini");
            if (monitor.IsAdmin()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[" ICON_FA_SHIELD_ALT "]"); }
            ImGui::SameLine(ImGui::GetWindowWidth() - 100 * main_scale);
            if (DrawToggle("On Top", &alwaysOnTop, currentAccentColor)) SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            ImGui::Separator(); ImGui::Spacing();
            ImGui::Text(ICON_FA_MICROCHIP " CPU: %5.1f%%", cpuLoad);
            if (cpuTemp > 0.0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[%.1f C]", cpuTemp); }
            DrawColoredProgressBar((float)cpuLoad / 100.0f, ImVec2(-1.0f, 14.0f));
            ImGui::Text(ICON_FA_MEMORY " RAM: %5.1f%%", memInfo.loadPercent);
            DrawColoredProgressBar((float)memInfo.loadPercent / 100.0f, ImVec2(-1.0f, 14.0f));
            ImGui::Text(ICON_FA_GAMEPAD " GPU: %5.1f%%", gpuInfo.loadPercent);
            DrawColoredProgressBar((float)gpuInfo.loadPercent / 100.0f, ImVec2(-1.0f, 14.0f));
            ImGui::Spacing();
            if (powerInfo.hasBattery) ImGui::Text(ICON_FA_BATTERY_HALF " BAT: %3u%% (%s)", powerInfo.batteryPercent, powerInfo.isCharging ? "AC" : "Battery");
            if (monitor.IsLogging()) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[" ICON_FA_CIRCLE "] Logging to CSV...");
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_EXPAND_ARROWS_ALT " Full View", ImVec2(-1.0f, 0.0f))) miniMode = false;
        }
        else {
            ImGui::Text(ICON_FA_DESKTOP " PC Name: %s", sysInfo.pcName.c_str());
            if (monitor.IsAdmin()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[" ICON_FA_SHIELD_ALT " Admin]"); }
            else {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.5f, 0.2f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
                if (ImGui::Button(ICON_FA_SYNC_ALT " Restart as Admin")) monitor.RestartAsAdmin();
                ImGui::PopStyleColor(3);
            }
            ImGui::SameLine(ImGui::GetWindowWidth() - 210 * main_scale);
            if (ImGui::Button(ICON_FA_COMPRESS " Mini Mode")) miniMode = true;
            ImGui::SameLine();
            if (DrawToggle("On Top", &alwaysOnTop, currentAccentColor)) SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            ImVec4 infoColor = (currentTheme == 1) ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : ImVec4(0.6f, 0.6f, 0.62f, 1.0f);
            ImGui::TextColored(infoColor, "OS: %s | Uptime: %u h %u m", sysInfo.osVersion.c_str(), sysInfo.uptimeHours, sysInfo.uptimeMinutes);
            ImGui::TextColored(infoColor, "CPU: %s", sysInfo.cpuName.c_str());

            if (powerInfo.hasBattery) {
                ImGui::Text("Power: %s | Battery: %u%%", powerInfo.isCharging ? "Plugged In (AC)" : "On Battery", powerInfo.batteryPercent);
                if (!powerInfo.isCharging && powerInfo.batteryLifeSeconds != (DWORD)-1) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "(~%lu mins left)", powerInfo.batteryLifeSeconds / 60); }
            }
            else ImGui::TextColored(infoColor, "Power: Desktop (AC Power)");

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            static bool showActiveNetsOnly = true;
            if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_NoTooltip)) {
                if (ImGui::BeginTabItem(" " ICON_FA_TACHOMETER_ALT " Dashboard  ")) {
                    ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_MICROCHIP " PROCESSOR (CPU)");
                    ImGui::Text("Overall Load: %5.1f%%", cpuLoad);
                    if (cpuTemp > 0.0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "| Temp: %.1f C (ACPI)", cpuTemp); }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 100 * main_scale);
                    if (ImGui::SmallButton(showCpuGraph ? "Hide Graph##CPU" : "Show Graph##CPU")) showCpuGraph = !showCpuGraph;
                    DrawColoredProgressBar((float)cpuLoad / 100.0f, ImVec2(-1.0f, 0.0f));
                    if (showCpuGraph) {
                        char cpuOverlay[32]; snprintf(cpuOverlay, sizeof(cpuOverlay), "Overall CPU History: %.1f%%", cpuLoad);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, currentAccentColor); ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(currentAccentColor.x, currentAccentColor.y, currentAccentColor.z, 0.5f));
                        ImGui::PlotLines("##CPUPlot", monitor.GetCpuHistory(), 100, 0, cpuOverlay, 0.0f, 100.0f, ImVec2(-1.0f, 80.0f)); ImGui::PopStyleColor(2);
                    }
                    if (ImGui::CollapsingHeader(ICON_FA_SERVER " Per-Core CPU Details")) {
                        for (size_t i = 0; i < coreLoads.size(); i++) {
                            char label[32]; snprintf(label, sizeof(label), "Core %zu: %.1f%%", i, coreLoads[i]);
                            DrawColoredProgressBar((float)coreLoads[i] / 100.0f, ImVec2(-1.0f, 0.0f), label); ImGui::Spacing();
                        }
                    }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_MEMORY " MEMORY (RAM)");
                    ImGui::Text("Load: %5.1f%% (%llu MB free of %llu MB)", memInfo.loadPercent, memInfo.availableMB, memInfo.totalMB);
                    ImGui::SameLine(ImGui::GetWindowWidth() - 100 * main_scale);
                    if (ImGui::SmallButton(showRamGraph ? "Hide Graph##RAM" : "Show Graph##RAM")) showRamGraph = !showRamGraph;
                    DrawColoredProgressBar((float)memInfo.loadPercent / 100.0f, ImVec2(-1.0f, 0.0f));
                    if (showRamGraph) {
                        char ramOverlay[32]; snprintf(ramOverlay, sizeof(ramOverlay), "RAM History: %.1f%%", memInfo.loadPercent);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, currentAccentColor); ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(currentAccentColor.x, currentAccentColor.y, currentAccentColor.z, 0.5f));
                        ImGui::PlotLines("##RAMPlot", monitor.GetRamHistory(), 100, 0, ramOverlay, 0.0f, 100.0f, ImVec2(-1.0f, 80.0f)); ImGui::PopStyleColor(2);
                    }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_GAMEPAD " GRAPHICS (GPU)");
                    if (gpus.size() > 1) {
                        ImGui::SetNextItemWidth(250.0f * main_scale);
                        if (ImGui::BeginCombo("##GPUCombo", gpuInfo.name.c_str())) {
                            for (int i = 0; i < gpus.size(); ++i) {
                                bool is_selected = (selectedGpuIndex == i);
                                if (ImGui::Selectable(gpus[i].name.c_str(), is_selected)) selectedGpuIndex = i;
                                if (is_selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                    else ImGui::Text("%s", gpuInfo.name.c_str());
                    ImGui::TextColored(infoColor, "VRAM Load: (%llu MB used of %llu MB)", gpuInfo.usedVRAM_MB, gpuInfo.totalVRAM_MB);
                    ImGui::SameLine(ImGui::GetWindowWidth() - 100 * main_scale);
                    if (ImGui::SmallButton(showGpuGraph ? "Hide Graph##GPU" : "Show Graph##GPU")) showGpuGraph = !showGpuGraph;
                    DrawColoredProgressBar((float)gpuInfo.loadPercent / 100.0f, ImVec2(-1.0f, 0.0f));
                    if (showGpuGraph) {
                        char gpuOverlay[32]; snprintf(gpuOverlay, sizeof(gpuOverlay), "VRAM History: %.1f%%", gpuInfo.loadPercent);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, currentAccentColor); ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(currentAccentColor.x, currentAccentColor.y, currentAccentColor.z, 0.5f));
                        ImGui::PlotLines("##GPUPlot", monitor.GetGpuHistory(), 100, 0, gpuOverlay, 0.0f, 100.0f, ImVec2(-1.0f, 80.0f)); ImGui::PopStyleColor(2);
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" " ICON_FA_LIST_UL " Processes  ")) {
                    ImGui::Spacing();
                    auto processes = monitor.GetProcesses();
                    static ImGuiTextFilter filter;
                    ImGui::Text(ICON_FA_SEARCH " Search:"); ImGui::SameLine(); filter.Draw("##ProcessFilter", 250.0f * main_scale); ImGui::Spacing();
                    if (ImGui::BeginTable("ProcTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImVec2(0, 360 * main_scale))) {
                        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending); ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort); ImGui::TableSetupColumn("CPU (%)", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending); ImGui::TableSetupColumn("RAM (MB)", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending); ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending); ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_NoSort); ImGui::TableHeadersRow();
                        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                            if (sortSpecs->SpecsCount > 0) {
                                const ImGuiTableColumnSortSpecs* currentSpec = &sortSpecs->Specs[0];
                                bool ascend = currentSpec->SortDirection == ImGuiSortDirection_Ascending;
                                std::sort(processes.begin(), processes.end(), [currentSpec, ascend](const ProcessData& a, const ProcessData& b) {
                                    switch (currentSpec->ColumnIndex) {
                                    case 0: return ascend ? (a.pid < b.pid) : (a.pid > b.pid);
                                    case 1: { int cmp = _stricmp(a.name.c_str(), b.name.c_str()); return ascend ? (cmp < 0) : (cmp > 0); }
                                    case 2: return ascend ? (a.cpuLoad < b.cpuLoad) : (a.cpuLoad > b.cpuLoad);
                                    case 3: return ascend ? (a.ramUsedMB < b.ramUsedMB) : (a.ramUsedMB > b.ramUsedMB);
                                    case 4: return ascend ? (a.threads < b.threads) : (a.threads > b.threads);
                                    default: return false;
                                    }
                                    });
                            }
                        }
                        for (const auto& p : processes) {
                            if (!filter.PassFilter(p.name.c_str())) continue;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::Text("%lu", p.pid);
                            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", p.name.c_str());
                            ImGui::TableSetColumnIndex(2);
                            if (p.cpuLoad > 10.0) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%.1f%%", p.cpuLoad);
                            else ImGui::Text("%.1f%%", p.cpuLoad);
                            ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", p.ramUsedMB);
                            ImGui::TableSetColumnIndex(4); ImGui::Text("%lu", p.threads);
                            ImGui::TableSetColumnIndex(5); ImGui::PushID(p.pid);
                            if (ImGui::Button(ICON_FA_SKULL " Kill")) { processToKillPID = p.pid; processToKillName = p.name; openKillPopup = true; }
                            if (!p.path.empty()) {
                                ImGui::SameLine();
                                if (ImGui::Button(ICON_FA_FOLDER_OPEN " Folder")) { std::string args = "/select,\"" + p.path + "\""; ShellExecuteA(NULL, "open", "explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL); }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" " ICON_FA_HDD " I/O & Network  ")) {
                    ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_SAVE " LOGICAL DRIVES");
                    const auto& drives = monitor.GetDrivesInfo();
                    for (const auto& d : drives) {
                        ImGui::BulletText("%s [%s] - Free: %llu GB / Total: %llu GB", d.name.c_str(), d.type.c_str(), d.freeGB, d.totalGB);
                        ImGui::Indent(); ImGui::TextColored(infoColor, "Read: %7.1f MB/s | Write: %7.1f MB/s", d.readSpeedMBps, d.writeSpeedMBps); ImGui::Unindent();
                    }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_WIFI " NETWORK INTERFACES");
                    DrawToggle("Show Active Interfaces Only", &showActiveNetsOnly, currentAccentColor);
                    ImGui::Spacing();
                    const auto& nets = monitor.GetNetworkStats();
                    bool anyDisplayed = false;
                    for (const auto& n : nets) {
                        if (showActiveNetsOnly && n.downloadSpeedKBps < 0.1 && n.uploadSpeedKBps < 0.1) continue;
                        anyDisplayed = true;
                        ImGui::BulletText("IF %lu: DL: %7.1f KB/s | UL: %7.1f KB/s  [Total: %llu MB In / %llu MB Out]", n.interfaceIndex, n.downloadSpeedKBps, n.uploadSpeedKBps, n.receivedMB, n.sentMB);
                    }
                    if (showActiveNetsOnly && !anyDisplayed) { ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  No active network traffic right now..."); }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" " ICON_FA_COGS " Settings  ")) {
                    ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_INFO_CIRCLE " SYSTEM INFORMATION");
                    ImGui::TextColored(infoColor, "OS Version: %s", sysInfo.osVersion.c_str());
                    ImGui::TextColored(infoColor, "Processor:  %s", sysInfo.cpuName.c_str());
                    ImGui::TextColored(infoColor, "Uptime:     %u hours, %u minutes", sysInfo.uptimeHours, sysInfo.uptimeMinutes);
                    if (powerInfo.hasBattery) { ImGui::TextColored(infoColor, "Power:      %s | Battery: %u%%", powerInfo.isCharging ? "Plugged In (AC)" : "On Battery", powerInfo.batteryPercent); }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextColored(currentAccentColor, ICON_FA_SLIDERS_H " CUSTOMIZATION & BEHAVIOR");

                    bool settingsChanged = false;

                    ImGui::Text("Accent Color:"); ImGui::SameLine();
                    if (ImGui::ColorButton("Blue", ImVec4(0.16f, 0.44f, 0.75f, 1.0f))) { currentAccentColor = ImVec4(0.16f, 0.44f, 0.75f, 1.0f); settingsChanged = true; } ImGui::SameLine();
                    if (ImGui::ColorButton("Red", ImVec4(0.85f, 0.25f, 0.35f, 1.0f))) { currentAccentColor = ImVec4(0.85f, 0.25f, 0.35f, 1.0f); settingsChanged = true; } ImGui::SameLine();
                    if (ImGui::ColorButton("Green", ImVec4(0.25f, 0.75f, 0.45f, 1.0f))) { currentAccentColor = ImVec4(0.25f, 0.75f, 0.45f, 1.0f); settingsChanged = true; } ImGui::SameLine();
                    if (ImGui::ColorButton("Purple", ImVec4(0.65f, 0.25f, 0.85f, 1.0f))) { currentAccentColor = ImVec4(0.65f, 0.25f, 0.85f, 1.0f); settingsChanged = true; } ImGui::SameLine();
                    if (ImGui::ColorEdit3("Custom", (float*)&currentAccentColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) settingsChanged = true;

                    ImGui::SameLine(ImGui::GetWindowWidth() - 170 * main_scale);
                    if (ImGui::Button(ICON_FA_UNDO " Default UI")) {
                        currentTheme = 0;
                        currentAccentColor = ImVec4(0.16f, 0.44f, 0.75f, 1.0f);
                        settingsChanged = true;
                    }

                    ImGui::Spacing();
                    if (DrawToggle("Auto-Start with Windows (in Tray)", &autoStart, currentAccentColor)) monitor.EnableAutoStart(autoStart);
                    ImGui::Spacing(); ImGui::SetNextItemWidth(250.0f * main_scale); ImGui::SliderInt("Refresh Rate", &updateIntervalMs, 100, 3000, "%d ms");
                    ImGui::Spacing(); const char* themes[] = { "Modern Dark", "Light", "Classic" }; ImGui::SetNextItemWidth(150.0f * main_scale);
                    if (ImGui::Combo("Theme", &currentTheme, themes, IM_ARRAYSIZE(themes))) settingsChanged = true;

                    if (settingsChanged) {
                        monitor.SaveSettings(currentTheme, currentAccentColor.x, currentAccentColor.y, currentAccentColor.z);
                    }

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(currentAccentColor, ICON_FA_FILE_CSV " TELEMETRY LOGGING");
                    if (monitor.IsLogging()) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        if (ImGui::Button(ICON_FA_STOP_CIRCLE " Stop Logging CSV")) monitor.ToggleCsvLogging();
                        ImGui::PopStyleColor(3);
                    }
                    else { if (ImGui::Button(ICON_FA_PLAY_CIRCLE " Start CSV Logging")) monitor.ToggleCsvLogging(); }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            if (cpuLoad > 90.0 || memInfo.loadPercent > 90.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[WARNING] CRITICAL RESOURCE LOAD DETECTED!");
                ULONGLONG currentTime = GetTickCount64();
                if (currentTime - lastWarningTime > 60000 || lastWarningTime == 0) {
                    monitor.Log("ALERT: Critical Load. CPU: " + std::to_string(cpuLoad) + "%, RAM: " + std::to_string(memInfo.loadPercent) + "%");
                    nid.uFlags = NIF_INFO; lstrcpyW(nid.szInfoTitle, L"CoreSense Warning");
                    if (cpuLoad > 90.0 && memInfo.loadPercent > 90.0) lstrcpyW(nid.szInfo, L"Critical: High CPU and RAM load (>90%)!");
                    else if (cpuLoad > 90.0) lstrcpyW(nid.szInfo, L"Critical: High CPU load (>90%)!");
                    else lstrcpyW(nid.szInfo, L"Critical: High RAM usage (>90%)!");
                    nid.dwInfoFlags = NIIF_WARNING; Shell_NotifyIconW(NIM_MODIFY, &nid);
                    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; lastWarningTime = currentTime;
                }
            }

            if (openKillPopup) { ImGui::OpenPopup("Confirm Kill Process"); openKillPopup = false; }
            ImVec2 center = ImGui::GetMainViewport()->GetCenter(); ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Confirm Kill Process", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("WARNING: Terminating a process can cause loss of data\nor system instability."); ImGui::Separator(); ImGui::Spacing();
                ImGui::Text("Are you sure you want to terminate:"); ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s (PID: %lu)", processToKillName.c_str(), processToKillPID);
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                if (ImGui::Button("Yes, Kill it!", ImVec2(120 * main_scale, 0))) { monitor.KillProcess(processToKillPID); ImGui::CloseCurrentPopup(); }
                ImGui::PopStyleColor(3); ImGui::SetItemDefaultFocus(); ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120 * main_scale, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        ImGui::End();
        ImGui::Render();
        const float cc[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0; D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fla[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext))) return false;
    CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() { CleanupRenderTarget(); if (g_pSwapChain) g_pSwapChain->Release(); if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release(); if (g_pd3dDevice) g_pd3dDevice->Release(); }
void CreateRenderTarget() { ID3D11Texture2D* pBB; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB)); g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView); pBB->Release(); }
void CleanupRenderTarget() { if (g_mainRenderTargetView) g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE: if (wParam != SIZE_MINIMIZED) { g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam); } return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_MINIMIZE) { ShowWindow(hWnd, SW_HIDE); return 0; } break;
    case WM_TRAYICON: if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd); } return 0;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
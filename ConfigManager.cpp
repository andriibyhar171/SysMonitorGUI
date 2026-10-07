#include "ConfigManager.h"
#include <string>

namespace ConfigManager {
    bool IsAutoStartEnabled() {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t value[MAX_PATH];
            DWORD size = sizeof(value);
            if (RegQueryValueExW(hKey, L"CoreSenseUtility", NULL, NULL, (LPBYTE)value, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return true;
            }
            RegCloseKey(hKey);
        }
        return false;
    }

    void EnableAutoStart(bool enable) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
            if (enable) {
                wchar_t path[MAX_PATH];
                GetModuleFileNameW(NULL, path, MAX_PATH);
                std::wstring fullPath = std::wstring(L"\"") + path + L"\" -tray";
                RegSetValueExW(hKey, L"CoreSenseUtility", 0, REG_SZ, (const BYTE*)fullPath.c_str(), (DWORD)((fullPath.size() + 1) * sizeof(wchar_t)));
            }
            else {
                RegDeleteValueW(hKey, L"CoreSenseUtility");
            }
            RegCloseKey(hKey);
        }
    }

    void SaveSettings(int theme, float r, float g, float b) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\CoreSense", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"Theme", 0, REG_DWORD, (const BYTE*)&theme, sizeof(DWORD));
            float color[3] = { r, g, b };
            RegSetValueExW(hKey, L"AccentColor", 0, REG_BINARY, (const BYTE*)color, sizeof(color));
            RegCloseKey(hKey);
        }
    }

    void LoadSettings(int& theme, float& r, float& g, float& b) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\CoreSense", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD size = sizeof(DWORD);
            DWORD tempTheme;
            if (RegQueryValueExW(hKey, L"Theme", NULL, NULL, (LPBYTE)&tempTheme, &size) == ERROR_SUCCESS) {
                theme = tempTheme;
            }

            float color[3];
            DWORD colorSize = sizeof(color);
            if (RegQueryValueExW(hKey, L"AccentColor", NULL, NULL, (LPBYTE)color, &colorSize) == ERROR_SUCCESS && colorSize == sizeof(color)) {
                r = color[0];
                g = color[1];
                b = color[2];
            }
            RegCloseKey(hKey);
        }
    }
}
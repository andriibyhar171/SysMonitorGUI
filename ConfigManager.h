#pragma once
#include <windows.h>

namespace ConfigManager {
    bool IsAutoStartEnabled();
    void EnableAutoStart(bool enable);
    void SaveSettings(int theme, float r, float g, float b);
    void LoadSettings(int& theme, float& r, float& g, float& b);
}
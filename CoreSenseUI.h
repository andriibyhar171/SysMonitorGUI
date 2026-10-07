#pragma once
#include <windows.h>
#include "SystemMonitor.h"

namespace CoreSenseUI {
    void Render(SystemMonitor& monitor, HWND hwnd, NOTIFYICONDATAW& nid, float main_scale);
}
#include "counter.h"
#include "log.h"
#include "session.h"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION dev
#endif

#define APM_STRINGIZE2(x) #x
#define APM_STRINGIZE(x) APM_STRINGIZE2(x)
#define APM_VERSION_STRING APM_STRINGIZE(APP_VERSION)

HHOOK eHook = NULL;
HHOOK mHook = NULL;

std::atomic<bool> keepRunning(true);

Counter *g_counter = nullptr;
Settings g_settings;
std::string g_sessionsDir;
std::string g_lastSaved;

bool g_ctrlDown = false;
bool g_altDown = false;
bool g_shiftDown = false;
bool g_winDown = false;
bool g_hotkeyHeld = false;

static std::wstring widen(const std::string &text)
{
    return std::wstring(text.begin(), text.end());
}

static std::string narrow(const std::wstring &text)
{
    std::string result;
    result.reserve(text.size());
    for (wchar_t c : text)
    {
        if (c < 128)
            result.push_back(static_cast<char>(c));
    }
    return result;
}

static std::wstring getExeDirectory()
{
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path = path.substr(0, slash);
    return path;
}

static std::string baseName(const std::string &path)
{
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void tick()
{
    while (keepRunning)
    {
        for (int i = 0; i < 10 && keepRunning; i++)
            Sleep(100);
        if (!keepRunning)
            break;
        if (g_counter)
            g_counter->tick();
    }
}

static void onToggleSession()
{
    if (!g_counter)
        return;

    g_counter->toggleSession();

    if (g_counter->active())
    {
        g_lastSaved.clear();
        logMessage("info", "Session started");
    }
    else
    {
        APMStats stats = g_counter->snapshot();
        std::string path;
        if (saveSessionCsv(g_sessionsDir, stats, path))
        {
            g_lastSaved = path;
            logMessage("info", "Session saved: " + path);
        }
        else
        {
            logMessage("error", "Failed to save session");
        }
    }
}

static void updateModifier(unsigned int vk, bool down)
{
    switch (vk)
    {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        g_ctrlDown = down;
        break;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        g_altDown = down;
        break;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        g_shiftDown = down;
        break;
    case VK_LWIN:
    case VK_RWIN:
        g_winDown = down;
        break;
    default:
        break;
    }
}

static bool modifiersMatch(unsigned int modifiers)
{
    if ((modifiers & HotkeyModControl) && !g_ctrlDown)
        return false;
    if ((modifiers & HotkeyModAlt) && !g_altDown)
        return false;
    if ((modifiers & HotkeyModShift) && !g_shiftDown)
        return false;
    if ((modifiers & HotkeyModWin) && !g_winDown)
        return false;
    return true;
}

static bool isHotkey(unsigned int vk)
{
    return vk == g_settings.hotkey.vk && modifiersMatch(g_settings.hotkey.modifiers);
}

LRESULT mouseProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(eHook, nCode, wparam, lparam);

    if (wparam == WM_LBUTTONDOWN || wparam == WM_RBUTTONDOWN || wparam == WM_XBUTTONDOWN ||
        wparam == WM_MBUTTONDOWN)
    {
        if (g_counter)
            g_counter->addMouse(static_cast<unsigned int>(wparam));
    }

    return CallNextHookEx(eHook, nCode, wparam, lparam);
}

LRESULT keyboardProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(mHook, nCode, wparam, lparam);

    KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lparam);
    unsigned int vk = kb->vkCode;

    if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
    {
        bool hotkey = isHotkey(vk);
        updateModifier(vk, true);

        if (hotkey)
        {
            if (!g_hotkeyHeld)
            {
                g_hotkeyHeld = true;
                onToggleSession();
            }
            return 1;
        }

        if (g_counter)
            g_counter->addKey(vk);
    }
    else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP)
    {
        updateModifier(vk, false);

        if (vk == g_settings.hotkey.vk && g_hotkeyHeld)
        {
            g_hotkeyHeld = false;
            return 1;
        }

        if (g_counter)
            g_counter->addKeyUp(vk);
    }

    return CallNextHookEx(mHook, nCode, wparam, lparam);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paintStruct;
        HDC hdc = BeginPaint(hwnd, &paintStruct);

        RECT rect;
        GetClientRect(hwnd, &rect);

        int value = 0;
        if (g_counter)
            value = g_settings.overlayEapm ? g_counter->currentEapm() : g_counter->currentApm();

        std::string text = std::to_string(value);
        text += g_settings.overlayEapm ? " : eAPM " : " : APM ";

        std::wstring wide(text.begin(), text.end());
        DrawText(hdc, wide.c_str(), -1, &rect, DT_RIGHT | DT_NOCLIP | DT_SINGLELINE | DT_VCENTER);

        EndPaint(hwnd, &paintStruct);
        break;
    }
    case WM_TIMER:
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        InvalidateRect(hwnd, &rect, TRUE);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

struct MonitorSearch
{
    bool foundSecondary = false;
    RECT secondary = {0, 0, 0, 0};
};

static BOOL CALLBACK monitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM lparam)
{
    MonitorSearch *search = reinterpret_cast<MonitorSearch *>(lparam);

    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfo(monitor, &info))
    {
        if (!(info.dwFlags & MONITORINFOF_PRIMARY) && !search->foundSecondary)
        {
            search->secondary = info.rcWork;
            search->foundSecondary = true;
        }
    }

    return TRUE;
}

static std::string formatDuration(int seconds)
{
    if (seconds < 0)
        seconds = 0;

    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    char buffer[32];
    if (hours > 0)
        sprintf(buffer, "%d:%02d:%02d", hours, minutes, secs);
    else
        sprintf(buffer, "%d:%02d", minutes, secs);
    return std::string(buffer);
}

static std::string formatNumber(long long value)
{
    std::string digits = std::to_string(value);
    std::string result;
    int length = (int)digits.size();
    for (int i = 0; i < length; i++)
    {
        if (i > 0 && (length - i) % 3 == 0)
            result.push_back(',');
        result.push_back(digits[i]);
    }
    return result;
}

static void drawText(HDC hdc, int x, int y, const std::string &text)
{
    std::wstring wide(text.begin(), text.end());
    TextOut(hdc, x, y, wide.c_str(), (int)wide.size());
}

static int textWidth(HDC hdc, const std::string &text)
{
    std::wstring wide(text.begin(), text.end());
    SIZE size = {0, 0};
    GetTextExtentPoint32(hdc, wide.c_str(), (int)wide.size(), &size);
    return size.cx;
}

static std::vector<int> metricValues(const std::vector<SecondSample> &history, bool eapm)
{
    std::vector<int> values;
    values.reserve(history.size());
    for (const SecondSample &sample : history)
        values.push_back(eapm ? sample.eapm : sample.raw);
    return values;
}

static void drawLine(HDC hdc, const std::vector<int> &data, int left, int bottom, int plotWidth,
                     int plotHeight, int maxValue, COLORREF color)
{
    int n = (int)data.size();
    if (n < 2)
        return;

    std::vector<POINT> points;
    if (n <= plotWidth)
    {
        points.resize(n);
        for (int i = 0; i < n; i++)
        {
            points[i].x = left + (LONG)(((long long)i * plotWidth) / (n - 1));
            points[i].y = bottom - (LONG)(((long long)data[i] * plotHeight) / maxValue);
        }
    }
    else
    {
        points.resize(plotWidth);
        for (int column = 0; column < plotWidth; column++)
        {
            int start = (int)(((long long)column * n) / plotWidth);
            int end = (int)(((long long)(column + 1) * n) / plotWidth);
            if (end <= start)
                end = start + 1;
            if (end > n)
                end = n;

            int columnMax = 0;
            for (int i = start; i < end; i++)
                if (data[i] > columnMax)
                    columnMax = data[i];

            points[column].x = left + column;
            points[column].y = bottom - (LONG)(((long long)columnMax * plotHeight) / maxValue);
        }
    }

    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    Polyline(hdc, points.data(), (int)points.size());
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void drawGraph(HDC hdc, int width, int height)
{
    APMStats stats = g_counter ? g_counter->snapshot() : APMStats();

    RECT full = {0, 0, width, height};
    HBRUSH background = CreateSolidBrush(RGB(20, 20, 28));
    FillRect(hdc, &full, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);

    const int padding = 8;
    const int headerHeight = 44;
    const int leftMargin = 44;
    const int bottomMargin = 16;

    std::string state = stats.active ? "REC" : "PAUSED";
    SetTextColor(hdc, stats.active ? RGB(80, 220, 120) : RGB(230, 180, 80));
    drawText(hdc, padding, 6, state);

    std::string line1 = "   Time " + formatDuration(stats.elapsedSeconds) + "   Actions " +
                        formatNumber(stats.totalActions);
    if (!g_lastSaved.empty())
        line1 += "   Saved " + baseName(g_lastSaved);
    SetTextColor(hdc, RGB(210, 210, 230));
    drawText(hdc, padding + textWidth(hdc, state), 6, line1);

    std::string line2 = "APM " + std::to_string(stats.current);
    if (stats.eapmEnabled)
        line2 += "   eAPM " + std::to_string(stats.currentEapm);
    line2 += "   Avg " + std::to_string(stats.average);
    line2 += "   5m " + std::to_string(stats.average5Min);
    line2 += "   Peak " + std::to_string(stats.peak);
    line2 += "   Keys " + std::to_string(stats.currentKeyboard);
    line2 += "   Clicks " + std::to_string(stats.currentMouse);
    SetTextColor(hdc, RGB(170, 170, 200));
    drawText(hdc, padding, 24, line2);

    const int left = leftMargin;
    const int right = width - padding;
    const int top = headerHeight;
    const int bottom = height - bottomMargin;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    if (plotWidth <= 1 || plotHeight <= 1)
        return;

    int maxValue = 100;
    for (const SecondSample &sample : stats.history)
    {
        maxValue = std::max(maxValue, sample.raw);
        if (stats.eapmEnabled)
            maxValue = std::max(maxValue, sample.eapm);
    }

    int step = 100;
    while (maxValue > step)
        step *= 2;
    maxValue = step;

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 75));
    HPEN oldPen = (HPEN)SelectObject(hdc, gridPen);

    SetTextColor(hdc, RGB(140, 140, 170));
    for (int i = 0; i <= 2; i++)
    {
        int y = bottom - (plotHeight * i) / 2;
        MoveToEx(hdc, left, y, NULL);
        LineTo(hdc, right, y);
        drawText(hdc, 2, y - 7, std::to_string((maxValue * i) / 2));
    }

    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    drawText(hdc, left, bottom + 1, "0:00");
    std::string endLabel = formatDuration(stats.elapsedSeconds);
    std::wstring wideEnd(endLabel.begin(), endLabel.end());
    SIZE endSize = {0, 0};
    GetTextExtentPoint32(hdc, wideEnd.c_str(), (int)wideEnd.size(), &endSize);
    TextOut(hdc, right - endSize.cx, bottom + 1, wideEnd.c_str(), (int)wideEnd.size());

    if (stats.eapmEnabled)
    {
        drawLine(hdc, metricValues(stats.history, true), left, bottom, plotWidth, plotHeight,
                 maxValue, RGB(90, 200, 240));
        SetTextColor(hdc, RGB(90, 200, 240));
        drawText(hdc, right - textWidth(hdc, "eapm"), top + 2, "eapm");
    }

    drawLine(hdc, metricValues(stats.history, false), left, bottom, plotWidth, plotHeight, maxValue,
             RGB(80, 220, 120));
    SetTextColor(hdc, RGB(80, 220, 120));
    drawText(hdc, right - textWidth(hdc, "raw"), top + 2 + (stats.eapmEnabled ? 14 : 0), "raw");
}

static LRESULT CALLBACK graphWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paintStruct;
        HDC hdc = BeginPaint(hwnd, &paintStruct);

        RECT rect;
        GetClientRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        if (width > 0 && height > 0)
        {
            HDC bufferDc = CreateCompatibleDC(hdc);
            HBITMAP bufferBitmap = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(bufferDc, bufferBitmap);

            drawGraph(bufferDc, width, height);

            BitBlt(hdc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);

            SelectObject(bufferDc, oldBitmap);
            DeleteObject(bufferBitmap);
            DeleteDC(bufferDc);
        }

        EndPaint(hwnd, &paintStruct);
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        InvalidateRect(hwnd, &rect, FALSE);
        break;
    }
    case WM_CREATE:
        SetTimer(hwnd, 1, 500, 0);
        break;
    case WM_CLOSE:
    {
        RECT rect;
        if (GetWindowRect(hwnd, &rect))
        {
            g_settings.graphX = rect.left;
            g_settings.graphY = rect.top;
            g_settings.graphWidth = rect.right - rect.left;
            g_settings.graphHeight = rect.bottom - rect.top;
        }
        DestroyWindow(hwnd);
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring exeDir = getExeDirectory();
    std::string exeDirA = narrow(exeDir);
    std::string settingsPath = exeDirA + "\\settings.xml";
    std::string logPath = exeDirA + "\\apm-tracker.log";
    g_sessionsDir = exeDirA + "\\sessions";

    initLog(logPath);
    logMessage("info", std::string("APM Tracker v") + APM_VERSION_STRING + " starting");

    if (!loadSettings(settingsPath, g_settings))
    {
        logMessage("info", "No settings file found, writing defaults");
        saveSettings(settingsPath, g_settings);
    }

    CounterConfig counterConfig;
    counterConfig.preset = g_settings.preset;
    counterConfig.eapm = g_settings.eapm || g_settings.preset == Preset::AoE2;
    Counter counter(counterConfig);
    g_counter = &counter;

    CreateDirectoryW(widen(g_sessionsDir).c_str(), nullptr);

    eHook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)keyboardProc, GetModuleHandle(NULL), 0);
    mHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)mouseProc, GetModuleHandle(NULL), 0);

    HINSTANCE instance = GetModuleHandle(0);
    HCURSOR cursor = LoadCursor(0, IDC_ARROW);

    WNDCLASSEX wndclass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW,          // style
        wndProc,                          // window proc
        0,                                // extra bytes following window class
        0,                                // extra bytes following window instance
        instance,                         // hInstance
        LoadIcon(0, IDI_APPLICATION),     // hIcon
        cursor,                           // hCursor
        HBRUSH(COLOR_WINDOW + 1),         // hbrBackground
        0,                                // MenuName
        TEXT("actions-per-minute-class"), // ClassName
        LoadIcon(0, IDI_APPLICATION)      // small icon
    };

    if (!RegisterClassEx(&wndclass))
    {
        MessageBox(NULL, TEXT("Could not register the overlay window class."), TEXT("APM Tracker"),
                   MB_ICONERROR);
        return EXIT_FAILURE;
    }

    int overlayWidth = g_settings.overlayEapm ? 88 : 70;
    int overlayHeight = 25;
    int extraStyles =
        WS_EX_COMPOSITED | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT;
    int styles = WS_VISIBLE | WS_POPUP;
    int overlayX = g_settings.overlayX >= 0 ? g_settings.overlayX
                                            : GetSystemMetrics(SM_CXSCREEN) - overlayWidth;
    int overlayY = g_settings.overlayY >= 0 ? g_settings.overlayY : overlayHeight * 3;
    HWND hwnd = CreateWindowEx(extraStyles, TEXT("actions-per-minute-class"),
                               TEXT("actions-per-minute"), styles, overlayX, overlayY, overlayWidth,
                               overlayHeight, 0, 0, instance, NULL);

    if (!g_settings.overlayVisible)
        ShowWindow(hwnd, SW_HIDE);

    WNDCLASSEX graphClass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW,      // style
        graphWndProc,                 // window proc
        0,                            // extra bytes following window class
        0,                            // extra bytes following window instance
        instance,                     // hInstance
        LoadIcon(0, IDI_APPLICATION), // hIcon
        cursor,                       // hCursor
        HBRUSH(COLOR_WINDOW + 1),     // hbrBackground
        0,                            // MenuName
        TEXT("apm-graph-class"),      // ClassName
        LoadIcon(0, IDI_APPLICATION)  // small icon
    };

    if (!RegisterClassEx(&graphClass))
    {
        MessageBox(NULL, TEXT("Could not register the graph window class."), TEXT("APM Tracker"),
                   MB_ICONERROR);
        return EXIT_FAILURE;
    }

    int graphWidth = g_settings.graphWidth;
    int graphHeight = g_settings.graphHeight;
    int graphX = g_settings.graphX;
    int graphY = g_settings.graphY;
    if (graphX < 0 || graphY < 0)
    {
        MonitorSearch monitorSearch;
        EnumDisplayMonitors(NULL, NULL, monitorEnumProc, reinterpret_cast<LPARAM>(&monitorSearch));

        graphX = 60;
        graphY = 120;
        if (monitorSearch.foundSecondary)
        {
            RECT area = monitorSearch.secondary;
            int areaWidth = area.right - area.left;
            if (graphWidth > areaWidth - 40)
                graphWidth = areaWidth - 40;
            graphX = area.left + 20;
            graphY = area.top + 20;
        }
    }

    std::wstring graphTitle = widen(std::string("APM over time - v") + APM_VERSION_STRING);
    HWND graphHwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TEXT("apm-graph-class"),
                                    graphTitle.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, graphX,
                                    graphY, graphWidth, graphHeight, 0, 0, instance, NULL);

    if (!graphHwnd)
    {
        MessageBox(NULL, TEXT("Could not create the graph window."), TEXT("APM Tracker"),
                   MB_ICONERROR);
        return EXIT_FAILURE;
    }

    std::thread t(tick);

    int timer = 500;
    SetTimer(hwnd, timer, timer, 0);

    MSG msg = {};
    while (WM_QUIT != msg.message)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(1);
    }

    keepRunning = false;
    if (t.joinable())
        t.join();

    RECT overlayRect;
    if (GetWindowRect(hwnd, &overlayRect))
    {
        g_settings.overlayX = overlayRect.left;
        g_settings.overlayY = overlayRect.top;
    }

    RECT graphRect;
    if (graphHwnd && GetWindowRect(graphHwnd, &graphRect))
    {
        g_settings.graphX = graphRect.left;
        g_settings.graphY = graphRect.top;
        g_settings.graphWidth = graphRect.right - graphRect.left;
        g_settings.graphHeight = graphRect.bottom - graphRect.top;
    }

    saveSettings(settingsPath, g_settings);
    logMessage("info", "APM Tracker exiting");

    if (graphHwnd)
        DestroyWindow(graphHwnd);

    UnhookWindowsHookEx(eHook);
    UnhookWindowsHookEx(mHook);

    CoUninitialize();
    return EXIT_SUCCESS;
}

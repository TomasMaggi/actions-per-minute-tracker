#include "counter.h"

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

HHOOK eHook = NULL;
HHOOK mHook = NULL;

std::atomic<bool> keepRunning(true);

void tick() {
    while (keepRunning) {
        for (int i = 0; i < 10 && keepRunning; i++)
            Sleep(100);
        if (!keepRunning)
            break;
        incrementSecond();
    }
}

LRESULT mouseProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(eHook, nCode, wparam, lparam);

    if (wparam == WM_LBUTTONDOWN ||
        wparam == WM_RBUTTONDOWN ||
        wparam == WM_XBUTTONDOWN ||
        wparam == WM_MBUTTONDOWN)
        addAction();

    return CallNextHookEx(eHook, nCode, wparam, lparam);
}


static bool shiftDown = false;
static bool toggleHeld = false;

LRESULT keyboardProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(mHook, nCode, wparam, lparam);

    KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lparam);

    if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
    {
        if (kb->vkCode == VK_SHIFT || kb->vkCode == VK_LSHIFT || kb->vkCode == VK_RSHIFT)
        {
            shiftDown = true;
        }
        else if (kb->vkCode == VK_BACK && shiftDown)
        {
            if (!toggleHeld)
            {
                toggleHeld = true;
                toggleSession();
            }
            return 1;
        }
        else
        {
            addAction();
        }
    }
    else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP)
    {
        if (kb->vkCode == VK_SHIFT || kb->vkCode == VK_LSHIFT || kb->vkCode == VK_RSHIFT)
            shiftDown = false;

        if (kb->vkCode == VK_BACK && toggleHeld)
        {
            toggleHeld = false;
            return 1;
        }
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

        std::string text = " : APM ";
        text = std::to_string(currentAPM()) + text;

        std::wstring widestr = std::wstring(text.begin(), text.end());
        const wchar_t* widecstr = widestr.c_str();

        DrawText(
            hdc,
            widecstr,
            -1,
            &rect,
            DT_RIGHT|DT_NOCLIP|DT_SINGLELINE|DT_VCENTER
        );

        EndPaint(hwnd, &paintStruct);
        break;

    }
    case WM_TIMER:
        RECT rect;
        GetClientRect(hwnd, &rect);
        InvalidateRect(hwnd, &rect, TRUE);
        break;
    case WM_CREATE:
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

struct MonitorSearch {
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

static void drawGraph(HDC hdc, int width, int height)
{
    APMStats stats = getAPMStats();

    RECT full = {0, 0, width, height};
    HBRUSH background = CreateSolidBrush(RGB(20, 20, 28));
    FillRect(hdc, &full, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);

    const int padding = 8;
    const int headerHeight = 28;
    const int leftMargin = 44;
    const int bottomMargin = 16;

    std::string state = stats.active ? "REC" : "PAUSED";
    SetTextColor(hdc, stats.active ? RGB(80, 220, 120) : RGB(230, 180, 80));
    drawText(hdc, padding, 6, state);

    std::wstring wideState(state.begin(), state.end());
    SIZE stateSize = {0, 0};
    GetTextExtentPoint32(hdc, wideState.c_str(), (int)wideState.size(), &stateSize);

    std::string header =
        "   Time " + formatDuration(stats.elapsedSeconds) +
        "   APM " + std::to_string(stats.current) +
        "   Avg " + std::to_string(stats.average) +
        "   5m " + std::to_string(stats.average5Min) +
        "   Peak " + std::to_string(stats.peak) +
        "   Actions " + formatNumber(stats.totalActions);

    SetTextColor(hdc, RGB(210, 210, 230));
    drawText(hdc, padding + stateSize.cx, 6, header);

    const int left = leftMargin;
    const int right = width - padding;
    const int top = headerHeight;
    const int bottom = height - bottomMargin;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    if (plotWidth <= 1 || plotHeight <= 1)
        return;

    int maxValue = 100;
    for (int value : stats.history)
        if (value > maxValue)
            maxValue = value;

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

    const std::vector<int> &data = stats.history;
    int n = (int)data.size();
    if (n >= 2)
    {
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

        HPEN linePen = CreatePen(PS_SOLID, 2, RGB(80, 220, 120));
        oldPen = (HPEN)SelectObject(hdc, linePen);
        Polyline(hdc, points.data(), (int)points.size());
        SelectObject(hdc, oldPen);
        DeleteObject(linePen);
    }
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
        DestroyWindow(hwnd);
        break;
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
    eHook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)keyboardProc, GetModuleHandle(NULL), 0);
    mHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)mouseProc, GetModuleHandle(NULL), 0);

    HINSTANCE instance = GetModuleHandle(0);
    HCURSOR cursor = LoadCursor(0,IDC_ARROW);

    WNDCLASSEX wndclass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW, // style
        wndProc, // window proc
        0, // extra bytes following window class
        0, // extra bytes following window instance
        instance, // hInstance
        LoadIcon(0,IDI_APPLICATION), // hIcon
        cursor, // hCursor
        HBRUSH(COLOR_WINDOW + 1), // hbrBackground
        0, // MenuName
        TEXT("actions-per-minute-class"), // ClassName
        LoadIcon(0,IDI_APPLICATION) // small icon
    };

    bool isClassRegistered = RegisterClassEx(&wndclass);
    if (!isClassRegistered) {
        MessageBox(NULL, TEXT("Could not register the overlay window class."), TEXT("APM Tracker"), MB_ICONERROR);
        return EXIT_FAILURE;
    }

    int height = 25;
    int width = 70;
    int extraStyles = WS_EX_COMPOSITED | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT;
	int styles = WS_VISIBLE | WS_POPUP;
    HWND hwnd = CreateWindowEx(
        extraStyles,
        TEXT("actions-per-minute-class"),
        TEXT("actions-per-minute"),
        styles,
        GetSystemMetrics(SM_CXSCREEN)-width, // x
        height*3, // y
        width, // width
        height, // height,
        0, // parent
        0, // menu
        instance,
        NULL
    );

    WNDCLASSEX graphClass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW, // style
        graphWndProc, // window proc
        0, // extra bytes following window class
        0, // extra bytes following window instance
        instance, // hInstance
        LoadIcon(0,IDI_APPLICATION), // hIcon
        cursor, // hCursor
        HBRUSH(COLOR_WINDOW + 1), // hbrBackground
        0, // MenuName
        TEXT("apm-graph-class"), // ClassName
        LoadIcon(0,IDI_APPLICATION) // small icon
    };

    if (!RegisterClassEx(&graphClass)) {
        MessageBox(NULL, TEXT("Could not register the graph window class."), TEXT("APM Tracker"), MB_ICONERROR);
        return EXIT_FAILURE;
    }

    MonitorSearch monitorSearch;
    EnumDisplayMonitors(NULL, NULL, monitorEnumProc, reinterpret_cast<LPARAM>(&monitorSearch));

    int graphWidth = 760;
    int graphHeight = 270;
    int graphX = 60;
    int graphY = 120;
    if (monitorSearch.foundSecondary) {
        RECT area = monitorSearch.secondary;
        int areaWidth = area.right - area.left;
        if (graphWidth > areaWidth - 40)
            graphWidth = areaWidth - 40;
        graphX = area.left + 20;
        graphY = area.top + 20;
    }

    HWND graphHwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        TEXT("apm-graph-class"),
        TEXT("APM over time"),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        graphX, // x
        graphY, // y
        graphWidth, // width
        graphHeight, // height
        0, // parent
        0, // menu
        instance,
        NULL
    );

    if (!graphHwnd) {
        MessageBox(NULL, TEXT("Could not create the graph window."), TEXT("APM Tracker"), MB_ICONERROR);
        return EXIT_FAILURE;
    }

    std::thread t(tick);

    int timer = 500;
    SetTimer(hwnd, timer, timer, 0);

    MSG msg = { };
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

    if (graphHwnd)
        DestroyWindow(graphHwnd);

    UnhookWindowsHookEx(eHook);
    UnhookWindowsHookEx(mHook);

    return EXIT_SUCCESS;
}

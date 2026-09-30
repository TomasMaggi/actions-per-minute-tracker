#include "counter.h"
#include "eapm.h"
#include "log.h"
#include "rec.h"
#include "session.h"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
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
bool g_replayHeld = false;

std::mutex g_recMutex;
RecStats g_recStats;
std::string g_recSourcePath;
std::string g_lastAnalyzedPath;
std::atomic<bool> g_forceAnalyze(false);
std::atomic<bool> g_finalizeRec(false);
std::atomic<bool> g_recLive(false);

int g_hoverX = -1;
int g_hoverY = -1;

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

// App icon embedded from app.rc (resource id 1). Falls back to the generic
// application icon when the resource is not present.
static HICON appIcon(int size)
{
    HICON icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_SHARED));
    if (!icon)
        icon = LoadIconW(NULL, IDI_APPLICATION);
    return icon;
}

static std::string baseName(const std::string &path)
{
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool fileExists(const std::wstring &path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// User data (settings, sessions, log) lives in %APPDATA%\APM Tracker by default
// so a per-machine install under Program Files stays writable. Dropping a
// "portable" marker (or a settings.xml) next to the exe keeps it portable.
static std::wstring resolveDataDir(const std::wstring &exeDir)
{
    if (fileExists(exeDir + L"\\portable") || fileExists(exeDir + L"\\settings.xml"))
        return exeDir;

    PWSTR appData = nullptr;
    std::wstring dataDir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
    {
        dataDir = appData;
        CoTaskMemFree(appData);
        dataDir += L"\\APM Tracker";
    }
    else
    {
        dataDir = exeDir;
    }

    CreateDirectoryW(dataDir.c_str(), nullptr);
    return dataDir;
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
        {
            std::lock_guard<std::mutex> lock(g_recMutex);
            g_recStats = RecStats();
            g_recLive = false;
        }
        logMessage("info", "Session started");
    }
    else
    {
        g_finalizeRec = true;
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

static bool isReplayHotkey(unsigned int vk)
{
    return vk == g_settings.replayHotkey.vk && modifiersMatch(g_settings.replayHotkey.modifiers);
}

static std::wstring resolveRecFolder()
{
    if (!g_settings.recFolder.empty())
        return widen(g_settings.recFolder);

    PWSTR profile = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)))
    {
        base = profile;
        CoTaskMemFree(profile);
    }
    else
    {
        return L"";
    }

    std::wstring root = base + L"\\Games\\Age of Empires 2 DE";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return L"";

    std::wstring bestFolder;
    std::wstring fallbackFolder;
    FILETIME bestTime = {0, 0};

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..")
            continue;

        std::wstring savegame = root + L"\\" + name + L"\\savegame";
        DWORD attr = GetFileAttributesW(savegame.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        if (fallbackFolder.empty())
            fallbackFolder = savegame;

        WIN32_FIND_DATAW rd;
        HANDLE rh = FindFirstFileW((savegame + L"\\*.aoe2record").c_str(), &rd);
        if (rh == INVALID_HANDLE_VALUE)
            continue;

        do
        {
            if (!(rd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                CompareFileTime(&rd.ftLastWriteTime, &bestTime) > 0)
            {
                bestTime = rd.ftLastWriteTime;
                bestFolder = savegame;
            }
        } while (FindNextFileW(rh, &rd));
        FindClose(rh);
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    if (!bestFolder.empty())
        return bestFolder;
    return fallbackFolder;
}

static std::wstring findNewestRec(const std::wstring &dir)
{
    if (dir.empty())
        return L"";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.aoe2record").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return L"";

    std::wstring newest;
    FILETIME newestTime = {0, 0};
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0)
            {
                newestTime = fd.ftLastWriteTime;
                newest = dir + L"\\" + fd.cFileName;
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return newest;
}

static bool recFileStamp(const std::wstring &path, long long &size, unsigned long long &writeTime)
{
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
        return false;
    size = (static_cast<long long>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
    writeTime = (static_cast<unsigned long long>(attr.ftLastWriteTime.dwHighDateTime) << 32) |
                attr.ftLastWriteTime.dwLowDateTime;
    return true;
}

static bool analyzeRecFile(const std::string &path, bool readSaveVersion, bool saveCsv,
                           RecStats &outStats)
{
    RecParseResult rec;
    if (!parseRecFile(path, rec, readSaveVersion))
        return false;

    RecConfig recConfig;
    recConfig.dedupMs = g_settings.eapmDedupMs;
    recConfig.consecutive = g_settings.eapmConsecutive;
    recConfig.ignoreGame = g_settings.eapmIgnoreGame;
    RecStats stats = computeRecStats(rec, rec.recOwner, recConfig);
    if (!stats.ok)
        return false;

    if (saveCsv)
    {
        std::string csvPath;
        saveRecCsv(g_sessionsDir, stats, csvPath);
    }

    outStats = stats;
    return true;
}

void recWatcher()
{
    std::wstring folder = resolveRecFolder();
    if (folder.empty())
    {
        logMessage("info", "Rec analysis: no replay folder found");
        return;
    }
    logMessage("info", "Rec analysis folder: " + narrow(folder));

    // Only a recording that we have seen grow counts as a live match. A
    // pre-existing finished replay is baselined and ignored, so the tracker
    // waits for the next recording to start instead of reading an old one.
    std::string trackedPath;
    long long trackedSize = -1;
    unsigned long long trackedTime = 0;
    bool liveActive = false;

    while (keepRunning)
    {
        for (int i = 0; i < 10 && keepRunning; i++)
            Sleep(100);
        if (!keepRunning)
            break;

        if (!g_settings.recAnalysis)
            continue;

        bool force = g_forceAnalyze.exchange(false);
        std::wstring newest = findNewestRec(folder);
        if (newest.empty())
        {
            trackedPath.clear();
            trackedSize = -1;
            trackedTime = 0;
            liveActive = false;
            continue;
        }

        std::string path = narrow(newest);
        long long size = -1;
        unsigned long long writeTime = 0;
        if (!recFileStamp(newest, size, writeTime))
            continue;

        if (path != trackedPath)
        {
            trackedPath = path;
            trackedSize = size;
            trackedTime = writeTime;
            liveActive = false;
            continue;
        }

        bool grew = size > trackedSize || writeTime > trackedTime;
        trackedSize = size;
        trackedTime = writeTime;

        if (grew)
        {
            RecStats stats;
            if (analyzeRecFile(path, false, false, stats))
            {
                std::lock_guard<std::mutex> lock(g_recMutex);
                g_recStats = stats;
                g_recSourcePath = path;
                g_recLive = true;
            }
            liveActive = true;
        }

        // Finalize on demand (session stop) rather than on a growth pause, so a
        // paused game is not mistaken for a finished one.
        if (g_finalizeRec.exchange(false) && liveActive)
        {
            RecStats stats;
            if (analyzeRecFile(path, true, true, stats))
            {
                {
                    std::lock_guard<std::mutex> lock(g_recMutex);
                    g_recStats = stats;
                    g_recSourcePath = path;
                    g_lastAnalyzedPath = path;
                    g_recLive = false;
                }
                logMessage("info", "Rec analyzed: " + path +
                                       " avgAPM=" + std::to_string(stats.avgApm) +
                                       " avgEAPM=" + std::to_string(stats.avgEapm));
            }
            liveActive = false;
        }

        // Manual re-analyze (replay hotkey) still works on the newest replay.
        if (force)
        {
            RecStats stats;
            if (analyzeRecFile(path, true, true, stats))
            {
                {
                    std::lock_guard<std::mutex> lock(g_recMutex);
                    g_recStats = stats;
                    g_recSourcePath = path;
                    g_lastAnalyzedPath = path;
                    g_recLive = false;
                }
                logMessage("info", "Rec re-analyzed: " + path +
                                       " avgAPM=" + std::to_string(stats.avgApm) +
                                       " avgEAPM=" + std::to_string(stats.avgEapm));
            }
        }
    }
}

static bool isModifierVk(unsigned int vk)
{
    switch (vk)
    {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

LRESULT mouseProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(mHook, nCode, wparam, lparam);

    if (wparam == WM_LBUTTONDOWN || wparam == WM_RBUTTONDOWN || wparam == WM_XBUTTONDOWN ||
        wparam == WM_MBUTTONDOWN)
    {
        if (g_counter)
        {
            MSLLHOOKSTRUCT *ms = reinterpret_cast<MSLLHOOKSTRUCT *>(lparam);
            g_counter->addMouse(static_cast<unsigned int>(wparam), ms->pt.x, ms->pt.y);
        }
    }

    return CallNextHookEx(mHook, nCode, wparam, lparam);
}

LRESULT keyboardProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < 0)
        return CallNextHookEx(eHook, nCode, wparam, lparam);

    KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lparam);
    unsigned int vk = kb->vkCode;

    if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
    {
        bool hotkey = isHotkey(vk);
        bool replay = isReplayHotkey(vk);
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

        if (replay)
        {
            if (!g_replayHeld)
            {
                g_replayHeld = true;
                g_forceAnalyze = true;
            }
            return 1;
        }

        if (g_counter)
            g_counter->addKey(vk, isModifierVk(vk));
    }
    else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP)
    {
        updateModifier(vk, false);

        if (vk == g_settings.hotkey.vk && g_hotkeyHeld)
        {
            g_hotkeyHeld = false;
            return 1;
        }

        if (vk == g_settings.replayHotkey.vk && g_replayHeld)
        {
            g_replayHeld = false;
            return 1;
        }

        if (g_counter)
            g_counter->addKeyUp(vk);
    }

    return CallNextHookEx(mHook, nCode, wparam, lparam);
}

static std::wstring overlayText()
{
    int value = 0;
    if (g_counter)
        value = g_settings.overlayEapm ? g_counter->currentEapm() : g_counter->currentApm();

    std::string text = std::to_string(value);
    text += g_settings.overlayEapm ? " : eAPM " : " : APM ";
    return std::wstring(text.begin(), text.end());
}

// Fit the overlay to its text and keep it inside the monitor work area. The
// right edge stays put so the window grows leftward instead of off-screen.
static void updateOverlayLayout(HWND hwnd)
{
    std::wstring wide = overlayText();

    HDC hdc = GetDC(hwnd);
    SIZE textSize = {0, 0};
    GetTextExtentPoint32(hdc, wide.c_str(), (int)wide.size(), &textSize);
    ReleaseDC(hwnd, hdc);

    int width = textSize.cx + 12;
    int height = textSize.cy + 6;

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);

    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfo(monitor, &info))
        work = info.rcWork;

    int left = windowRect.right - width;
    int top = windowRect.top;
    if (left + width > work.right)
        left = work.right - width;
    if (left < work.left)
        left = work.left;
    if (top + height > work.bottom)
        top = work.bottom - height;
    if (top < work.top)
        top = work.top;

    int currentWidth = windowRect.right - windowRect.left;
    int currentHeight = windowRect.bottom - windowRect.top;
    if (left != windowRect.left || top != windowRect.top || width != currentWidth ||
        height != currentHeight)
    {
        SetWindowPos(hwnd, NULL, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
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
        rect.left += 6;
        rect.right -= 6;

        std::wstring wide = overlayText();
        DrawText(hdc, wide.c_str(), -1, &rect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        EndPaint(hwnd, &paintStruct);
        break;
    }
    case WM_TIMER:
    {
        updateOverlayLayout(hwnd);
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
    int headerHeight = 44;
    const int leftMargin = 44;
    const int bottomMargin = 16;

    bool haveRec = false;
    int recPlayerId = 0;
    int recAvgApm = 0;
    int recAvgEapm = 0;
    int recAvg5mEapm = 0;
    int recPeakEapm = 0;
    long long recTotal = 0;
    int recSeconds = 0;
    std::vector<int> recApmTimeline;
    std::vector<int> recEapmTimeline;
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        if (g_recStats.ok)
        {
            haveRec = true;
            recPlayerId = g_recStats.playerId;
            recAvgApm = g_recStats.avgApm;
            recAvgEapm = g_recStats.avgEapm;
            recAvg5mEapm = g_recStats.avg5mEapm;
            recPeakEapm = g_recStats.peakEapm;
            recTotal = g_recStats.totalActions;
            recSeconds = static_cast<int>(g_recStats.durationMs / 1000);
            recApmTimeline = g_recStats.apmTimeline;
            recEapmTimeline = g_recStats.eapmTimeline;
        }
    }
    if (haveRec)
        headerHeight = 64;

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

    if (haveRec)
    {
        std::string line3 = g_recLive ? "LIVE " : "Rec ";
        line3 += "APM " + std::to_string(recAvgApm) + "   eAPM " + std::to_string(recAvgEapm) +
                 "   5m " + std::to_string(recAvg5mEapm) + "   Peak " +
                 std::to_string(recPeakEapm) + "   P" + std::to_string(recPlayerId) + "   " +
                 formatNumber(recTotal) + "   (" + formatDuration(recSeconds) + ")";
        SetTextColor(hdc, g_recLive ? RGB(120, 220, 150) : RGB(150, 150, 190));
        drawText(hdc, padding, 42, line3);
    }

    const int left = leftMargin;
    const int right = width - padding;
    const int top = headerHeight;
    const int bottom = height - bottomMargin;
    const int plotWidth = right - left;
    const int plotHeight = bottom - top;
    if (plotWidth <= 1 || plotHeight <= 1)
        return;

    int maxValue = 100;
    if (haveRec)
    {
        for (int v : recApmTimeline)
            maxValue = std::max(maxValue, v);
        for (int v : recEapmTimeline)
            maxValue = std::max(maxValue, v);
    }
    else
    {
        for (const SecondSample &sample : stats.history)
        {
            maxValue = std::max(maxValue, sample.raw);
            if (stats.eapmEnabled)
                maxValue = std::max(maxValue, sample.eapm);
        }
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

    int plotSeconds = haveRec ? (int)recApmTimeline.size() : stats.elapsedSeconds;
    if (plotSeconds <= 0)
        plotSeconds = 1;

    // X-axis time ticks every 5 minutes.
    const int tickInterval = 300;
    SetTextColor(hdc, RGB(140, 140, 170));
    for (int t = 0; t < plotSeconds; t += tickInterval)
    {
        int x = left + (int)(((long long)t * plotWidth) / plotSeconds);
        HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 110));
        HPEN oldTick = (HPEN)SelectObject(hdc, tickPen);
        MoveToEx(hdc, x, bottom, NULL);
        LineTo(hdc, x, bottom - 4);
        SelectObject(hdc, oldTick);
        DeleteObject(tickPen);

        std::string label = formatDuration(t);
        int labelWidth = textWidth(hdc, label);
        drawText(hdc, x - labelWidth / 2, bottom + 1, label);
    }

    std::string endLabel = formatDuration(plotSeconds);
    std::wstring wideEnd(endLabel.begin(), endLabel.end());
    SIZE endSize = {0, 0};
    GetTextExtentPoint32(hdc, wideEnd.c_str(), (int)wideEnd.size(), &endSize);
    TextOut(hdc, right - endSize.cx, bottom + 1, wideEnd.c_str(), (int)wideEnd.size());

    if (haveRec)
    {
        drawLine(hdc, recEapmTimeline, left, bottom, plotWidth, plotHeight, maxValue,
                 RGB(90, 200, 240));
        SetTextColor(hdc, RGB(90, 200, 240));
        drawText(hdc, right - textWidth(hdc, "eapm"), top + 2, "eapm");

        drawLine(hdc, recApmTimeline, left, bottom, plotWidth, plotHeight, maxValue,
                 RGB(80, 220, 120));
        SetTextColor(hdc, RGB(80, 220, 120));
        drawText(hdc, right - textWidth(hdc, "raw"), top + 16, "raw");
    }
    else
    {
        if (stats.eapmEnabled)
        {
            drawLine(hdc, metricValues(stats.history, true), left, bottom, plotWidth, plotHeight,
                     maxValue, RGB(90, 200, 240));
            SetTextColor(hdc, RGB(90, 200, 240));
            drawText(hdc, right - textWidth(hdc, "eapm"), top + 2, "eapm");
        }

        drawLine(hdc, metricValues(stats.history, false), left, bottom, plotWidth, plotHeight,
                 maxValue, RGB(80, 220, 120));
        SetTextColor(hdc, RGB(80, 220, 120));
        drawText(hdc, right - textWidth(hdc, "raw"), top + 2 + (stats.eapmEnabled ? 14 : 0), "raw");
    }

    // Hover crosshair + value readout at the pointed time.
    if (g_hoverX >= left && g_hoverX <= right && g_hoverY >= top && g_hoverY <= bottom)
    {
        int t = (int)(((long long)(g_hoverX - left) * plotSeconds) / plotWidth);
        if (t < 0)
            t = 0;
        if (t > plotSeconds - 1)
            t = plotSeconds - 1;

        int apm = 0;
        int eapm = 0;
        bool havePoint = false;
        if (haveRec)
        {
            if (t >= 0 && t < (int)recApmTimeline.size())
            {
                apm = recApmTimeline[t];
                eapm = recEapmTimeline[t];
                havePoint = true;
            }
        }
        else if (t >= 0 && t < (int)stats.history.size())
        {
            apm = stats.history[t].raw;
            eapm = stats.history[t].eapm;
            havePoint = true;
        }

        HPEN hoverPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
        HPEN oldHover = (HPEN)SelectObject(hdc, hoverPen);
        MoveToEx(hdc, g_hoverX, top, NULL);
        LineTo(hdc, g_hoverX, bottom);
        SelectObject(hdc, oldHover);
        DeleteObject(hoverPen);

        if (havePoint)
        {
            int dotY = bottom - (int)(((long long)eapm * plotHeight) / maxValue);
            HBRUSH dot = CreateSolidBrush(RGB(255, 255, 255));
            RECT dotRect = {g_hoverX - 3, dotY - 3, g_hoverX + 3, dotY + 3};
            FillRect(hdc, &dotRect, dot);
            DeleteObject(dot);

            std::string hoverLabel = formatDuration(t) + "  APM " + std::to_string(apm) +
                                     "  eAPM " + std::to_string(eapm);
            int labelWidth = textWidth(hdc, hoverLabel);
            int labelX = g_hoverX + 8;
            if (labelX + labelWidth > right)
                labelX = g_hoverX - labelWidth - 8;
            int labelY = top + 4;

            std::wstring wideLabel(hoverLabel.begin(), hoverLabel.end());
            SIZE sz = {0, 0};
            GetTextExtentPoint32(hdc, wideLabel.c_str(), (int)wideLabel.size(), &sz);
            HBRUSH bg = CreateSolidBrush(RGB(20, 20, 28));
            RECT bgRect = {labelX - 2, labelY - 1, labelX + sz.cx + 2, labelY + sz.cy + 1};
            FillRect(hdc, &bgRect, bg);
            DeleteObject(bg);

            SetTextColor(hdc, RGB(230, 230, 240));
            drawText(hdc, labelX, labelY, hoverLabel);
        }
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
    case WM_MOUSEMOVE:
    {
        g_hoverX = (int)(short)LOWORD(lParam);
        g_hoverY = (int)(short)HIWORD(lParam);

        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);

        RECT rect;
        GetClientRect(hwnd, &rect);
        InvalidateRect(hwnd, &rect, FALSE);
        break;
    }
    case WM_MOUSELEAVE:
    {
        g_hoverX = -1;
        g_hoverY = -1;
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
    std::wstring dataDir = resolveDataDir(exeDir);
    std::string dataDirA = narrow(dataDir);
    std::string settingsPath = dataDirA + "\\settings.xml";
    std::string logPath = dataDirA + "\\apm-tracker.log";
    g_sessionsDir = dataDirA + "\\sessions";

    initLog(logPath);
    logMessage("info", std::string("APM Tracker v") + APM_VERSION_STRING + " starting");
    logMessage("info", "Data directory: " + dataDirA);

    if (!loadSettings(settingsPath, g_settings))
    {
        logMessage("info", "No settings file found, writing defaults");
        saveSettings(settingsPath, g_settings);
    }

    CounterConfig counterConfig;
    counterConfig.preset = g_settings.preset;
    counterConfig.eapm =
        g_settings.eapm || g_settings.overlayEapm || g_settings.preset == Preset::AoE2;
    counterConfig.eapmDebounceMs = g_settings.liveEapmDebounceMs;
    Counter counter(counterConfig);
    g_counter = &counter;

    CreateDirectoryW(widen(g_sessionsDir).c_str(), nullptr);

    eHook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)keyboardProc, GetModuleHandle(NULL), 0);
    mHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)mouseProc, GetModuleHandle(NULL), 0);

    HINSTANCE instance = GetModuleHandle(0);
    HCURSOR cursor = LoadCursor(0, IDC_ARROW);
    HICON largeIcon = appIcon(GetSystemMetrics(SM_CXICON));
    HICON smallIcon = appIcon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEX wndclass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW,          // style
        wndProc,                          // window proc
        0,                                // extra bytes following window class
        0,                                // extra bytes following window instance
        instance,                         // hInstance
        largeIcon,                        // hIcon
        cursor,                           // hCursor
        HBRUSH(COLOR_WINDOW + 1),         // hbrBackground
        0,                                // MenuName
        TEXT("actions-per-minute-class"), // ClassName
        smallIcon                         // small icon
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

    updateOverlayLayout(hwnd);

    if (!g_settings.overlayVisible)
        ShowWindow(hwnd, SW_HIDE);

    WNDCLASSEX graphClass = {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW,  // style
        graphWndProc,             // window proc
        0,                        // extra bytes following window class
        0,                        // extra bytes following window instance
        instance,                 // hInstance
        largeIcon,                // hIcon
        cursor,                   // hCursor
        HBRUSH(COLOR_WINDOW + 1), // hbrBackground
        0,                        // MenuName
        TEXT("apm-graph-class"),  // ClassName
        smallIcon                 // small icon
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
    std::thread recThread(recWatcher);

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
    if (recThread.joinable())
        recThread.join();

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

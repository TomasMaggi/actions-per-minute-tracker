#pragma once

#include <string>

#include "counter.h"

// Modifier bits match the Win32 RegisterHotKey MOD_* values.
constexpr unsigned int HotkeyModAlt = 0x0001;
constexpr unsigned int HotkeyModControl = 0x0002;
constexpr unsigned int HotkeyModShift = 0x0004;
constexpr unsigned int HotkeyModWin = 0x0008;

struct Hotkey
{
    unsigned int modifiers = HotkeyModShift;
    unsigned int vk = 0x08; // VK_BACK
};

struct Settings
{
    Preset preset = Preset::Generic;
    bool eapm = false;
    Hotkey hotkey;
    bool overlayVisible = true;
    bool overlayEapm = false;
    int overlayX = -1;
    int overlayY = -1;
    int graphX = -1;
    int graphY = -1;
    int graphWidth = 760;
    int graphHeight = 290;
    bool recAnalysis = true;
    std::string recFolder;
    int eapmDedupMs = 2000;
    bool eapmConsecutive = false;
    bool eapmIgnoreGame = true;
    int liveEapmDebounceMs = 300;
    Hotkey replayHotkey = {HotkeyModControl | HotkeyModShift, 0x52}; // Ctrl+Shift+R
};

bool loadSettings(const std::string &path, Settings &settings);
bool saveSettings(const std::string &path, const Settings &settings);
bool parseHotkey(const std::string &text, Hotkey &hotkey);
std::string formatHotkey(const Hotkey &hotkey);

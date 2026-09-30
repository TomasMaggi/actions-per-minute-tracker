#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class Preset
{
    Generic,
    AoE2
};

struct CounterConfig
{
    int windowSeconds = 60;
    int averageWindowSeconds = 300;
    Preset preset = Preset::Generic;
    bool eapm = false;
    int eapmDebounceMs = 300;
};

struct SecondSample
{
    int raw = 0;
    int eapm = 0;
    int keyboard = 0;
    int mouse = 0;
};

struct APMStats
{
    std::vector<SecondSample> history;
    int current = 0;
    int average = 0;
    int average5Min = 0;
    int peak = 0;
    int currentEapm = 0;
    int averageEapm = 0;
    int peakEapm = 0;
    int currentKeyboard = 0;
    int currentMouse = 0;
    long long totalActions = 0;
    long long totalEapmActions = 0;
    int elapsedSeconds = 0;
    bool active = false;
    bool eapmEnabled = false;
};

class Counter
{
  public:
    explicit Counter(const CounterConfig &config);

    void addKey(unsigned int vk, bool modifier = false);
    void addKeyUp(unsigned int vk);
    void addMouse(unsigned int button, int x, int y);
    void tick();
    void toggleSession();
    void reset();
    APMStats snapshot() const;
    bool active() const;
    int currentApm() const;
    int currentEapm() const;

  private:
    int scaled(int rollingCount, int elapsed) const;
    int trailingAverage(const std::vector<int> &samples, int count) const;
    void resetLocked();

    CounterConfig m_config;
    mutable std::mutex m_mutex;

    std::vector<int> m_rawKeyPerSecond;
    std::vector<int> m_rawMousePerSecond;
    std::vector<int> m_eapmKeyPerSecond;
    std::vector<int> m_eapmMousePerSecond;

    int m_rollingRawKey = 0;
    int m_rollingRawMouse = 0;
    int m_rollingEapmKey = 0;
    int m_rollingEapmMouse = 0;

    int m_totalSeconds = 0;
    long long m_totalActions = 0;
    long long m_totalEapmActions = 0;
    bool m_sessionActive = false;

    std::vector<SecondSample> m_history;
    std::unordered_set<unsigned int> m_keysDown;

    std::unordered_map<unsigned int, long long> m_lastKeyEapmMs;
    int m_lastMouseButton = -1;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    long long m_lastMouseEapmMs = -1;
};

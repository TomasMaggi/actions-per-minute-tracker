#include "counter.h"

#include <algorithm>

namespace
{
int positive(int value)
{
    return value < 0 ? 0 : value;
}
} // namespace

Counter::Counter(const CounterConfig &config) : m_config(config)
{
    if (m_config.windowSeconds < 1)
        m_config.windowSeconds = 1;
    if (m_config.averageWindowSeconds < 1)
        m_config.averageWindowSeconds = 1;

    m_rawKeyPerSecond.assign(m_config.windowSeconds, 0);
    m_rawMousePerSecond.assign(m_config.windowSeconds, 0);
    m_eapmKeyPerSecond.assign(m_config.windowSeconds, 0);
}

int Counter::scaled(int rollingCount, int elapsed) const
{
    if (elapsed <= 0)
        return 0;
    if (elapsed < m_config.windowSeconds)
    {
        float factor = static_cast<float>(m_config.windowSeconds) / static_cast<float>(elapsed);
        return static_cast<int>(static_cast<float>(rollingCount) * factor);
    }
    return rollingCount;
}

int Counter::trailingAverage(const std::vector<int> &samples, int count) const
{
    if (samples.empty())
        return 0;

    size_t start = samples.size() > static_cast<size_t>(count) ? samples.size() - count : 0;
    long long sum = 0;
    for (size_t i = start; i < samples.size(); i++)
        sum += samples[i];
    return static_cast<int>(sum / static_cast<long long>(samples.size() - start));
}

void Counter::addKey(unsigned int vk)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive)
        return;

    bool repeat = m_keysDown.count(vk) > 0;
    m_keysDown.insert(vk);

    int index = m_totalSeconds % m_config.windowSeconds;
    m_rawKeyPerSecond[index]++;
    m_rollingRawKey++;
    m_totalActions++;

    if (!(m_config.eapm && repeat))
    {
        m_eapmKeyPerSecond[index]++;
        m_rollingEapmKey++;
        m_totalEapmActions++;
    }
}

void Counter::addKeyUp(unsigned int vk)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_keysDown.erase(vk);
}

void Counter::addMouse(unsigned int)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive)
        return;

    int index = m_totalSeconds % m_config.windowSeconds;
    m_rawMousePerSecond[index]++;
    m_rollingRawMouse++;
    m_totalActions++;
    m_totalEapmActions++;
}

void Counter::tick()
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive)
        return;

    m_totalSeconds++;

    int index = m_totalSeconds % m_config.windowSeconds;
    m_rollingRawKey -= m_rawKeyPerSecond[index];
    m_rollingRawMouse -= m_rawMousePerSecond[index];
    m_rollingEapmKey -= m_eapmKeyPerSecond[index];
    m_rawKeyPerSecond[index] = 0;
    m_rawMousePerSecond[index] = 0;
    m_eapmKeyPerSecond[index] = 0;

    SecondSample sample;
    sample.raw = scaled(m_rollingRawKey + m_rollingRawMouse, m_totalSeconds);
    sample.eapm = scaled(m_rollingEapmKey + m_rollingRawMouse, m_totalSeconds);
    sample.keyboard = scaled(m_rollingRawKey, m_totalSeconds);
    sample.mouse = scaled(m_rollingRawMouse, m_totalSeconds);
    m_history.push_back(sample);
}

void Counter::resetLocked()
{
    std::fill(m_rawKeyPerSecond.begin(), m_rawKeyPerSecond.end(), 0);
    std::fill(m_rawMousePerSecond.begin(), m_rawMousePerSecond.end(), 0);
    std::fill(m_eapmKeyPerSecond.begin(), m_eapmKeyPerSecond.end(), 0);

    m_rollingRawKey = 0;
    m_rollingRawMouse = 0;
    m_rollingEapmKey = 0;

    m_totalSeconds = 0;
    m_totalActions = 0;
    m_totalEapmActions = 0;
    m_history.clear();
    m_keysDown.clear();
}

void Counter::reset()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    resetLocked();
}

void Counter::toggleSession()
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sessionActive)
    {
        m_sessionActive = false;
        return;
    }

    resetLocked();
    m_sessionActive = true;
}

bool Counter::active() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionActive;
}

int Counter::currentApm() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return scaled(m_rollingRawKey + m_rollingRawMouse, m_totalSeconds);
}

int Counter::currentEapm() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return scaled(m_rollingEapmKey + m_rollingRawMouse, m_totalSeconds);
}

APMStats Counter::snapshot() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    APMStats stats;
    stats.history = m_history;
    stats.current = scaled(m_rollingRawKey + m_rollingRawMouse, m_totalSeconds);
    stats.currentEapm = scaled(m_rollingEapmKey + m_rollingRawMouse, m_totalSeconds);
    stats.currentKeyboard = scaled(m_rollingRawKey, m_totalSeconds);
    stats.currentMouse = scaled(m_rollingRawMouse, m_totalSeconds);
    stats.elapsedSeconds = m_totalSeconds;
    stats.totalActions = m_totalActions;
    stats.totalEapmActions = m_totalEapmActions;
    stats.active = m_sessionActive;
    stats.eapmEnabled = m_config.eapm;

    if (m_totalSeconds > 0)
    {
        stats.average = static_cast<int>((m_totalActions * 60) / m_totalSeconds);
        stats.averageEapm = static_cast<int>((m_totalEapmActions * 60) / m_totalSeconds);
    }

    int peakStart = m_config.windowSeconds - 1;
    for (size_t i = static_cast<size_t>(peakStart); i < m_history.size(); i++)
    {
        stats.peak = std::max(stats.peak, m_history[i].raw);
        stats.peakEapm = std::max(stats.peakEapm, m_history[i].eapm);
    }
    stats.peak = positive(stats.peak);
    stats.peakEapm = positive(stats.peakEapm);

    std::vector<int> rawSamples;
    rawSamples.reserve(m_history.size());
    for (const SecondSample &sample : m_history)
        rawSamples.push_back(sample.raw);
    stats.average5Min = trailingAverage(rawSamples, m_config.averageWindowSeconds);

    return stats;
}

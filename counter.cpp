#include <mutex>
#include <vector>

#include "counter.h"

#define windowSize 60

std::mutex mtx;
int actionsPerSecond [windowSize] = {0};
int rollingActionCount = 0;
int totalSeconds = 0;
long long totalActions = 0;
bool sessionActive = false;
std::vector<int> apmHistory;

void addAction()
{
    const std::lock_guard<std::mutex> lock(mtx);

    if (!sessionActive)
        return;

    rollingActionCount++;
    totalActions++;
    int currentSecond = totalSeconds % windowSize;

    actionsPerSecond[currentSecond]++;
}

int currentAPM()
{
    if (totalSeconds == 0)
        return 0;

    if (totalSeconds < windowSize)
        return static_cast<int>(static_cast<float>(rollingActionCount) * (static_cast<float>(windowSize) / static_cast<float>(totalSeconds)));

    return rollingActionCount;
}

void incrementSecond() {
    const std::lock_guard<std::mutex> lock(mtx);

    if (!sessionActive)
        return;

    totalSeconds++;

    int currentSecond = totalSeconds % windowSize;
    rollingActionCount -= actionsPerSecond[currentSecond];
    actionsPerSecond[currentSecond] = 0;

    if (apmHistory.empty())
        apmHistory.reserve(3600);
    apmHistory.push_back(currentAPM());
}

APMStats getAPMStats() {
    const std::lock_guard<std::mutex> lock(mtx);

    APMStats stats;
    stats.history = apmHistory;
    stats.current = currentAPM();
    stats.elapsedSeconds = totalSeconds;
    stats.totalActions = totalActions;
    stats.active = sessionActive;

    stats.average = 0;
    if (totalSeconds > 0)
        stats.average = static_cast<int>((totalActions * 60) / totalSeconds);

    stats.peak = 0;
    for (size_t i = windowSize - 1; i < apmHistory.size(); i++)
        if (apmHistory[i] > stats.peak)
            stats.peak = apmHistory[i];

    stats.average5Min = 0;
    if (!apmHistory.empty()) {
        size_t start = apmHistory.size() > 300 ? apmHistory.size() - 300 : 0;
        long long sum = 0;
        for (size_t i = start; i < apmHistory.size(); i++)
            sum += apmHistory[i];
        stats.average5Min = static_cast<int>(sum / (apmHistory.size() - start));
    }

    return stats;
}

void toggleSession() {
    const std::lock_guard<std::mutex> lock(mtx);

    if (sessionActive) {
        sessionActive = false;
        return;
    }

    for (int i = 0; i < windowSize; i++)
        actionsPerSecond[i] = 0;
    rollingActionCount = 0;
    totalSeconds = 0;
    totalActions = 0;
    apmHistory.clear();
    sessionActive = true;
}

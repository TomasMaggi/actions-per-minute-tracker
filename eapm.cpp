#include "eapm.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace
{

std::string dedupKey(const RecAction &a)
{
    std::string key;
    key.reserve(64);
    key += std::to_string(a.type);
    key += ':';
    key += std::to_string(a.targetId);
    key += ':';
    key += std::to_string(a.unitId);
    key += ':';
    key += std::to_string(a.x);
    key += ':';
    key += std::to_string(a.y);
    key += ':';
    key += std::to_string(a.x2);
    key += ':';
    key += std::to_string(a.y2);
    key += ':';
    key += std::to_string(a.amount);
    key += ':';
    key += std::to_string(a.objectHash);
    return key;
}

} // namespace

RecStats computeRecStats(const RecParseResult &rec, int playerId, const RecConfig &config)
{
    RecStats stats;
    stats.playerId = playerId;
    stats.durationMs = rec.durationMs;

    int dedupMs = config.dedupMs;
    if (dedupMs < 0)
        dedupMs = 0;

    long long durationSeconds = (rec.durationMs + 999) / 1000;
    if (durationSeconds <= 0)
        durationSeconds = 1;

    std::vector<int> actionPerSecond(static_cast<size_t>(durationSeconds), 0);
    std::vector<int> eapmPerSecond(static_cast<size_t>(durationSeconds), 0);

    std::unordered_map<std::string, long long> lastSeen;
    std::string lastKey;
    long long lastTime = -1;

    for (const RecAction &action : rec.actions)
    {
        if (action.playerId != playerId)
            continue;

        long long second = action.timeMs / 1000;
        if (second < 0 || second >= durationSeconds)
            continue;

        stats.totalActions++;
        actionPerSecond[static_cast<size_t>(second)]++;

        bool effective = true;
        if (config.ignoreGame && action.type == 103)
        {
            effective = false;
        }
        else if (dedupMs > 0)
        {
            std::string key = dedupKey(action);
            if (config.consecutive)
            {
                if (key == lastKey && lastTime >= 0 && (action.timeMs - lastTime) < dedupMs)
                    effective = false;
                lastKey = key;
                lastTime = action.timeMs;
            }
            else
            {
                auto it = lastSeen.find(key);
                if (it != lastSeen.end() && (action.timeMs - it->second) < dedupMs)
                    effective = false;
                lastSeen[key] = action.timeMs;
            }
        }

        if (effective)
        {
            stats.totalEapm++;
            eapmPerSecond[static_cast<size_t>(second)]++;
        }
    }

    stats.avgApm = static_cast<int>((stats.totalActions * 60) / durationSeconds);
    stats.avgEapm = static_cast<int>((stats.totalEapm * 60) / durationSeconds);

    // Trailing 60-second window for peak + timeline.
    stats.apmTimeline.assign(static_cast<size_t>(durationSeconds), 0);
    stats.eapmTimeline.assign(static_cast<size_t>(durationSeconds), 0);

    int windowApm = 0;
    int windowEapm = 0;
    for (size_t i = 0; i < static_cast<size_t>(durationSeconds); i++)
    {
        windowApm += actionPerSecond[i];
        windowEapm += eapmPerSecond[i];
        if (i >= 60)
        {
            windowApm -= actionPerSecond[i - 60];
            windowEapm -= eapmPerSecond[i - 60];
        }
        stats.apmTimeline[i] = windowApm;
        stats.eapmTimeline[i] = windowEapm;
        if (windowApm > stats.peakApm)
            stats.peakApm = windowApm;
        if (windowEapm > stats.peakEapm)
            stats.peakEapm = windowEapm;
    }

    // Average over the last 5 minutes of the timeline.
    size_t start = durationSeconds > 300 ? static_cast<size_t>(durationSeconds - 300) : 0;
    long long sumApm = 0;
    long long sumEapm = 0;
    size_t count = static_cast<size_t>(durationSeconds) - start;
    if (count > 0)
    {
        for (size_t i = start; i < static_cast<size_t>(durationSeconds); i++)
        {
            sumApm += stats.apmTimeline[i];
            sumEapm += stats.eapmTimeline[i];
        }
        stats.avg5mApm = static_cast<int>(sumApm / static_cast<long long>(count));
        stats.avg5mEapm = static_cast<int>(sumEapm / static_cast<long long>(count));
    }

    stats.ok = true;
    return stats;
}

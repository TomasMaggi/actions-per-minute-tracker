#pragma once

#include <string>
#include <vector>

#include "rec.h"

struct RecStats
{
    bool ok = false;
    std::string error;
    int playerId = -1;
    long long durationMs = 0;
    long long totalActions = 0;
    long long totalEapm = 0;
    int avgApm = 0;
    int avgEapm = 0;
    int avg5mApm = 0;
    int avg5mEapm = 0;
    int peakApm = 0;
    int peakEapm = 0;
    std::vector<int> apmTimeline;  // per-second trailing-60s APM
    std::vector<int> eapmTimeline; // per-second trailing-60s eAPM
};

struct RecConfig
{
    int dedupMs = 2000;
    bool consecutive = false; // collapse only immediate repeats
    bool ignoreGame = true;   // exclude GAME settings toggles from eAPM
};

// Compute APM / eAPM for a single player from a parsed recording.
// eAPM drops an action when an identical command (same type + target key +
// selected units) repeats within dedupMs for the same player.
RecStats computeRecStats(const RecParseResult &rec, int playerId, const RecConfig &config);

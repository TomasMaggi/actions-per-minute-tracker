#pragma once

#include <string>
#include <vector>

// A single player action extracted from a .aoe2record body.
struct RecAction
{
    int type = 0;
    int playerId = -1;
    long long timeMs = 0;

    // Dedup discriminators (context depends on action type).
    int targetId = 0;
    int unitId = 0;
    int x = 0;
    int y = 0;
    int x2 = 0;
    int y2 = 0;
    int amount = 0;
    unsigned long long objectHash = 0;
};

struct RecParseResult
{
    bool ok = false;
    std::string error;
    int recOwner = -1;
    long long durationMs = 0;
    double saveVersion = 0.0;
    std::vector<RecAction> actions;
};

// Parse a Definitive Edition recorded game (.aoe2record).
// Reads only the header length + meta block + operation stream; the
// compressed header body is not decompressed, so this is resilient to
// header-format drift across game patches.
// When readSaveVersion is false the header decompression (for the save
// version) is skipped, which is much faster for live re-parses.
bool parseRecFile(const std::string &path, RecParseResult &out, bool readSaveVersion = true);

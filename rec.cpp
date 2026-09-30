#include "rec.h"

#include "third_party/puff.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{

struct Cursor
{
    const uint8_t *base;
    size_t size;
    size_t pos = 0;

    Cursor(const uint8_t *b, size_t s) : base(b), size(s)
    {
    }

    bool avail(size_t n) const
    {
        return pos + n <= size;
    }

    uint8_t u8()
    {
        uint8_t v = base[pos];
        pos += 1;
        return v;
    }

    uint16_t u16()
    {
        uint16_t v;
        std::memcpy(&v, base + pos, 2);
        pos += 2;
        return v;
    }

    int16_t i16()
    {
        return static_cast<int16_t>(u16());
    }

    uint32_t u32()
    {
        uint32_t v;
        std::memcpy(&v, base + pos, 4);
        pos += 4;
        return v;
    }

    int32_t i32()
    {
        return static_cast<int32_t>(u32());
    }

    float f32()
    {
        float v;
        std::memcpy(&v, base + pos, 4);
        pos += 4;
        return v;
    }

    void skip(size_t n)
    {
        pos += n;
    }
};

int quantize(float v)
{
    return static_cast<int>(std::lround(v));
}

// Hash the selected-unit list into the dedup key (order preserved).
unsigned long long readObjHash(Cursor &c, int count)
{
    if (count <= 0 || count > 100000)
        return 0;

    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < count; i++)
    {
        if (!c.avail(4))
            break;
        unsigned long long id = c.u32();
        h ^= id + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

// Decode an action payload (raw bytes after the player-id + length prefix).
// Mirrors mgz.fast.actions.parse_action_71094 for DE builds >= 71094.
RecAction decodeAction(int type, int playerId, Cursor &c)
{
    RecAction a;
    a.type = type;
    a.playerId = playerId;

    switch (type)
    {
    case 0: // ORDER: <I2fh  target_id, x, y, selected, 6x, selected*I
        a.targetId = static_cast<int>(c.u32());
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        {
            int sel = c.i16();
            c.skip(6);
            a.objectHash = readObjHash(c, sel);
        }
        break;

    case 1: // STOP: <I selected, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 3: // MOVE: <4x2fh  x, y, selected, 6x, selected*I
        c.skip(4);
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        {
            int sel = c.i16();
            c.skip(6);
            a.objectHash = readObjHash(c, sel);
        }
        break;

    case 10: // AI_ORDER: <II4xIff  a, object_id, c, x, y
        c.skip(4);
        a.targetId = static_cast<int>(c.u32());
        c.skip(4);
        c.skip(4);
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        break;

    case 11: // RESIGN
        break;

    case 18: // STANCE: <II selected, stance_id, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.unitId = static_cast<int>(c.u32());
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 19: // GUARD
    case 20: // FOLLOW: <II selected, target_id, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.targetId = static_cast<int>(c.u32());
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 21: // PATROL
    case 33: // DE_ATTACK_MOVE: <I4xf36xf36x selected, x, y, selected*I
    {
        int sel = static_cast<int>(c.u32());
        c.skip(4);
        a.x = quantize(c.f32());
        c.skip(36);
        a.y = quantize(c.f32());
        c.skip(36);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 23: // FORMATION: <II selected, formation_id, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.unitId = static_cast<int>(c.u32());
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 38: // DE_AUTOSCOUT
    case 43: // RATHA_ABILITY: <I selected, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 41: // DE_TRANSFORM: <II object_id, y
        a.targetId = static_cast<int>(c.u32());
        c.skip(4);
        break;

    case 45: // DE_MULTI_GATHERPOINT: <iff target_id, x, y
        a.targetId = static_cast<int>(c.i32());
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        break;

    case 100: // MAKE: <H6xh building_id, unit_id
        a.targetId = static_cast<int>(c.u16());
        c.skip(6);
        a.unitId = static_cast<int>(c.i16());
        break;

    case 101: // RESEARCH: <Ihh5x object_id, selected, technology_id, 5x
        a.targetId = static_cast<int>(c.u32());
        c.skip(2); // selected (building ids, discarded)
        a.unitId = static_cast<int>(c.i16());
        c.skip(5);
        break;

    case 102: // BUILD: <h2xffI8xhbb selected, x, y, building_id, selected*I
    {
        int sel = c.i16();
        c.skip(2);
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        a.targetId = static_cast<int>(c.u32());
        c.skip(8);
        c.skip(2);
        c.skip(1);
        c.skip(1);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 103: // GAME: <h command_id
        a.unitId = static_cast<int>(c.i16());
        break;

    case 105: // WALL: <IHHHHI selected, x1, y1, x2, y2, building_id, 8x, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.x = static_cast<int>(c.u16());
        a.y = static_cast<int>(c.u16());
        a.x2 = static_cast<int>(c.u16());
        a.y2 = static_cast<int>(c.u16());
        a.targetId = static_cast<int>(c.u32());
        c.skip(8);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 106: // DELETE
    case 114: // GATE
    case 126: // DROP_RELIC: <I object_id
        a.targetId = static_cast<int>(c.u32());
        break;

    case 107: // ATTACK_GROUND: <Iff selected, x, y, 4x, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        c.skip(4);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 110: // REPAIR: <II selected, target_id, 4x, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.targetId = static_cast<int>(c.u32());
        c.skip(4);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 111: // UNGARRISON: <IffiI selected, x, y, target_id, unk, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        a.targetId = static_cast<int>(c.i32());
        c.skip(4);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 115: // FLARE: <4xffb x, y, num
        c.skip(4);
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        break;

    case 117: // SPECIAL: <Iiff4xh2xh2x selected, target_id, x, y, slot_id, order_id, selected*I
    {
        int sel = static_cast<int>(c.u32());
        a.targetId = static_cast<int>(c.i32());
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        c.skip(4);
        c.skip(2);
        c.skip(2);
        c.skip(2);
        c.skip(2);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 119: // QUEUE: <3xIhh object_id, unit_id, amount
        c.skip(3);
        a.targetId = static_cast<int>(c.u32());
        a.unitId = static_cast<int>(c.i16());
        a.amount = static_cast<int>(c.i16());
        break;

    case 120: // GATHER_POINT: <h2xffiix selected, x, y, target_id, target_type, selected*I
    {
        int sel = c.i16();
        c.skip(2);
        a.x = quantize(c.f32());
        a.y = quantize(c.f32());
        a.targetId = static_cast<int>(c.i32());
        c.skip(4); // target_type
        c.skip(1);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 122: // SELL
    case 123: // BUY: <hhI resource_id, amount, object_id
        a.unitId = static_cast<int>(c.i16());
        a.amount = static_cast<int>(c.i16());
        a.targetId = static_cast<int>(c.u32());
        break;

    case 127: // TOWN_BELL: <Ib building_id, mode
        a.targetId = static_cast<int>(c.u32());
        a.unitId = static_cast<int>(c.u8());
        break;

    case 128: // BACK_TO_WORK: <3xI object_id
        c.skip(3);
        a.targetId = static_cast<int>(c.u32());
        break;

    case 129: // DE_QUEUE: <h4xhhh4x selected, building_type, unit_id, amount, selected*I
    {
        int sel = c.i16();
        c.skip(4);
        c.skip(2); // building_type
        a.unitId = static_cast<int>(c.i16());
        a.amount = static_cast<int>(c.i16());
        c.skip(4);
        a.objectHash = readObjHash(c, sel);
        break;
    }

    case 196: // DE_TRIBUTE: <ffff wood, food, gold, stone, 16x, 8x, target_id
        c.skip(16);
        c.skip(16);
        c.skip(8);
        a.targetId = static_cast<int>(c.u8());
        break;

    default:
        break;
    }

    return a;
}

// Decompress the header and read the save version (informational only; the
// body format is stable across patches so a failure here is non-fatal).
double decodeSaveVersion(const uint8_t *comp, size_t compLen)
{
    if (compLen == 0)
        return 0.0;

    unsigned long destLen = 0;
    unsigned long srcLen = static_cast<unsigned long>(compLen);
    if (puff(nullptr, &destLen, comp, &srcLen) != 0 || destLen == 0 || destLen > (1u << 28))
        return 0.0;

    std::vector<uint8_t> decomp(static_cast<size_t>(destLen));
    unsigned long outLen = destLen;
    srcLen = static_cast<unsigned long>(compLen);
    if (puff(decomp.data(), &outLen, comp, &srcLen) != 0)
        return 0.0;

    if (decomp.size() < 16)
        return 0.0;

    float old;
    std::memcpy(&old, decomp.data() + 8, 4);
    if (old == -1.0f)
    {
        uint32_t raw;
        std::memcpy(&raw, decomp.data() + 12, 4);
        if (raw == 37)
            return 37.0;
        return raw / static_cast<double>(1 << 16);
    }
    return static_cast<double>(old);
}

} // namespace

bool parseRecFile(const std::string &path, RecParseResult &out, bool readSaveVersion)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        out.error = "could not open file";
        return false;
    }

    std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (!data.empty())
        in.read(reinterpret_cast<char *>(data.data()), fileSize);
    in.close();

    if (data.size() < 16)
    {
        out.error = "file too small";
        return false;
    }

    uint32_t headerLen;
    std::memcpy(&headerLen, data.data(), 4);
    if (headerLen < 8 || headerLen + 40 > data.size())
    {
        out.error = "invalid header length";
        return false;
    }

    out.saveVersion = readSaveVersion ? decodeSaveVersion(data.data() + 8, headerLen - 8) : 0.0;

    // Meta block sits directly after the compressed header. For DE the
    // log_version is present and the owner/recorder index is the 4th uint32.
    size_t meta = headerLen;
    uint32_t logVersion;
    std::memcpy(&logVersion, data.data() + meta, 4);
    if (logVersion == 500)
    {
        out.error = "not a Definitive Edition recording";
        return false;
    }

    uint32_t recOwner;
    std::memcpy(&recOwner, data.data() + meta + 12, 4);
    out.recOwner = static_cast<int>(recOwner);

    // Operation stream starts 32 bytes into the meta block for DE.
    Cursor c(data.data(), data.size());
    c.pos = meta + 32;

    long long currentTimeMs = 0;
    bool foundEnd = false;

    while (c.avail(4))
    {
        uint32_t opId = c.u32();

        if (opId == 1) // ACTION
        {
            if (!c.avail(5))
                break;
            uint32_t length = c.u32();
            if (length < 1 || !c.avail(length + 4))
                break;
            uint8_t actionId = c.u8();

            // Payload = (length - 1) bytes, then a 4-byte sequence number.
            size_t payloadStart = c.pos;
            size_t payloadLen = length - 1;
            c.pos = payloadStart + payloadLen + 4;

            if (actionId == 255) // POSTGAME action -> end of stream
            {
                foundEnd = true;
                break;
            }

            Cursor ac(c.base + payloadStart, payloadLen);
            if (ac.avail(3))
            {
                int playerId = ac.u8();
                ac.i16(); // inner length, unused
                RecAction a = decodeAction(actionId, playerId, ac);
                a.timeMs = currentTimeMs;
                out.actions.push_back(a);
            }
        }
        else if (opId == 2) // SYNC (time increment / full checksum sync)
        {
            if (!c.avail(8))
                break;
            uint32_t increment = c.u32();
            uint32_t marker = c.u32();
            if (marker != 0)
            {
                c.pos -= 4; // marker is not part of the increment
                currentTimeMs += increment;
            }
            else
            {
                size_t afterMarker = c.pos;
                c.skip(4); // padding
                c.u32();   // checksum
                c.skip(4); // padding
                uint32_t isDe = c.u32();
                if (isDe == 0)
                {
                    c.skip(8);
                    currentTimeMs += increment;
                }
                else
                {
                    c.pos = afterMarker;
                    c.skip(352); // 88 per-player uint32 values
                    if (!c.avail(4))
                        break;
                    currentTimeMs = static_cast<long long>(c.u32());
                }
            }
        }
        else if (opId == 3) // VIEWLOCK
        {
            if (!c.avail(12))
                break;
            c.skip(12);
        }
        else if (opId == 4) // CHAT
        {
            if (!c.avail(8))
                break;
            c.u32(); // padding
            uint32_t length = c.u32();
            if (!c.avail(length))
                break;
            c.skip(length);
        }
        else if (opId == 6) // POSTGAME operation -> end of stream
        {
            foundEnd = true;
            break;
        }
        else
        {
            // SAVE (7), START (5) or unknown: restored/unsupported stream.
            break;
        }
    }

    out.durationMs = currentTimeMs;

    // Restored games may lack a postgame marker; accept any parsed actions so
    // they still yield a partial result instead of failing outright.
    if (out.actions.empty())
    {
        out.error = "no actions found in recording";
        return false;
    }

    out.ok = true;
    return true;
}

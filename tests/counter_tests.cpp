#include "../counter.h"
#include "../eapm.h"
#include "../rec.h"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do                                                                                             \
    {                                                                                              \
        long long a_ = (long long)(actual);                                                        \
        long long e_ = (long long)(expected);                                                      \
        if (a_ != e_)                                                                              \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual,    \
                        a_, e_);                                                                   \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

static CounterConfig config(int windowSeconds, bool eapm)
{
    CounterConfig cfg;
    cfg.windowSeconds = windowSeconds;
    cfg.averageWindowSeconds = windowSeconds;
    cfg.preset = eapm ? Preset::AoE2 : Preset::Generic;
    cfg.eapm = eapm;
    cfg.eapmDebounceMs = 0; // deterministic: only auto-repeat filtering
    return cfg;
}

static void testInactiveIgnoresInput()
{
    Counter counter(config(2, false));

    counter.addKey(65);
    counter.addMouse(0, 0, 0);
    counter.tick();

    APMStats stats = counter.snapshot();
    CHECK_EQ(counter.currentApm(), 0);
    CHECK_EQ(stats.elapsedSeconds, 0);
    CHECK_EQ(stats.totalActions, 0);
    CHECK(!stats.active);
}

static void testScalingBeforeWindowFills()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    counter.addKey(65);
    counter.addKey(66);
    counter.tick();
    CHECK_EQ(counter.currentApm(), 4); // 2 actions * (2 / 1)

    counter.tick();
    CHECK_EQ(counter.currentApm(), 0); // aged out of the 2s window
}

static void testWindowRollover()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    counter.addKey(65);
    counter.addKey(66);
    counter.addKey(67);
    counter.tick();
    CHECK_EQ(counter.currentApm(), 6); // 3 * (2 / 1)

    counter.addKey(68);
    counter.tick();
    CHECK_EQ(counter.currentApm(), 1); // only second 1 remains

    counter.tick();
    CHECK_EQ(counter.currentApm(), 0);
}

static void testPeakSkipsFirstWindow()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    counter.addKey(65);
    counter.addKey(66);
    counter.addKey(67);
    counter.tick(); // elapsed 1, raw 6 (ignored for peak)

    counter.addKey(68);
    counter.addKey(69);
    counter.addKey(70);
    counter.tick(); // elapsed 2, raw 3

    counter.tick(); // elapsed 3, raw 0

    APMStats stats = counter.snapshot();
    CHECK_EQ(stats.peak, 3);
}

static void testAverage()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    for (int i = 0; i < 3; i++)
        counter.addKey(65);
    counter.tick();

    for (int i = 0; i < 3; i++)
        counter.addKey(65);
    counter.tick();
    counter.tick();

    APMStats stats = counter.snapshot();
    CHECK_EQ(stats.totalActions, 6);
    CHECK_EQ(stats.elapsedSeconds, 3);
    CHECK_EQ(stats.average, 120); // 6 actions in 3s -> 120/min
}

static void testKeyboardMouseSplit()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    counter.addKey(65);
    counter.addKey(66);
    counter.addMouse(0, 0, 0);
    counter.tick();

    APMStats stats = counter.snapshot();
    CHECK_EQ(stats.current, 6);
    CHECK_EQ(stats.currentKeyboard, 4);
    CHECK_EQ(stats.currentMouse, 2);
}

static void testEapmIgnoresAutoRepeat()
{
    Counter counter(config(2, true));
    counter.toggleSession();

    counter.addKey(65);
    counter.addKey(65); // auto-repeat, held key
    counter.tick();

    CHECK_EQ(counter.currentApm(), 4);
    CHECK_EQ(counter.currentEapm(), 2);

    counter.addKeyUp(65);
    counter.addKey(65); // fresh press counts
    counter.tick();
    CHECK_EQ(counter.currentEapm(), 1);
}

static void testEapmKeyDebounce()
{
    CounterConfig cfg = config(2, true);
    cfg.eapmDebounceMs = 100000;
    Counter counter(cfg);
    counter.toggleSession();

    counter.addKey(65);
    counter.addKeyUp(65);
    counter.addKey(65); // spam same key (not auto-repeat)
    counter.tick();

    CHECK_EQ(counter.currentApm(), 4);
    CHECK_EQ(counter.currentEapm(), 2); // second press deduped
}

static void testEapmModifierIgnored()
{
    CounterConfig cfg = config(2, true);
    Counter counter(cfg);
    counter.toggleSession();

    counter.addKey(0x11, true); // VK_CONTROL as modifier
    counter.tick();

    CHECK_EQ(counter.currentApm(), 2);
    CHECK_EQ(counter.currentEapm(), 0); // modifier is not an effective action
}

static void testEapmMouseDebounce()
{
    CounterConfig cfg = config(2, true);
    cfg.eapmDebounceMs = 100000;
    Counter counter(cfg);
    counter.toggleSession();

    counter.addMouse(0, 100, 100);
    counter.addMouse(0, 100, 100); // same position, deduped
    counter.addMouse(0, 200, 200); // different position, counts
    counter.tick();

    CHECK_EQ(counter.currentApm(), 6);  // 3 raw clicks * (2/1)
    CHECK_EQ(counter.currentEapm(), 4); // 2 effective clicks * (2/1)
}

static void testPauseAndReset()
{
    Counter counter(config(2, false));
    counter.toggleSession();

    for (int i = 0; i < 5; i++)
        counter.addKey(65);
    counter.tick();

    counter.toggleSession(); // stop
    CHECK(!counter.active());

    counter.addKey(65);
    counter.tick();
    APMStats paused = counter.snapshot();
    CHECK_EQ(paused.elapsedSeconds, 1);
    CHECK_EQ(paused.totalActions, 5);

    counter.toggleSession(); // start fresh
    CHECK(counter.active());
    APMStats fresh = counter.snapshot();
    CHECK_EQ(fresh.elapsedSeconds, 0);
    CHECK_EQ(fresh.totalActions, 0);
    CHECK_EQ((int)fresh.history.size(), 0);
}

static RecAction action(int type, int player, long long ms, int target, int unit, int x, int y)
{
    RecAction a;
    a.type = type;
    a.playerId = player;
    a.timeMs = ms;
    a.targetId = target;
    a.unitId = unit;
    a.x = x;
    a.y = y;
    return a;
}

static void testRecStatsDedup()
{
    RecParseResult rec;
    rec.recOwner = 1;
    rec.durationMs = 30000;

    rec.actions.push_back(action(0, 1, 0, 100, 0, 10, 10));
    rec.actions.push_back(action(0, 1, 0, 100, 0, 10, 10)); // duplicate
    rec.actions.push_back(action(3, 1, 0, 0, 0, 20, 20));   // distinct

    RecStats s = computeRecStats(rec, 1, RecConfig{2000, false, false});
    CHECK_EQ(s.totalActions, 3);
    CHECK_EQ(s.totalEapm, 2);
    CHECK_EQ(s.avgApm, 6);  // 3 / 30s * 60
    CHECK_EQ(s.avgEapm, 4); // 2 / 30s * 60
}

static void testRecStatsDedupWindowExpiry()
{
    RecParseResult rec;
    rec.recOwner = 1;
    rec.durationMs = 10000;

    rec.actions.push_back(action(0, 1, 0, 100, 0, 5, 5));
    rec.actions.push_back(action(0, 1, 3000, 100, 0, 5, 5)); // 3s later, same key

    RecStats s = computeRecStats(rec, 1, RecConfig{2000, false, false});
    CHECK_EQ(s.totalEapm, 2); // window expired, both count
}

static void testRecStatsIgnoresOtherPlayers()
{
    RecParseResult rec;
    rec.recOwner = 1;
    rec.durationMs = 10000;

    rec.actions.push_back(action(0, 1, 0, 1, 0, 1, 1));
    rec.actions.push_back(action(0, 2, 0, 1, 0, 1, 1)); // other player

    RecStats s = computeRecStats(rec, 1, RecConfig{2000, false, false});
    CHECK_EQ(s.totalActions, 1);
    CHECK_EQ(s.totalEapm, 1);
}

static void testRecStatsIgnoreGame()
{
    RecParseResult rec;
    rec.recOwner = 1;
    rec.durationMs = 10000;

    rec.actions.push_back(action(103, 1, 0, 0, 0, 0, 0)); // GAME settings toggle
    rec.actions.push_back(action(0, 1, 0, 1, 0, 1, 1));   // ORDER

    RecConfig cfg{0, false, true}; // ignoreGame = true
    RecStats s = computeRecStats(rec, 1, cfg);
    CHECK_EQ(s.totalActions, 2);
    CHECK_EQ(s.totalEapm, 1); // GAME excluded from eAPM
}

static void testRecStatsConsecutive()
{
    RecParseResult rec;
    rec.recOwner = 1;
    rec.durationMs = 10000;

    rec.actions.push_back(action(0, 1, 0, 100, 0, 10, 10)); // A
    rec.actions.push_back(action(0, 1, 0, 100, 0, 10, 10)); // A (immediate repeat)
    rec.actions.push_back(action(3, 1, 0, 0, 0, 20, 20));   // B
    rec.actions.push_back(action(0, 1, 0, 100, 0, 10, 10)); // A again, B intervened

    RecConfig cfg{2000, true, false}; // consecutive mode
    RecStats s = computeRecStats(rec, 1, cfg);
    CHECK_EQ(s.totalActions, 4);
    CHECK_EQ(s.totalEapm, 3); // only the immediate A-A collapsed
}

int main()
{
    testInactiveIgnoresInput();
    testScalingBeforeWindowFills();
    testWindowRollover();
    testPeakSkipsFirstWindow();
    testAverage();
    testKeyboardMouseSplit();
    testEapmIgnoresAutoRepeat();
    testEapmKeyDebounce();
    testEapmModifierIgnored();
    testEapmMouseDebounce();
    testPauseAndReset();
    testRecStatsDedup();
    testRecStatsDedupWindowExpiry();
    testRecStatsIgnoresOtherPlayers();
    testRecStatsIgnoreGame();
    testRecStatsConsecutive();

    if (g_failures == 0)
    {
        std::printf("All counter tests passed.\n");
        return 0;
    }

    std::printf("%d test check(s) failed.\n", g_failures);
    return 1;
}

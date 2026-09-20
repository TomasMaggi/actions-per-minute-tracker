#include "../counter.h"

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
    return cfg;
}

static void testInactiveIgnoresInput()
{
    Counter counter(config(2, false));

    counter.addKey(65);
    counter.addMouse(0);
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
    counter.addMouse(0);
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

int main()
{
    testInactiveIgnoresInput();
    testScalingBeforeWindowFills();
    testWindowRollover();
    testPeakSkipsFirstWindow();
    testAverage();
    testKeyboardMouseSplit();
    testEapmIgnoresAutoRepeat();
    testPauseAndReset();

    if (g_failures == 0)
    {
        std::printf("All counter tests passed.\n");
        return 0;
    }

    std::printf("%d test check(s) failed.\n", g_failures);
    return 1;
}

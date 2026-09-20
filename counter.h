#include <vector>

struct APMStats {
    std::vector<int> history;
    int current;
    int average;
    int average5Min;
    int peak;
    long long totalActions;
    int elapsedSeconds;
    bool active;
};

void addAction();
void incrementSecond();
int currentAPM();
APMStats getAPMStats();
void toggleSession();

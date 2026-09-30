#include "session.h"

#include <ctime>
#include <fstream>

namespace
{
std::string timestampNow()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}
} // namespace

bool saveSessionCsv(const std::string &directory, const APMStats &stats, std::string &outPath)
{
    std::string stamp = timestampNow();
    std::string sessionPath = directory + "\\" + stamp + ".csv";

    std::ofstream out(sessionPath, std::ios::trunc);
    if (!out)
        return false;

    out << "# duration_seconds=" << stats.elapsedSeconds << "\n";
    out << "# avg_apm=" << stats.average << "\n";
    out << "# peak_apm=" << stats.peak << "\n";
    out << "# avg_eapm=" << stats.averageEapm << "\n";
    out << "# peak_eapm=" << stats.peakEapm << "\n";
    out << "# total_actions=" << stats.totalActions << "\n";
    out << "# eapm_enabled=" << (stats.eapmEnabled ? 1 : 0) << "\n";
    out << "second,raw,eapm,keyboard,mouse\n";

    for (size_t i = 0; i < stats.history.size(); i++)
    {
        const SecondSample &sample = stats.history[i];
        out << (i + 1) << "," << sample.raw << "," << sample.eapm << "," << sample.keyboard << ","
            << sample.mouse << "\n";
    }
    out.close();
    outPath = sessionPath;

    std::string indexPath = directory + "\\index.csv";
    bool exists = false;
    {
        std::ifstream in(indexPath);
        exists = in.good();
    }

    std::ofstream index(indexPath, std::ios::app);
    if (!index)
        return true;

    if (!exists)
        index << "timestamp,duration_seconds,avg_apm,peak_apm,avg_eapm,peak_eapm,total_actions\n";
    index << stamp << "," << stats.elapsedSeconds << "," << stats.average << "," << stats.peak
          << "," << stats.averageEapm << "," << stats.peakEapm << "," << stats.totalActions << "\n";

    return true;
}

bool saveRecCsv(const std::string &directory, const RecStats &stats, std::string &outPath)
{
    std::string stamp = timestampNow();
    std::string sessionPath = directory + "\\rec-" + stamp + ".csv";

    std::ofstream out(sessionPath, std::ios::trunc);
    if (!out)
        return false;

    out << "# player_id=" << stats.playerId << "\n";
    out << "# duration_seconds=" << (stats.durationMs / 1000) << "\n";
    out << "# avg_apm=" << stats.avgApm << "\n";
    out << "# avg_eapm=" << stats.avgEapm << "\n";
    out << "# avg5m_apm=" << stats.avg5mApm << "\n";
    out << "# avg5m_eapm=" << stats.avg5mEapm << "\n";
    out << "# peak_apm=" << stats.peakApm << "\n";
    out << "# peak_eapm=" << stats.peakEapm << "\n";
    out << "# total_actions=" << stats.totalActions << "\n";
    out << "# total_eapm=" << stats.totalEapm << "\n";
    out << "second,apm,eapm\n";

    size_t count = stats.apmTimeline.size() > stats.eapmTimeline.size() ? stats.apmTimeline.size()
                                                                        : stats.eapmTimeline.size();
    for (size_t i = 0; i < count; i++)
    {
        int apm = i < stats.apmTimeline.size() ? stats.apmTimeline[i] : 0;
        int eapm = i < stats.eapmTimeline.size() ? stats.eapmTimeline[i] : 0;
        out << (i + 1) << "," << apm << "," << eapm << "\n";
    }
    out.close();
    outPath = sessionPath;
    return true;
}

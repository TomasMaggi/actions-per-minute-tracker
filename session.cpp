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

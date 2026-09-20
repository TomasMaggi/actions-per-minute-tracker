#include "log.h"

#include <ctime>
#include <fstream>
#include <mutex>

namespace
{
std::mutex g_logMutex;
std::string g_logPath;
bool g_initialized = false;

std::string timestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}
} // namespace

void initLog(const std::string &path)
{
    const std::lock_guard<std::mutex> lock(g_logMutex);
    g_logPath = path;
    g_initialized = true;
}

void logMessage(const std::string &level, const std::string &message)
{
    const std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_initialized)
        return;

    std::ofstream out(g_logPath, std::ios::app);
    if (!out)
        return;

    out << timestamp() << " [" << level << "] " << message << "\n";
}

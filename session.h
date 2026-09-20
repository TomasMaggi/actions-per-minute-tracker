#pragma once

#include <string>

#include "counter.h"

bool saveSessionCsv(const std::string &directory, const APMStats &stats, std::string &outPath);

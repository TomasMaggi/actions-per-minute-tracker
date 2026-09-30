#pragma once

#include <string>

#include "counter.h"
#include "eapm.h"

bool saveSessionCsv(const std::string &directory, const APMStats &stats, std::string &outPath);
bool saveRecCsv(const std::string &directory, const RecStats &stats, std::string &outPath);

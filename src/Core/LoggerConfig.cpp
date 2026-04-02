#include "LoggerConfig.hpp"
#include "Core/Paths.hpp"
#include "Utils/FileSystem.hpp"
#include <ss_yaml.hpp>

LoggerConfig g_LoggerConfig;

void LoggerConfig::Load()
{
    const std::string fileName = VFPC_LOGGER_CFG_PATH;

    if (!FileExist(fileName))
        return;

    ss_yaml::Yaml doc;
    doc.parse(fileName.c_str());

    enabled            = doc.root()["enabled"].boolean();
    windowMs           = (uint32_t)doc.root()["performance"]["windowMs"].integer();
    updateIntervalMs   = (uint32_t)doc.root()["performance"]["updateIntervalMs"].integer();
    hitchThresholdMs   = (float)doc.root()["performance"]["hitchThresholdMs"].dbl();

    // Keep sane defaults if values are missing/zero
    if (windowMs == 0)         windowMs = 3000;
    if (updateIntervalMs == 0) updateIntervalMs = 1000;
    if (hitchThresholdMs <= 0) hitchThresholdMs = 50.0f;

    doc.parseEnd();
}

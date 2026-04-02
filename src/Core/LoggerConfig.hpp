#pragma once
#include <stdint.h>

struct LoggerConfig
{
    bool enabled = false;

    // Performance metric parameters
    uint32_t windowMs = 3000;            // sample retention window
    uint32_t updateIntervalMs = 1000;    // derived metric refresh rate
    float    hitchThresholdMs = 50.0f;   // hitch detection threshold

    void Load();
};

extern LoggerConfig g_LoggerConfig;

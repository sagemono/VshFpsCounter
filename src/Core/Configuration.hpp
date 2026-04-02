#pragma once
#include <string>
#include <vsh/stdc.h>
#include <ss_yaml.hpp>

class Config
{
public:
    enum DisplayMode : uint8_t
    {
        XMB,
        GAME,
        XMB_GAME
    };

    enum class PostionStyle : uint8_t
    {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    enum class TemperatureType : uint8_t
    {
        BOTH,
        CELSIUS,
        FAHRENHEIT
    };

public:
    Config();

    void Load();
    void LoadFile(const std::string& fileName);
    void ResetSettings();

public:
    uint8_t version{};

    struct
    {
        DisplayMode displayMode = DisplayMode::XMB_GAME;

        struct
        {
            PostionStyle positionStyle = PostionStyle::TOP_LEFT;

            // existing toggles
            bool showFPS = true;
            bool showCpuInfo = true;
            bool showGpuInfo = true;
            bool showRamInfo = false;
            bool showFanSpeed = true;
            bool showFirmware = true;
            bool showAppName = true;
            bool showClockSpeeds = true;
            TemperatureType temperatureType = TemperatureType::BOTH;
            float textSize = 16.0f;
            bool showPlayTime = false;

            // new v4
            bool showFrameTime = false;        // current frame time (ms)
            bool showAvgFPS = false;           // average FPS over window
            bool showFps1Low = false;          // 1% low (p99 frametime -> fps)
            bool showFps01Low = false;         // 0.1% low (p99.9 frametime -> fps)
            bool showFpsMinMax = false;        // min/max fps over window
            bool showFrameTimeMinMax = false;  // min/max frametime over window
            bool showHitches = false;          // hitch count over window
            bool showFrameTimeStdev = false;   // frame time std deviation
            bool showPacing = false;           // frame pacing consistency %
            bool showDroppedFrames = false;    // dropped frame %
        } mode[5];
    } overlay;
};

extern Config g_Config;

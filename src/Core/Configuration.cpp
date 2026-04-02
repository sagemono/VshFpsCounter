#include "Configuration.hpp"
#include "Core/Paths.hpp"
#include "Utils/FileSystem.hpp"

Config g_Config;

Config::Config()
{
    ResetSettings();
    Load();
}

void Config::Load()
{
    // Create base directory on first load (needed for config, sessions, sensor)
    CreateDirectory(std::string(VFPC_BASE_DIR));

    const std::string fileName = VFPC_CONFIG_PATH;

    if (FileExist(fileName))
        LoadFile(fileName);
}

constexpr unsigned int hash_str(const char* s, int off = 0)
{
    return !s[off] ? 5381 : (hash_str(s, off + 1) * 33) ^ s[off];
}

void Config::LoadFile(const std::string& fileName)
{
    ss_yaml::Yaml doc;
    doc.parse(fileName.c_str());

    auto _version = doc.root()["version"].integer();
    auto _displayMode = doc.root()["overlay"]["displayMode"].str();

    version = (uint8_t)_version;

    switch (hash_str(_displayMode.c_str()))
    {
    case hash_str("XMB"):
        overlay.displayMode = DisplayMode::XMB;
        break;
    case hash_str("GAME"):
        overlay.displayMode = DisplayMode::GAME;
        break;
    case hash_str("XMB_GAME"):
        overlay.displayMode = DisplayMode::XMB_GAME;
        break;
    }

    if (_version >= 3)
    {
        auto xmb_position = doc.root()["overlay"]["type"]["xmb"]["position"].str();
        auto xmb_showFPS = doc.root()["overlay"]["type"]["xmb"]["showFPS"].boolean();
        auto xmb_showCpuInfo = doc.root()["overlay"]["type"]["xmb"]["showCpuInfo"].boolean();
        auto xmb_showGpuInfo = doc.root()["overlay"]["type"]["xmb"]["showGpuInfo"].boolean();
        auto xmb_showRamInfo = doc.root()["overlay"]["type"]["xmb"]["showRamInfo"].boolean();
        auto xmb_showFanSpeed = doc.root()["overlay"]["type"]["xmb"]["showFanSpeed"].boolean();
        auto xmb_showFirmware = doc.root()["overlay"]["type"]["xmb"]["showFirmware"].boolean();
        auto xmb_showAppName = doc.root()["overlay"]["type"]["xmb"]["showAppName"].boolean();
        auto xmb_showClockSpeeds = doc.root()["overlay"]["type"]["xmb"]["showClockSpeeds"].boolean();
        std::string xmb_temperatureType = doc.root()["overlay"]["type"]["xmb"]["temperatureType"].str();
        float xmb_textSize = (float)doc.root()["overlay"]["type"]["xmb"]["textSize"].dbl();
        auto xmb_showPlayTime = doc.root()["overlay"]["type"]["xmb"]["showPlayTime"].boolean();

        switch (hash_str(xmb_position.c_str()))
        {
        case hash_str("TOP_LEFT"):
            overlay.mode[DisplayMode::XMB].positionStyle = PostionStyle::TOP_LEFT;
            break;
        case hash_str("TOP_RIGHT"):
            overlay.mode[DisplayMode::XMB].positionStyle = PostionStyle::TOP_RIGHT;
            break;
        case hash_str("BOTTOM_LEFT"):
            overlay.mode[DisplayMode::XMB].positionStyle = PostionStyle::BOTTOM_LEFT;
            break;
        case hash_str("BOTTOM_RIGHT"):
            overlay.mode[DisplayMode::XMB].positionStyle = PostionStyle::BOTTOM_RIGHT;
            break;
        }

        switch (hash_str(xmb_temperatureType.c_str()))
        {
        case hash_str("BOTH"):
            overlay.mode[DisplayMode::XMB].temperatureType = TemperatureType::BOTH;
            break;
        case hash_str("CELSIUS"):
            overlay.mode[DisplayMode::XMB].temperatureType = TemperatureType::CELSIUS;
            break;
        case hash_str("FAHRENHEIT"):
            overlay.mode[DisplayMode::XMB].temperatureType = TemperatureType::FAHRENHEIT;
            break;
        }

        overlay.mode[DisplayMode::XMB].showFPS = xmb_showFPS;
        overlay.mode[DisplayMode::XMB].showCpuInfo = xmb_showCpuInfo;
        overlay.mode[DisplayMode::XMB].showGpuInfo = xmb_showGpuInfo;
        overlay.mode[DisplayMode::XMB].showRamInfo = xmb_showRamInfo;
        overlay.mode[DisplayMode::XMB].showFanSpeed = xmb_showFanSpeed;
        overlay.mode[DisplayMode::XMB].showFirmware = xmb_showFirmware;
        overlay.mode[DisplayMode::XMB].showAppName = xmb_showAppName;
        overlay.mode[DisplayMode::XMB].showClockSpeeds = xmb_showClockSpeeds;
        overlay.mode[DisplayMode::XMB].textSize = xmb_textSize;
        overlay.mode[DisplayMode::XMB].showPlayTime = xmb_showPlayTime;

        auto game_position = doc.root()["overlay"]["type"]["game"]["position"].str();
        auto game_showFPS = doc.root()["overlay"]["type"]["game"]["showFPS"].boolean();
        auto game_showCpuInfo = doc.root()["overlay"]["type"]["game"]["showCpuInfo"].boolean();
        auto game_showGpuInfo = doc.root()["overlay"]["type"]["game"]["showGpuInfo"].boolean();
        auto game_showRamInfo = doc.root()["overlay"]["type"]["game"]["showRamInfo"].boolean();
        auto game_showFanSpeed = doc.root()["overlay"]["type"]["game"]["showFanSpeed"].boolean();
        auto game_showFirmware = doc.root()["overlay"]["type"]["game"]["showFirmware"].boolean();
        auto game_showAppName = doc.root()["overlay"]["type"]["game"]["showAppName"].boolean();
        auto game_showClockSpeeds = doc.root()["overlay"]["type"]["game"]["showClockSpeeds"].boolean();
        std::string game_temperatureType = doc.root()["overlay"]["type"]["game"]["temperatureType"].str();
        float game_textSize = (float)doc.root()["overlay"]["type"]["game"]["textSize"].dbl();
        auto game_showPlayTime = doc.root()["overlay"]["type"]["game"]["showPlayTime"].boolean();

        switch (hash_str(game_position.c_str()))
        {
        case hash_str("TOP_LEFT"):
            overlay.mode[DisplayMode::GAME].positionStyle = PostionStyle::TOP_LEFT;
            break;
        case hash_str("TOP_RIGHT"):
            overlay.mode[DisplayMode::GAME].positionStyle = PostionStyle::TOP_RIGHT;
            break;
        case hash_str("BOTTOM_LEFT"):
            overlay.mode[DisplayMode::GAME].positionStyle = PostionStyle::BOTTOM_LEFT;
            break;
        case hash_str("BOTTOM_RIGHT"):
            overlay.mode[DisplayMode::GAME].positionStyle = PostionStyle::BOTTOM_RIGHT;
            break;
        }

        switch (hash_str(game_temperatureType.c_str()))
        {
        case hash_str("BOTH"):
            overlay.mode[DisplayMode::GAME].temperatureType = TemperatureType::BOTH;
            break;
        case hash_str("CELSIUS"):
            overlay.mode[DisplayMode::GAME].temperatureType = TemperatureType::CELSIUS;
            break;
        case hash_str("FAHRENHEIT"):
            overlay.mode[DisplayMode::GAME].temperatureType = TemperatureType::FAHRENHEIT;
            break;
        }

        overlay.mode[DisplayMode::GAME].showFPS = game_showFPS;
        overlay.mode[DisplayMode::GAME].showCpuInfo = game_showCpuInfo;
        overlay.mode[DisplayMode::GAME].showGpuInfo = game_showGpuInfo;
        overlay.mode[DisplayMode::GAME].showRamInfo = game_showRamInfo;
        overlay.mode[DisplayMode::GAME].showFanSpeed = game_showFanSpeed;
        overlay.mode[DisplayMode::GAME].showFirmware = game_showFirmware;
        overlay.mode[DisplayMode::GAME].showAppName = game_showAppName;
        overlay.mode[DisplayMode::GAME].showClockSpeeds = game_showClockSpeeds;
        overlay.mode[DisplayMode::GAME].textSize = game_textSize;
        overlay.mode[DisplayMode::GAME].showPlayTime = game_showPlayTime;
    }

    if (_version >= 4)
    {
        // xmb
        overlay.mode[DisplayMode::XMB].showFrameTime = doc.root()["overlay"]["type"]["xmb"]["showFrameTime"].boolean();
        overlay.mode[DisplayMode::XMB].showAvgFPS = doc.root()["overlay"]["type"]["xmb"]["showAvgFPS"].boolean();
        overlay.mode[DisplayMode::XMB].showFps1Low = doc.root()["overlay"]["type"]["xmb"]["showFps1Low"].boolean();
        overlay.mode[DisplayMode::XMB].showFps01Low = doc.root()["overlay"]["type"]["xmb"]["showFps01Low"].boolean();
        overlay.mode[DisplayMode::XMB].showFpsMinMax = doc.root()["overlay"]["type"]["xmb"]["showFpsMinMax"].boolean();
        overlay.mode[DisplayMode::XMB].showFrameTimeMinMax = doc.root()["overlay"]["type"]["xmb"]["showFrameTimeMinMax"].boolean();
        overlay.mode[DisplayMode::XMB].showHitches = doc.root()["overlay"]["type"]["xmb"]["showHitches"].boolean();
        overlay.mode[DisplayMode::XMB].showFrameTimeStdev = doc.root()["overlay"]["type"]["xmb"]["showFrameTimeStdev"].boolean();
        overlay.mode[DisplayMode::XMB].showPacing = doc.root()["overlay"]["type"]["xmb"]["showPacing"].boolean();
        overlay.mode[DisplayMode::XMB].showDroppedFrames = doc.root()["overlay"]["type"]["xmb"]["showDroppedFrames"].boolean();

        // game
        overlay.mode[DisplayMode::GAME].showFrameTime = doc.root()["overlay"]["type"]["game"]["showFrameTime"].boolean();
        overlay.mode[DisplayMode::GAME].showAvgFPS = doc.root()["overlay"]["type"]["game"]["showAvgFPS"].boolean();
        overlay.mode[DisplayMode::GAME].showFps1Low = doc.root()["overlay"]["type"]["game"]["showFps1Low"].boolean();
        overlay.mode[DisplayMode::GAME].showFps01Low = doc.root()["overlay"]["type"]["game"]["showFps01Low"].boolean();
        overlay.mode[DisplayMode::GAME].showFpsMinMax = doc.root()["overlay"]["type"]["game"]["showFpsMinMax"].boolean();
        overlay.mode[DisplayMode::GAME].showFrameTimeMinMax = doc.root()["overlay"]["type"]["game"]["showFrameTimeMinMax"].boolean();
        overlay.mode[DisplayMode::GAME].showHitches = doc.root()["overlay"]["type"]["game"]["showHitches"].boolean();
        overlay.mode[DisplayMode::GAME].showFrameTimeStdev = doc.root()["overlay"]["type"]["game"]["showFrameTimeStdev"].boolean();
        overlay.mode[DisplayMode::GAME].showPacing = doc.root()["overlay"]["type"]["game"]["showPacing"].boolean();
        overlay.mode[DisplayMode::GAME].showDroppedFrames = doc.root()["overlay"]["type"]["game"]["showDroppedFrames"].boolean();
    }

    doc.parseEnd();
}

void Config::ResetSettings()
{
    version = 4;

    overlay.displayMode = DisplayMode::XMB_GAME;

    for (int i = 0; i < 5; i++)
    {
        overlay.mode[i].positionStyle = PostionStyle::TOP_LEFT;

        overlay.mode[i].showFPS = true;
        overlay.mode[i].showCpuInfo = true;
        overlay.mode[i].showGpuInfo = true;
        overlay.mode[i].showRamInfo = true;
        overlay.mode[i].showFanSpeed = true;
        overlay.mode[i].showFirmware = false;
        overlay.mode[i].showAppName = true;
        overlay.mode[i].showClockSpeeds = true;
        overlay.mode[i].temperatureType = TemperatureType::CELSIUS;
        overlay.mode[i].textSize = 16.0f;
        overlay.mode[i].showPlayTime = true;

        overlay.mode[i].showFrameTime = true;
        overlay.mode[i].showAvgFPS = true;
        overlay.mode[i].showFps1Low = true;
        overlay.mode[i].showFps01Low = true;
        overlay.mode[i].showFpsMinMax = true;
        overlay.mode[i].showFrameTimeMinMax = true;
        overlay.mode[i].showHitches = true;
        overlay.mode[i].showFrameTimeStdev = true;
        overlay.mode[i].showPacing = true;
        overlay.mode[i].showDroppedFrames = true;
    }
}

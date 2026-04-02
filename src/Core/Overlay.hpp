#pragma once
#include <string>
#include <sys/ppu_thread.h>
#include <vsh/netctl_main.h>
#include <vsh/vshmain.h>
#include <vsh/vshcommon.h>
#include <vsh/pafView.h>
#include <vsh/explore_plugin.h>
#include <vsh/game_plugin.h>

#include "Utils/ConsoleInfo.hpp"
#include "Core/Helpers.hpp"
#include "Core/Rendering.hpp"
#include "Core/Configuration.hpp"
#include "Core/shared_metrics.h"

class SessionLogger;

class Overlay
{
    friend class SessionLogger;

public:
    enum class TempType : uint8_t
    {
        Fahrenheit,
        Celsius,
    };

public:
    Overlay();

    void OnUpdate();
    void OnShutdown();

private:
    void DrawOverlay();
    void UpdatePosition();
    void CalculateFps();

    void UpdatePerformance(float frameTimeSeconds);
    bool AnyPerfEnabledForCurrentMode() const;

    bool ReadSensorMetrics();

public:
    void GetGameName(char outTitleId[16], char outTitleName[64]);

private:
    uint32_t GetGpuClockSpeed();
    uint32_t GetGpuGddr3RamClockSpeed();
    uint32_t GetCpuClockSpeed();
    static void UpdateInfoThread(uint64_t arg);

public:
    sys_ppu_thread_t UpdateInfoThreadId = SYS_PPU_THREAD_ID_INVALID;
    bool m_StateRunning{};
    float m_CPUTemp{};
    float m_GPUTemp{};
    float m_FanSpeed{};
    memUsage_s m_MemoryUsage{};
    uint64_t m_KernelType{};
    float m_FirmwareVersion{};
    uint16_t m_PayloadVersion{};
    bool m_CheckedHenVersion = false;
    uint16_t m_CachedPayloadVersion = 0;
    float m_CachedFirmwareVersion = 0;
    uint64_t m_CachedKernelType = 0;
    std::wstring m_CachedPayloadText; 
    bool m_CachedPayloadTextBuilt = false;
    uint64_t m_TemperatureCycleTime{};
    bool m_CycleTemperatureType{};
    TempType m_TempType{};

    uint32_t m_CpuClock{};
    uint32_t m_GpuClock{};
    uint32_t m_GpuGddr3RamClock{};
private:
    vsh::eCooperationMode m_CooperationMode;

    vsh::vec2 m_Position{};
    Render::Align m_HorizontalAlignment{};
    Render::Align m_VerticalAlignment{};
    vsh::vec2 m_SafeArea{ 31, 18 };
    vsh::vec4 m_ColorText{ 1, 1, 1, 1 };
    static const int refreshDelay = 0;

    float m_FPS = 100.0f;
    float m_FpsLastTime = 0;
    int m_FpsFrames = 0;
    int m_FpsFramesLastReport = 0;
    double m_FpsTimeElapsed = 0;
    double m_FpsTimeReport = 0;
    double m_FpsTimeLastReport = 0;
    float m_FpsREPORT_TIME = 1.0f;

    uint64_t m_ReloadConfigTime{};

    static const int PERF_MAX_SAMPLES = 2048;
    static const int HIST_MAX_MS = 200;
    static const int HIST_BINS = HIST_MAX_MS + 2;

    uint64_t m_PerfTimeMs[PERF_MAX_SAMPLES];
    uint16_t m_PerfFtMs[PERF_MAX_SAMPLES];
    int m_PerfHead = 0;
    int m_PerfCount = 0;

    uint32_t m_Hist[HIST_BINS];

    uint64_t m_LastPerfComputeMs = 0;

    float m_FrameTimeMsCurrent = 0.0f;

    bool m_PerfValid = false;
    uint32_t m_PerfSampleCount = 0;

    float m_FpsAvg = 0.0f;
    float m_FpsMin = 0.0f;
    float m_FpsMax = 0.0f;
    float m_Fps1Low = 0.0f;
    float m_Fps01Low = 0.0f;

    float m_FrameTimeMinMs = 0.0f;
    float m_FrameTimeMaxMs = 0.0f;

    float m_FpsInstant = 0.0f;
    float m_FpsSmooth = 0.0f;

    uint32_t m_HitchCount = 0;

    uint32_t m_SensorBufCount = 0;     // display buffer count (2=double, 3=triple)
    uint32_t m_SensorRsxCoreMhz = 0;   // RSX core clock (MHz) as seen by GCM
    uint32_t m_SensorRsxMemMhz = 0;    // RSX memory clock (MHz) as seen by GCM
    uint32_t m_SensorVramMb = 0;       // RSX VRAM size (MB)
    uint32_t m_SensorMissedVsyncs = 0; // cumulative missed vsync count
    float m_GpuBusyPct = 0.0f;         // RSX command buffer busy %
    float m_DroppedPct = 0.0f;         // % of vsync opportunities missed
    float m_FtStdevMs = 0.0f;          // frame time standard deviation (ms)
    float m_PacingPct = 0.0f;          // % of frames within target tolerance

    bool m_SensorActive = false;       // true when valid data read from LV2
    uint32_t m_SensorFrameCount = 0;   // detect stale data
    uint64_t m_SensorLastReadMs = 0;   // timeout detection


};

extern Overlay g_Overlay;

#include "Overlay.hpp"
#include "Core/shared_metrics.h"
#include "Core/SessionLogger.hpp"
#include "Core/LoggerConfig.hpp"
#include <vsh/stdc.h>

// NOTE:
// Do NOT use std::vector / std::nth_element here.
// The main thread stack is only 0x1000 bytes (4 KB). Some STL implementations
// can blow that stack easily. We use heap buffers + iterative selection instead.

Overlay g_Overlay;

bool Overlay::ReadSensorMetrics()
{
    uint64_t header = PeekLv2(METRICS_SLOT_HEADER);

    // check for syscall failure (CFW disabled)
    if (header == 0xFFFFFFFF80010003ULL)
    {
        m_SensorActive = false;
        return false;
    }

    uint32_t magic = unpack_u32_hi(header);
    if (magic != METRICS_MAGIC)
    {
        m_SensorActive = false;
        return false;
    }

    uint32_t frame_count = unpack_u32_lo(header);

    // dtect stale data (sensor stopped updating)
    if (frame_count == m_SensorFrameCount)
    {
        uint64_t now_ms = GetTimeNow();
        if (m_SensorLastReadMs != 0 && (now_ms - m_SensorLastReadMs) > 2000)
        {
            // no new frames for 2 seconds, sensor is likely dead
            m_SensorActive = false;
            return false;
        }
        // same frame, but within timeout so its still valid, just no new data
        return m_SensorActive;
    }

    m_SensorFrameCount = frame_count;
    m_SensorLastReadMs = GetTimeNow();

    uint64_t slot_fps_ft = PeekLv2(METRICS_SLOT_FPS_FT);
    uint64_t slot_avg_1low = PeekLv2(METRICS_SLOT_AVG_1LOW);
    uint64_t slot_01low_min = PeekLv2(METRICS_SLOT_01LOW_MIN);
    uint64_t slot_max_ftmin = PeekLv2(METRICS_SLOT_MAX_FTMIN);
    uint64_t slot_ftmax_hit = PeekLv2(METRICS_SLOT_FTMAX_HIT);
    uint64_t slot_cnt_flags = PeekLv2(METRICS_SLOT_CNT_FLAGS);

    uint32_t flags = unpack_u32_lo(slot_cnt_flags);
    if (!(flags & METRICS_FLAG_ACTIVE))
    {
        m_SensorActive = false;
        return false;
    }

    // unpack into the same member variables that DrawOverlay reads
    m_FPS = unpack_f32_hi(slot_fps_ft);
    m_FrameTimeMsCurrent = unpack_f32_lo(slot_fps_ft);

    m_FpsAvg = unpack_f32_hi(slot_avg_1low);
    m_Fps1Low = unpack_f32_lo(slot_avg_1low);

    m_Fps01Low = unpack_f32_hi(slot_01low_min);
    m_FpsMin = unpack_f32_lo(slot_01low_min);

    m_FpsMax = unpack_f32_hi(slot_max_ftmin);
    m_FrameTimeMinMs = unpack_f32_lo(slot_max_ftmin);

    // slot 5 high 32 bits is ft_max stored as u32 bit pattern
    {
        union { uint32_t u; float f; } conv;
        conv.u = unpack_u32_hi(slot_ftmax_hit);
        m_FrameTimeMaxMs = conv.f;
    }
    m_HitchCount = unpack_u32_lo(slot_ftmax_hit);

    m_PerfSampleCount = unpack_u32_hi(slot_cnt_flags);
    m_PerfValid = (m_PerfSampleCount >= 3);

    // read hw info slots (debug, RSX clocks, VRAM)
    {
        uint64_t slot_debug = PeekLv2(METRICS_SLOT_DEBUG);
        m_SensorBufCount = unpack_u32_lo(slot_debug);

        uint64_t slot_rsx_clk = PeekLv2(METRICS_SLOT_RSX_CLK);
        m_SensorRsxCoreMhz = unpack_u32_hi(slot_rsx_clk);
        m_SensorRsxMemMhz = unpack_u32_lo(slot_rsx_clk);

        uint64_t slot_rsx_mem = PeekLv2(METRICS_SLOT_RSX_MEM);
        m_SensorVramMb = unpack_u32_hi(slot_rsx_mem);
        m_SensorMissedVsyncs = unpack_u32_lo(slot_rsx_mem);

        uint64_t slot_gpu_busy = PeekLv2(METRICS_SLOT_GPU_BUSY);
        m_GpuBusyPct = unpack_f32_hi(slot_gpu_busy);
        m_DroppedPct = unpack_f32_lo(slot_gpu_busy);

        uint64_t slot_derived = PeekLv2(METRICS_SLOT_DERIVED);
        m_FtStdevMs = unpack_f32_hi(slot_derived);
        m_PacingPct = unpack_f32_lo(slot_derived);
    }

    m_SensorActive = true;
    return true;
}

Overlay::Overlay()
{
    m_ReloadConfigTime = GetTimeNow() + 10000;
    m_PerfHead = 0;
    m_PerfCount = 0;
    m_LastPerfComputeMs = 0;

    vsh::memset(m_PerfTimeMs, 0, sizeof(m_PerfTimeMs));
    vsh::memset(m_PerfFtMs, 0, sizeof(m_PerfFtMs));
    vsh::memset(m_Hist, 0, sizeof(m_Hist));


    // make sure this is valid immediately so it prevents first frame junk usage
    m_CooperationMode = vsh::GetCooperationMode();
    if (m_CooperationMode != vsh::eCooperationMode::XMB)
        m_CooperationMode = vsh::eCooperationMode::Game;

    sys_ppu_thread_create(&UpdateInfoThreadId, UpdateInfoThread, 0, 0xB01, 0x4000, SYS_PPU_THREAD_CREATE_JOINABLE, "Overlay::UpdateInfoThread()");
}

void Overlay::OnUpdate()
{
    // FIX: set coop mode early so UpdatePosition/CalculateFps don't use garbage
    m_CooperationMode = vsh::GetCooperationMode();
    if (m_CooperationMode != vsh::eCooperationMode::XMB)
        m_CooperationMode = vsh::eCooperationMode::Game;

    UpdatePosition();
    CalculateFps();
    DrawOverlay();
}

void Overlay::OnShutdown()
{
    g_SessionLogger.EndSession();

    if (UpdateInfoThreadId != SYS_PPU_THREAD_ID_INVALID)
    {
        m_StateRunning = false;

        sys_ppu_thread_yield();
        Sleep(refreshDelay * 1000 + 500);

        uint64_t exitCode;
        sys_ppu_thread_join(UpdateInfoThreadId, &exitCode);
    }
}

bool Overlay::AnyPerfEnabledForCurrentMode() const
{
    const auto& cfg = g_Config.overlay.mode[(int)m_CooperationMode];
    return cfg.showFrameTime
        || cfg.showAvgFPS
        || cfg.showFps1Low
        || cfg.showFps01Low
        || cfg.showFpsMinMax
        || cfg.showFrameTimeMinMax
        || cfg.showHitches;
}

void Overlay::UpdatePerformance(float frameTimeSeconds)
{
    if (g_Config.version < 4)
        return;

    const auto& cfg = g_Config.overlay.mode[(int)m_CooperationMode];

    // if none of the perf fields are enabled skip work
    if (!(cfg.showFrameTime || cfg.showAvgFPS || cfg.showFps1Low || cfg.showFps01Low ||
        cfg.showFpsMinMax || cfg.showFrameTimeMinMax || cfg.showHitches))
        return;

    // ft in ms
    float ftMsF = frameTimeSeconds * 1000.0f;
    if (ftMsF < 0.0f) ftMsF = 0.0f;

    // @ top of UpdatePerformance after ftMsF computed
    if (frameTimeSeconds > 0.000001f)
    {
        const float fpsInstant = 1.0f / frameTimeSeconds;

        // smooth it a little so you can actually read it
        const float alpha = 0.15f;
        m_FpsInstant = fpsInstant;
        m_FpsSmooth = (m_FpsSmooth <= 0.0f) ? fpsInstant : (m_FpsSmooth + alpha * (fpsInstant - m_FpsSmooth));
    }

    m_FrameTimeMsCurrent = ftMsF;

    // ignore huge gaps likely caused by hook stalls (but DON'T nuke validity)
    const float stallIgnoreMs = 250.0f;
    if (ftMsF > stallIgnoreMs)
        return;

    // clamp stored FT (ms) into [0..1000] so it fits uint16_t
    const uint16_t ftMs = (ftMsF > 1000.0f) ? (uint16_t)1000 : (uint16_t)(ftMsF + 0.5f);

    const uint64_t nowMs = GetTimeNow();

    // then write to ring
    m_PerfTimeMs[m_PerfHead] = nowMs;
    m_PerfFtMs[m_PerfHead] = ftMs;

    ++m_PerfHead;
    if (m_PerfHead >= PERF_MAX_SAMPLES)
        m_PerfHead = 0;

    if (m_PerfCount < PERF_MAX_SAMPLES)
        ++m_PerfCount;

    // choke recompute
    uint32_t updateInterval = g_LoggerConfig.updateIntervalMs;
    if (updateInterval < 16) updateInterval = 16;

    if (m_LastPerfComputeMs != 0 && (nowMs - m_LastPerfComputeMs) < updateInterval)
        return;

    m_LastPerfComputeMs = nowMs;

    uint32_t windowMs = g_LoggerConfig.windowMs;
    if (windowMs < 250) windowMs = 250;
    if (windowMs > 60000) windowMs = 60000;

    const uint64_t windowStart = (nowMs > windowMs) ? (nowMs - windowMs) : 0;

    // reset histogram
    vsh::memset(m_Hist, 0, sizeof(m_Hist));

    // oldests sample index
    int tail = m_PerfHead - m_PerfCount;
    if (tail < 0) tail += PERF_MAX_SAMPLES;

    // skip samplse older than window
    int idx = tail;
    int remaining = m_PerfCount;

    while (remaining > 0 && m_PerfTimeMs[idx] < windowStart)
    {
        ++idx;
        if (idx >= PERF_MAX_SAMPLES) idx = 0;
        --remaining;
    }

    // If we don't have enough samples in window, keep previous computed values there
    // this prevents the performance block disappearing after mode switch / launch stall
    if (remaining < 2)
    {
        m_PerfSampleCount = (uint32_t)remaining;
        return; // <- !!! do NOT set m_PerfValid=false here
    }

    double sumMs = 0.0; //accumulate
    uint16_t minMs = 65535;
    uint16_t maxMs = 0;
    uint32_t hitches = 0; 

    float hitchThreshold = g_LoggerConfig.hitchThresholdMs;
    if (hitchThreshold < 0.0f) hitchThreshold = 0.0f;

    int used = 0;
    int it = idx;

    for (int k = 0; k < remaining; ++k)
    {
        const uint16_t ms = m_PerfFtMs[it];

        sumMs += (double)ms;
        if (ms < minMs) minMs = ms;
        if (ms > maxMs) maxMs = ms;
        if ((float)ms > hitchThreshold) ++hitches;

        const int bin = (ms > HIST_MAX_MS) ? (HIST_BINS - 1) : (int)ms;
        ++m_Hist[bin];

        ++used;

        ++it;
        if (it >= PERF_MAX_SAMPLES) it = 0;
    }

    m_PerfSampleCount = (uint32_t)used;
    m_HitchCount = hitches;

    m_FrameTimeMinMs = (float)minMs;
    m_FrameTimeMaxMs = (float)maxMs;

    if (sumMs > 0.000001)
        m_FpsAvg = (float)(1000.0 * (double)used / sumMs);
    else
        m_FpsAvg = 0.0f;

    m_FpsMax = (minMs > 0) ? (1000.0f / (float)minMs) : 0.0f;
    m_FpsMin = (maxMs > 0) ? (1000.0f / (float)maxMs) : 0.0f;

    const uint32_t target99 = (uint32_t)((99 * used + 99) / 100);
    const uint32_t target999 = (uint32_t)((999 * used + 999) / 1000);

    uint16_t p99Ms = 0;
    uint16_t p999Ms = 0;

    uint32_t cum = 0;
    bool got99 = false;
    bool got999 = false;

    for (int b = 0; b < HIST_BINS; ++b)
    {
        cum += m_Hist[b];

        if (!got99 && cum >= target99)
        {
            p99Ms = (b >= HIST_BINS - 1) ? (uint16_t)(HIST_MAX_MS + 1) : (uint16_t)b;
            got99 = true;
        }
        if (!got999 && cum >= target999)
        {
            p999Ms = (b >= HIST_BINS - 1) ? (uint16_t)(HIST_MAX_MS + 1) : (uint16_t)b;
            got999 = true;
        }

        if (got99 && got999)
            break;
    }

    m_Fps1Low = (p99Ms > 0) ? (1000.0f / (float)p99Ms) : 0.0f;
    m_Fps01Low = (p999Ms > 0) ? (1000.0f / (float)p999Ms) : 0.0f;

    m_PerfValid = true;
}

void Overlay::DrawOverlay()
{
    const bool isXmb = (m_CooperationMode == vsh::eCooperationMode::XMB);

    switch (g_Config.overlay.displayMode)
    {
    case Config::DisplayMode::XMB:
        if (!isXmb) return;
        break;
    case Config::DisplayMode::GAME:
        if (isXmb) return;
        break;
    case Config::DisplayMode::XMB_GAME:
    default:
        break;
    }


    static const int BUF_SIZE = 512;
    wchar_t bufA[BUF_SIZE]; int posA = 0; int linesA = 1;
    wchar_t bufC[BUF_SIZE]; int posC = 0; int linesC = 1;
    wchar_t bufB[BUF_SIZE]; int posB = 0; int linesB = 1;

    // eugh
    #define WFMT(buf, pos, lines, fmt, ...) do { \
        int _n = vsh::swprintf((buf) + (pos), (BUF_SIZE - (pos)) * sizeof(wchar_t), fmt, ##__VA_ARGS__); \
        if (_n > 0) { \
            for (int _i = 0; _i < _n; _i++) if ((buf)[(pos) + _i] == L'\n') (lines)++; \
            (pos) += _n; \
        } \
    } while(0)

    const auto& modeCfg = g_Config.overlay.mode[(int)m_CooperationMode];
    const wchar_t* tempSym = (m_TempType == TempType::Fahrenheit) ? L"\u2109" : L"\u2103";

    // chunk a
    if (modeCfg.showFPS)
        WFMT(bufA, posA, linesA, L"FPS: %.2f\n", m_FPS);

    if (g_Config.version >= 4)
    {
        if (modeCfg.showFrameTime)
            WFMT(bufA, posA, linesA, L"FT: %.2f ms\n", m_FrameTimeMsCurrent);

        if (m_PerfValid)
        {
            if (modeCfg.showAvgFPS)
                WFMT(bufA, posA, linesA, L"AVG: %.2f fps\n", m_FpsAvg);
            if (modeCfg.showFps1Low)
                WFMT(bufA, posA, linesA, L"1%%: %.2f fps\n", m_Fps1Low);
            if (modeCfg.showFps01Low)
                WFMT(bufA, posA, linesA, L"0.1%%: %.2f fps\n", m_Fps01Low);
            if (modeCfg.showFpsMinMax)
                WFMT(bufA, posA, linesA, L"MIN/MAX: %.2f / %.2f fps\n", m_FpsMin, m_FpsMax);
            if (modeCfg.showFrameTimeMinMax)
                WFMT(bufA, posA, linesA, L"FT MIN/MAX: %.2f / %.2f ms\n", m_FrameTimeMinMs, m_FrameTimeMaxMs);
            if (modeCfg.showHitches)
                WFMT(bufA, posA, linesA, L"HITCHES(>%.0fms): %u (%uf)\n",
                    g_LoggerConfig.hitchThresholdMs, m_HitchCount, m_PerfSampleCount);

            if (modeCfg.showFrameTimeStdev)
                WFMT(bufC, posC, linesC, L"FT STDEV: %.2f ms\n", m_FtStdevMs);
            if (modeCfg.showPacing)
                WFMT(bufC, posC, linesC, L"PACING: %.1f%%\n", m_PacingPct);
            if (modeCfg.showDroppedFrames)
                WFMT(bufC, posC, linesC, L"DROPPED: %.1f%%\n", m_DroppedPct);
        }

        if (m_SensorActive)
        {
            if (m_SensorBufCount >= 2)
                WFMT(bufC, posC, linesC, L"BUF: %ux\n", m_SensorBufCount);
            if (m_SensorRsxCoreMhz > 0 && modeCfg.showClockSpeeds)
                WFMT(bufC, posC, linesC, L"RSX: %u / %u MHz\n", m_SensorRsxCoreMhz, m_SensorRsxMemMhz);
            WFMT(bufC, posC, linesC, L"RSX Load: %.1f%%\n", m_GpuBusyPct);
        }
    }

    // chunk b
    if (modeCfg.showCpuInfo)
    {
        WFMT(bufB, posB, linesB, L"CPU: %.0f%ls", m_CPUTemp, tempSym);
        if (m_CpuClock != 0 && modeCfg.showClockSpeeds)
            WFMT(bufB, posB, linesB, L" / %.3f GHz", m_CpuClock / 1000.0f);
        if (!modeCfg.showGpuInfo || modeCfg.showClockSpeeds)
            WFMT(bufB, posB, linesB, L"\n");
    }

    if (modeCfg.showGpuInfo)
    {
        if (modeCfg.showCpuInfo && !modeCfg.showClockSpeeds)
            WFMT(bufB, posB, linesB, L" / ");
        WFMT(bufB, posB, linesB, L"GPU: %.0f%ls", m_GPUTemp, tempSym);
        if (m_GpuGddr3RamClock != 0 && modeCfg.showClockSpeeds)
            WFMT(bufB, posB, linesB, L" / %u MHz / %u MHz", m_GpuClock, m_GpuGddr3RamClock);
        WFMT(bufB, posB, linesB, L"\n");
    }

    if (modeCfg.showRamInfo)
        WFMT(bufB, posB, linesB, L"RAM: %.1f%% %.1f / %.1f MB\n",
            m_MemoryUsage.percent, m_MemoryUsage.used, m_MemoryUsage.total);

    if (modeCfg.showFanSpeed)
        WFMT(bufB, posB, linesB, L"Fan speed: %.0f%%\n", m_FanSpeed);

    if (modeCfg.showFirmware)
    {
        // build firmware string once
        if (!m_CachedPayloadTextBuilt && m_PayloadVersion != 0 && m_FirmwareVersion > 0)
        {
            m_CachedPayloadVersion = m_PayloadVersion;
            m_CachedFirmwareVersion = m_FirmwareVersion;
            m_CachedKernelType = m_KernelType;

            const wchar_t* kernelName = (m_CachedKernelType == 1) ? L"CEX"
                                      : (m_CachedKernelType == 2) ? L"DEX"
                                      : (m_CachedKernelType == 3) ? L"DEH" : L"N/A";
            const wchar_t* payloadName = IsConsoleHen() ? L"PS3HEN" : IsConsoleMamba() ? L"Mamba" : L"Cobra";

            wchar_t fwBuf[80];
            if (IsConsoleHen())
                vsh::swprintf(fwBuf, sizeof(fwBuf), L"%.2f %ls %ls %d.%d.%d\n",
                    m_CachedFirmwareVersion, kernelName, payloadName,
                    m_CachedPayloadVersion >> 8, (m_CachedPayloadVersion & 0xF0) >> 4, m_CachedPayloadVersion & 0xF);
            else
                vsh::swprintf(fwBuf, sizeof(fwBuf), L"%.2f %ls %ls %d.%d\n",
                    m_CachedFirmwareVersion, kernelName, payloadName,
                    m_CachedPayloadVersion >> 8, (m_CachedPayloadVersion & 0xF0) >> 4);

            m_CachedPayloadText = fwBuf;
            m_CachedPayloadTextBuilt = true;
        }

        if (m_CachedPayloadTextBuilt)
        {
            int n = vsh::swprintf(bufB + posB, (BUF_SIZE - posB) * sizeof(wchar_t), L"%ls", m_CachedPayloadText.c_str());
            if (n > 0) { for (int i = 0; i < n; i++) if (bufB[posB + i] == L'\n') linesB++; posB += n; }
        }
    }

    if (modeCfg.showAppName && g_Helpers.game_plugin)
    {
        char gameTitleId[16]{};
        char gameTitleName[64]{};
        GetGameName(gameTitleId, gameTitleName);
        bool isTitleIdEmpty = (gameTitleId[0] == 0) || (gameTitleId[0] == ' ');
        WFMT(bufB, posB, linesB, L"%s %c %s\n", gameTitleName, isTitleIdEmpty ? L' ' : L'/', gameTitleId);
    }

    if (modeCfg.showPlayTime && g_Helpers.game_plugin)
    {
        uint64_t msec = GetCurrentTick();
        if (msec)
        {
            uint32_t sec = ((msec + 500) / 1000) % 86400;
            WFMT(bufB, posB, linesB, L"Play Time: %02d:%02d:%02d\n",
                sec / 3600, (sec % 3600) / 60, sec % 60);
        }
    }

    #undef WFMT

    // paf doesnt like empty strings#
    if (posA == 0) { bufA[0] = L' '; bufA[1] = L'\n'; bufA[2] = 0; linesA = 1; } else bufA[posA] = 0;
    if (posC == 0) { bufC[0] = L' '; bufC[1] = L'\n'; bufC[2] = 0; linesC = 1; } else bufC[posC] = 0;
    if (posB == 0) { bufB[0] = L' '; bufB[1] = L'\n'; bufB[2] = 0; } else bufB[posB] = 0;

    const float lineH = modeCfg.textSize;

    std::wstring wsA(bufA);
    g_Render.Text(wsA, m_Position, modeCfg.textSize, m_HorizontalAlignment, m_VerticalAlignment, m_ColorText);

    //a
    vsh::vec2 pC = m_Position;
    if (m_VerticalAlignment == Render::Align::Top)        pC.y -= (lineH * (float)linesA);
    else if (m_VerticalAlignment == Render::Align::Bottom) pC.y += (lineH * (float)linesA);
    //b
    std::wstring wsC(bufC);
    g_Render.Text(wsC, pC, modeCfg.textSize, m_HorizontalAlignment, m_VerticalAlignment, m_ColorText);
    //c
    vsh::vec2 pB = pC;
    if (m_VerticalAlignment == Render::Align::Top)        pB.y -= (lineH * (float)linesC);
    else if (m_VerticalAlignment == Render::Align::Bottom) pB.y += (lineH * (float)linesC);

    std::wstring wsB(bufB);
    g_Render.Text(wsB, pB, modeCfg.textSize, m_HorizontalAlignment, m_VerticalAlignment, m_ColorText);
}


void Overlay::UpdatePosition()
{
    switch (g_Config.overlay.mode[(int)m_CooperationMode].positionStyle)
    {
    case Config::PostionStyle::TOP_LEFT:
    {
        m_Position.x = -vsh::paf::PhWidget::GetViewportWidth() / 2 + m_SafeArea.x + 5;
        m_Position.y = vsh::paf::PhWidget::GetViewportHeight() / 2 - m_SafeArea.y - 5;

        m_VerticalAlignment = Render::Align::Top;
        m_HorizontalAlignment = Render::Align::Left;
        break;
    }
    case Config::PostionStyle::TOP_RIGHT:
    {
        m_Position.x = vsh::paf::PhWidget::GetViewportWidth() / 2 - m_SafeArea.x - 5;
        m_Position.y = vsh::paf::PhWidget::GetViewportHeight() / 2 - m_SafeArea.y - 5;

        m_VerticalAlignment = Render::Align::Top;
        m_HorizontalAlignment = Render::Align::Right;
        break;
    }
    case Config::PostionStyle::BOTTOM_LEFT:
    {
        m_Position.x = -vsh::paf::PhWidget::GetViewportWidth() / 2 + m_SafeArea.x + 5;
        m_Position.y = -vsh::paf::PhWidget::GetViewportHeight() / 2 + m_SafeArea.y + 5;

        m_VerticalAlignment = Render::Align::Bottom;
        m_HorizontalAlignment = Render::Align::Left;
        break;
    }
    case Config::PostionStyle::BOTTOM_RIGHT:
    {
        m_Position.x = vsh::paf::PhWidget::GetViewportWidth() / 2 - m_SafeArea.x - 5;
        m_Position.y = -vsh::paf::PhWidget::GetViewportHeight() / 2 + m_SafeArea.y + 5;

        m_VerticalAlignment = Render::Align::Bottom;
        m_HorizontalAlignment = Render::Align::Right;
        break;
    }
    }
}

void Overlay::CalculateFps()
{
    if (m_CooperationMode != vsh::eCooperationMode::XMB)
    {
        if (ReadSensorMetrics())
        {
            //fallback
            m_FpsLastTime = (float)sys_time_get_system_time() * 0.000001f;
            return;
        }
    }

    // complete fallback if sensor fails

    const float timeNow = (float)sys_time_get_system_time() * 0.000001f;

    if (m_FpsLastTime == 0.0f)
    {
        m_FpsLastTime = timeNow;
        m_FpsTimeElapsed = 0.0;
        m_FpsTimeReport = 0.0;
        m_FpsTimeLastReport = 0.0;
        m_FpsFrames = 0;
        m_FpsFramesLastReport = 0;
        m_FrameTimeMsCurrent = 0.0f;
        return;
    }

    float fElapsedInFrame = (timeNow - m_FpsLastTime);
    m_FpsLastTime = timeNow;

    if (fElapsedInFrame < 0.0f) fElapsedInFrame = 0.0f;
    if (fElapsedInFrame > 1.0f) fElapsedInFrame = 1.0f;

    m_FrameTimeMsCurrent = fElapsedInFrame * 1000.0f;
    const float fpsInstant = (fElapsedInFrame > 0.000001f) ? (1.0f / fElapsedInFrame) : 0.0f;

    m_FPS = fpsInstant;

    ++m_FpsFrames;
    m_FpsTimeElapsed += fElapsedInFrame;

    if (m_FpsTimeElapsed >= m_FpsTimeReport + m_FpsREPORT_TIME)
    {
        const double denom = (m_FpsTimeElapsed - m_FpsTimeLastReport);
        if (denom > 0.000001)
            m_FPS = (float)((m_FpsFrames - m_FpsFramesLastReport) / denom);

        m_FpsTimeLastReport = m_FpsTimeElapsed;
        m_FpsFramesLastReport = m_FpsFrames;
        m_FpsTimeReport = m_FpsTimeElapsed;
    }

    UpdatePerformance(fElapsedInFrame);
}
 

void Overlay::GetGameName(char outTitleId[16], char outTitleName[64])
{
    if (!g_Helpers.game_plugin)
        return;

    vsh::game_plugin_interface* game_interface = g_Helpers.game_plugin->GetInterface<vsh::game_plugin_interface*>(1);
    if (!game_interface)
        return;

    char _gameInfo[0x120]{};
    game_interface->gameInfo(_gameInfo);
    vsh::snprintf(outTitleId, 10, "%s", _gameInfo + 0x04);
    vsh::snprintf(outTitleName, 63, "%s", _gameInfo + 0x14);
}

union clock_s
{
public:
    struct
    {
    public:
        uint32_t junk0;
        uint8_t junk1;
        uint8_t junk2;
        uint8_t mul;
        uint8_t junk3;
    };

    uint64_t value;
};

uint32_t Overlay::GetGpuClockSpeed()
{
    clock_s clock;
    clock.value = PeekLv1(0x28000004028);

    if (clock.value == 0xFFFFFFFF80010003) // if cfw syscalls r disabled
        return 0;

    return (clock.mul * 50);
}

uint32_t Overlay::GetGpuGddr3RamClockSpeed()
{
    clock_s clock;
    clock.value = PeekLv1(0x28000004010);

    if (clock.value == 0xFFFFFFFF80010003) // if cfw syscalls r disabled
        return 0;

    return (clock.mul * 25);
}

uint32_t Overlay::GetCpuClockSpeed()
{
    system_call_8(10, 1, 0x62650000, 0, 0x6e636c6b00000000, 0, 0, 0, 91);
    uint64_t v = register_passing_1(uint64_t);

    if (p1 != 0)
        return 0;

    return (v / 1000000);
}

void Overlay::UpdateInfoThread(uint64_t arg)
{
    (void)arg;

    if (!g_Overlay.m_CachedPayloadTextBuilt)
        g_Overlay.m_PayloadVersion = GetPayloadVersion();

    {
        int ret = get_target_type(&g_Overlay.m_KernelType);
        if (ret != SUCCEEDED)
            g_Overlay.m_KernelType = 0;
        g_Overlay.m_FirmwareVersion = GetFirmwareVersion();
    }

    g_Overlay.m_StateRunning = true;

    while (g_Overlay.m_StateRunning)
    {
        Sleep(1000);

        if (g_Helpers.m_StateGameJustLaunched)
        {
            vsh::printf("[FpsSensor] Game just launched, sleeping 15s (m_IsHen=%d)\n", (int)g_Helpers.m_IsHen);
            Sleep(5 * 1000);
            g_Helpers.m_StateGameJustLaunched = false;

            if (g_Helpers.m_SensorPendingInject)
            {
                vsh::printf("[FpsSensor] Post-delay: calling InjectSensor()\n");
                g_Helpers.InjectSensor();

                if (g_Helpers.IsSensorInjected())
                {
                    Sleep(500); // let module_start complete
                    uint64_t dbg = PeekLv2(METRICS_SLOT_DEBUG);
                    uint32_t status = unpack_u32_hi(dbg);
                    const char* status_str = (status == SENSOR_STATUS_WAITING) ? "waiting for first flip"
                                           : (status == SENSOR_STATUS_MEASURING) ? "measuring"
                                           : "unknown";
                    vsh::printf("[FpsSensor] Sensor status: %u (%s)\n", status, status_str);

                    char titleId[16]{};
                    char titleName[64]{};
                    g_Overlay.GetGameName(titleId, titleName);
                    g_SessionLogger.StartSession(titleId, titleName);
                }
            }
            else
            {
                vsh::printf("[FpsSensor] Post-delay: m_SensorPendingInject=false (already handled or cleared)\n");
            }
        }

        vsh::eCooperationMode coop = vsh::GetCooperationMode();
        if (coop != vsh::eCooperationMode::XMB)
            coop = vsh::eCooperationMode::Game;

        const auto& cfg = g_Config.overlay.mode[(int)coop];
        bool logging = g_SessionLogger.IsActive();

        if (cfg.showRamInfo || logging)
        {
            g_Overlay.m_MemoryUsage = GetMemoryUsage();
            g_Overlay.m_MemoryUsage.total /= 1024;
            g_Overlay.m_MemoryUsage.free /= 1024;
            g_Overlay.m_MemoryUsage.used /= 1024;
        }

        if (cfg.showFanSpeed || logging)
            g_Overlay.m_FanSpeed = GetFanSpeed();

        if (cfg.showCpuInfo || cfg.showGpuInfo)
        {
            switch (cfg.temperatureType)
            {
            case Config::TemperatureType::BOTH:
                if (g_Overlay.m_CycleTemperatureType)
                {
                    g_Overlay.m_CPUTemp = GetTemperatureFahrenheit(0);
                    g_Overlay.m_GPUTemp = GetTemperatureFahrenheit(1);
                    g_Overlay.m_TempType = TempType::Fahrenheit;
                }
                else
                {
                    g_Overlay.m_CPUTemp = GetTemperatureCelsius(0);
                    g_Overlay.m_GPUTemp = GetTemperatureCelsius(1);
                    g_Overlay.m_TempType = TempType::Celsius;
                }
                break;

            case Config::TemperatureType::CELSIUS:
                g_Overlay.m_CPUTemp = GetTemperatureCelsius(0);
                g_Overlay.m_GPUTemp = GetTemperatureCelsius(1);
                g_Overlay.m_TempType = TempType::Celsius;
                break;

            case Config::TemperatureType::FAHRENHEIT:
                g_Overlay.m_CPUTemp = GetTemperatureFahrenheit(0);
                g_Overlay.m_GPUTemp = GetTemperatureFahrenheit(1);
                g_Overlay.m_TempType = TempType::Fahrenheit;
                break;

            default:
                g_Overlay.m_CPUTemp = GetTemperatureCelsius(0);
                g_Overlay.m_GPUTemp = GetTemperatureCelsius(1);
                g_Overlay.m_TempType = TempType::Celsius;
                break;
            }

            if (cfg.temperatureType == Config::TemperatureType::BOTH)
            {
                uint64_t timeNow = GetTimeNow();
                if (timeNow - g_Overlay.m_TemperatureCycleTime > 5000)
                {
                    g_Overlay.m_CycleTemperatureType ^= 1;
                    g_Overlay.m_TemperatureCycleTime = timeNow;
                }
            }
        }

        if ((cfg.showClockSpeeds || logging) && !g_Helpers.m_IsDeh)
        {
            g_Overlay.m_CpuClock = g_Overlay.GetCpuClockSpeed();
            g_Overlay.m_GpuClock = g_Overlay.GetGpuClockSpeed();
            g_Overlay.m_GpuGddr3RamClock = g_Overlay.GetGpuGddr3RamClockSpeed();
        }

        g_SessionLogger.SampleMetrics();

        if (g_Helpers.m_SensorPendingInject && !g_Helpers.m_IsHen)
        {
            vsh::printf("[FpsSensor] Non-HEN path: sleeping 3s before inject\n");
            Sleep(3000);
            g_Helpers.InjectSensor();

            if (g_Helpers.IsSensorInjected())
            {
                Sleep(500);
                uint64_t dbg = PeekLv2(METRICS_SLOT_DEBUG);
                uint32_t status = unpack_u32_hi(dbg);
                const char* status_str = (status == 1) ? "waiting for first flip"
                                       : (status == 2) ? "measuring"
                                       : "unknown";
                vsh::printf("[FpsSensor] Sensor status: %u (%s)\n", status, status_str);

                // Start session logging
                char titleId[16]{};
                char titleName[64]{};
                g_Overlay.GetGameName(titleId, titleName);
                g_SessionLogger.StartSession(titleId, titleName);
            }
        }

        if (g_Helpers.m_SensorPendingUnload)
        {
            vsh::printf("[FpsSensor] Unloading sensor\n");
            g_Helpers.UnloadSensor();
        }
    }

    sys_ppu_thread_exit(0);
}

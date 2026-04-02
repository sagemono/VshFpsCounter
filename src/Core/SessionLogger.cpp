#include "SessionLogger.hpp"
#include "Core/Overlay.hpp"
#include "Core/LoggerConfig.hpp"
#include "Core/Notify.hpp"
#include "Core/Paths.hpp"
#include "Utils/FileSystem.hpp"
#include "Utils/ConsoleInfo.hpp"
#include "Utils/Timers.hpp"
#include <vsh/stdc.h>

SessionLogger g_SessionLogger;

SessionLogger::SessionLogger()
{
    m_Active = false;
    m_BufferUsed = 0;
    m_RowCount = 0;
}

void SessionLogger::StartSession(const char* titleId, const char* titleName)
{
    if (m_Active)
        EndSession();

    if (!g_LoggerConfig.enabled)
        return;

    CreateDirectory(std::string(VFPC_SESSIONS_DIR));

    // TITLEID_YYYY-MM-DD_HH-MM-SS.csv
    time_t now;
    vsh::time(&now);
    struct tm* t = vsh::localtime(&now);

    char filename[128];
    vsh::snprintf(filename, sizeof(filename),
        "%s/%s_%04d-%02d-%02d_%02d-%02d-%02d.csv",
        VFPC_SESSIONS_DIR,
        (titleId && titleId[0]) ? titleId : "UNKNOWN",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec);

    m_FilePath = filename;
    m_SessionStartMs = GetTimeNow();
    m_BufferUsed = 0;
    m_RowCount = 0;

    WriteHeader(titleId, titleName);

    m_Active = true;
    vsh::printf("[SessionLog] Started: %s\n", filename);

    wchar_t wTitle[64];
    vsh::swprintf(wTitle, 64, L"Logging: %s", (titleName && titleName[0]) ? titleName : "Unknown");
    Notify::Info(wTitle);
}

void SessionLogger::WriteHeader(const char* titleId, const char* titleName)
{
    char meta[256];
    time_t now;
    vsh::time(&now);
    struct tm* t = vsh::localtime(&now);

    vsh::snprintf(meta, sizeof(meta),
        "# %s - %s - %04d/%02d/%02d %02d:%02d:%02d\n",
        (titleId && titleId[0]) ? titleId : "UNKNOWN",
        (titleName && titleName[0]) ? titleName : "Unknown",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec);

    const char* columns =
        "elapsed_s,"
        "fps,fps_avg,fps_1low,fps_01low,fps_min,fps_max,"
        "ft_ms,ft_min_ms,ft_max_ms,ft_stdev_ms,"
        "pacing_pct,dropped_pct,hitch_count,"
        "cpu_temp,gpu_temp,fan_pct,"
        "cpu_mhz,gpu_mhz,gddr3_mhz,"
        "ram_used_mb,ram_total_mb\n";

    AppendFile(m_FilePath, meta, vsh::strlen(meta));
    AppendFile(m_FilePath, columns, vsh::strlen(columns));
}

void SessionLogger::SampleMetrics()
{
    if (!m_Active)
        return;

    uint64_t elapsedMs = GetTimeNow() - m_SessionStartMs;
    float elapsedS = (float)elapsedMs / 1000.0f;

    // force celcius... heh...
    float cpuTempC = GetTemperatureCelsius(0);
    float gpuTempC = GetTemperatureCelsius(1);

    char row[256];
    int len = vsh::snprintf(row, sizeof(row),
        "%.1f,"
        "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
        "%.2f,%.2f,%.2f,%.2f,"
        "%.1f,%.1f,%u,"
        "%.1f,%.1f,%.1f,"
        "%u,%u,%u,"
        "%.1f,%.1f\n",
        elapsedS,
        g_Overlay.m_FPS, g_Overlay.m_FpsAvg,
        g_Overlay.m_Fps1Low, g_Overlay.m_Fps01Low,
        g_Overlay.m_FpsMin, g_Overlay.m_FpsMax,
        g_Overlay.m_FrameTimeMsCurrent,
        g_Overlay.m_FrameTimeMinMs, g_Overlay.m_FrameTimeMaxMs,
        g_Overlay.m_FtStdevMs,
        g_Overlay.m_PacingPct, g_Overlay.m_DroppedPct,
        g_Overlay.m_HitchCount,
        cpuTempC, gpuTempC, g_Overlay.m_FanSpeed,
        g_Overlay.m_CpuClock, g_Overlay.m_GpuClock, g_Overlay.m_GpuGddr3RamClock,
        g_Overlay.m_MemoryUsage.used, g_Overlay.m_MemoryUsage.total);

    if (len <= 0 || len >= (int)sizeof(row))
        return;

    if (m_BufferUsed + len >= BUFFER_MAX)
        FlushBuffer();

    if (m_BufferUsed + len < BUFFER_MAX)
    {
        vsh::memcpy(m_Buffer + m_BufferUsed, row, len);
        m_BufferUsed += len;
        m_RowCount++;
    }

    if (m_RowCount % 16 == 0)
        FlushBuffer();
}

void SessionLogger::FlushBuffer()
{
    if (m_BufferUsed <= 0)
        return;

    AppendFile(m_FilePath, m_Buffer, m_BufferUsed);
    m_BufferUsed = 0;
}

void SessionLogger::EndSession()
{
    if (!m_Active)
        return;

    FlushBuffer();
    m_Active = false;

    vsh::printf("[SessionLog] Ended: %d rows written to %s\n", m_RowCount, m_FilePath.c_str());
    Notify::InfoFmt(L"Session saved (%d samples)", m_RowCount);
}

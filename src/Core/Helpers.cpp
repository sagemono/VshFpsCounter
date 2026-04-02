#include "Helpers.hpp"
#include <sys/process.h>
#include <vsh/stdc.h>
#include "Core/Notify.hpp"
#include "Core/Overlay.hpp"
#include "Core/LoggerConfig.hpp"
#include "Core/shared_metrics.h"
#include "Utils/FileSystem.hpp"

#define SENSOR_LOG(fmt, ...) vsh::printf("[FpsSensor] " fmt "\n", ##__VA_ARGS__)

Helpers g_Helpers;

Helpers::Helpers()
{
    m_IsHen = IsConsoleHen();
    m_IsDeh = IsConsoleDeh();
}

void Helpers::OnUpdate()
{
    game_ext_plugin = vsh::paf::View::Find("game_ext_plugin");
    game_plugin = vsh::paf::View::Find("game_plugin");
    system_plugin = vsh::paf::View::Find("system_plugin");
    xmb_plugin = vsh::paf::View::Find("xmb_plugin");

    page_notification = system_plugin ? system_plugin->FindWidget("page_notification") : nullptr;
    page_xmb_indicator = system_plugin ? system_plugin->FindWidget("page_xmb_indicator") : nullptr;

    MonitorGameState();
}

void Helpers::MonitorGameState()
{
    uint64_t timeNow = GetTimeNow();

    bool is_game = (vsh::GetCooperationMode() != vsh::eCooperationMode::XMB);

    if (is_game && !m_StateGameRunning)
    {
        m_StateGameRunning = true;
        m_StateGameJustLaunched = true;
        m_GameLaunchTime = timeNow;

        SENSOR_LOG("Game launched detected (coop != XMB)");

        uint64_t header = PeekLv2(METRICS_SLOT_HEADER);
        uint32_t magic = unpack_u32_hi(header);

        if (magic == METRICS_MAGIC)
        {
            m_GamePid = FindGamePid();
            m_SensorInjected = true;
            m_SensorPendingInject = false;
            m_SensorPendingUnload = false;
            m_StateGameJustLaunched = false;
            SENSOR_LOG("Sensor still active (magic=FPS1, pid=0x%x) — reconnecting", m_GamePid);

            char titleId[16]{};
            char titleName[64]{};
            g_Overlay.GetGameName(titleId, titleName);
            g_SessionLogger.StartSession(titleId, titleName);
        }
        else
        {
            m_SensorInjected = false;
            m_SensorPendingUnload = false;
            m_GamePid = 0;

            m_SensorPendingInject = true;
            SENSOR_LOG("Queued sensor injection (m_IsHen=%d)", (int)m_IsHen);
        }
    }
    else if (!is_game && m_StateGameRunning)
    {
        m_StateGameRunning = false;
        SENSOR_LOG("Game exit detected");

        g_SessionLogger.EndSession();

        if (m_SensorInjected)
            m_SensorPendingUnload = true;
    }

    if (timeNow - m_GameLaunchTime > 15 * 1000)
        m_StateGameJustLaunched = false;
}

uint32_t Helpers::FindGamePid()
{
    uint32_t pid_list[16]{};
    int list_ret = ps3mapi_get_all_processes_pid(pid_list);
    SENSOR_LOG("ps3mapi_get_all_processes_pid returned %d", list_ret);

    uint32_t vsh_pid = sys_process_getpid();
    SENSOR_LOG("VSH PID = 0x%x", vsh_pid);

    for (int i = 0; i < 16; i++)
    {
        if (pid_list[i] == 0)
            continue;

        char proc_name[64]{};
        int ret = ps3mapi_get_process_name_by_pid(pid_list[i], proc_name);

        SENSOR_LOG("  [%d] PID=0x%x name='%s' (ret=%d)%s",
            i, pid_list[i], proc_name, ret,
            (pid_list[i] == vsh_pid) ? " [VSH-SKIP]" : "");

        if (pid_list[i] == vsh_pid)
            continue;
        if (ret < 0)
            continue;

        if (strstr(proc_name, "agent") != nullptr)
        {
            SENSOR_LOG("  -> skipped (agent)");
            continue;
        }
        if (strstr(proc_name, "vsh") != nullptr)
        {
            SENSOR_LOG("  -> skipped (vsh)");
            continue;
        }
        if (strstr(proc_name, "spu_") != nullptr)
        {
            SENSOR_LOG("  -> skipped (spu_)");
            continue;
        }
        if (strstr(proc_name, "ss_") != nullptr)
        {
            SENSOR_LOG("  -> skipped (ss_)");
            continue;
        }

        SENSOR_LOG("  -> SELECTED as game process");
        return pid_list[i];
    }

    SENSOR_LOG("No game process found!");
    return 0;
}

void Helpers::InjectSensor()
{
    m_SensorPendingInject = false;
    SENSOR_LOG("=== InjectSensor() called ===");

    {
        uint64_t header = PeekLv2(METRICS_SLOT_HEADER);
        uint32_t magic = unpack_u32_hi(header);
        if (magic == METRICS_MAGIC)
        {
            m_GamePid = FindGamePid();
            m_SensorInjected = true;
            SENSOR_LOG("Sensor already active (magic=FPS1, pid=0x%x) — skipping injection", m_GamePid);
            Notify::Info(L"FPS sensor reconnected");
            return;
        }
    }

    // check if .sprx exists on disk
    bool exists = FileExist(FPS_SENSOR_SPRX_PATH);
    SENSOR_LOG("FileExist('%s') = %d", FPS_SENSOR_SPRX_PATH, (int)exists);
    if (!exists)
    {
        Notify::Warning(L"FPS sensor not found");
        return;
    }

    uint32_t game_pid = FindGamePid();
    SENSOR_LOG("FindGamePid() = 0x%x", game_pid);
    if (game_pid == 0)
        return;

    m_GamePid = game_pid;

    SENSOR_LOG("Calling ps3mapi_load_process_modules(pid=0x%x, path='%s')", game_pid, FPS_SENSOR_SPRX_PATH);
    int ret = ps3mapi_load_process_modules(
        game_pid,
        (char*)FPS_SENSOR_SPRX_PATH,
        nullptr,
        0
    );
    SENSOR_LOG("ps3mapi_load_process_modules returned %d (0x%x)", ret, (uint32_t)ret);

    if (ret >= 0) // returns prx_id on success
    {
        m_SensorInjected = true;
        SENSOR_LOG("Sensor injected successfully! prx_id=%d", ret);
        Notify::Info(L"FPS sensor loaded");
    }
    else
    {
        SENSOR_LOG("Sensor injection FAILED! ret=%d (0x%08x)", ret, (uint32_t)ret);
        Notify::Warning(L"FPS sensor failed to load");
    }
}

void Helpers::UnloadSensor()
{
    m_SensorPendingUnload = false;
    m_SensorInjected = false;
    m_GamePid = 0;

    // clear the LV2 magic so stale data doesn't trick us into thinking
    // the sensor is still running on the next game launch. we can't rely
    // on the sensor's module_stop cleanup because the game process may
    // terminate before it runs
    PokeLv2(METRICS_SLOT_HEADER, 0);
    SENSOR_LOG("Cleared LV2 magic (slot header zeroed)");
}

#include "Helpers.hpp"
#include "Utils/FileSystem.hpp"

Helpers g_Helpers;

Helpers::Helpers()
{
    m_IsHen = IsConsoleHen();
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

        // signal an injection request (handled by UpdateInfoThread after HEN delay)
        if (!m_SensorInjected)
            m_SensorPendingInject = true;
    }
    else if (!is_game && m_StateGameRunning)
    {
        m_StateGameRunning = false;

        // game exit unload sensor
        if (m_SensorInjected)
            m_SensorPendingUnload = true;
    }

    if (timeNow - m_GameLaunchTime > 15 * 1000)
        m_StateGameJustLaunched = false;
}

uint32_t Helpers::FindGamePid()
{
    uint32_t pid_list[16]{};
    ps3mapi_get_all_processes_pid(pid_list);

    uint32_t vsh_pid = sys_process_getpid();

    for (int i = 0; i < 16; i++)
    {
        if (pid_list[i] == 0)
            continue;
        if (pid_list[i] == vsh_pid)
            continue;

        return pid_list[i];
    }

    return 0;
}

void Helpers::InjectSensor()
{
    m_SensorPendingInject = false;

    if (!FileExist(FPS_SENSOR_SPRX_PATH))
        return;

    uint32_t game_pid = FindGamePid();
    if (game_pid == 0)
        return;

    m_GamePid = game_pid;

    int ret = ps3mapi_load_process_modules(
        game_pid,
        (char*)FPS_SENSOR_SPRX_PATH,
        nullptr,
        0
    );

    if (ret >= 0) // returns prx_id on success
        m_SensorInjected = true;
}

void Helpers::UnloadSensor()
{
    m_SensorPendingUnload = false;
    m_SensorInjected = false;
    m_GamePid = 0;

    // The sensor PRX auto-clears LV2 memory in its module_stop.
    // The game process is already terminating, so we don't need
    // to explicitly unload it, the OS cleans up loaded modules
    // when the process exits
    //
    // If we ever need explicit unloading (e.g. hot reload while
    // game is running), call:
    //   ps3mapi_unload_process_modules(m_GamePid, prx_id);
    // But that requires storing the prx_id from InjectSensor()
}

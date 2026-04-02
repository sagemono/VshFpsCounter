#pragma once
#include <string>
#include <vsh/system_plugin.h>
#include <vsh/pafPhWidget.h>
#include <vsh/pafView.h>
#include <vsh/vshcommon.h>
#include <vsh/vshmain.h>
#include "Utils/SystemCalls.hpp"
#include "Utils/Timers.hpp"
#include "Core/Paths.hpp"
#include "Core/SessionLogger.hpp"

class Helpers
{
public:
    Helpers();

    void OnUpdate();

    // sensor injection (called from UpdateInfoThread, NOT from PAF callback)
    void InjectSensor();
    void UnloadSensor();
    bool IsSensorInjected() const { return m_SensorInjected; }

private:
    void MonitorGameState();
    uint32_t FindGamePid();

public:
    vsh::paf::View* game_ext_plugin{};
    vsh::paf::View* game_plugin{};
    vsh::paf::View* system_plugin{};
    vsh::paf::View* xmb_plugin{};
    vsh::paf::PhWidget* page_notification{};
    vsh::paf::PhWidget* page_xmb_indicator{};

    bool m_IsHen{};
    bool m_IsDeh{};
    bool m_StateGameRunning{};
    bool m_StateGameJustLaunched{};

    // Sensor state (written from UpdateInfoThread, read from PAF thread)
    volatile bool m_SensorInjected{};
    volatile bool m_SensorPendingInject{};
    volatile bool m_SensorPendingUnload{};

private:
    uint64_t m_GameLaunchTime{};
    uint32_t m_GamePid{};
    bool m_PrevGameRunning{};
};

extern Helpers g_Helpers;

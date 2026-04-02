#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>
#include <cell/pad.h>

#include "Utils/NewDeleteOverride.hpp"
#include "Utils/SystemCalls.hpp"
#include "Utils/Timers.hpp"
#include "Core/Overlay.hpp"
#include "Core/Hooks.hpp"
#include "Core/Configuration.hpp"
#include "Core/LoggerConfig.hpp"

SYS_MODULE_INFO(VshFpsCounter, 0, 1, 1);
SYS_MODULE_START(module_start);
SYS_MODULE_STOP(module_stop);

sys_ppu_thread_t gVshMenuPpuThreadId = SYS_PPU_THREAD_ID_INVALID;

static void VshFpsCounterStartThread(uint64_t /*arg*/)
{
    // Wait until XMB is ready
    do
        Sleep(1000);
    while (!vsh::paf::View::Find("explore_plugin"));

    g_Render = Render();
    g_Helpers = Helpers();
    g_Config = Config();
    g_LoggerConfig.Load();
    g_Overlay = Overlay();

    InstallHooks();

    sys_ppu_thread_exit(0);
}

static void VshFpsCounterStopThread(uint64_t /*arg*/)
{
    RemoveHooks();

    g_Overlay.OnShutdown();
    g_Render.DestroyWidgets();

    // Give other threads a chance to exit cleanly
    sys_ppu_thread_yield();
    Sleep(1000);

    if (gVshMenuPpuThreadId != SYS_PPU_THREAD_ID_INVALID)
    {
        uint64_t exitCode = 0;
        sys_ppu_thread_join(gVshMenuPpuThreadId, &exitCode);
        gVshMenuPpuThreadId = SYS_PPU_THREAD_ID_INVALID;
    }

    sys_ppu_thread_exit(0);
}

CDECL_BEGIN
int module_start(unsigned int /*args*/, void* /*argp*/)
{
    // IMPORTANT: stack size should be >= 0x1000 and page-aligned.
    const uint32_t kStartStack = 0x10000; // 64 KB but could be smaller?
    const uint32_t kStopStack = 0x8000;  // 32 KB

    sys_ppu_thread_create(
        &gVshMenuPpuThreadId,
        VshFpsCounterStartThread,
        0,
        1059,
        kStartStack,
        SYS_PPU_THREAD_CREATE_JOINABLE,
        "VshFpsCounterStart"
    );

    _sys_ppu_thread_exit(0);
    return 0;
}

int module_stop(unsigned int /*args*/, void* /*argp*/)
{
    const uint32_t kStopStack = 0x8000; // 32 KB

    sys_ppu_thread_t stopPpuThreadId = SYS_PPU_THREAD_ID_INVALID;
    int ret = sys_ppu_thread_create(
        &stopPpuThreadId,
        VshFpsCounterStopThread,
        0,
        2816,
        kStopStack,
        SYS_PPU_THREAD_CREATE_JOINABLE,
        "VshFpsCounterStop"
    );

    if (ret == SUCCEEDED)
    {
        uint64_t exitCode = 0;
        sys_ppu_thread_join(stopPpuThreadId, &exitCode);
    }

    Sleep(5);

    UnloadMyModule();

    _sys_ppu_thread_exit(0);
    return 0;
}
CDECL_END

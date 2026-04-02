#pragma once
#include <vsh/vshcommon.h>
#include <vsh/stdc.h>

// Simple VSH notification wrapper.
// Shows a pop-up on the XMB.
namespace Notify
{
    inline void Info(const wchar_t* text)
    {
        vsh::ShowNofityWithSound(text, vsh::eNotifyIcon::Info, vsh::eNotifySound::OK);
    }

    inline void Warning(const wchar_t* text)
    {
        vsh::ShowNofityWithSound(text, vsh::eNotifyIcon::Caution, vsh::eNotifySound::Error);
    }

    // format n show. buffer kept small for VSH stack constraints
    inline void InfoFmt(const wchar_t* fmt, ...)
    {
        wchar_t buf[128];
        va_list ap;
        va_start(ap, fmt);
        vsh::vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, ap);
        va_end(ap);
        Info(buf);
    }

    inline void WarningFmt(const wchar_t* fmt, ...)
    {
        wchar_t buf[128];
        va_list ap;
        va_start(ap, fmt);
        vsh::vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, ap);
        va_end(ap);
        Warning(buf);
    }
}

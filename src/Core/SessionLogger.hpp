#pragma once
#include <string>
#include <stdint.h>

class SessionLogger
{
public:
    SessionLogger();

    void StartSession(const char* titleId, const char* titleName);
    void EndSession();
    void SampleMetrics();
    bool IsActive() const { return m_Active; }

private:
    void FlushBuffer();
    void WriteHeader(const char* titleId, const char* titleName);

    bool m_Active = false;
    std::string m_FilePath;
    uint64_t m_SessionStartMs = 0;

    static const int BUFFER_MAX = 4096;
    char m_Buffer[BUFFER_MAX];
    int m_BufferUsed = 0;
    int m_RowCount = 0;
};

extern SessionLogger g_SessionLogger;

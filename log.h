#ifndef LOG_H
#define LOG_H

#include <windows.h>

static HANDLE g_handleCrashFile = nullptr;

int writeLog(const char *format, ...)
{
    char szBuffer[1024];
    int retValue;
    va_list ap;

    va_start(ap, format);
    retValue = _vsnprintf(szBuffer, sizeof szBuffer, format, ap);
    va_end(ap);

    DWORD cbWritten;
    const char *szText = szBuffer;
    while (*szText != '\0') {
        const char *p = szText;
        while (*p != '\0' && *p != '\n') {
            ++p;
        }
        WriteFile(g_handleCrashFile, szText, p - szText, &cbWritten, 0);
        if (*p == '\n') {
            WriteFile(g_handleCrashFile, "\r\n", 2, &cbWritten, 0);
            ++p;
        }
        szText = p;
    };

    return retValue;
}

#endif // LOG_H

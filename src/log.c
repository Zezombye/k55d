#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

static FILE *log_file;
static CRITICAL_SECTION log_lock;

void log_open(void)
{
    InitializeCriticalSection(&log_lock);
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n == MAX_PATH)
        fatal("GetModuleFileName failed (error %lu)", GetLastError());
    strcpy(strrchr(path, '\\') + 1, "k55d.log");
    log_file = fopen(path, "w");
    if (!log_file) {
        MessageBoxA(NULL, "Cannot open k55d.log for writing.", "k55d", MB_ICONERROR);
        ExitProcess(1);
    }
}

static void log_va(const char *fmt, va_list ap)
{
    if (!log_file)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&log_lock);
    fprintf(log_file, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    vfprintf(log_file, fmt, ap);
    fputc('\n', log_file);
    fflush(log_file);
    LeaveCriticalSection(&log_lock);
}

void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_va(fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_msg("FATAL: %s", buf);
    MessageBoxA(NULL, buf, "k55d: fatal error", MB_ICONERROR | MB_SYSTEMMODAL);
    ExitProcess(1);
}

static DWORD WINAPI alert_thread(LPVOID text)
{
    MessageBoxA(NULL, text, "k55d", MB_ICONWARNING | MB_SYSTEMMODAL);
    free(text);
    return 0;
}

void alert(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_msg("ERROR: %s", buf);
    // The message box runs in its own thread, because the caller is often the device thread, which must not stop.
    HANDLE t = CreateThread(NULL, 0, alert_thread, _strdup(buf), 0, NULL);
    if (!t)
        fatal("CreateThread failed (error %lu)", GetLastError());
    CloseHandle(t);
}

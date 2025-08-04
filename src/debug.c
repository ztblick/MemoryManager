//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/debug.h"

#include <stdio.h>

VOID fatal_error(char *msg)
{
    if (msg == NULL) {
        msg = "system unexpectedly terminated";
    }
    DWORD error_code = GetLastError();
    LPVOID error_msg;
    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &error_msg,
            0, NULL );

    printf(COLOR_RED "fatal error" COLOR_RESET " : %s\n" COLOR_RED "%s" COLOR_RESET "\n", msg, (char*)error_msg);
    fflush(stdout);
    DebugBreak();
    TerminateProcess(GetCurrentProcess(), 1);
}

#include <winternl.h>  // for RtlCaptureStackBackTrace
#pragma comment(lib, "ntdll.lib")


void log_stack_trace(ULONG64 disk_slot, PULONG_PTR pfn, PULONG_PTR pte) {
    // Atomically bump our write index (wraparound via modulo)
    LONG raw = InterlockedIncrement(&g_traceIndex);
    LONG idx = raw % TRACE_BUFFER_SIZE;
    if (idx < 0) idx += TRACE_BUFFER_SIZE;  // guard against negative modulo

    // Capture up to MAX_STACK_FRAMES, skipping this Log function (skip 1)
    USHORT count = RtlCaptureStackBackTrace(
        1,                  // Frames to skip
        MAX_STACK_FRAMES,   // Max frames to capture
        g_traceBuffer[idx].frames,
        NULL                // optionally retrieve hash
    );

    // Populate metadata
    g_traceBuffer[idx].frameCount = count;
    g_traceBuffer[idx].threadId   = GetCurrentThreadId();
    g_traceBuffer[idx].timestamp  = GetTickCount64();
    g_traceBuffer[idx].disk_slot = disk_slot;
    g_traceBuffer[idx].pfn = pfn;
    g_traceBuffer[idx].pte   = pte;
}


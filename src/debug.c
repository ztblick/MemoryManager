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

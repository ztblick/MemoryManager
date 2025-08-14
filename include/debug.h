//
// Created by zblickensderfer on 5/6/2025.
//

#pragma once
#include "threads.h"
#include <winternl.h>  // for RtlCaptureStackBackTrace
#pragma comment(lib, "ntdll.lib")

/*
 * This is my central controller for editing with a debug mode. All debug settings are enabled
 * and disabled by DEBUG.
 */
#define DEBUG                      0

/*
 *  Assert provides a quick check for a given true or false value, terminating if the condition is not met.
 */
#if DEBUG
#define ASSERT(x)                   if (!(x)) {DebugBreak();}
#else
#define  ASSERT(x)                  if (!(x)) {DebugBreak();}
#endif

/*
 *  Null check provides a quick way to determine if a given variable is null, and calls fatal error
 *  with the given message parameter.
 */
#if DEBUG
#define NULL_CHECK(x, msg)       if (x == NULL) {DebugBreak();}
#else
#define NULL_CHECK(x, msg)       if (x == NULL) {DebugBreak();}
#endif

// Color settings for fatal error.
#define COLOR_RED "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

// Here is some code to help me track stack traces
#define MAX_STACK_FRAMES    16
#define TRACE_BUFFER_SIZE   1024

#define ACCEPTABLE_MISS     100

#if DEBUG
typedef struct {
    DWORD    threadId;
    ULONG64  timestamp;      // e.g. GetTickCount64()
    USHORT   frameCount;
    ULONG64  disk_slot;
    PULONG_PTR pte;
    PULONG_PTR pfn;
    PVOID    frames[MAX_STACK_FRAMES];
} TraceEntry;

extern TraceEntry g_traceBuffer[TRACE_BUFFER_SIZE];
extern volatile LONG g_traceIndex;

void log_stack_trace(ULONG64 disk_slot, PULONG_PTR pfn, PULONG_PTR pte);

VOID debug_thread_function(VOID);

VOID validate_free_counts(VOID);
#endif

/*
 *  Fatal error outputs crucial information to the console. It also terminates all processes.
 */
VOID fatal_error(char *msg);
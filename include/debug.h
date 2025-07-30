//
// Created by zblickensderfer on 5/6/2025.
//

#pragma once
#include <Windows.h>

// This is my central controller for editing with a debug mode. All debug settings are enabled
// and disabled by DEBUG.
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

/*
 *  Fatal error outputs crucial information to the console. It also terminates all processes.
 */
VOID fatal_error(char *msg);

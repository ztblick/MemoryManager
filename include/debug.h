//
// Created by zblickensderfer on 5/6/2025.
//

#pragma once
#include "initializer.h"

// This is my central controller for editing with a debug mode. All debug settings are enabled
// and disabled by DEBUG.
#define DEBUG                       1


/*
 *  Assert provides a quick check for a given true or false value, terminating if the condition is not met.
 */
#if DEBUG
#define ASSERT(x)                   if (!x) { fatal_error("Assert failed.");}
#else
#define  ASSERT(x)
#endif

/*
 *  Null check provides a quick way to determine if a given variable is null, and calls fatal error
 *  with the given message parameter.
 */
#define NULL_CHECK(x, msg)       if (x == NULL) {fatal_error(msg); }

// Color settings for fatal error.
#define COLOR_RED "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

/*
 *  Fatal error outputs crucial information to the console. It also terminates all processes.
 */
VOID fatal_error(char *msg);

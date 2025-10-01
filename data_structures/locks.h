//
// Created by zachb on 7/25/2025.
//

#include <Windows.h>
#include "../utils/debug.h"

#pragma once

#define LOCK_SIZE_IN_BYTES  2
#define LOCK_SIZE_IN_BITS   (LOCK_SIZE_IN_BYTES << 3)   // 8 bits per byte

#define UNLOCKED        0
#define LOCKED          1

#define MAX_WAIT_TIME_BEFORE_RETRY  64

#define LOCKED_BIT      1
#define VERSION_MASK    ((1 << LOCK_SIZE_IN_BITS) - 0x2)
#define VERSION_SHIFT   0x02

typedef struct {
    volatile USHORT semaphore;
} BYTE_LOCK, *PBYTE_LOCK;

VOID initialize_byte_lock(PBYTE_LOCK lock);

VOID lock(PBYTE_LOCK lock);

BOOL try_lock(PBYTE_LOCK lock);

VOID unlock(PBYTE_LOCK lock);

VOID wait(ULONG time);
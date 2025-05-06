//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>

// This is my central controller for editing with a debug mode. All debug settings are enabled
// and disabled by DEBUG.
#define DEBUG                       1

#if DEBUG
#define assert(x)                   if (!x) { printf("error");}
#else
#define  assert(x)
#endif
#define NULL_CHECK(x, msg)       if (x == NULL) {fatal_error(msg); }

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

// These will change as we decide how many pages to write out or read from to disk at once.
#define MAX_WRITE_BATCH_SIZE        1
#define MAX_READ_BATCH_SIZE         1
#define WRITE_BATCH_SIZE            1
#define READ_BATCH_SIZE             1


// This is intentionally a power of two so we can use masking to stay within bounds.
#define VIRTUAL_ADDRESS_SIZE                            MB(16)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))
#define NUM_PTEs                                        (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

// Deliberately use a physical page pool that is approximately 1% of the VA space.
// This is, initially, 64 physical pages.
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

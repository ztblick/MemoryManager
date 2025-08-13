//
// Created by zachb on 8/6/2025.
//

#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "debug.h"

// This is our overarching VM state struct, which will package together information
// about the defining characteristics of each test, such as the number of physical pages
// and the base of the VA region.
typedef struct __vm_state {
    // This is the number of threads that run the simulating thread -- which become fault-handling threads.
    ULONG num_user_threads;
    ULONG num_free_lists;

    // This is the number of iterations each thread will run, as set by a command-line argument.
    ULONG64 iterations;

    // VA spaces
    PULONG_PTR application_va_base;
    PULONG_PTR kernel_write_va;

    // PFN Data Structures
    ULONG_PTR max_frame_number;
    ULONG_PTR min_frame_number;
    PULONG_PTR allocated_frame_numbers;
    ULONG_PTR allocated_frame_count;

    // Handles
    HANDLE physical_page_handle;

    // Parameter to provide VirtualAlloc2 with the ability to create a VA space
    // that supports multiple VAs to map to the same shared page.
    MEM_EXTENDED_PARAMETER virtual_alloc_shared_parameter;
} VM, *PVM;

// Our global vm variable
extern VM vm;

// Statistics struct
typedef struct __stats {
    volatile LONG64 n_available;
    volatile LONG64 *n_free;
    volatile LONG64 *n_modified;
    volatile LONG64 *n_standby;
    volatile LONG64 n_hard;
    volatile LONG64 n_soft;
} STATS, *PSTATS;

// Our global statistics variable
extern STATS stats;

// These are the number of threads running background tasks for the system -- scheduler, trimmer, writer
#define NUM_SCHEDULING_THREADS          0
#define NUM_AGING_THREADS               0
#define NUM_TRIMMING_THREADS            1
#define NUM_WRITING_THREADS             1

// Default values for variables
#define DEFAULT_NUM_USER_THREADS        8
#define DEFAULT_ITERATIONS              (MB(1))
#define DEFAULT_FREE_LIST_COUNT         16

// We will begin trimming and writing when our standby + free page count falls below this threshold.
#define START_TRIMMING_THRESHOLD        (NUMBER_OF_PHYSICAL_PAGES / 8)
// Trimming will stop when we fall below our active page threshold
#define ACTIVE_PAGE_THRESHOLD           (NUMBER_OF_PHYSICAL_PAGES * 3 / 4)
// We will begin writing when the modified list has sufficient pages!
#define BEGIN_WRITING_THRESHOLD         (NUMBER_OF_PHYSICAL_PAGES / 32)

// These will change as we decide how many pages to write, read, or trim at once.
#define MAX_WRITE_BATCH_SIZE            512
#define MIN_WRITE_BATCH_SIZE            1
#define MAX_READ_BATCH_SIZE             1
#define MAX_TRIM_BATCH_SIZE             512
#define MAX_FREE_BATCH_SIZE             1

// Pages in memory and page file, which are used to calculate VA span
#define NUMBER_OF_PHYSICAL_PAGES        (KB(256))
#define PAGES_IN_PAGE_FILE              (KB(128))

// We create a VA space that is never too large -- otherwise, we would run out of memory!
// The -2 takes into account (1) that we need our VA space to be one smaller than our physical
// space, as we will always need one page in memory to support movement between memory and disk, and
// (2) that our page file does not permit ZERO as an index (as it is indistinguishable from a zeroed PTE).
// So, we will always set the first bit in our page file bitmap, invalidating that location and wasting one
// page of disk space (oh no, 4KB lost, so sad).
#define VA_SPAN                                         (NUMBER_OF_PHYSICAL_PAGES + PAGES_IN_PAGE_FILE - 2)
#define VIRTUAL_ADDRESS_SIZE                            (VA_SPAN * PAGE_SIZE)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

#define PAGE_SIZE                       4096
#define KB(x)                           ((x) * 1024ULL)         // ULL in case our operation exceeds 2^32 - 1
#define MB(x)                           (KB((x)) * 1024)
#define GB(x)                           (MB((x)) * 1024)
#define BITS_PER_BYTE                   8
#define BYTES_PER_VA                    8


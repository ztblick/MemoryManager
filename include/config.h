//
// Created by zachb on 8/6/2025.
//

#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

// These switches turn on particular configurations for the program.
#define LOGGING_MODE                0       // Outputs statistics to the console
#define RUN_FOREVER                 0       // Does not stop user threads
#define STATS_MODE                  0       // Collects data on page consumption
#define AGING                       1       // Initiates aging of PTEs when accessed / trimmed
#define SCHEDULING                  0       // Adds a scheduling thread

// This is our overarching VM state struct, which will package together information
// about the defining characteristics of each test, such as the number of physical pages
// and the base of the VA region.
typedef struct __vm_state {
    // This is the number of threads that run the simulating thread -- which become fault-handling threads.
    ULONG num_user_threads;
    ULONG num_free_lists;

    // Command line arguments set these values, initially
    LONG64 pages_in_page_file;

    // This is the number of iterations each thread will run, as set by a command-line argument.
    ULONG64 iterations;

    // VA spaces
    ULONG64 va_size_in_bytes;
    ULONG64 va_size_in_pointers;
    PULONG_PTR application_va_base;
    PULONG_PTR kernel_write_va;
    ULONG64 num_ptes;

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
    LONG64 writer_batch_target;
    LONG64 trimmer_batch_target;
    volatile LONG64 n_available;
    volatile LONG64 *n_free;
    volatile LONG64 *n_modified;
    volatile LONG64 *n_standby;
    volatile LONG64 n_hard;
    volatile LONG64 n_soft;
    volatile LONG64 wait_time;
    volatile LONG64 hard_faults_missed;
    LONGLONG timer_frequency;
} STATS, *PSTATS;

// Our global statistics variable
extern STATS stats;

// Default values for variables
#define DEFAULT_NUM_USER_THREADS        8
#define DEFAULT_ITERATIONS              (MB(1))
#define DEFAULT_FREE_LIST_COUNT         16

// We will begin trimming and writing when our standby + free page count falls below this threshold.
#define START_TRIMMING_THRESHOLD        (10000)

// These will change as we decide how many pages to write, read, or trim at once.
#define MAX_WRITE_BATCH_SIZE            4096
#define MIN_WRITE_BATCH_SIZE            16
#define MAX_READ_BATCH_SIZE             1
#define MAX_TRIM_BATCH_SIZE             512
#define MAX_FREE_BATCH_SIZE             1

// Default pages in memory and page file, which are used to calculate VA span
#define DEFAULT_NUMBER_OF_PHYSICAL_PAGES        (KB(256))
#define DEFAULT_PAGES_IN_PAGE_FILE              (KB(128))

// We create a VA space that is never too large -- otherwise, we would run out of memory!
// Here, we set a ratio of how much less space is in the user's VA space relative to the
// configuration of our page file and our memory.
#define VA_TO_PHYSICAL_RATIO            (0.91)
#define VA_SPAN(x, y)                   ((int) (VA_TO_PHYSICAL_RATIO * (x + y)))

#define PAGE_SIZE                       4096
#define KB(x)                           ((x) * 1024ULL)         // ULL in case our operation exceeds 2^32 - 1
#define MB(x)                           (KB((x)) * 1024)
#define GB(x)                           (MB((x)) * 1024)
#define BITS_PER_BYTE                   8
#define BYTES_PER_VA                    8
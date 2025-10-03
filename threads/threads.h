//
// Created by zachb on 8/6/2025.
//

#pragma once

#include "../utils/config.h"

// Thread IDs
#define TRIMMING_THREAD_ID      0
#define WRITING_THREAD_ID       1
#define PRUNING_THREAD_ID       2
#define SCHEDULING_THREAD_ID    3
#define AGING_THREAD_ID         4

// Thread information
#define DEFAULT_SECURITY                ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE              0
#define DEFAULT_CREATION_FLAGS          0
#define AUTO_RESET                      FALSE
#define MANUAL_RESET                    TRUE

#define ACTIVE_EVENT_INDEX              0
#define EXIT_EVENT_INDEX                1

#define NUM_KERNEL_READ_ADDRESSES       (16)

// The size of each user thread's free page cache
#define FREE_PAGE_CACHE_SIZE            64

// Our smoothing factor, which helps us generate the exponential moving weighted average
// for the thread runtimes (supporting scheduling)
#define EWMA_SMOOTHING_FACTOR           0.5

// User thread struct. This will contain a set of kernel VA spaces. Each thread
// will manage many, which will allow us to remove locks and contention on them.
// Additionally, it will allow us to delay unmap calls, giving us the opportunity
// to batch them.
typedef struct _USER_THREAD_INFO {
    ULONG thread_id;
    ULONG kernel_va_index;
    PULONG_PTR kernel_va_space;
    ULONG64 random_seed;
    PVOID free_page_cache[FREE_PAGE_CACHE_SIZE];
    USHORT free_page_count;
} USER_THREAD_INFO, *PUSER_THREAD_INFO;

// Events
extern HANDLE system_start_event;
extern HANDLE initiate_aging_event;
extern HANDLE initiate_trimming_event;
extern HANDLE initiate_writing_event;
extern HANDLE initiate_pruning_event;
extern HANDLE standby_pages_ready_event;
extern HANDLE system_exit_event;

// Thread handles
extern PHANDLE user_threads;
extern HANDLE scheduling_thread;
extern HANDLE aging_thread;
extern HANDLE trimming_thread;
extern HANDLE writing_thread;
extern HANDLE pruning_thread;
extern HANDLE debug_thread;

// Thread IDs
extern PULONG user_thread_ids;
extern PULONG worker_thread_ids;

// The info struct for each user thread.
extern PUSER_THREAD_INFO user_thread_info;

#define NUMBER_OF_SAMPLES       512

typedef struct {
    ULONG64 batch_size;
    double time_in_seconds;
} batch_sample;

typedef struct {
    batch_sample data[NUMBER_OF_SAMPLES];
    USHORT head;
} sample_buffer;

extern sample_buffer trim_samples;
extern sample_buffer write_samples;

/*
    Records information statistical information about the previous batch of
    trimming or writing.
 */
VOID record_batch_size_and_time(double time_in_fractional_seconds,
                                ULONG64 batch_size,
                                ULONG thread_id);

/*
    Adds sample to circular buffer of batch data
 */
VOID push_to_samples(batch_sample *sample, ULONG thread_id);

/*
 *  Prints out data on statistics
 */
VOID analyze_and_print_statistics(ULONG thread_id);

/*
    Updates the estimated job time using the most-recent job time.
    Calculates the estimate using weighted moving averages (EWMA).
 */
VOID update_estimated_job_time(USHORT thread_id, double);

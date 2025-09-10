//
// Created by zachb on 8/6/2025.
//

#pragma once

#include "config.h"

// Thread information
#define DEFAULT_SECURITY                ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE              0
#define DEFAULT_CREATION_FLAGS          0
#define AUTO_RESET                      FALSE
#define MANUAL_RESET                    TRUE

#define USER_STATE_INCREMENT            0
#define USER_STATE_DECREMENT            1
#define USER_STATE_RANDOM               2
#define NUM_USER_STATES                 3

#define ACTIVE_EVENT_INDEX              0
#define EXIT_EVENT_INDEX                1

#define NUM_KERNEL_READ_ADDRESSES       (16)

#define DEFAULT_WRITE_FREQUENCY         10

#define FREE_PAGE_CACHE_SIZE            64

// User thread struct. This will contain a set of kernel VA spaces. Each thread
// will manage many, which will allow us to remove locks and contention on them.
// Additionally, it will allow us to delay unmap calls, giving us the opportunity
// to batch them.
typedef struct _USER_THREAD_INFO {
    USHORT state;                                                // Indicates how the user thread is accessing VAs.
    ULONG thread_id;
    ULONG kernel_va_index;
    PULONG_PTR kernel_va_spaces[NUM_KERNEL_READ_ADDRESSES];
    ULONG64 random_seed;
    PVOID free_page_cache[FREE_PAGE_CACHE_SIZE];
    USHORT free_page_count;
} THREAD_INFO, *PTHREAD_INFO;

// Events
extern HANDLE system_start_event;
extern HANDLE initiate_aging_event;
extern HANDLE initiate_trimming_event;
extern HANDLE initiate_writing_event;
extern HANDLE standby_pages_ready_event;
extern HANDLE system_exit_event;

// Thread handles
extern PHANDLE user_threads;
extern HANDLE scheduling_thread;
extern HANDLE aging_thread;
extern HANDLE trimming_thread;
extern HANDLE writing_thread;
extern HANDLE debug_thread;

// Thread IDs
extern PULONG user_thread_ids;
extern ULONG scheduling_thread_id;
extern ULONG aging_thread_id;
extern ULONG trimming_thread_id;
extern ULONG writing_thread_id;

// The frequency for the writing thread
extern ULONG64 trim_and_write_frequency;

// The info struct for each user thread.
extern PTHREAD_INFO user_thread_info;

// The transition probabilities for the different states
extern double transition_probabilities[NUM_USER_STATES][NUM_USER_STATES];

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
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

#define ACTIVE_EVENT_INDEX    0
#define EXIT_EVENT_INDEX      1

#define NUM_KERNEL_READ_ADDRESSES   (16)

// User thread struct. This will contain a set of kernel VA spaces. Each thread
// will manage many, which will allow us to removes locks and contention on them.
// Additionally, it will allow us to delay unmap calls, giving us the opportunity
// to batch them.
typedef struct _USER_THREAD_INFO {

    ULONG kernel_va_index;
    PULONG_PTR kernel_va_spaces[NUM_KERNEL_READ_ADDRESSES];
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
extern PHANDLE scheduling_threads;
extern PHANDLE aging_threads;
extern PHANDLE trimming_threads;
extern PHANDLE writing_threads;

// Thread IDs
extern PULONG user_thread_ids;
extern PULONG scheduling_thread_ids;
extern PULONG aging_thread_ids;
extern PULONG trimming_thread_ids;
extern PULONG writing_thread_ids;
//
// Created by zachb on 8/7/2025.
//

#include "../include/threads.h"

// Events
HANDLE system_start_event;
HANDLE initiate_aging_event;
HANDLE initiate_trimming_event;
HANDLE initiate_writing_event;
HANDLE standby_pages_ready_event;
HANDLE system_exit_event;

// Thread handles
PHANDLE user_threads;
PHANDLE scheduling_threads;
PHANDLE aging_threads;
PHANDLE trimming_threads;
PHANDLE writing_threads;

// Thread IDs
PULONG user_thread_ids;
PULONG scheduling_thread_ids;
PULONG aging_thread_ids;
PULONG trimming_thread_ids;
PULONG writing_thread_ids;
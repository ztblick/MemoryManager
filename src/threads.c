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
HANDLE scheduling_thread;
HANDLE aging_thread;
HANDLE trimming_thread;
HANDLE writing_thread;
HANDLE debug_thread;

// Thread IDs
PULONG user_thread_ids;
ULONG scheduling_thread_id = 0;
ULONG aging_thread_id = 1;
ULONG trimming_thread_id = 2;
ULONG writing_thread_id = 3;
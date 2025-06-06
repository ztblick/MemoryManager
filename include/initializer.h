//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>
#include "pfn.h"
#include "page_list.h"

#define NUM_SYSTEM_THREADS          0        // TODO Remove me
#define NUM_SCHEDULING_THREADS      1
#define NUM_AGING_THREADS           0
#define NUM_TRIMMING_THREADS        1
#define NUM_WRITING_THREADS         1


// This is the number of threads that run the simulating thread -- which become fault-handling threads.
#define NUM_USER_THREADS            2

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)
#define BITS_PER_BYTE               8
#define BYTES_PER_VA                8

// This is the number of times the system is tested.
#define NUM_TESTS                   1

// This is the number of times the simulator will access a VA.
#define ITERATIONS                  10000

// These will change as we decide how many pages to write out or read from to disk at once.
#define MAX_WRITE_BATCH_SIZE        1
#define MAX_READ_BATCH_SIZE         1
#define WRITE_BATCH_SIZE            1
#define READ_BATCH_SIZE             1

// Pages in memory and page file, which are used to calculate VA span
#define NUMBER_OF_PHYSICAL_PAGES        64
#define PAGES_IN_PAGE_FILE              64

// This is intentionally a power of two so we can use masking to stay within bounds.
#define VA_SPAN                                         (NUMBER_OF_PHYSICAL_PAGES + PAGES_IN_PAGE_FILE - 1)
#define VIRTUAL_ADDRESS_SIZE                            (VA_SPAN * PAGE_SIZE)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

// Thread information
#define DEFAULT_SECURITY        ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE      0
#define DEFAULT_CREATION_FLAGS  0
#define AUTO_RESET              FALSE
#define MANUAL_RESET            TRUE

// PFN Data Structures
PPFN PFN_array;
ULONG_PTR max_frame_number;
ULONG_PTR min_frame_number;
PULONG_PTR allocated_frame_numbers;
ULONG_PTR allocated_frame_count;

// Page lists
PAGE_LIST zero_list;
PAGE_LIST free_list;
PAGE_LIST modified_list;
PAGE_LIST standby_list;

// VA spaces
PULONG_PTR application_va_base;
PULONG_PTR kernel_write_va;
PULONG_PTR kernel_read_va;

// PTEs
PPTE PTE_base;

// The initial trimmer_offset in the PTE region for the trimmer -- this will change over time.
ULONG trimmer_offset;

// Page File and Page File Metadata
char* page_file;
char* page_file_metadata;
UINT64 empty_disk_slots;

#define DISK_SLOT_IN_USE    1
#define DISK_SLOT_EMPTY     0

// Handles
HANDLE physical_page_handle;

// Events
HANDLE initiate_trimming_event;
HANDLE initiate_writing_event;
HANDLE standby_pages_ready_event;
HANDLE system_start_event;
HANDLE system_exit_event;

// Locks
CRITICAL_SECTION page_fault_lock;
CRITICAL_SECTION kernal_read_lock;
CRITICAL_SECTION kernal_write_lock;

// Threads
PHANDLE user_threads;
PHANDLE system_threads;
PULONG user_thread_ids;
PULONG system_thread_ids;

// Statistics
ULONG64 free_page_count;
ULONG64 active_page_count;
ULONG64 modified_page_count;
ULONG64 standby_page_count;
ULONG64 faults_unresolved;
ULONG64 hard_faults_resolved;
ULONG64 soft_faults_resolved;

/*
 *  Malloc the given amount of space, then zero the memory.
 */
PULONG_PTR zero_malloc(size_t bytes_to_allocate);

/*
 *  Initialize all data structures, as declared above.
 */
void initialize_system(void);

/*
 *  Find the largest physical frame number.
 */
void set_max_frame_number(void);

/*
 *  Frees all data allocated by initializer.
 */
void free_all_data(void);

/*
 *  Maps the given page (or pages) to the given VA.
 */
void map_pages(int num_pages, PULONG_PTR va, PULONG_PTR frame_numbers);

/*
 *  Un-maps the given page from the given VA.
 */
void unmap_pages(int num_pages, PULONG_PTR va);

/*
 *  Unmaps all physical pages when user app is terminated.
 */
void unmap_all_pages(void);

/*
 *  Boilerplate Landy code for multiple threads -- not to be concerned with at this time.
 */
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

BOOL GetPrivilege (VOID);
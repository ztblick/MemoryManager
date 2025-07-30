//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>
#include "pfn.h"
#include "page_list.h"

// This is the number of threads that run the simulating thread -- which become fault-handling threads.
ULONG64 num_user_threads;

// These are the number of threads running background tasks for the system -- scheduler, trimmer, writer
#define NUM_SCHEDULING_THREADS          1
#define NUM_AGING_THREADS               0
#define NUM_TRIMMING_THREADS            1
#define NUM_WRITING_THREADS             1

#define PAGE_SIZE                       4096
#define KB(x)                           ((x) * 1024)
#define MB(x)                           (KB((x)) * 1024)
#define GB(x)                           (MB((x)) * 1024)
#define BITS_PER_BYTE                   8
#define BYTES_PER_VA                    8

// We will begin trimming and writing when our standby + free page count falls below this threshold.
#define STANDBY_PAGE_THRESHOLD          (NUMBER_OF_PHYSICAL_PAGES / 8)
// Trimming will stop when we fall below our active page threshold
#define ACTIVE_PAGE_THRESHOLD           (NUMBER_OF_PHYSICAL_PAGES * 3 / 4)
// We will begin writing when the modified list has sufficient pages!
#define BEGIN_WRITING_THRESHOLD         (NUMBER_OF_PHYSICAL_PAGES / 32)

// These will change as we decide how many pages to write, read, or trim at once.
#define MAX_WRITE_BATCH_SIZE            (512)
#define MIN_WRITE_BATCH_SIZE            1

#define MAX_READ_BATCH_SIZE             1

#define MAX_TRIM_BATCH_SIZE             (512)
#define MAX_FREE_BATCH_SIZE             1

// Pages in memory and page file, which are used to calculate VA span
#define NUMBER_OF_PHYSICAL_PAGES        (KB(256))
#define PAGES_IN_PAGE_FILE              (KB(128))

// This is intentionally a power of two so we can use masking to stay within bounds.
#define VA_SPAN                                         (NUMBER_OF_PHYSICAL_PAGES + PAGES_IN_PAGE_FILE - 1)
#define VIRTUAL_ADDRESS_SIZE                            (VA_SPAN * PAGE_SIZE)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

// Thread information
#define DEFAULT_SECURITY                ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE              0
#define DEFAULT_CREATION_FLAGS          0
#define AUTO_RESET                      FALSE
#define MANUAL_RESET                    TRUE

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

// Page File and Page File Metadata
// This is the first acceptable disk index, since 0 is reserved to encode the empty slot.
#define MIN_DISK_INDEX                  1
#define MAX_DISK_INDEX                  PAGES_IN_PAGE_FILE

char* page_file;
char* page_file_metadata;
ULONG64 empty_disk_slots;

#define DISK_SLOT_IN_USE    1
#define DISK_SLOT_EMPTY     0

// Handles
HANDLE physical_page_handle;

// Parameter to provide VirtualAlloc2 with the ability to create a VA space
// that supports multiple VAs to map to the same shared page.
extern MEM_EXTENDED_PARAMETER virtual_alloc_shared_parameter;

#define ACTIVE_EVENT_INDEX    0
#define EXIT_EVENT_INDEX      1

// Events
HANDLE system_start_event;
HANDLE initiate_aging_event;
HANDLE initiate_trimming_event;
HANDLE initiate_writing_event;
HANDLE standby_pages_ready_event;
HANDLE system_exit_event;

// Locks
CRITICAL_SECTION kernel_read_lock;
CRITICAL_SECTION kernel_write_lock;
PCRITICAL_SECTION disk_metadata_locks;

// Thread handles
PHANDLE user_threads;
PHANDLE scheduling_threads;
PHANDLE aging_threads;
PHANDLE trimming_threads;
PHANDLE writing_threads;

// Notification global variables
volatile LONG64 trimmer_exit_flag;
volatile LONG64 writer_exit_flag;
#define SYSTEM_RUN          0
#define SYSTEM_SHUTDOWN     1

// Thread IDs
PULONG user_thread_ids;
PULONG scheduling_thread_ids;
PULONG aging_thread_ids;
PULONG trimming_thread_ids;
PULONG writing_thread_ids;

// Statistics
volatile LONG64 free_page_count;
volatile LONG64 active_page_count;
volatile LONG64 modified_page_count;
volatile LONG64 standby_page_count;
volatile LONG64 hard_faults_resolved;
volatile LONG64 soft_faults_resolved;

/*
 *  Malloc the given amount of space, then zero the memory.
 */
PULONG_PTR zero_malloc(size_t bytes_to_allocate);

/*
 *  Initialize a lock with a spin count that is high enough to show meaningful data on xperf.
 */
void initialize_lock(PCRITICAL_SECTION);

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
void map_pages(ULONG64 num_pages, PULONG_PTR va, PULONG_PTR frame_numbers);

/*
 *  Un-maps the given page from the given VA.
 */
void unmap_pages(ULONG64 num_pages, PULONG_PTR va);

/*
 *  Unmaps all physical pages when user app is terminated.
 */
void unmap_all_pages(void);

/*
 *  This includes libraries for the linker which are used to support multple VAs concurrently mapped to the same
 *  allocated physical page for the process.
 */
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "onecore.lib")

BOOL GetPrivilege (VOID);
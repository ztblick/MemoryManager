//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>
#include "pfn.h"
#include "page_list.h"

// This is the number of threads that run the simulating thread -- which become fault-handling threads.
ULONG64 num_user_threads;

// This is the number of iterations each thread will run, as set by a command-line argument.
ULONG64 iterations;

// These are the number of threads running background tasks for the system -- scheduler, trimmer, writer
#define NUM_SCHEDULING_THREADS          0
#define NUM_AGING_THREADS               0
#define NUM_TRIMMING_THREADS            1
#define NUM_WRITING_THREADS             1

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

// Details for the page file and the bitmaps that describe it:
#define BITS_PER_BITMAP_ROW             64
#define PAGE_FILE_BITMAP_ROWS           (PAGES_IN_PAGE_FILE / BITS_PER_BITMAP_ROW)
#define BITMAP_ROW_FULL                 MAXULONG64
#define BITMAP_ROW_EMPTY                0ULL

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

// PTEs
PPTE PTE_base;

// Page File and Page File Metadata     --  Neither of these values correspond with valid disk indices,
// as 0 is always set to be filled, and the number of pages in the page file is one greater than the largest index.
#define MIN_DISK_INDEX                  0
#define MAX_DISK_INDEX                  (PAGES_IN_PAGE_FILE - 1)

char* page_file;
PULONG64 page_file_bitmaps;
volatile LONG64 empty_disk_slots;
PULONG64 slot_stack;                    // TODO turn this into a page file struct
ULONG64 last_checked_bitmap_row;
ULONG64 num_stashed_slots;

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
CRITICAL_SECTION kernel_write_lock;

// Thread handles
PHANDLE user_threads;
PHANDLE scheduling_threads;
PHANDLE aging_threads;
PHANDLE trimming_threads;
PHANDLE writing_threads;

// Notification global variables
#define SYSTEM_RUN          0
#define SYSTEM_SHUTDOWN     1

// Thread IDs
PULONG user_thread_ids;
PULONG scheduling_thread_ids;
PULONG aging_thread_ids;
PULONG trimming_thread_ids;
PULONG writing_thread_ids;

#define NUM_KERNEL_READ_ADDRESSES   (16)

// User thread struct. This will contain a set of kernel VA spaces. Each thread
// will manage many, which will allow us to removes locks and contention on them.
// Additionally, it will allow us to delay unmap calls, giving us the opportunity
// to batch them.
typedef struct _USER_THREAD_INFO {

    ULONG kernel_va_index;
    PULONG_PTR kernel_va_spaces[NUM_KERNEL_READ_ADDRESSES];
} THREAD_INFO, *PTHREAD_INFO;

// Statistics
volatile LONG64 n_available;

volatile PULONG64 n_free;
volatile PULONG64 n_modified;
volatile PULONG64 n_standby;
volatile LONG64 n_hard;
volatile LONG64 n_soft;

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
 *  Given a PTE, maps it (and only it) to its page.
 */
VOID map_single_page_from_pte(PPTE pte);

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
 *  Bumps the count of available pages (free + standby)
 */
VOID increment_available_count(VOID);

/*
 *  Drops the count of available pages. Initiates the trimmer, if necessary.
 *  Asserts that the count must be positive.
 */
VOID decrement_available_count(VOID);

/*
 *  This includes libraries for the linker which are used to support multple VAs concurrently mapped to the same
 *  allocated physical page for the process.
 */
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "onecore.lib")

BOOL GetPrivilege (VOID);
//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>
#include "pfn.h"

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)
#define BITS_PER_BYTE               8

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

// PFN Data Structures
PPFN PFN_array;
ULONG_PTR max_frame_number;
ULONG_PTR min_frame_number;
PULONG_PTR allocated_frame_numbers;
ULONG_PTR allocated_frame_count;

// Page lists
PLIST_ENTRY zero_list;
PLIST_ENTRY free_list;
PLIST_ENTRY modified_list;
PLIST_ENTRY standby_list;

// VA spaces
PULONG_PTR application_va_base;
PULONG_PTR kernal_write_va;
PULONG_PTR kernel_read_va;

// PTEs
PPTE PTE_base;

// The initial trimmer_offset in the PTE region for the trimmer -- this will change over time.
ULONG trimmer_offset;

// Page File and Page File Metadata
PULONG_PTR page_file;
PBYTE page_file_metadata;

// Statisitcs
ULONG64 free_page_count;
ULONG64 active_page_count;
ULONG64 modified_page_count;
ULONG64 standby_page_count;
ULONG64 faults_unresolved;
ULONG64 faults_resolved;

/*
 *  Malloc the given amount of space, then zero the memory.
 */
PULONG_PTR zero_malloc(size_t bytes_to_allocate);

/*
 *  Initialize all data structures, as declared above.
 */
void initialize_data_structures(void);

/*
 *  Find the largest physical frame number.
 */
void set_max_frame_number(void);

/*
 *  Gets the PFN for the head frame on any given list, or NULL if the list is empty.
 */
PPFN get_first_frame_from_list(PLIST_ENTRY head);

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
//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once
#include <Windows.h>
#include "pfn.h"
#include "pte.h"

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

// These will change as we decide how many pages to write out or read from to disk at once.
#define MAX_WRITE_BATCH_SIZE        1
#define MAX_READ_BATCH_SIZE         1
#define WRITE_BATCH_SIZE            1
#define READ_BATCH_SIZE             1


// This is intentionally a power of two so we can use masking to stay within bounds.
#define VIRTUAL_ADDRESS_SIZE                            MB(16)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))
#define NUM_PTEs                                        (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

// Deliberately use a physical page pool that is approximately 1% of the VA space.
// This is, initially, 64 physical pages.
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

// PFN Data Structures
PPFN PFN_array;
ULONG_PTR max_page_number;
ULONG_PTR min_page_number;
PULONG_PTR physical_page_numbers;
ULONG_PTR physical_page_count;

// Page lists
PLIST_ENTRY free_list;
PLIST_ENTRY active_list;
PLIST_ENTRY modified_list;

// VA spaces
PULONG_PTR application_va_base;
PULONG_PTR kernal_write_va;
PULONG_PTR kernal_read_va;

// PTEs
PPTE PTE_base;

// Page File and Page File Metadata
PULONG_PTR page_file;
PULONG_PTR page_file_metadata;

/*
 *  Initialize all data structures, as declared above.
 */
void initialize_data_structures(void);

/*
 *  Find the largest physical frame number.
 */
void set_max_frame_number(void);

/*
 *  Frees all data allocated by initializer.
 */
void free_all_data(void);


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
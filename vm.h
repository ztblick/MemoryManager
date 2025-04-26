//
// Created by zblickensderfer on 4/22/2025.
//

#ifndef VM_H
#define VM_H
#include <Windows.h>

// This is my central controller for editing with a debug mode. All debug settings are enabled
// and disabled by DEBUG.
#define DEBUG                       1

#if DEBUG
#define assert(x)                   if (!x) { printf("error");}
#else
#define  assert(x)
#endif

typedef struct {
    UINT64 frame_number : 40;   // 40 bits to hold the frame number
    UINT64 unused : 22;         // Remaining bits reserved for later
    UINT64 status : 1;          // 1 bit to encode transition (00) or on disk (10)
    UINT64 valid : 1;           // Valid bit -- 1 indicating PTE is valid
} VALID_PTE;

typedef struct {
    UINT64 disk_index : 22;   // 40 bits to hold the frame number
    UINT64 unused : 41;         // Remaining bits reserved for later
    UINT64 valid : 1;           // Valid bit -- 1 indicating PTE is valid
} INVALID_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disk_format;
    };
} PTE, *PPTE;

typedef struct __pfn {
    PLIST_ENTRY FLink;
    PLIST_ENTRY BLink;
    ULONG_PTR status;
    // TODO Think about reducing the size of the status field while keeping the size of PFN a power of 2
    PPTE PTE;
} PFN, *PPFN;

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

#define VIRTUAL_ADDRESS_SIZE                            MB(16)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS         (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))
#define NUM_PTEs                                        (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//
// This is, initially, 64 physical pages.
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

#endif //VM_H

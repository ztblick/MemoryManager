//
// Created by zblickensderfer on 4/26/2025.
//

#include "initializer.h"

#pragma once

#define PTE_INVALID             0
#define PTE_VALID               1
#define PTE_IN_TRANSITION       0
#define PTE_ON_DISK             1

#define NUM_PTEs                (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE)

#define STATE_BITS              5

#define DISK_INDEX_BITS         22
#define MAX_DISK_INDEX          (PAGES_IN_PAGE_FILE - 1)

#define FRAME_NUMBER_BITS       40
#define MAX_FRAME_NUMBER        ((1U << FRAME_NUMBER_BITS) - 1)

typedef struct {
    UINT64 valid : 1;                       // Valid bit -- 1 indicating PTE is valid
    UINT64 status : 1;                      // 1 bit to encode transition (0) or on disk (1)
    UINT64 readwrite : 1;                   // Read/Write bit -- 0 for read privileges, 1 for write privileges
    UINT64 dirty : 1;                       // Dirty bit -- 0 for unmodified, 1 for modified
    UINT64 accessed : 1;                    // Accessed bit -- indicates if the page has been accessed to track frequency
    UINT64 frame_number : FRAME_NUMBER_BITS;// 40 bits to hold the frame number
    UINT64 reserved : (64 - STATE_BITS - FRAME_NUMBER_BITS); // Remaining bits reserved for later
} VALID_PTE;

typedef struct {
    UINT64 valid : 1;                       // Valid bit -- 0 indicating PTE is invalid
    UINT64 status : 1;                      // 1 bit to encode transition (0) or on disk (1)
    UINT64 readwrite : 1;                   // Read/Write bit -- 0 for read privileges, 1 for write privileges
    UINT64 dirty : 1;                       // Dirty bit -- 0 for unmodified, 1 for modified
    UINT64 accessed : 1;                    // Accessed bit -- indicates if the page has been accessed to track frequency
    UINT64 disk_index : DISK_INDEX_BITS;    // 22 bits to hold the disk index
    UINT64 reserved : (64 - STATE_BITS - DISK_INDEX_BITS);  // Remaining bits reserved for later
} INVALID_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disk_format;
    };
} PTE, *PPTE;

#define IS_PTE_ZEROED(pte) ((pte)->memory_format.valid == PTE_INVALID && (pte)->memory_format.frame_number == 0)
#define IS_PTE_VALID(pte) ((pte)->memory_format.valid == PTE_VALID)
#define IS_PTE_TRANSITION(pte) ((pte)->memory_format.status == PTE_IN_TRANSITION)
#define IS_PTE_ON_DISK(pte) ((pte)->memory_format.status == PTE_ON_DISK && ((pte)->memory_format.valid == PTE_INVALID))



/*
 *  Provides translations between VAs and their associated PTEs and vice-versa.
 */
PPTE get_PTE_from_VA(PULONG_PTR va);
PVOID get_VA_from_PTE(PPTE pte);

/*
 *  Moves an active PTE into the transition state.
 */
void set_PTE_to_transition(PPTE pte);

/*
 *  Moves an invalid PTE into the valid state.
 */
void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number);

/*
 *  These will likely be replaced with a call to the page file metadata
 */
void map_pte_to_disk(PPTE pte, UINT64 disk_index);
UINT64 get_disk_index_from_pte(PPTE pte);
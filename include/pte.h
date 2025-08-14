//
// Created by zblickensderfer on 4/26/2025.
//

#pragma once

#include "locks.h"
#include "disk.h"

#define PTE_INVALID             0
#define PTE_VALID               1
#define PTE_IN_TRANSITION       0
#define PTE_ON_DISK             1

#define PTE_STATUS_BIT_FOR_VALID    0       // This is used to prevent the PTE from having a 1
                                            // (representing on disk) when read back into valid format

#define STATE_BITS              5

#define DISK_INDEX_BITS         40

#define FRAME_NUMBER_BITS       40
#define MAX_FRAME_NUMBER        ((1ULL << FRAME_NUMBER_BITS) - 1)

// This is the default value given to the frame_number field for a PTE that has no connected frame.
#define NO_FRAME_ASSIGNED       0

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
    UINT64 valid : 1;                       // Valid bit -- 1 indicating PTE is valid
    UINT64 status : 1;                      // 1 bit to encode transition (0) or on disk (1)
    UINT64 readwrite : 1;                   // Read/Write bit -- 0 for read privileges, 1 for write privileges
    UINT64 dirty : 1;                       // Dirty bit -- 0 for unmodified, 1 for modified
    UINT64 accessed : 1;                    // Accessed bit -- indicates if the page has been accessed to track frequency
    UINT64 frame_number : FRAME_NUMBER_BITS;// 40 bits to hold the frame number
    UINT64 reserved : (64 - STATE_BITS - FRAME_NUMBER_BITS); // Remaining bits reserved for later
} TRANSITION_PTE;

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
        TRANSITION_PTE transition_format;
        INVALID_PTE disk_format;
        ULONG_PTR entire_pte;
    };
    BYTE_LOCK lock;
} PTE, *PPTE;

#define IS_PTE_ZEROED(pte)      ((pte)->memory_format.valid == PTE_INVALID && (pte)->memory_format.frame_number == NO_FRAME_ASSIGNED)
#define IS_PTE_VALID(pte)       ((pte)->memory_format.valid == PTE_VALID)
#define IS_PTE_TRANSITION(pte)  ((pte)->transition_format.valid == PTE_INVALID && (pte)->transition_format.status == PTE_IN_TRANSITION && (pte)->transition_format.frame_number != NO_FRAME_ASSIGNED)
#define IS_PTE_ON_DISK(pte)     ((pte)->disk_format.valid == PTE_INVALID && (pte)->disk_format.status == PTE_ON_DISK)

/*
 *  This represents the base of our page table. For now, it is simply
 *  an array of PTEs, each of which contains information about its
 *  corresponding virtual page.
 */
extern PPTE PTE_base;

/*
 *  Initializes the above array of PTEs to be large enough to provide a PTE for each
 *  virtual page in the VA space.
 */
VOID initialize_page_table(VOID);

/*
 *  Provides a translation from the given VA to its associated PTE.
 */
PPTE get_PTE_from_VA(PULONG_PTR va);

/*
 *  Provides a translation from the given VA to its corresponding PTE.
 */
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

/*
 *  Waits until it can acquire the lock on the given PTE.
 */
VOID lock_pte(PPTE pte);

/*
 *  Attempts to lock the given PTE. Returns true if lock is acquired.
 */
BOOL try_lock_pte(PPTE pte);

/*
 *  Releases the lock held on the given PTE.
 */
VOID unlock_pte(PPTE pte);

/*
 *  Given a PTE, maps it (and only it) to its page.
 */
VOID map_single_page_from_pte(PPTE pte);
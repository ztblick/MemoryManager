//
// Created by zblickensderfer on 4/25/2025.
//

#pragma once
#include "pte.h"

// PFN Status Constants
#define PFN_FREE      0x0
#define PFN_ACTIVE    0x1
#define PFN_MODIFIED  0x2
#define PFN_STANDBY   0x3

// This is the default value given to a PFN's disk index variable. It is zero so there are never any issues
// brought up by increasing the size of the page file.
#define NO_DISK_INDEX                   0
// This is the first acceptable disk index, since 0 is reserved to encode the empty slot.
#define MIN_DISK_INDEX                  1

// Macros for easy analysis later.
#define IS_PFN_FREE(pfn)                ((pfn)->status == PFN_FREE)
#define IS_PFN_ACTIVE(pfn)              ((pfn)->status == PFN_ACTIVE)
#define IS_PFN_MODIFIED(pfn)            ((pfn)->status == PFN_MODIFIED)
#define IS_PFN_STANDBY(pfn)             ((pfn)->status == PFN_STANDBY)
#define SET_PFN_STATUS(pfn, s)          ((pfn)->status = (s))

// We need the list entry to be first, as its address is also the address of the PFN.
// Size: 72 bytes total. 1 will not completely fit in a cache line.
// TODO Later on, reduce the size of the PFN to be 64 bytes (so it can fit in a cache line).
// We can do this by making lock smaller (32 bytes, not 40)
typedef struct __pfn {
    LIST_ENTRY entry;       // Size: 16 bytes
    UINT64 status : 4;       // Size: 8 bytes
    UINT64 disk_index : 60;
    PPTE PTE;               // Size: 8 bytes
        // FYI -- The 3 least-significant bits here are always zer0, so we can save some bits with cleverness...
    CRITICAL_SECTION lock;  // Size: 40 bytes
} PFN, *PPFN;

/*
 *  Initializes a zeroed PFN. This is usually done when creating a PFN for the first time.
 */
VOID create_zeroed_pfn(PPFN new_pfn);

/*
 *  Returns frame number associated with this PFN.
 */
ULONG_PTR get_frame_from_PFN(PPFN pfn);

/*
 *  Transition PFN into its active state.
 */
VOID set_PFN_active(PPFN pfn, PPTE pte);

/*
 *  Returns PFN associated with this frame number.
 */
PPFN get_PFN_from_frame(ULONG_PTR frame_number);

/*
 *  Returns PFN associated with this PTE.
 */
PPFN get_PFN_from_PTE(PPTE pte);

/*
 *  Acquires the lock on a PFN, waiting as long as necessary.
 */
VOID lock_pfn(PPFN pfn);


/*
 *  Tries to, but does not always, acquire the lock on a PFN.
 */
BOOL try_lock_pfn(PPFN pfn);

/*
 *  Releases the lock on a PFN.
 */
VOID unlock_pfn(PPFN pfn);
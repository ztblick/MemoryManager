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

// Macros for easy analysis later.
#define IS_PFN_FREE(pfn)            ((pfn)->status == PFN_STATUS_FREE)
#define SET_PFN_STATUS(pfn, s)      ((pfn)->status = (s))

// We need the list entry to be first, as its address is also the address of the PFN.
typedef struct __pfn {
    LIST_ENTRY entry;
    ULONG_PTR status;
    // TODO Think about reducing the size of the status field while keeping the size of PFN a power of 2
    PPTE PTE;
} PFN, *PPFN;

/*
 *  Returns frame number associated with this PFN.
 */
ULONG_PTR get_frame_from_PFN(PPFN pfn);

/*
 *  Returns PFN associated with this frame number.
 */
PPFN get_PFN_from_frame(ULONG_PTR frame_number);

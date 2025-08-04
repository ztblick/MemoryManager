//
// Created by zblickensderfer on 4/25/2025.
//

#pragma once
#include "pte.h"

// PFN Status Constants
#define PFN_FREE        0x0
#define PFN_ACTIVE      0x1
#define PFN_MID_TRIM    0x2         // Indicates that the page is part of a trim batch
#define PFN_MODIFIED    0x3
#define PFN_MID_WRITE   0x4         // This is used to indicate that the page is off the modified list
                                    // but not yet on the standby list, as it is in the process of being
                                    // written out.
#define PFN_STANDBY     0x5

// This is the default value given to a PFN's disk index variable. It is zero so there are never any issues
// brought up by increasing the size of the page file.
#define NO_DISK_INDEX                   0

// These values are used to indicate whether a soft-fault happens while a page is in a transition state,
// such as mid-trim or mid-write.
#define NO_SOFT_FAULT_YET                0
#define SOFT_FAULT_OCCURRED              1

// Macros for easy analysis later.
#define IS_PFN_FREE(pfn)                ((pfn)->status == PFN_FREE)
#define IS_PFN_ACTIVE(pfn)              ((pfn)->status == PFN_ACTIVE)
#define IS_PFN_MODIFIED(pfn)            ((pfn)->status == PFN_MODIFIED)
#define IS_PFN_MID_WRITE(pfn)           ((pfn)->status == PFN_MID_WRITE)
#define IS_PFN_MID_TRIM(pfn)            ((pfn)->status == PFN_MID_TRIM)
#define IS_PFN_STANDBY(pfn)             ((pfn)->status == PFN_STANDBY)

// To easily set the status bits.
#define SET_PFN_STATUS(pfn, s)          ((pfn)->status = (s))

// We need the list entry to be first, as its address is also the address of the PFN.
// Size: 80 bytes total. 1 will not completely fit in a cache line.
// TODO Later on, reduce the size of the PFN to be 64 bytes (so it can fit in a cache line).
// We can do this by making lock smaller (32 bytes, not 40)
typedef struct __pfn {
    LIST_ENTRY entry;               // Size: 16 bytes
    UINT64 disk_index;              // Size: 8 bytes
    SHORT status;                   // Size: 8 bytes
    SHORT soft_fault_mid_write;          // Set when a soft-fault occurs during a disk write.
    SHORT soft_fault_mid_trim;          // Set when a soft-fault occurs during a batched trim.
    PPTE PTE;                       // Size: 8 bytes
        // FYI -- The 3 least-significant bits here are always zer0, so we can save some bits with cleverness...
    CRITICAL_SECTION lock;          // Size: 40 bytes
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
 *  Sets a PFN into its free state.
 */
VOID set_PFN_free(PPFN pfn);

/*
 *  Transition PFN into its active state.
 */
VOID set_PFN_active(PPFN pfn, PPTE pte);

/*
 *  Adds disk index to PFN and updates its status, all in one 64-bit write.
 *  Does not alter PTE, list entry, or lock.
 */
VOID set_pfn_standby(PPFN pfn, ULONG64 disk_index);

/*
 *  Moves the PFN into its mid-trim state. Sets the state field.
 *  Clears the soft_fault_mid_trim bit.
 */
VOID set_pfn_mid_trim(PPFN pfn);

/*
 *  Move the PFN into its mid-write state. Sets the state field.
 *  Clears the soft_fault_mid_write bit.
 */
VOID set_pfn_mid_write(PPFN pfn);

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
 *  These functions support the soft fault mid write situation, in which a disk-bound page was soft-faulted.
 *  In this case, the writer will check this bit after acquiring the page lock. If the bit is cleared, the
 *  write continues as normal. But if the bit is set, the write is aborted, and the corresponding disk slot is
 *  cleared. After this, the bit is cleared as well to reset it for the next possible occurance of this situation.
 *
 *  Precondition: pfn lock has already been acquired.
 */
VOID set_soft_fault_write_bit(PPFN pfn);
VOID set_soft_fault_trim_bit(PPFN pfn);
BOOL soft_fault_happened_mid_write(PPFN pfn);
BOOL soft_fault_happened_mid_trim(PPFN pfn);

/*
 *  Tries to, but does not always, acquire the lock on a PFN.
 */
BOOL try_lock_pfn(PPFN pfn);

/*
 *  Releases the lock on a PFN.
 */
VOID unlock_pfn(PPFN pfn);
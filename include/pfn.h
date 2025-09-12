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

#define PFN_STATUS_BITS 3

// This is the default value given to a PFN's disk index variable. It is zero so there are never any issues
// brought up by increasing the size of the page file.
#define NO_DISK_INDEX                   0

// These values are used to indicate whether a soft-fault happens while a page is in a transition state,
// such as mid-trim or mid-write.
#define NO_SOFT_FAULT_YET                0
#define SOFT_FAULT_OCCURRED              1

// Macros for easy analysis later.
#define IS_PFN_FREE(pfn)                ((pfn)->fields.status == PFN_FREE)
#define IS_PFN_ACTIVE(pfn)              ((pfn)->fields.status == PFN_ACTIVE)
#define IS_PFN_MODIFIED(pfn)            ((pfn)->fields.status == PFN_MODIFIED)
#define IS_PFN_MID_WRITE(pfn)           ((pfn)->fields.status == PFN_MID_WRITE)
#define IS_PFN_MID_TRIM(pfn)            ((pfn)->fields.status == PFN_MID_TRIM)
#define IS_PFN_STANDBY(pfn)             ((pfn)->fields.status == PFN_STANDBY)

// To easily set the status bits.
#define SET_PFN_STATUS(pfn, s)          ((pfn)->fields.status = (s))

// PFN lock data
#define PFN_LOCK_SIZE_IN_BITS            16

typedef struct __pfn_fields {
    ULONG64 lock : PFN_LOCK_SIZE_IN_BITS;
    ULONG64 disk_index : DISK_INDEX_BITS;
    ULONG64 status : PFN_STATUS_BITS;
    ULONG64 reserved : 5;
} FIELDS;

// We need the list entry to be first, as its address is also the address of the PFN.
// Size: 32 bytes total. 2 will completely fit in a cache line.
// Ideally, we will cache pages that are used by the same thread. If not, we may want to add
// 32 bytes of padding to prevent cache ping-ponging.

struct __pfn;          // forward declaration
typedef struct __pfn PFN;
typedef PFN* PPFN;

struct __pfn {
    PPFN flink;
    PPFN blink;
    PPTE PTE;                                   // Size: 8 bytes -- FYI -- The 3 least-significant bits here are always zero, so we can save some bits with cleverness...
    union {                                     // Size: 8 bytes
        FIELDS fields;
        ULONG64 raw_pfn_data;
        BYTE_LOCK lock;
    };
    char padding[32];                           // Unclear if sharing cache lines hurts of helps so far...
#if DEBUG
    CRITICAL_SECTION crit_sec;
#endif
};

/*
 *  The base of our sparse array of PFNs.
 */
extern PPFN PFN_array;

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
 *  Tries to, but does not always, acquire the lock on a PFN.
 */
BOOL try_lock_pfn(PPFN pfn);

/*
 *  Releases the lock on a PFN.
 */
VOID unlock_pfn(PPFN pfn);
//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/debug.h"
#include "../include/initializer.h"


VOID create_zeroed_pfn(PPFN new_pfn) {
    new_pfn->status = PFN_FREE;
    new_pfn->PTE = NULL;
    new_pfn->disk_index = NO_DISK_INDEX;
    new_pfn->soft_fault_on_write = 0;
    new_pfn->soft_fault_mid_trim = 0;
    initialize_lock(&new_pfn->lock);
}

ULONG_PTR get_frame_from_PFN(PPFN pfn) {
    if (pfn < PFN_array) {
        fatal_error("PFN out of bounds while attempting to get frame number from PFN.");
    }
    if (pfn - PFN_array > max_frame_number) {
        fatal_error("Max frame number exceeded in get_frame_from_pfn mapping.");
    }
    return pfn - PFN_array;
}

PPFN get_PFN_from_PTE(PPTE pte) {

    // In our rare case of a transition PTE becoming disk format during concurrent
    // soft- and hard-faults on the same standby page, we will return NULL.
    if (pte->memory_format.status == PTE_ON_DISK) return NULL;

    // Otherwise, we return our PFN!
    return get_PFN_from_frame(pte->memory_format.frame_number);
}

PPFN get_PFN_from_frame(ULONG_PTR frame_number) {
    if (frame_number < min_frame_number || frame_number > max_frame_number) {
        fatal_error("Frame number out of bounds while attempting to get PFN from frame number.");
    }
    return PFN_array + frame_number;
}

VOID set_PFN_active(PPFN pfn, PPTE pte) {
    // TODO Update this to read in all at once into the PFN to prevent word tearing

    SET_PFN_STATUS(pfn, PFN_ACTIVE);
    pfn->PTE = pte;
    pfn->disk_index = NO_DISK_INDEX;
}

VOID set_PFN_free(PPFN pfn) {

    pfn->status = PFN_FREE;
    pfn->PTE = NULL;
    pfn->disk_index = NO_DISK_INDEX;
    pfn->soft_fault_on_write = 0;
}

/*
 *  Acquires the lock on a PFN, waiting as long as necessary.
 */
VOID lock_pfn(PPFN pfn) {
    EnterCriticalSection(&pfn->lock);
}


/*
 *  Tries to, but does not always, acquire the lock on a PFN.
 */
BOOL try_lock_pfn(PPFN pfn) {
    return TryEnterCriticalSection(&pfn->lock);
}

/*
 *  Releases the lock on a PFN.
 */
VOID unlock_pfn(PPFN pfn) {
    LeaveCriticalSection(&pfn->lock);
}

VOID set_soft_fault_write_bit(PPFN pfn) {
    pfn->soft_fault_on_write = 1;
}

VOID set_soft_fault_trim_bit(PPFN pfn) {
    pfn->soft_fault_mid_trim = 1;
}

// Checks bit, clears it, then returns result.
BOOL soft_fault_happened_mid_write(PPFN pfn) {
    BOOL fault_occured = pfn->soft_fault_on_write;
    pfn->soft_fault_on_write = 0;

    return fault_occured;
}

BOOL soft_fault_happened_mid_trim(PPFN pfn) {
    BOOL fault_occured = pfn->soft_fault_mid_trim;
    pfn->soft_fault_mid_trim = 0;

    return fault_occured;
}
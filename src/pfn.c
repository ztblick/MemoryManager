//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/debug.h"
#include "../include/initializer.h"


VOID create_zeroed_pfn(PPFN new_pfn) {
    new_pfn->status = PFN_FREE;
    new_pfn->PTE = NULL;
    new_pfn->disk_index = NO_DISK_INDEX;
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
    if (pte->memory_format.status == PTE_ON_DISK) {
        fatal_error("Frame number requested from disk format PTE.");
    }
    return PFN_array + pte->memory_format.frame_number;
}

PPFN get_PFN_from_frame(ULONG_PTR frame_number) {
    if (frame_number < min_frame_number || frame_number > max_frame_number) {
        fatal_error("Frame number out of bounds while attempting to get PFN from frame number.");
    }
    return PFN_array + frame_number;
}

VOID set_PFN_active(PPFN pfn, PPTE pte) {
    SET_PFN_STATUS(pfn, PFN_ACTIVE);
    pfn->PTE = pte;
    pfn->disk_index = NO_DISK_INDEX;
}
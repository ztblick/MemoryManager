//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/debug.h"
#include "../include/initializer.h"

ULONG_PTR get_frame_from_PFN(PPFN pfn) {
    if (pfn < PFN_array) {
        fatal_error("PFN out of bounds while attempting to get frame number from PFN.");
    }
    return pfn - PFN_array;
}

PPFN get_PFN_from_frame(ULONG_PTR frame_number) {
    if (frame_number < min_frame_number || frame_number > max_frame_number) {
        fatal_error("Frame number out of bounds while attempting to get PFN from frame number.");
    }
    return PFN_array + frame_number;
}
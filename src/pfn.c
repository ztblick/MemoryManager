//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/debug.h"
#include "../include/initializer.h"

ULONG_PTR get_frame_from_PFN(PPFN pfn) {
    if (pfn < PFN_array) {
        fatal_error("PFN out of bounds while attempting to map PFN to frame number.");
    }
    return pfn - PFN_array;
}

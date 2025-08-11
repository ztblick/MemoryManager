//
// Created by zachb on 8/6/2025.
//

#include "../include/utils.h"

PULONG_PTR zero_malloc(size_t bytes_to_allocate) {
    PULONG_PTR destination = malloc(bytes_to_allocate);
    NULL_CHECK(destination, "Zero malloc failed!");
    memset(destination, 0, bytes_to_allocate);
    return destination;
}

void map_pages(ULONG64 num_pages, PULONG_PTR va, PULONG_PTR frame_numbers) {
    if (MapUserPhysicalPages (va, num_pages, frame_numbers) == FALSE) {
        fatal_error("Could not map VA to page in MapUserPhysicalPages.");
    }
}

void unmap_pages(ULONG64 num_pages, PULONG_PTR va) {
    if (MapUserPhysicalPages (va, num_pages, NULL) == FALSE) {
        fatal_error("Could not un-map old VA.");
    }
}

VOID increment_available_count(VOID) {
    InterlockedIncrement64(&stats.n_available);
}


VOID decrement_available_count(VOID) {
    ASSERT(stats.n_available > 0);
    InterlockedDecrement64(&stats.n_available);

    if (stats.n_available == START_TRIMMING_THRESHOLD) SetEvent(initiate_trimming_event);
}
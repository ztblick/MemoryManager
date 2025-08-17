//
// Created by zachb on 8/6/2025.
//

#include "../include/utils.h"

PVOID zero_malloc(size_t bytes_to_allocate) {
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

void map_both_va_to_same_page(PULONG_PTR va_one, PULONG_PTR va_two, ULONG64 frame_number) {

    PULONG_PTR va_array[2];
    ULONG64 frame_array[2];

    va_array[0] = va_one;
    va_array[1] = va_two;
    frame_array[0] = frame_number;
    frame_array[1] = frame_number;

    if (MapUserPhysicalPagesScatter(va_array, 2, frame_array) == FALSE) {
        fatal_error("Could not map both VAs to same page.");
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
    LONG64 new_count = InterlockedDecrement64(&stats.n_available);
    if (new_count == START_TRIMMING_THRESHOLD) SetEvent(initiate_trimming_event);
}

LONGLONG get_timestamp(VOID) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double get_time_difference(LONGLONG end, LONGLONG start) {
    return (double) (end - start) / stats.timer_frequency;
}
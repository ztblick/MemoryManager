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

VOID increase_available_count(LONG64 amt) {
    InterlockedAdd64(&stats.n_available, amt);
}

VOID decrease_available_count(LONG64 amt) {
    LONG64 old_value = stats.n_available;
    InterlockedAdd64(&stats.n_available, -amt);

    // If we have crossed our trimming threshold, start trimming!
    if (old_value > START_TRIMMING_THRESHOLD &&
        old_value - amt <= START_TRIMMING_THRESHOLD) {
        SetEvent(initiate_trimming_event);
    }
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
    return (double) (end - start) / (double) stats.timer_frequency;
}

uint64_t xorshift64(ULONG64 *seed) {
    uint64_t x = *seed;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *seed = x;
    return x * 0x2545F4914F6CDD1DULL;
}

PULONG_PTR get_arbitrary_va(ULONG64 *thread_random_seed) {

    // Begin at the start of our VA region.
    PULONG_PTR p = vm.application_va_base;

    ULONG64 random_value = xorshift64(thread_random_seed);

    // This is the old randomness calculation -- for comparing to
    // old runtime analysis
    // ULONG64 random_value = rand () * rand () * rand ();

    // Choose index safely (mod handles non power-of-two sizes)
    size_t index = random_value % vm.va_size_in_pointers;

    // Align down so the 8-byte store won’t straddle a page boundary
    index &= ~0x7ULL;

    return p + index;
}


PULONG_PTR get_next_va(PULONG_PTR previous_va, PTHREAD_INFO thread_info) {
    USHORT state = thread_info->state;
    PULONG_PTR new_va;

    if (state == USER_STATE_INCREMENT) {
        new_va = (previous_va + 1);
        if (new_va >= vm.application_va_base + vm.va_size_in_pointers)
            new_va = vm.application_va_base;
    }

    else if (state == USER_STATE_DECREMENT) {
        new_va = previous_va;
        if (new_va == vm.application_va_base)
            new_va = vm.application_va_base + vm.va_size_in_pointers;
        new_va = new_va - 1;
    }

    // Otherwise, we are in our random state!
    else new_va = get_arbitrary_va(&thread_info->random_seed);

    // Before we return, we will move into a new state (possibly)
    double p = (double) (xorshift64(&thread_info->random_seed) >> 11) * (1.0 / 9007199254740992.0);
    if (p < transition_probabilities[state][USER_STATE_INCREMENT])
        state = USER_STATE_INCREMENT;
    else if (p < transition_probabilities[state][USER_STATE_INCREMENT] + transition_probabilities[state][USER_STATE_DECREMENT])
        state = USER_STATE_DECREMENT;
    else
        state = USER_STATE_RANDOM;

    // Update to the new state
    thread_info->state = state;

    return new_va;
}
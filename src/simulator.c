//
// Created by zblickensderfer on 5/5/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../include/initializer.h"
#include "../include/simulator.h"
#include "../include/debug.h"
#include "../include/page_fault_handler.h"
#include "../include/scheduler.h"
#include "../include/releaser.h"

PULONG_PTR get_arbitrary_va(PULONG_PTR p) {
    // Randomly access different portions of the virtual address space.
    unsigned random_number = rand () * rand () * rand ();
    random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

    // Ensure the write to the arbitrary virtual address doesn't
    // straddle a PAGE_SIZE boundary.
    random_number &= ~0x7;
    return p + random_number;
}

void run_user_app_simulation(void) {

    // Wait for system start event before beginning!
    WaitForSingleObject(system_start_event, INFINITE);

    // Adding variables only necessary to kick off fault handler!
    PULONG_PTR arbitrary_va;
    BOOL page_faulted = FALSE;
    BOOL fault_handler_accessed_correctly = TRUE;

    // Now perform random accesses
    for (int i = 0; i < ITERATIONS; i += 1) {

        // Randomly access different portions of the virtual address space.
        arbitrary_va = get_arbitrary_va(application_va_base);

        // Attempt to write the virtual address into memory page.
        do {
            page_faulted = FALSE;
            __try {
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
            } __except (EXCEPTION_EXECUTE_HANDLER) {

                // Set faulted flag (for second try)
                page_faulted = TRUE;

                // Fault handler maps the VA to its new page
                fault_handler_accessed_correctly = page_fault_handler(arbitrary_va);

                // If we were successful, we will do allow our usermode program to continue with its goal.
                if (!fault_handler_accessed_correctly){
                    fatal_error("User app attempted to access invalid VA.");
                }
            }
        } while (page_faulted);
    }
}

void begin_system_test(void) {
    // System is initialized! Broadcast system start event.
    SetEvent(system_start_event);

    // This waits for the tests to finish running before exiting the function
    // Our controlling thread will wait for this function to finish before exiting the test and reporting stats
    WaitForMultipleObjects(NUM_USER_THREADS, user_threads, TRUE, INFINITE);

    // Test is finished! Tell all threads to stop.
    SetEvent(system_exit_event);
}

VOID main (int argc, char** argv) {

    ULONG64 cumulative_time = 0;
    for (ULONG i = 0; i < NUM_TESTS; i++) {

        // Initialize all data structures, events, threads, and handles. Get physical pages from OS.
        initialize_system();

        printf("Beginning test #%lu...\n", i);

        // Set up a timer to evaluate speed
        ULONG64 start_time = GetTickCount64();

        // Run system test. This will broadcast the system start event to all listening threads,
        // including the user threads. This begins the user app simulation, which begins faulting.
        // This function will wait for all user app threads to finish before returning.
        begin_system_test();

        // Grab the time to evaluate speed
        ULONG64 end_time = GetTickCount64();

        // Free all memory and end the simulation.
        free_all_data_and_shut_down();

        // Print statistics
        cumulative_time += end_time - start_time;
        printf("Program terminated successfully. Time elapsed: %llu milliseconds.\n", end_time - start_time);
        printf ("Finished accessing %u random virtual addresses.\n", ITERATIONS * NUM_USER_THREADS);
        printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    }
    printf("Over %u tests, we averaged %llu milliseconds, which is %llu milliseconds per thread.\n",
            NUM_TESTS,
            cumulative_time / NUM_TESTS,
            cumulative_time / NUM_TESTS / NUM_USER_THREADS);
}
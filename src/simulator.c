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
    BOOL page_faulted = FALSE;
    BOOL fault_handler_accessed_correctly = TRUE;

    // Now perform random accesses
#if RUN_FOREVER
    while (TRUE) {
#else
    for (int i = 0; i < iterations; i += 1) {
#endif
        // Randomly access different portions of the virtual address space.
        PULONG_PTR arbitrary_va = get_arbitrary_va(application_va_base);

        // Attempt to write the virtual address into memory page.
        do {
            page_faulted = FALSE;

            // Try stamping the page.
            __try {
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
                // TODO set accessed bit in PTE
            }

            // If we fault, we set this flag to go around again.
            __except (EXCEPTION_EXECUTE_HANDLER) {
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
    WaitForMultipleObjects(num_user_threads, user_threads, TRUE, INFINITE);

    // Test is finished! Tell all threads to stop.
    trimmer_exit_flag = SYSTEM_SHUTDOWN;
    writer_exit_flag = SYSTEM_SHUTDOWN;
    SetEvent(system_exit_event);

    WaitForMultipleObjects(NUM_TRIMMING_THREADS, trimming_threads, TRUE, INFINITE);
    WaitForMultipleObjects(NUM_WRITING_THREADS, writing_threads, TRUE, INFINITE);
    WaitForMultipleObjects(NUM_SCHEDULING_THREADS, scheduling_threads, TRUE, INFINITE);
}

VOID main (int argc, char** argv) {

#if RUN_FOREVER
    num_user_threads = DEFAULT_USER_THREAD_COUNT;
#else
    if (argc > 2) {
        printf("About to initiate test with %s threads running %s iterations each...\n", argv[1], argv[2]);
    } else {
        printf("No arguments passed.\n");
        return;
    }
    num_user_threads = strtol(argv[1], NULL, 10);  // Base 10
    iterations = strtol(argv[2], NULL, 10);
#endif
    // Initialize all data structures, events, threads, and handles. Get physical pages from OS.
    initialize_system();

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
    printf("Test successful. Time elapsed: %llu milliseconds.\n", end_time - start_time);
    printf ("Each of %llu threads accessed %llu VAs.\n", num_user_threads, iterations);
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}
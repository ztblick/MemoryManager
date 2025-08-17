//
// Created by ztblick on 5/5/2025.
//

#include "../include/simulator.h"

#include <sys/stat.h>

PULONG_PTR get_arbitrary_va(PULONG_PTR p) {
    // Randomly access different portions of the virtual address space.
    unsigned random_number = rand () * rand () * rand ();
    random_number %= vm.va_size_in_pointers;

    // Ensure our 8-byte stamp to the arbitrary virtual address doesn't
    // straddle a PAGE_SIZE boundary.
    random_number &= ~0x7;
    return p + random_number;
}

void run_user_app_simulation(PVOID user_thread_info) {

    // Wait for system start event before beginning!
    WaitForSingleObject(system_start_event, INFINITE);

    // Adding variables only necessary to kick off fault handler!
    BOOL page_faulted = FALSE;
    BOOL fault_handler_accessed_correctly = TRUE;

    // Now perform random accesses
#if RUN_FOREVER
    while (TRUE) {
#else
    for (int i = 0; i < vm.iterations; i += 1) {
#endif
        // Randomly access different portions of the virtual address space.
        PULONG_PTR arbitrary_va = get_arbitrary_va(vm.application_va_base);

        // Attempt to write the virtual address into memory page.
        do {
            page_faulted = FALSE;

            // Try stamping the page.
            __try {
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
            }

            // If we fault, we set this flag to go around again.
            __except (EXCEPTION_EXECUTE_HANDLER) {
                page_faulted = TRUE;

                // Fault handler maps the VA to its new page
                fault_handler_accessed_correctly = page_fault_handler(arbitrary_va, user_thread_info);

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
    WaitForMultipleObjects(vm.num_user_threads, user_threads, TRUE, INFINITE);

#if STATS_MODE
    analyze_and_print_statistics(trimming_thread_id);
    analyze_and_print_statistics(writing_thread_id);
#endif

    // Test is finished! Tell all threads to stop.
    SetEvent(system_exit_event);

    WaitForSingleObject(trimming_thread, INFINITE);
    WaitForSingleObject(writing_thread, INFINITE);
    WaitForSingleObject(scheduling_thread, INFINITE);
}

VOID main (int argc, char** argv) {

#if RUN_FOREVER
    num_user_threads = DEFAULT_USER_THREAD_COUNT;
#else
    set_defaults();

    if (argc > 2) {
        printf("About to initiate test with %s threads running\n%s iterations each...\n", argv[1], argv[2]);
        printf("~~~~~~~~~~~~~~~~~~~~~\n");
        vm.num_user_threads = strtol(argv[1], NULL, 10);  // Base 10
        vm.iterations = strtol(argv[2], NULL, 10);
    }
    else {
        printf("No arguments passed. Using defaults...\n");
    }
#endif
    // Initialize all data structures, events, threads, and handles. Get physical pages from OS.
    initialize_system();

    // Set up a timer to evaluate speed
    LONGLONG start_time = get_timestamp();

    // Run system test. This will broadcast the system start event to all listening threads,
    // including the user threads. This begins the user app simulation, which begins faulting.
    // This function will wait for all user app threads to finish before returning.
    begin_system_test();

    // Grab the time to evaluate speed
    LONGLONG end_time = get_timestamp();

    // Get total runtime in seconds
    double runtime = get_time_difference(end_time, start_time);

    // Print statistics
    printf("Test successful. Time elapsed: " COLOR_GREEN "%.3f" COLOR_RESET " seconds.\n", runtime);
    printf ("Each of %lu threads accessed %llu VAs.\n", vm.num_user_threads, vm.iterations);
    print_statistics();
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    // Free all memory and end the simulation.
    free_all_data_and_shut_down();
}
//
// Created by ztblick on 5/5/2025.
//

#include "../include/simulator.h"

#include <sys/stat.h>

void run_user_app_simulation(PTHREAD_INFO user_thread_info) {

    // Wait for system start event before beginning!
    WaitForSingleObject(system_start_event, INFINITE);

    // Adding variables only necessary to kick off fault handler!
    BOOL page_faulted = FALSE;
    BOOL fault_handler_accessed_correctly = TRUE;

    // Get a reference to the thread's randomness
    ULONG64* seed = &user_thread_info->random_seed;

    // Create the arbitrary VA to simulate user memory accesses.
    PULONG_PTR arbitrary_va = get_arbitrary_va(seed);

    // Now perform random accesses
#if RUN_FOREVER
    while (TRUE) {
#else
    for (int i = 0; i < vm.iterations; i += 1) {
#endif
        // Access different portions of the virtual address space according to the state of the user thread.
        arbitrary_va = get_next_va(arbitrary_va, user_thread_info);

        // Attempt to write the virtual address into memory page.
        do {
            page_faulted = FALSE;

            // Try stamping the page.
            __try {
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
#if AGING
                // set_accessed_bit(arbitrary_va);
#endif
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
    print_consumption_data();
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

    if (argc == 5) {
        vm.num_user_threads = strtol(argv[1], NULL, 10);  // Base 10
        vm.iterations = strtol(argv[2], NULL, 10);
        vm.allocated_frame_count = strtol(argv[3], NULL, 10);
        vm.pages_in_page_file = strtol(argv[4], NULL, 10);
        printf("Physical to Virtual ratio: %.1f%%.\n", 100 * (double) vm.allocated_frame_count / (double) VA_SPAN(vm.allocated_frame_count, vm.pages_in_page_file));
#if STATS_MODE
        printf("%d user threads\n%llu iterations each\n%llu MB of memory (%llu pages)\n%llu MB in page file (%llu pages).\n",
            vm.num_user_threads, vm.iterations,
            vm.allocated_frame_count * PAGE_SIZE / MB(1), vm.allocated_frame_count,
            vm.pages_in_page_file * PAGE_SIZE / MB(1), vm.pages_in_page_file);
        printf("~~~~~~~~~~~~~~~~~~~~~\n");
#endif
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
#if STATS_MODE
    printf ("Each of %lu threads accessed %llu VAs.\n", vm.num_user_threads, vm.iterations);
    print_statistics();
#endif
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    // Free all memory and end the simulation.
    free_all_data_and_shut_down();
}
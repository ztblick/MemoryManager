//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"

VOID print_statistics(VOID) {

    ULONG64 allocated_frame_count = vm.allocated_frame_count;

    ULONG64 active_page_count = NUMBER_OF_PHYSICAL_PAGES -
        (*stats.n_free + *stats.n_modified + *stats.n_standby);

    printf("\n");
    printf("FREE:\t\t%llu\t\t%.2f%%\n",
        *stats.n_free, 100.0 * *stats.n_free / allocated_frame_count);
    printf("ACTIVE:\t\t%llu\t\t%.2f%%\n",
        active_page_count, 100.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED:\t%llu\t\t%.2f%%\n",
        *stats.n_modified, 100.0 * *stats.n_modified / allocated_frame_count);
    printf("STANDBY:\t%llu\t\t%.2f%%\n",
        *stats.n_standby, 100.0 * *stats.n_standby / allocated_frame_count);
    // printf("\nEMPTY DISK SLOTS:\t%lld\n", pf.empty_disk_slots);
    printf("\nHARD:\t\t%llu\t\t%.2f%%\n",
        stats.n_hard, 100.0 * stats.n_hard / (stats.n_hard + stats.n_soft));
    printf("SOFT:\t\t%llu\t\t%.2f%%\n",
        stats.n_soft, 100.0 * stats.n_soft / (stats.n_hard + stats.n_soft));
}

VOID schedule_tasks(VOID) {
    DWORD status;
    ULONG64 step = 0;

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        // Print the statistics to the log, if necessary
#if LOGGING_MODE
        if (step % PRINT_FREQUENCY_IN_MILLISECONDS == 0) print_statistics();
#endif
        step++;

        // Wait for a set amount of time before scheduling again.
        // Of course, if the system exit event is received, we will immediately break out of the thread.
        status = WaitForSingleObject(system_exit_event, SCHEDULER_DELAY_IN_MILLISECONDS);

        // If the system exit event is received, we will return.
        if (status == WAIT_OBJECT_0) break;
    }
}
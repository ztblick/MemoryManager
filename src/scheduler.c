//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"

VOID print_statistics(VOID) {

    ULONG64 active_page_count = NUMBER_OF_PHYSICAL_PAGES - (*n_free + *n_modified + *n_standby);

    printf("\n");
    printf("FREE PAGE COUNT:\t%llu\tFREE PAGE PERCENTAGE:\t%.2f%%\n",
        *n_free, 100.0 * *n_free / allocated_frame_count);
    printf("ACTIVE PAGE COUNT:\t%llu\tACTIVE PAGE PERCENTAGE:\t%.2f%%\n",
        active_page_count, 100.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED PAGE COUNT:\t%llu\tMODIFIED PAGE PERCENTAGE:%.2f%%\n",
        *n_modified, 100.0 * *n_modified / allocated_frame_count);
    printf("STANDBY PAGE COUNT:\t%llu\tSTANDBY PAGE PERCENTAGE:%.2f%%\n",
        *n_standby, 100.0 * *n_standby / allocated_frame_count);
    printf("EMPTY DISK SLOTS:\t%lld\n", pf.empty_disk_slots);
    printf("\nHARD FAULTS RESOLVED:\t%llu\tSOFT FAULTS RESOLVED:\t%llu\n\n",
        n_hard, n_soft);
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
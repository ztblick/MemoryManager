//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"
#include "../include/initializer.h"
#include "../include/simulator.h"

VOID print_statistics(VOID) {
#if LOGGING_MODE
    printf("\n");
    printf("FREE PAGE COUNT:\t%llu\tFREE PAGE PERCENTAGE:\t%.2f%%\n",
        free_page_count, 100.0 * free_page_count / allocated_frame_count);
    printf("ACTIVE PAGE COUNT:\t%llu\tACTIVE PAGE PERCENTAGE:\t%.2f%%\n",
        active_page_count, 100.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED PAGE COUNT:\t%llu\tMODIFIED PAGE PERCENTAGE:%.2f%%\n",
        modified_page_count, 100.0 * modified_page_count / allocated_frame_count);
    printf("STANDBY PAGE COUNT:\t%llu\tSTANDBY PAGE PERCENTAGE:%.2f%%\n",
        standby_page_count, 100.0 * standby_page_count / allocated_frame_count);
    printf("EMPTY DISK SLOTS:\t%llu\n", empty_disk_slots);
    printf("\nHARD FAULTS RESOLVED:\t%llu\tSOFT FAULTS RESOLVED:\t%llu\n\n",
        hard_faults_resolved, soft_faults_resolved);
#endif
}

VOID update_statistics(VOID) {
    // Get current counts.
    free_page_count = get_size(&free_list);
    modified_page_count = get_size(&modified_list);
    standby_page_count = get_size(&standby_list);
    active_page_count = NUMBER_OF_PHYSICAL_PAGES - (free_page_count + modified_page_count + standby_page_count);
}

VOID schedule_tasks(VOID) {
    DWORD status;
    ULONG64 step = 0;

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        update_statistics();

        // Print the statistics to the log, if necessary
        if (step % PRINT_FREQUENCY_IN_MILLISECONDS == 0) print_statistics();
        step++;

        // Wait for a set amount of time before scheduling again.
        // Of course, if the system exit event is received, we will immediately break out of the thread.
        status = WaitForSingleObject(system_exit_event, SCHEDULER_DELAY_IN_MILLISECONDS);

        // If the system exit event is received, we will return.
        if (status == WAIT_OBJECT_0) break;
    }
}
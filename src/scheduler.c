//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"
#include "../include/initializer.h"
#include "../include/ager.h"
#include "../include/trimmer.h"
#include "../include/writer.h"
#include "../include/debug.h"

VOID print_statistics(VOID) {
    printf("\n\n");
    printf("FREE PAGE COUNT:\t\t%llu\t\tFREE PAGE PERCENTAGE:\t\t%.2f%%\n", free_page_count, 100.0 * free_page_count / allocated_frame_count);
    printf("ACTIVE PAGE COUNT:\t\t%llu\t\tACTIVE PAGE PERCENTAGE:\t\t%.2f%%\n", active_page_count, 100.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED PAGE COUNT:\t%llu\t\tMODIFIED PAGE PERCENTAGE:\t%.2f%%\n", modified_page_count, 100.0 * modified_page_count / allocated_frame_count);
    printf("STANDBY PAGE COUNT:\t\t%llu\t\tSTANDBY PAGE PERCENTAGE:\t%.2f%%\n", standby_page_count, 100.0 * standby_page_count / allocated_frame_count);
    printf("EMPTY DISK SLOTS:\t\t%llu\n", empty_disk_slots);
    printf("\nHARD FAULTS RESOLVED:\t%llu\t\tSOFT FAULTS RESOLVED:\t\t%llu\n", hard_faults_resolved, soft_faults_resolved);
}

VOID schedule_tasks(VOID) {
    DWORD status;

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {
        // Get current counts.
        free_page_count = get_size(&free_list);
        modified_page_count = get_size(&modified_list);
        standby_page_count = get_size(&standby_list);
        active_page_count = NUMBER_OF_PHYSICAL_PAGES - free_page_count - modified_page_count - standby_page_count;

#if DEBUG
        print_statistics();
#endif

        // If there is sufficient need, write pages out to disk to free up pages for anticipated page faults.
        // if (empty_disk_slots > 0) {
        //     SetEvent(initiate_writing_event);
        // }

        // Wait for a set amount of time before scheduling again.
        // Of course, if the system exit event is received, we will immediately break out of the thread.
        status = WaitForSingleObject(system_exit_event, SCHEDULER_DELAY_IN_MILLISECONDS);

        // If the system exit event is received, we will scheduling and return.
        if (status == WAIT_OBJECT_0) {
            break;
        }
    }
}
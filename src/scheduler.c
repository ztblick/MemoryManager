//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"
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
    printf("FAULTS UNRESOLVED:\t\t%llu\n", faults_unresolved);
}

VOID schedule_tasks(VOID) {

#if DEBUG
    printf("Beginning scheduled tasks...\n");
    print_statistics();
#endif
    if (free_page_count < FREE_PAGE_THRESHOLD) {
        age_active_ptes();
        trim_pages();
    }
    if (empty_disk_slots > 0) {
        write_pages(WRITE_BATCH_SIZE);

        // Broadcast to waiting user threads that there are standby pages ready.
        SetEvent(standby_pages_ready_event);
    }
#if DEBUG
    printf("\nDone with scheduled tasks!\n");
    print_statistics();
#endif
}
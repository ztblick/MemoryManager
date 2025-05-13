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
    printf("FREE PAGE COUNT:\t\t%llu\t\tFREE PAGE PERCENTAGE:\t\t%.3f\n", free_page_count, 1.0 * free_page_count / allocated_frame_count);
    printf("ACTIVE PAGE COUNT:\t\t%llu\t\tACTIVE PAGE PERCENTAGE:\t\t%.3f\n", active_page_count, 1.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED PAGE COUNT:\t\t%llu\t\tMODIFIED PAGE PERCENTAGE:\t\t%.3f\n", modified_page_count, 1.0 * modified_page_count / allocated_frame_count);
    printf("STANDBY PAGE COUNT:\t\t%llu\t\tSTANDBY PAGE PERCENTAGE:\t\t%.3f\n", standby_page_count, 1.0 * standby_page_count / allocated_frame_count);
}

VOID schedule_tasks(VOID) {

#if DEBUG
    print_statistics();
#endif
    if (free_page_count < FREE_PAGE_THRESHOLD) {
        age_active_ptes();
        trim_pages();
        write_pages(WRITE_BATCH_SIZE);
    }
}
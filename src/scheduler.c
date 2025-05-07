//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"

#include "../include/ager.h"
#include "../include/trimmer.h"
#include "../include/writer.h"

VOID schedule_tasks(VOID) {

    age_active_ptes();
    trim_pages();
    write_pages(WRITE_BATCH_SIZE);
}
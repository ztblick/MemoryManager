//
// Created by zblickensderfer on 5/6/2025.
//

#include "scheduler.h"

#include "ager.h"
#include "trimmer.h"
#include "writer.h"

VOID schedule_tasks(VOID) {

    age_active_ptes();
    trim_pages();
    write_pages(WRITE_BATCH_SIZE);
}
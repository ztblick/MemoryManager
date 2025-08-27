//
// Created by zblickensderfer on 5/6/2025.
//

#pragma once

#include "pfn.h"
#include "page_list.h"
#include "threads.h"

#define MAX_TRIM_ATTEMPTS       MAX_TRIM_BATCH_SIZE * 4
#define TRIMMER_DELAY           10

// The initial trimmer_offset in the PTE region for the trimmer -- this will change over time.
PPTE pte_to_trim;

/*
 *  This is the function called by CreateThread. It waits for the initialize_system event.
 *  Once received, it will wait for the trim_pages event (which can be called multiple times),
 *  which leads to calls to the above method, trim_pages. But it also waits for the system exit event,
 *  which terminated this threas.
 */
VOID trim_pages_thread(VOID);
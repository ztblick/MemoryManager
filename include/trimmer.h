//
// Created by zblickensderfer on 5/6/2025.
//

#pragma once

#include "macros.h"

// The initial trimmer_offset in the PTE region for the trimmer -- this will change over time.
PPTE pte_to_trim;

/*
 *  Trims active pages, moving them to the modified or standby lists.
 */
VOID trim_pages(VOID);

/*
 *  This is the function called by CreateThread. It waits for the initialize_system event.
 *  Once received, it will wait for the trim_pages event (which can be called multiple times),
 *  which leads to calls to the above method, trim_pages. But it also waits for the system exit event,
 *  which terminated this threas.
 */
VOID trim_pages_thread(VOID);
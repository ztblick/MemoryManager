//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once
#include "initializer.h"


/*
 *  This function is called by the CreateThread function in initializer.
 *  This function waits for the system start event, then waits for
 *  either the write pages event (which can be called multiple times) or the
 *  system exit event (which terminates it).
 */
VOID write_pages_thread(VOID);

/*
 *  Write the given number of pages to the disk.
 */
VOID write_pages(VOID);


void write_pages_to_disk(PPTE pte, ULONG_PTR num_pages);


void load_page_from_disk(PPTE pte, PVOID destination_va);


UINT64 get_free_disk_index(void);
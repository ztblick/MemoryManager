//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once
#include "initializer.h"



/*
 *  Write the given number of pages to the disk.
 */
VOID write_pages(int num_pages);


void write_pages_to_disk(PPTE pte, ULONG_PTR num_pages);


void load_page_from_disk(PPTE pte, PVOID destination_va);
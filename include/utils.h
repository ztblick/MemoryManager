//
// Created by zachb on 8/6/2025.
//

#pragma once

#include "threads.h"

/*
 *  Malloc the given amount of space, then zero the memory.
 */
PULONG_PTR zero_malloc(size_t bytes_to_allocate);

/*
 *  Maps the given page (or pages) to the given VA.
 */
void map_pages(ULONG64 num_pages, PULONG_PTR va, PULONG_PTR frame_numbers);

/*
 *  Un-maps the given page from the given VA.
 */
void unmap_pages(ULONG64 num_pages, PULONG_PTR va);

/*
 *  Bumps the count of available pages (free + standby)
 */
VOID increment_available_count(VOID);

/*
 *  Drops the count of available pages. Initiates the trimmer, if necessary.
 *  Asserts that the count must be positive.
 */
VOID decrement_available_count(VOID);
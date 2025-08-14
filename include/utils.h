//
// Created by zachb on 8/6/2025.
//

#pragma once

#include "debug.h"

#define MAX_PAGES_TO_MAP  2

/*
 *  Malloc the given amount of space, then zero the memory.
 */
PVOID zero_malloc(size_t bytes_to_allocate);

/*
 *  Maps the given page (or pages) to the given VA.
 */
void map_pages(ULONG64 num_pages, PULONG_PTR va, PULONG_PTR frame_numbers);

/*
 *  Un-maps the given page from the given VA.
 */
void unmap_pages(ULONG64 num_pages, PULONG_PTR va);

/*
 *  Maps two virtual addresses to the same frame.
 */
void map_both_va_to_same_page(PULONG_PTR va_one, PULONG_PTR va_two, ULONG64 frame_number);

/*
 *  Bumps the count of available pages (free + standby)
 */
VOID increment_available_count(VOID);

/*
 *  Drops the count of available pages. Initiates the trimmer, if necessary.
 *  Asserts that the count must be positive.
 */
VOID decrement_available_count(VOID);
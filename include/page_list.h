//
// Created by zblickensderfer on 6/5/2025.
//

#pragma once

#include "../include/macros.h"
#include "../include/PFN.h"

// A page list is the head of a doubly-linked list of pages.
// When the list is empty, the head's entry's flink and blink point to the entry itself.
// The head also keeps track of the size of the list.
// It also has the lock on the page list.
// Total size: 64 bytes. One (and only one) will fit in a cache line.
typedef struct __page_list {
    LIST_ENTRY head;           // 16 bytes
    ULONG_PTR list_size;        // 8 bytes
    CRITICAL_SECTION lock;      // 40 bytes
} PAGE_LIST, *PPAGE_LIST;


/*
 *  Initialize a page list. This creates the critical section, initializes the list head,
 *  and sets the size to zero.
 */
VOID initialize_page_list(PPAGE_LIST list);

/*
 *  Check if the list is empty. Returns true or false.
 */
BOOL is_page_list_empty(PPAGE_LIST list);

/*
 *  Adds a page to the head of the list.
 */
VOID insert_head_list(PPAGE_LIST list, PLIST_ENTRY entry);

/*
 *  Adds a page to the tail of the list.
 */
VOID insert_tail_list(PPAGE_LIST list, PLIST_ENTRY entry);

/*
 *  Removes a page to the head of the list.
 */

/*
 *  Removes a page from the tail of the list.
 */

/*
 *  Pops and returns from the head of the list.
 *  Returns NULL is the list is empty.
 */
PPFN pop_from_list_head(PPAGE_LIST list);
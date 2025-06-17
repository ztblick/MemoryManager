//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once
#include "initializer.h"
#include "pte.h"
#include "pfn.h"

/*
 *  Resolve a page fault by mapping a page to the faulting VA, if possible. Not guaranteed, though.
 */
BOOL page_fault_handler(PULONG_PTR faulting_va, int i);


/*
 *  Get a pointer to an offset in the page file!
 */
char* get_page_file_offset(UINT64 disk_slot);

/*
 *  Clears the given slot in the page file metadata
 */
void clear_disk_slot(UINT64 disk_slot);

/*
 *  Sets the given slot to occupied in the page file metadata.
 */
void set_disk_slot(UINT64 disk_slot);

/*
 *  These functions acquire and release locks on the given disk slot.
 */
void lock_disk_slot(UINT64 disk_slot);
void unlock_disk_slot(UINT64 disk_slot);
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
VOID page_fault_handler(PULONG_PTR faulting_va, int i);

/*
 *  Returns a page from the standby list or NULL if none can be returned.
 */
PPFN get_standby_page(void);
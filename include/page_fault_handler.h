//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once
#include "initializer.h"
#include "pte.h"
#include "pfn.h"

VOID page_fault_handler(PULONG_PTR faulting_va, int i);
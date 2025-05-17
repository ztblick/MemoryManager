//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/initializer.h"
#include "../include/pte.h"

#include "../include/debug.h"

// Returns the pte associated with the faulting VA. Divides the offset of the VA within the VA space
// by the page_size, resulting in the index of the VA within the PTE array.
PPTE get_PTE_from_VA(PULONG_PTR faulting_VA) {
    ULONG_PTR va_offset = (ULONG_PTR)faulting_VA - (ULONG_PTR)application_va_base;
    size_t pte_index = va_offset / PAGE_SIZE;
    return PTE_base + pte_index;
}

PVOID get_VA_from_PTE(PPTE pte) {
    size_t index = (size_t)(pte - PTE_base);  // Already scaled correctly
    return (PVOID)((char*)application_va_base + index * PAGE_SIZE);
}

// Using a strategy of not modifying the PTE one piece at a time, but all at once.
void set_PTE_to_transition(PPTE pte) {

    // Start by copying the whole PTE as raw bits
    PTE temp;
    temp.memory_format = pte->memory_format;

    // Clear the valid and status bits
    temp.memory_format.valid = PTE_INVALID;
    temp.memory_format.status = PTE_IN_TRANSITION;

    // Write back all bits at once to avoid partial modification
    *pte = temp;
}

// TODO Set other bits, such as accessed, dirty, readwrite, etc.
void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {

    // Start creating a zeroed PTE
    PTE temp = {0};

    // Set valid bit, set frame number
    temp.memory_format.status = PTE_VALID;
    temp.memory_format.frame_number = frame_number;

    // Write back all bits at once to avoid partial modification
    *pte = temp;
}

void map_pte_to_disk(PPTE pte, size_t disk_index) {

    pte->disk_format.disk_index = disk_index;
    pte->disk_format.status = PTE_ON_DISK;
    pte->disk_format.valid = PTE_INVALID;
}

size_t get_disk_index_from_pte(PPTE pte) {
    return (size_t)(pte - PTE_base);
}
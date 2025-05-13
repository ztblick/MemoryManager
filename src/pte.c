//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/initializer.h"
#include "../include/pte.h"

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

void set_PTE_to_transition(PPTE pte) {
    // TODO should we clear any of the old data, such as the frame number?
    pte->disk_format.status = PTE_IN_TRANSITION;
    pte->disk_format.valid = PTE_INVALID;
}

void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {
    // TODO should we clear any of the old data, such as the disk slot??
    pte->memory_format.frame_number = frame_number;
    pte->memory_format.valid = PTE_VALID;
}

void map_pte_to_disk(PPTE pte, size_t disk_index) {

    pte->disk_format.disk_index = disk_index;
    pte->disk_format.status = PTE_ON_DISK;
    pte->disk_format.valid = PTE_INVALID;
}

size_t get_disk_index_from_pte(PPTE pte) {
    return (size_t)(pte - PTE_base);
}
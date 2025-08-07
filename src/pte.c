//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/pte.h"

PPTE PTE_base = {0};

VOID initialize_page_table(VOID) {
    // Allocate and zero all PTE data.
    PTE_base = (PPTE) zero_malloc(sizeof(PTE) * NUM_PTEs);

    // Initialize all PTE locks
    for (PPTE pte = PTE_base; pte < PTE_base + NUM_PTEs; pte++) {
        initialize_byte_lock(&pte->lock);
    }
}

// Returns the pte associated with the faulting VA. Divides the offset of the VA within the VA space
// by the page_size, resulting in the index of the VA within the PTE array.
PPTE get_PTE_from_VA(PULONG_PTR va) {

    ASSERT(va >= application_va_base && va <= application_va_base + VIRTUAL_ADDRESS_SIZE);

    ULONG_PTR va_offset = (ULONG_PTR)va - (ULONG_PTR)application_va_base;
    ULONG_PTR pte_index = va_offset / PAGE_SIZE;

    return PTE_base + pte_index;
}

// Returns the VA associated with the beginning of the region of VAs for this PTE.
PVOID get_VA_from_PTE(PPTE pte) {

    ASSERT(pte >= PTE_base && pte <= PTE_base + NUM_PTEs);

    ULONG_PTR index = (ULONG_PTR) (pte - PTE_base);  // Already scaled correctly
    return (PVOID) ((ULONG_PTR) application_va_base + index * PAGE_SIZE);
}

// Using a strategy of not modifying the PTE one piece at a time, but all at once.
void set_PTE_to_transition(PPTE pte) {

    // TODO read with no fence to prevent tearing on the read
    // TODO grab a snapshot of the pte, use that to populate the new pte, then read the new pte into the pte (no fence)

    // Start by copying the whole PTE
    PTE temp;
    temp.transition_format = pte->transition_format;

    // Clear the valid and status bits
    temp.transition_format.valid = PTE_INVALID;
    temp.transition_format.status = PTE_IN_TRANSITION;
    temp.transition_format.frame_number = pte->transition_format.frame_number;

    // Write back all bits at once to avoid partial modification
    WriteULong64NoFence((DWORD64*) pte, temp.entire_pte);
}

void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {

    // Start creating a zeroed PTE
    PTE temp = {0};

    // This copies over all data from the PTE.
    temp.memory_format = pte->memory_format;

    // Set valid bit, set frame number
    temp.memory_format.valid = PTE_VALID;
    temp.memory_format.status = PTE_STATUS_BIT_FOR_VALID;
    temp.memory_format.frame_number = frame_number;

    // Write back all bits at once to avoid partial modification
    WriteULong64NoFence((DWORD64*) pte, temp.entire_pte);
}

void map_pte_to_disk(PPTE pte, UINT64 disk_index) {

    validate_disk_slot(disk_index);

    // Start by copying the whole PTE
    PTE temp = {0};
    temp.memory_format = pte->memory_format;

    // Clear the frame number, add the disk index, update the status bits
    temp.memory_format.frame_number = NO_FRAME_ASSIGNED;
    temp.disk_format.valid = PTE_INVALID;
    temp.disk_format.status = PTE_ON_DISK;
    temp.disk_format.disk_index = disk_index;

    // Write back all bits at once to avoid partial modification
    WriteULong64NoFence((DWORD64*) pte, temp.entire_pte);
}

VOID lock_pte(PPTE pte) {
    lock(&pte->lock);
}

BOOL try_lock_pte(PPTE pte) {
    return try_lock(&pte->lock);
}

VOID unlock_pte(PPTE pte) {
    unlock(&pte->lock);
}

VOID map_single_page_from_pte(PPTE pte) {
    ULONG64 frame_pointer = pte->memory_format.frame_number;
    PULONG_PTR va = get_VA_from_PTE(pte);

    map_pages(1, va, &frame_pointer);
}
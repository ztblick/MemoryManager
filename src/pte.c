//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/pte.h"

PPTE PTE_base = {0};

VOID initialize_page_table(VOID) {

    vm.num_ptes = vm.va_size_in_bytes / PAGE_SIZE;

    // Allocate and zero all PTE data.
    PTE_base = (PPTE) zero_malloc(sizeof(PTE) * vm.num_ptes);

    // Initialize all PTE locks
    for (PPTE pte = PTE_base; pte < PTE_base + vm.num_ptes; pte++) {
        initialize_byte_lock(&pte->lock);
    }
}

// Returns the pte associated with the faulting VA. Divides the offset of the VA within the VA space
// by the page_size, resulting in the index of the VA within the PTE array.
PPTE get_PTE_from_VA(PULONG_PTR va) {

    PULONG_PTR application_va_base = vm.application_va_base;

    ASSERT(va >= application_va_base && va <= application_va_base + vm.va_size_in_bytes);

    ULONG_PTR va_offset = (ULONG_PTR)va - (ULONG_PTR)application_va_base;
    ULONG_PTR pte_index = va_offset / PAGE_SIZE;

    return PTE_base + pte_index;
}

// Returns the VA associated with the beginning of the region of VAs for this PTE.
PVOID get_VA_from_PTE(PPTE pte) {

    ASSERT(pte >= PTE_base && pte <= PTE_base + vm.num_ptes);

    ULONG_PTR index = (ULONG_PTR) (pte - PTE_base);  // Already scaled correctly
    return (PVOID) ((ULONG_PTR) vm.application_va_base + index * PAGE_SIZE);
}

void set_PTE_to_transition(PPTE pte) {

    // Start by copying the whole PTE
    ULONG64 raw = ReadULong64NoFence((ULONG64 *) pte);
    PTE temp = {0};
    temp.entire_pte = raw;

    // Update our two fields
    temp.transition_format.valid = PTE_INVALID;
    temp.transition_format.status = PTE_IN_TRANSITION;

    // Write back all bits at once to avoid partial modification
    WriteULong64NoFence((ULONG64 *) pte, temp.entire_pte);
}

void set_PTE_to_valid(PPTE pte, ULONG_PTR frame_number) {

    // Start by copying the whole PTE
    ULONG64 raw = ReadULong64NoFence((ULONG64 *) pte);
    PTE temp = {0};
    temp.entire_pte = raw;

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
    ULONG64 raw = ReadULong64NoFence((ULONG64 *) pte);
    PTE temp = {0};
    temp.entire_pte = raw;

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

VOID set_accessed_bit(PULONG_PTR va) {
    PPTE pte = get_PTE_from_VA(va);

    // Set the accessed bit
    _interlockedbittestandset64((LONG64 *) pte, ACCESSED_BIT_POSITION);
}

VOID clear_accessed_bit(PPTE pte) {
    // Set the accessed bit
    USHORT original = _interlockedbittestandreset64((LONG64 *) pte, ACCESSED_BIT_POSITION);
    ASSERT(original == 1);
}
//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"

VOID write_pages(int num_pages) {
    // TODO Write the given number of pages to the disk
}

void write_pages_to_disk(PPTE pte, ULONG_PTR num_pages) {

    // Temporarily map an un-used VA to this frame
    ULONG_PTR frame_number = pte->memory_format.frame_number;
    if (MapUserPhysicalPages (kernal_write_va, num_pages, &frame_number) == FALSE) {
        fatal_error("Could not map Kernal VA to physical page.");
    }

    // Get disk slot for this PTE
    // TODO update this with a reasonable disk slot strategy
    PULONG_PTR disk_slot = page_file + pte->disk_format.disk_index * PAGE_SIZE;
    memcpy(disk_slot, kernal_write_va, PAGE_SIZE);

    // Un-map kernal VA
    if (MapUserPhysicalPages (kernal_write_va, 1, NULL) == FALSE) {
        fatal_error("Could not unmap Kernal VA to physical page.");
    }
}

void load_page_from_disk(PPTE pte, PVOID destination_va) {

    PULONG_PTR disk_slot = page_file + pte->disk_format.disk_index * PAGE_SIZE;
    memcpy(destination_va, disk_slot, PAGE_SIZE);
}
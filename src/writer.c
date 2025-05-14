//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/macros.h"

VOID write_pages(int num_pages) {

    // First, check to see if there are any pages to write
    if (IsListEmpty(modified_list) || num_pages == 0) {
#if DEBUG
        printf("No modified pages to write to disk.\n");
#endif
        return;
    }

    PPFN pfn;
    PPTE pte;

    // TODO figure out how to quickly know how many pages can be batched together -- scheduler could do this


    // Get the frame numbers, then map kernal VA to them
    PULONG_PTR frames_to_write = malloc(sizeof(ULONG_PTR) * num_pages);

    // Get all pages to write:
    for (int i = 0; i < num_pages; i++) {

        // Get the next modified page for this batch
        pfn = get_first_frame_from_list(modified_list);
        modified_page_count--;

        // Get the transition PTE associated with this page
        pte = pfn->PTE;

        if (IS_PTE_VALID(pte)) {
            fatal_error("PTE for modified page still listed as active.");
        }

        frames_to_write[i] = get_frame_from_PFN(pfn);
    }

    // Map all pages to the kernel VA space
    map_pages(num_pages, kernal_write_va, frames_to_write);

    // Get disk slot for this PTE
    PULONG_PTR disk_slot = page_file + pte->disk_format.disk_index * PAGE_SIZE;

    // Write page out to disk
    // TODO update this with a reasonable disk slot strategy and attend to any issues with VA offset issues
    // Current issue: cannot write 16 bytes after disk slot?
    // memcpy(disk_slot, kernal_write_va, PAGE_SIZE);

    // Un-map kernal VA
    if (MapUserPhysicalPages (kernal_write_va, num_pages, NULL) == FALSE) {
        fatal_error("Could not unmap Kernal VA to physical page.");
    }

    // TODO eventually loop through and update all PTEs and all PFNs for the batch

    // Update PFN to be on disk
    SET_PFN_STATUS(pfn, PFN_STANDBY);

    // Update PTE with disk slot
    // map_pte_to_disk(pte, disk_slot);    // TODO fix me!

    // Add page to the standby list
    InsertTailList(standby_list, &pfn->entry);
    standby_page_count++;

}
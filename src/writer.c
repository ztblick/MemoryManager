//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/macros.h"
#include "../include/page_fault_handler.h"

UINT64 get_free_disk_index(void) {

    for (UINT64 disk_index = 0; disk_index < PAGES_IN_PAGE_FILE; disk_index++) {

        if (page_file_metadata[disk_index] == DISK_SLOT_EMPTY) {
            return disk_index;
        }
    }
    fatal_error("No empty pages in the page file!");        // TODO figure out why this is getting called!
    return 0;
}

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

        frame_numbers_to_map[i] = get_frame_from_PFN(pfn);
    }

    // Map all pages to the kernel VA space
    map_pages(num_pages, kernel_write_va, frame_numbers_to_map);

    // Get disk slot for this PTE
    UINT64 disk_index = get_free_disk_index();

    // Get the location to write to in page file
    char* page_file_location = get_page_file_offset(disk_index);

    // Perform the write
    memcpy(page_file_location, kernel_write_va, PAGE_SIZE * num_pages);
    set_disk_slot(disk_index);

    // TODO
        // We should check the total contents of this page to see what is in it.
        // We should also ask Landy about writing to and reading from specific regions
        // within a page.

    // Un-map kernal VA
    unmap_pages(num_pages, kernel_write_va);

    // Update PFN to be in standby state, add disk index
    SET_PFN_STATUS(pfn, PFN_STANDBY);
    pfn->disk_index = disk_index;

    // Add page to the standby list
    InsertTailList(standby_list, &pfn->entry);
    standby_page_count++;

#if DEBUG
    printf("Wrote one page out to disk, moved page to standby list!\n");
#endif
}
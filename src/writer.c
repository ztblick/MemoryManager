//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/macros.h"
#include "../include/page_fault_handler.h"

// Returns a disk index from 1 to PAGES_IN_PAGE_FILE
// It is important to note that the indices are off by one, as the value of 0 is reserved
// for a PFN that is not mapped to a disk slot
UINT64 get_free_disk_index(void) {

    for (UINT64 disk_index = MIN_DISK_INDEX; disk_index <= PAGES_IN_PAGE_FILE; disk_index++) {

        if (page_file_metadata[disk_index] == DISK_SLOT_EMPTY) {
            return disk_index;
        }
    }
    fatal_error("No empty pages in the page file!");
    return 0;
}

VOID write_pages(VOID) {

    // Initialize frame number array
    PULONG_PTR frame_numbers_to_map = zero_malloc(MAX_WRITE_BATCH_SIZE * sizeof(ULONG_PTR));

    // First, check to see if there are any pages to write
    if (is_page_list_empty(&modified_list)) {
#if DEBUG
        printf("No modified pages to write to disk.\n");
#endif
        return;
    }

    int num_pages = WRITE_BATCH_SIZE;

    PPFN pfn;
    PPTE pte;

    // Get all pages to write:
    for (int i = 0; i < num_pages; i++) {

        // Get the next modified page for this batch
        pfn = pop_from_list_head(&modified_list);
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

    // Un-map kernal VA
    unmap_pages(num_pages, kernel_write_va);

    // Update PFN to be in standby state, add disk index
    SET_PFN_STATUS(pfn, PFN_STANDBY);
    pfn->disk_index = disk_index;

    // Add page to the standby list
    insert_tail_list(&standby_list, &pfn->entry);
    standby_page_count++;

    // Free frame number array
    free(frame_numbers_to_map);

#if DEBUG
    printf("Wrote one page out to disk, moved page to standby list!\n");
#endif
}

VOID write_pages_thread(VOID) {

    ULONG index;
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_writing_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        // Wait for one of two events: initiate writing (which calls write_pages), or exit, which...exits!
        index = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE);

        if (index == EXIT_EVENT_INDEX) {
            return;
        }

        write_pages();
    }
}
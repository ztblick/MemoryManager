//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"

VOID write_pages(VOID) {

    // PFN for the current page being selected for disk write.
    PPFN pfn;
    // The number of pages we have added to our batch
    ULONG64 num_pages_batched = 0;

    // The upper bound on the size of THIS particular batch. This will be set to the max, then (possibly)
    // brought lower by the disk slot allocation, then (possibly) brought lower by the number of pages
    // that can be removed from the modified list.
    ULONG64 num_pages_in_write_batch = MAX_WRITE_BATCH_SIZE;

    // If there are insufficient empty disk slots, let's hold off on writing
    if (pf.empty_disk_slots < MIN_WRITE_BATCH_SIZE) return;

    // Let's get a sense of how many modified pages we can reasonably expect. There may be more (from trimming)
    // or fewer (from soft faults), but this gives us an estimate.
    ULONG64 approx_num_mod_pages = get_size(&modified_list);

    // First, check to see if there are sufficient pages to write
    if (approx_num_mod_pages < MIN_WRITE_BATCH_SIZE) return;

    // Update our target count -- no need to get too greedy
    ULONG64 target_page_count = min(MAX_WRITE_BATCH_SIZE, approx_num_mod_pages);

    // Let's see if we need any slots at all:
    // If our stash is too small, we will get more.
    if (pf.num_stashed_slots < target_page_count) {
        set_and_add_slots_to_stack(target_page_count);
    }

    // If we couldn't batch enough slots, we will need to return.
    // Note that we did not release our stashed slots!
    if (pf.num_stashed_slots < MIN_WRITE_BATCH_SIZE) return;

    // Update our upper bound on pages in the batch
    num_pages_in_write_batch = min(pf.num_stashed_slots, target_page_count);

    // Initialize frame number array
    PPFN pages_to_write[MAX_WRITE_BATCH_SIZE];

    // Get all pages to write
    while (num_pages_batched < num_pages_in_write_batch) {

        // Here we will check the list size. This is approximate: the list size may
        // change, but all we need is a snapshot. If there are no longer pages on the list,
        // we will move on.
        if (is_page_list_empty(&modified_list)) {
            num_pages_in_write_batch = num_pages_batched;
            break;
        }

        // Get the PFN for the page we want to try to use
        pfn = try_pop_from_list(&modified_list);

        // If we couldn't get a page, that's okay! We will try again later
        if (!pfn) continue;

        // Update PFN status
        set_pfn_mid_write(pfn);

        // Grab the frame number for mapping/unmapping
        pages_to_write[num_pages_batched] = pfn;
        num_pages_batched++;

        // Release the PFN lock, now that we pulled it off the modified list.
        unlock_pfn(pfn);
    }

    // If we couldn't get any pages, we will return.
    if (num_pages_in_write_batch == 0) return;

    // Create an array of our frame numbers for the calls to map and unmap
    ULONG64 frame_numbers_to_map[MAX_WRITE_BATCH_SIZE];

    for (ULONG64 i = 0; i < num_pages_in_write_batch; i++) {
        frame_numbers_to_map[i] = get_frame_from_PFN(pages_to_write[i]);
    }

    // Map all pages to the kernel VA space
    map_pages(num_pages_in_write_batch, vm.kernel_write_va, frame_numbers_to_map);

    // Flag to indicate that SOME pages were successfully written
    ULONG64 pages_written = 0;
    ULONG64 slots_cleared = 0;

    for (ULONG64 i = 0; i < num_pages_in_write_batch; i++) {

        // Get the current page
        pfn = pages_to_write[i];

        // Get the disk slot for THIS page
        ULONG64 disk_slot = pop_slot();

        // Get the destination in the page file
        char* page_file_location = get_page_file_offset(disk_slot);

        // Write the next page location in the kernal VA space
        memcpy(page_file_location, vm.kernel_write_va + i * PAGE_SIZE / 8, PAGE_SIZE);

        // Lock the PFN again
        lock_pfn(pfn);

        // If we had a soft fault on the page mid-write, we will need to undo this disk write.
        // We will do so by clearing the disk slot and not modifying the PFN's status.
        if (!IS_PFN_MID_WRITE(pfn)) {
            unlock_pfn(pfn);
            clear_disk_slot(disk_slot);
            slots_cleared++;
        }

        // Otherwise, we move along as normal: the page moves to standby, and we update the PFN with its disk slot.
        else {

            // Check to be sure this page is still mid-write
            ASSERT(IS_PFN_MID_WRITE(pfn));

            // Update PFN to be in standby state, add disk index
            set_pfn_standby(pfn, disk_slot);

            // Add page to the standby list
            insert_page_to_tail(&standby_list, pfn);
            unlock_pfn(pfn);
            increment_available_count();

            // Update our count
            pages_written++;
        }
    }

    // Un-map kernal VA
    unmap_pages(num_pages_in_write_batch, vm.kernel_write_va);

    // Broadcast to waiting user threads that there are standby pages ready.
    if (pages_written > 0) SetEvent(standby_pages_ready_event);
}

VOID write_pages_thread(VOID) {

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_writing_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    while (TRUE) {

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        // Once woken, begin writing a batch of pages. Then go back to sleep.
        write_pages();
    }
}
//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"

ULONG64 write_pages(VOID) {

    // PFN for the current page being selected for disk write.
    PPFN pfn;

    // The upper bound on the size of THIS particular batch. This will be set to the max, then (possibly)
    // brought lower by the disk slot allocation, then (possibly) brought lower by the number of pages
    // that can be removed from the modified list.
    ULONG64 target_page_count = min(stats.writer_batch_target, MAX_WRITE_BATCH_SIZE);

    // If there are insufficient empty disk slots, let's hold off on writing
    if (pf.empty_disk_slots < MIN_WRITE_BATCH_SIZE) return 0;

    // Let's get a sense of how many modified pages we can reasonably expect. There may be more (from trimming)
    // or fewer (from soft faults), but this gives us an estimate.
    ULONG64 approx_num_mod_pages = get_size(&modified_list);

    // First, check to see if there are sufficient pages to write.
    // If not, start the trimmer before exiting.
    if (approx_num_mod_pages < MIN_WRITE_BATCH_SIZE) {
        SetEvent(initiate_trimming_event);
        return 0;
    }
    // Update our target count if we probably have fewer modified pages than our max.
    target_page_count = min(target_page_count, approx_num_mod_pages);

    // Let's see if we need any slots at all:
    // If our stash is too small, we will get more.
    if (pf.num_stashed_slots < target_page_count) {
        set_and_add_slots_to_stack(target_page_count);
    }

    // If we couldn't batch enough slots, we will need to return.
    // Note that we did not release our stashed slots!
    if (pf.num_stashed_slots < MIN_WRITE_BATCH_SIZE) return 0;

    // Update our upper bound on pages in the batch
    target_page_count = min(pf.num_stashed_slots, target_page_count);

    // Initialize frame number array
    PPFN pages_to_write[MAX_WRITE_BATCH_SIZE];

    // Grab batches from the modified list until we reach capacity
    // or until the list is empty
    ULONG64 num_pages_in_write_batch = 0;
    ULONG64 current_batch_size = 0;
    USHORT misses = 0;
    while (num_pages_in_write_batch < target_page_count &&
            misses < BATCH_ATTEMPTS) {

        current_batch_size = remove_batch_from_list_head(
                                                &modified_list,
                                                pages_to_write,
                                                target_page_count,
                                                num_pages_in_write_batch);

        // If no pages were returned, we will note the miss.
        // After enough failed attempts, we will just move on.
        if (current_batch_size == 0) misses++;

        num_pages_in_write_batch += current_batch_size;

    }

    // If we couldn't get any pages, we will return.
    if (num_pages_in_write_batch == 0) return 0;

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

    // We will make a temporary page list to help do a batch insert to the standby list.
    PAGE_LIST temp_list;
    initialize_page_list(&temp_list);

    for (ULONG64 i = 0; i < num_pages_in_write_batch; i++) {

        // Get the current page
        pfn = pages_to_write[i];

        // Get the disk slot for THIS page
        ULONG64 disk_slot = pop_slot();

        // Get the destination in the page file
        char* page_file_location = get_page_file_offset(disk_slot);

        // Write the next page location in the kernal VA space
        memcpy(page_file_location,
            vm.kernel_write_va + i * PAGE_SIZE / 8,
            PAGE_SIZE);

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

            // Add page to the temp list
            insert_to_list_tail(&temp_list, pfn);

            // Update our count
            pages_written++;
        }
    }

    // Add ALL pages to standby list by updating flinks and blinks of head/tail
    // of page batch as well as head/tail of standby list
    insert_list_to_tail_list(&standby_list, &temp_list);

    // Unlock all pages in the batch!
    pfn = temp_list.head->flink;
    PPFN next;
    for (int i = 0; i < pages_written; ++i) {
        next = pfn->flink;
        unlock_pfn(pfn);
        pfn = next;
    }

    // Increase available count
    increase_available_count(pages_written);

    // Un-map kernal VA
    unmap_pages(num_pages_in_write_batch, vm.kernel_write_va);

    // Broadcast to waiting user threads that there are standby pages ready.
    if (pages_written > 0) SetEvent(standby_pages_ready_event);

    return pages_written;
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

#if STATS_MODE
        LONGLONG start_time = get_timestamp();
#endif
        // Once woken, begin writing a batch of pages. Then go back to sleep.
        ULONG64 batch_size = write_pages();
#if STATS_MODE
        LONGLONG end_time = get_timestamp();
        double difference = get_time_difference(end_time, start_time);
        record_batch_size_and_time(difference, batch_size, writing_thread_id);
#endif
    }
}
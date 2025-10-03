//
// Created by ztblick on 10/1/2025.
//

#include "pruner.h"

void clear_free_list_bit(ULONG index) {
    BOOL original_value = InterlockedBitTestAndReset64(&free_lists.low_list_bitmap, index);
    // Ensure that the original bit was, in fact, set!
    ASSERT(original_value);
}

ULONG64 prune_pages(VOID) {

    // This will hold the first PFN in our batch
    PPFN first_pfn;

    // Grab a batch of pages from the standby list
    USHORT batch_size = remove_batch_from_list_head(&standby_list,
                                                    &first_pfn,
                                                    MAX_PRUNE_BATCH_SIZE);

    // Using recent consumption, decide if we need to signal the writer to bring in more standby pages!
    signal_event_if_list_is_about_to_run_low(   &standby_list,
                                                initiate_writing_event,
                                                WRITING_THREAD_ID,
                                                LOW_PAGE_THRESHOLD);

    if (batch_size == 0) return 0;

    // Map each page to the disk, then unlock them
    PPFN current = first_pfn;
    PPFN next;
    for (USHORT i = 0; i < batch_size; ++i) {
#if DEBUG
        validate_pfn(current);
#endif
        // Map the previous page's owner to its disk slot! We are doing this WITHOUT the page table lock.
        // This is okay, but we will now need to double-check the PTE on a simultaneous soft fault.
        // If that PTE lock is acquired and the thread is waiting for the page lock, then the PTE will have changed
        // while they were waiting.
        PPTE old_pte = current->PTE;
        map_pte_to_disk(old_pte, current->fields.disk_index);

        // Now we can copy the page into the cache and release its lock
        next = current->flink;
        unlock_pfn(current);
        current = next;
    }

    // Calculate the number of lists that need pages, then split the batch across them
    ULONG free_lists_to_fill[FREE_LIST_COUNT];
    ULONG num_lists_to_fill = 0;
    ULONG low_list_snapshot = free_lists.low_list_bitmap;
    for (int i = 0; i < FREE_LIST_COUNT; i++) {
        if (low_list_snapshot & (1 << i)) {
            free_lists_to_fill[num_lists_to_fill++] = i;
            // Update the free list bits.
            clear_free_list_bit(i);
        }
    }
    if (num_lists_to_fill == 0) return 0;
    ULONG small_batch = batch_size / num_lists_to_fill;

    PPFN first_page_in_batch = first_pfn;
    for (int i = 0; i < num_lists_to_fill; i++) {

        // We will make a temporary page list to help do a batch insert to the free list.
        PAGE_LIST temp_list;
        initialize_page_list(&temp_list);

        // If we are on the final list, make sure we grab ALL remaining pages
        ULONG this_batch_size = small_batch;
        if (i == num_lists_to_fill - 1) this_batch_size += batch_size % num_lists_to_fill;
        if (this_batch_size == 0) continue;

        // Add a small batch of pages to the temporary list
        current = first_page_in_batch;
        for (int j = 0; j < this_batch_size; ++j) {
#if DEBUG
            validate_pfn(current);
#endif
            next = current->flink;
            insert_to_list_tail(&temp_list, current);
            current = next;
        }

        // Reset the head of the batch for the next go-around
        first_page_in_batch = current;

        // Insert the temporary list to the target free list
        ULONG list_index = free_lists_to_fill[i];
        PPAGE_LIST target_free_list = &free_lists.list_array[list_index];
        insert_list_to_tail_list(target_free_list, &temp_list);

        // Update statistics
        change_list_size(target_free_list, this_batch_size);
    }

    // Update total free page count (but don't change available count, since pages moved from standby -> free).
    increase_free_lists_total_count(batch_size);
    return batch_size;
}

VOID prune_pages_thread(void) {

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_pruning_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    while (TRUE) {

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        LONGLONG start_time = get_timestamp();

        // Before removing pages from the standby list, we should check to see if
        // the writer needs to start up again to replenish what we are going to take.
        signal_event_if_list_is_about_to_run_low(   &standby_list,
                                                    initiate_writing_event,
                                                    WRITING_THREAD_ID,
                                                    LOW_PAGE_THRESHOLD);

        // Once woken, begin writing a batch of pages. Then go back to sleep.
        ULONG64 batch_size = prune_pages();

        // Record the runtime to update future estimates
        LONGLONG end_time = get_timestamp();
        double difference = get_time_difference(end_time, start_time);
        update_estimated_job_time(PRUNING_THREAD_ID, difference);

#if STATS_MODE
        record_batch_size_and_time(difference, batch_size, PRUNING_THREAD_ID);
#endif
    }
}
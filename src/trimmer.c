//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/trimmer.h"

void check_to_start_writer(void) {
    if (*stats.n_standby < LOW_PAGE_THRESHOLD / 8) {
        SetEvent(initiate_writing_event);
    }
}

ULONG64 trim_pages(VOID) {

    // We will keep track of the number of pages we have batched
    ULONG64 attempts = 0;
    ULONG64 trim_batch_size = 0;
    PPFN trimmed_pages[MAX_TRIM_BATCH_SIZE];
    PULONG_PTR trimmed_VAs[MAX_TRIM_BATCH_SIZE];

    // Initialize current pte
    PPTE pte = pte_to_trim;

    // The PFN of the current PTE.
    PPFN pfn;

    // Walks PTE region until a valid page batch is made, beginning from one
    // beyond previous last trimmed page.
    while (trim_batch_size < MAX_TRIM_BATCH_SIZE && attempts < MAX_TRIM_ATTEMPTS) {

        // Move on to the next pte.
        pte++;
        attempts++;

        // Wrap around!
        if (pte == (PTE_base + vm.num_ptes)) pte = PTE_base;

        // If the PTE is not valid, no need for us to try to lock it.
        if (!IS_PTE_VALID(pte)) continue;

#if AGING
        // If the PTE has been accessed, clear its bit and continue
        if (IS_PTE_ACCESSED(pte)) {
            clear_accessed_bit(pte);
            continue;
        }
#endif
        // Try to acquire the PTE lock
        if (!try_lock_pte(pte)) continue;

        // Read in the the PFN.
        pfn = get_PFN_from_PTE(pte);

        // Lock the pfn (provides protection against soft-faulting mid-trim later on)
        lock_pfn(pfn);
#if DEBUG
        validate_pfn(pfn);
#endif

        // Now that you have both locks, transition both data structures into the
        // proper transition, mid-trim states.
        set_PTE_to_transition(pte);

        // Unlock the PTE -- we don't need it anymore
        unlock_pte(pte);

        // Great! We have a page. Let's add it to our array.
        trimmed_pages[trim_batch_size] = pfn;
        trimmed_VAs[trim_batch_size] = get_VA_from_PTE(pte);
        trim_batch_size++;
    }

    // If we couldn't trim anyone, return
    if (trim_batch_size == 0) {
        check_to_start_writer();
        return 0;
    }

    // Unmap ALL pages in one batch!
    if (!MapUserPhysicalPagesScatter(trimmed_VAs, trim_batch_size, NULL)) DebugBreak();

    // Update our starting point for the next run
    pte_to_trim = pte;

    // We will make a temporary page list to help do a batch insert to the modified list.
    PAGE_LIST temp_list;
    initialize_page_list(&temp_list);

    // Now, let's add all of our trimmed pages to the modified list
    for (ULONG i = 0; i < trim_batch_size; i++) {

        // Grab the PFN and take a snapshot of its PTE
        pfn = trimmed_pages[i];

        // Set PFN status as modified
        SET_PFN_STATUS(pfn, PFN_MODIFIED);

        // Add page to the temp list
        insert_to_list_tail(&temp_list, pfn);
    }

    // Add all pages to the modified list
    insert_list_to_tail_list(&modified_list, &temp_list);
    change_list_size(&modified_list, (LONG64) trim_batch_size);

    // Unlock all pages in the batch!
    pfn = temp_list.head->flink;
    PPFN next;
    for (int i = 0; i < trim_batch_size; ++i) {
        next = pfn->flink;
        unlock_pfn(pfn);
        pfn = next;
    }

    // Before we return, let's see if we desperately need to write.
    // If the standby list is dangerously low on pages, let's do a write
    check_to_start_writer();

    // Return our batch size, which is used by the statistics thread.
    return trim_batch_size;
}

VOID trim_pages_thread(VOID) {

    // Create our handles for the wait for multiple objects call in the loop.
    // We wait to trim or to exit.
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_trimming_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Initialize target pte to begin at the start of the PTE region.
    pte_to_trim = PTE_base;

    // If the exit flag has been set, then it's time to go!
    while (TRUE) {

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        // Since we are about to trim, we will reset our event
        ResetEvent(initiate_trimming_event);

        LONGLONG start = get_timestamp();

        // Once woken, begin trimming a batch of pages. Then go back to sleep.
        ULONG64 batch_size = trim_pages();

        // Record the runtime and update the future runtime estimate
        LONGLONG end = get_timestamp();
        double difference = get_time_difference(end, start);
        update_estimated_job_time(TRIMMING_THREAD_ID, difference);

        #if STATS_MODE
        record_batch_size_and_time(difference, batch_size, TRIMMING_THREAD_ID);
#endif
    }
}
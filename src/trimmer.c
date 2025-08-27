//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/trimmer.h"

ULONG64 trim_pages(VOID) {

    // Before we do anything, let's see if there is any need to trim. If the modified list has
    // sufficient pages, let's just wake the writer.
    if (*stats.n_modified > MAX_WRITE_BATCH_SIZE) {
        SetEvent(initiate_writing_event);
    }

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

        // Now that you have both locks, transition both data structures into the
        // proper transition, mid-trim states.
        set_PTE_to_transition(pte);

        // Great! We have a page. Let's add it to our array.
        trimmed_pages[trim_batch_size] = pfn;
        trimmed_VAs[trim_batch_size] = get_VA_from_PTE(pte);
        trim_batch_size++;
    }

    // If we couldn't trim anyone, return -- but still wake the writer! There may be plenty of modified
    // pages yet to be written!
    if (trim_batch_size == 0) {
        SetEvent(initiate_writing_event);
        return 0;
    }
    // Unmap ALL pages in one batch!
    if (!MapUserPhysicalPagesScatter(trimmed_VAs, trim_batch_size, NULL)) DebugBreak();

    // Update our starting point for the next run
    pte_to_trim = pte;

    // Now, let's add all of our trimmed pages to the modified list
    for (ULONG i = 0; i < trim_batch_size; i++) {

        // Grab the PFN and take a snapshot of its PTE
        pfn = trimmed_pages[i];
        pte = pfn->PTE;

        // Set PFN status as modified
        SET_PFN_STATUS(pfn, PFN_MODIFIED);

        // Add this page to the modified list -- now the page is hot!
        insert_page_to_tail(&modified_list, pfn);

        // Release the pte lock, which was acquired earlier when the trim batch was initially assembled.
        unlock_pte(pte);
    }

    // Return our batch size, which is used by the statistics thread.
    SetEvent(initiate_writing_event);
    return trim_batch_size;
}

VOID trim_pages_thread(VOID) {

    // Active page count will keep track of our active pages.
    ULONG64 active_page_count;

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

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, trim_and_write_frequency)
            == EXIT_EVENT_INDEX) return;

#if STATS_MODE
        LONGLONG start = get_timestamp();
#endif
        // Once woken, begin trimming a batch of pages. Then go back to sleep.
        ULONG64 batch_size = trim_pages();
#if STATS_MODE
        LONGLONG end = get_timestamp();
        double difference = get_time_difference(end, start);
        record_batch_size_and_time(difference, batch_size, trimming_thread_id);
#endif
    }
}
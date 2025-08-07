//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/trimmer.h"

VOID trim_pages(VOID) {

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
        if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;

        // If the PTE is not valid, no need for us to try to lock it.
        if (!IS_PTE_VALID(pte)) continue;

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

    // If we couldn't trim anyone, return
    if (trim_batch_size == 0) return;

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
        lock_list_then_insert_to_tail(&modified_list, &pfn->entry);

        // Release the pte lock, which was acquired earlier when the trim batch was initially assembled.
        unlock_pte(pte);
    }
    // Wake the writer!
    SetEvent(initiate_writing_event);
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

        if (WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE)
            == EXIT_EVENT_INDEX) return;

        // Once woken, begin trimming a batch of pages. Then go back to sleep.
        trim_pages();
    }
}
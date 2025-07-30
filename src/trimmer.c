//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/trimmer.h"

VOID trim_pages(VOID) {

    // We will keep track of the number of pages we have batched
    ULONG64 attempts = 0;
    ULONG64 trim_batch_size = 0;
    PPFN trimmed_pages[MAX_TRIM_BATCH_SIZE];

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

        // TODO consider removing this (again). It does not seem like we need to lock this as long as we
        // can update the PTE with an interlocked operation. Is writeNoFence atomic?

        // If the PTE is not valid, no need for us to try to lock it.
        if (!IS_PTE_VALID(pte)) continue;

        // Try to acquire the PTE lock
        if (!try_lock_pte(pte)) continue;

        // Check the PTE is again (now that we have the lock)
        if (!IS_PTE_VALID(pte)) {
            unlock_pte(pte);
            continue;
        }

        // Read in the the PFN.
        pfn = get_PFN_from_PTE(pte);

        // Set the PTE as memory format transition.
        set_PTE_to_transition(pte);

        // Set the page's status to mid-trim
        SET_PFN_STATUS(pfn, PFN_MID_TRIM);

        // Release the PTE lock
        unlock_pte(pte);

        // Great! We have a page. Let's add it to our array.
        trimmed_pages[trim_batch_size] = pfn;
        trim_batch_size++;
    }

    // Update our starting point for the next run
    pte_to_trim = pte;

    // Now, let's add all of our trimmed pages to the modified list
    for (ULONG i = 0; i < trim_batch_size; i++) {

        // Grab the PFN
        pfn = trimmed_pages[i];
        lock_pfn(pfn);

        // TODO Talk to Landy about making this an atomic operation, so it can be done lockless.

        // If the page was faulted on while we were trimming, don't worry about it.
        if (soft_fault_happened_mid_trim(pfn)) {
            unlock_pfn(pfn);
            continue;
        }

        // Add this page to the modified list
        lock_list_then_insert_to_tail(&modified_list, &pfn->entry);

        // Set PFN status as modified
        SET_PFN_STATUS(pfn, PFN_MODIFIED);

        // Leave PFN critical section
        unlock_pfn(pfn);
    }
}

VOID trim_pages_thread(VOID) {

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Initialize target pte to begin at the start of the PTE region.
    pte_to_trim = PTE_base;

    // If the exit flag has been set, then it's time to go!
    while (trimmer_exit_flag == SYSTEM_RUN) {

        update_statistics();

        // Otherwise: if there is sufficient need, wake the trimmer
        if (standby_page_count + free_page_count < STANDBY_PAGE_THRESHOLD &&
            active_page_count > ACTIVE_PAGE_THRESHOLD) trim_pages();

        // If there isn't need, let's sleep for a moment to avoid spinning and burning up this core.
        else YieldProcessor();
    }
}
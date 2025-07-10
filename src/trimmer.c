//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/trimmer.h"


VOID trim_pages(VOID) {

    // Initialize current pte
    PPTE pte = pte_to_trim;

    // The PFN of the current PTE.
    PPFN pfn;

    // Walks PTE region until a valid PTE is found to be trimmed, beginning from one
    // beyond previous last trimmed page.
    while (TRUE) {

        // Move on to the next pte.
        pte++;

        // Wrap around!
        if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;

        // Try to acquire the PTE lock
        if (!try_lock_pte(pte)) {
            continue;
        }

        // If the PTE is not memory valid, continue
        if (!pte->memory_format.valid) {
            unlock_pte(pte);
            continue;
        }

        // Read in the the PFN.
        pfn = get_PFN_from_PTE(pte);

        // If the PFN lock can not be acquired, continue.
        if (!try_lock_pfn(pfn)) {
            unlock_pte(pte);
            continue;
        }

        // If the page is no longer active, release locks and continue.
        if (!IS_PFN_ACTIVE(pfn)) {
            unlock_pte(pte);
            unlock_pfn(pfn);
            continue;
        }

        // Otherwise -- we have a valid page to trim!
        break;
    }

    // Unmap the VA from this page
    unmap_pages(1, get_VA_from_PTE(pte));

    // Set the PTE as memory format transition.
    set_PTE_to_transition(pte);

    // Trim this page to the modified list
    lock_list_then_insert_to_tail(&modified_list, &pfn->entry);

    // Set PFN status as modified
    SET_PFN_STATUS(pfn, PFN_MODIFIED);

    // Leave PFN, PTE critical sections
    unlock_pfn(pfn);
    unlock_pte(pte);
}

VOID trim_pages_thread(VOID) {

    ULONG index;
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_trimming_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Initialize target pte to begin at the start of the PTE region.
    pte_to_trim = PTE_base;

    while (TRUE) {

        // Wait for one of two events: initiate writing (which calls write_pages), or exit, which...exits!
        index = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE);

        if (index == EXIT_EVENT_INDEX) {
            return;
        }
        trim_pages();
    }
}
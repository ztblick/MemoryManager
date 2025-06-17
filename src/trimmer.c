//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/trimmer.h"


VOID trim_pages(VOID) {

    #if DEBUG
    printf("\nBeginning to trim...\n");
#endif

    // Initialize current pte
    PPTE pte = PTE_base + trimmer_offset;

    // Flag to indicate if active page was trimmed
    BOOL trimmed = FALSE;

    // Walks PTE region until a valid PTE is found to be trimmed, beginning from one
    // beyond previous last trimmed page.
    for (int i = 0; i < NUM_PTEs; i++) {

        // Try to acquire the PTE lock
        if (try_lock_pte(pte)) {

            // When an active PTE is found, break.
            if (pte->memory_format.valid) {
                trimmed = TRUE;
                break;
            }

            // If the PTE is not valid, release its lock.
            unlock_pte(pte);        }

        // Move on to the next pte
        pte++;

        // Wrap around!
        if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;
    }
    if (!trimmed) {
#if DEBUG
        printf("No active PTEs to be trimmed...");
#endif
        return;
    }

    // Get the PFN lock
    PPFN pfn = get_PFN_from_PTE(pte);
    lock_pfn(pfn);

    // TODO ensure that this PFN is still in its active state -- if not, we will need to try again.

    // Unmap the VA from this page
    unmap_pages(1, get_VA_from_PTE(pte));

    // Set the PTE as memory format transition.
    set_PTE_to_transition(pte);

    // Trim this page to the modified list
    lock_list_then_insert_to_tail(&modified_list, &pfn->entry);
    modified_page_count++;
    active_page_count--;

    // Set PFN status as modified
    SET_PFN_STATUS(pfn, PFN_MODIFIED);

    // Leave PFN, PTE critical sections
    unlock_pfn(pfn);
    unlock_pte(pte);

#if DEBUG
    printf("\nTrimmed one page to from active to modified.\n\n");
#endif

    // Update the offset -- current pte plus one!
    pte++;
    if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;
    trimmer_offset = pte - PTE_base;
}

VOID trim_pages_thread(VOID) {

    ULONG index;
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_trimming_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // Initialize trimmer offset to begin at the start of the PTE region.
    trimmer_offset = 0;

    while (TRUE) {

        // Wait for one of two events: initiate writing (which calls write_pages), or exit, which...exits!
        index = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE);

        if (index == EXIT_EVENT_INDEX) {
            return;
        }
        // TODO eventually remove these guards!
        EnterCriticalSection(&page_fault_lock);
        trim_pages();
        LeaveCriticalSection(&page_fault_lock);
    }
}
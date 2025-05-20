//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/trimmer.h"

VOID trim_pages(VOID) {

    // Initialize current pte
    PPTE pte = PTE_base + trimmer_offset;

    // Flag to indicate if active page was trimmed
    BOOL trimmed = FALSE;

    // Walks PTE region until a valid PTE is found to be trimmed, beginning from one
    // beyond previous last trimmed page.
    for (int i = 0; i < NUM_PTEs; i++) {

        // When an active PTE is found, break.
        if (pte->memory_format.valid) {
            trimmed = TRUE;
            break;
        }
        // Move on to the next pte
        pte++;

        // Wrap around!
        if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;
    }
    if (!trimmed) {
        fatal_error("No active PTEs to be trimmed...");
    }

    // Unmap the VA from this page
    unmap_pages(1, get_VA_from_PTE(pte));

    // Set the PTE as memory format transition.
    set_PTE_to_transition(pte);

    // Trim this page to the modified list
    PPFN pfn = get_PFN_from_PTE(pte);
    InsertHeadList(modified_list, &pfn->entry);
    modified_page_count++;
    active_page_count--;

    // Set PFN status as modified
    SET_PFN_STATUS(pfn, PFN_MODIFIED);

#if DEBUG
    printf("\nTrimmed one page to from active to modified.\n\n");
#endif

    // Update the offset -- current pte plus one!
    pte++;
    if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;
    trimmer_offset = pte - PTE_base;
}
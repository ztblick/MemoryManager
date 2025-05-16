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
    while (!trimmed) {

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

    // Unmap the VA from this page
    if (MapUserPhysicalPages (get_VA_from_PTE(pte), 1, NULL) == FALSE) {
        fatal_error("Could not unmap VA from physical page.");
    }

    // Set the PTE as memory format modified.
    set_PTE_to_transition(pte);

    // Trim this page to the modified list
    PPFN pfn = get_PFN_from_frame(pte->memory_format.frame_number);
    InsertHeadList(modified_list, &pfn->entry);
    modified_page_count++;
    active_page_count--;

    // Set PFN status as modified
    SET_PFN_STATUS(pfn, PFN_MODIFIED);

#if DEBUG
    printf("\nTrimmed one page to from active to modified.\n\n");
#endif

    // Return the new offset -- current pte plus one!
    pte++;
    if (pte == (PTE_base + NUM_PTEs)) pte = PTE_base;
    trimmer_offset = pte - PTE_base;
}
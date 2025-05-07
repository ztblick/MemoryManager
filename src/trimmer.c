//
// Created by zblickensderfer on 5/6/2025.
//
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/trimmer.h"

VOID trim_pages(VOID) {

    // Flag to indicate if active page was trimmed
    BOOL trimmed = FALSE;

    // Walks PTE region until a valid PTE is found to be trimmed.
    PPTE pte = PTE_base;
    while (pte < PTE_base + NUM_PTEs) {

        // When an active PTE is found, break.
        if (pte->memory_format.valid) {
            trimmed = TRUE;
            break;
        }
        pte++;
    }

    // If we reach the end of the PTE space, and no pages were trimmed, return.
    if (!trimmed) {
        return;
    }

    // Unmap the VA from this page
    if (MapUserPhysicalPages (get_VA_from_PTE(pte), 1, NULL) == FALSE) {
        fatal_error("Could not unmap Kernal VA to physical page.");
    }

    // Set the PTE as memory format modified.
    set_PTE_to_transition(pte);

    // Trim this page to the modified list
    PPFN pfn = get_PFN_from_frame(pte->memory_format.frame_number);
    InsertHeadList(modified_list, &pfn->entry);

    // Set PFN status as modified
    SET_PFN_STATUS(pfn, PFN_MODIFIED);
}
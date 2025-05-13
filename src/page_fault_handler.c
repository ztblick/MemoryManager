//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/page_fault_handler.h"
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"
#include "../include/writer.h"

PPFN get_standby_page(void) {
    return NULL;
}

BOOL page_fault_handler(PULONG_PTR faulting_va, int i) {

    // The PFN associated with the page that will be mapped to the faulting VA.
    PPFN available_pfn = NULL;

    // Grab the PTE for the VA
    PPTE pte = get_PTE_from_VA(faulting_va);

    // If we faulted on a valid VA, something is very wrong
    if (pte->memory_format.valid == 1) {
        fatal_error("Faulted on a hardware valid PTE.");
    }

    // TODO add zero list
    // First, try to get a zeroed page!

    // Then, try to get a free page
    if (!IsListEmpty(free_list)) {

        // Get the next free page.
        PPFN available_pfn = get_first_frame_from_list(free_list);
        NULL_CHECK(available_pfn, "Free page was null :(");
        free_page_count--;
        active_page_count++;

        // Set the PFN to its active state and add it to the end of the active list
        available_pfn->PTE = pte;
        SET_PFN_STATUS(available_pfn, PFN_ACTIVE);

        // Set the PTE into its memory format
        PULONG_PTR frame_number = malloc(sizeof(ULONG_PTR));
        *frame_number = get_frame_from_PFN(available_pfn);
        set_PTE_to_valid(pte, *frame_number);

        // Map the VA to its new page.
        map_pages(1, faulting_va, frame_number);

#if DEBUG
        printf("Successfully mapped page from free list to faulting VA in iteration %d.\n", i);
#endif

        free(frame_number);
        return TRUE;
    }

    // If no page is available on the zero or free list, then we will evict one from the head of the active list (FIFO)
    // Then, try to get a free page
    if (IsListEmpty(standby_list)) {
#if DEBUG
        printf("Fault handler could not resolve fault as no pages were available on zero, free, or standby");
#endif
        return FALSE;
    }

    // Get a page from the standby list
    available_pfn = get_first_frame_from_list(standby_list);
    NULL_CHECK(available_pfn, "Page returned by standby was null :(")
    standby_page_count--;
    active_page_count++;

    // Get the previous PTE mapping to this frame
    PULONG_PTR frame_number = malloc(sizeof(ULONG_PTR));
    *frame_number = get_frame_from_PFN(available_pfn);
    PPTE old_pte = available_pfn->PTE;

    // Handle soft fault: if the old pte and the pte are the same, then simply update the PTE to be active
    // TODO CURRENT BUG: this will not resolve a page fault! The program continuously loops with the same VA and page...
    if (pte == old_pte) {
#if DEBUG
        printf("Soft fault on standby page!\n");
#endif
        map_pages(1, faulting_va, frame_number);
        set_PTE_to_valid(pte, *frame_number);
        SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
        // TODO Also, mark the disk slot as available
    }

    // Handle hard fault: map new VA to this page, update PFN and PTE
    else {
        map_pages(1, faulting_va, frame_number);
        set_PTE_to_valid(pte, *frame_number);
        SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
        available_pfn->PTE = pte;
    }

#if DEBUG
    printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, *frame_number, pte);
#endif

    free(frame_number);

    return TRUE;
}
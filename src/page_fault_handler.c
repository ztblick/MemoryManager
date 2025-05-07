//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/page_fault_handler.h"
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"
#include "../include/writer.h"

PPFN get_next_free_page(void) {
    if (IsListEmpty(free_list)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(free_list);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

PPFN get_first_active_page(void) {
    if (IsListEmpty(active_list)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(active_list);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

VOID page_fault_handler(PULONG_PTR faulting_va, int i) {

    // Grab the PTE for the VA
    PPTE pte = get_PTE_from_VA(faulting_va);

    // If we faulted on a valid VA, something is very wrong
    if (pte->memory_format.valid == 1) {
        fatal_error("Faulted on a hardware valid PTE.");
    }

    // Get the next free page.
    PPFN available_pfn = get_next_free_page();

    // If a free page is available, map the PFN to the PTE.
    if (available_pfn != NULL) {
        // Set the PFN to its active state and add it to the end of the active list
        available_pfn->PTE = pte;
        SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
        InsertTailList(active_list, &available_pfn->entry);

        // Set the PTE into its memory format
        ULONG_PTR frame_number = get_frame_from_PFN(available_pfn);
        pte->memory_format.frame_number = frame_number;
        pte->memory_format.valid = 1;

        // Map the VA to its new page.
        if (MapUserPhysicalPages (faulting_va, 1, &frame_number) == FALSE) {
            fatal_error("Could not map VA to page in MapUserPhysicalPages.");
        }
        return;
    }

    // TODO modify this to take into account the work that trimmer is doing -- get pages from standby instead.
    // If no page is available on the free list, then we will evict one from the head of the active list (FIFO)
    if (available_pfn == NULL) {
        available_pfn = get_first_active_page();
    }

    // If no pages are available from the active list, fail immediately
    if (available_pfn == NULL) {
        fatal_error("No PFNs available to trim from active list");
    }

    // Get the previous PTE mapping to this frame
    ULONG_PTR frame_number = get_frame_from_PFN(available_pfn);
    PPTE old_pte = available_pfn->PTE;

    // Unmap the old VA from this physical page
    if (MapUserPhysicalPages (get_VA_from_PTE(old_pte), 1, NULL) == FALSE) {
        fatal_error("Could not un-map old VA.");
    }

    // Unmap the old PTE, put it into disk format, and map it to its disk index;
    size_t disk_index = get_disk_index_from_pte(old_pte);
    map_pte_to_disk(old_pte, disk_index);

    // Write the page out to the disk, saving its disk index
    write_pages_to_disk(old_pte, 1);

    // Map the new VA to the free page
    if (MapUserPhysicalPages (faulting_va, 1, &frame_number) == FALSE) {
        fatal_error("Could not map VA to page in MapUserPhysicalPages.");
    }

    // Update the PFN's PTE to the new PTE
    available_pfn->PTE = pte;

    // If the PTE is in disk format, then we know we need to load its contents from the disk
    // This writes them into the page that our old PTE's VA was previously mapped to,
    // which we will presently be mapping to the faulting VA.
    if (pte->disk_format.status == PTE_ON_DISK) {
        load_page_from_disk(pte, faulting_va);
    }

    if (IS_PTE_ZEROED(pte)) {
        // Nothing to be done!
    }

    // Set the PFN to its active state and add it to the end of the active list
    SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
    InsertTailList(active_list, &available_pfn->entry);

    // Set the PTE into its memory format
    pte->memory_format.frame_number = frame_number;
    pte->memory_format.valid = 1;

#if DEBUG
    printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, frame_number, pte);
#endif
}
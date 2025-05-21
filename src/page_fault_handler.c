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

    // First, check for a soft fault: if the PTE has been mapped before and is NOT in its disk format,
    // we should be able to locate its PFN!
    if (!IS_PTE_ZEROED(pte) && IS_PTE_TRANSITION(pte)) {
        available_pfn = get_PFN_from_PTE(pte);

        // Find the state of the PFN
        if (IS_PFN_ACTIVE(available_pfn) || IS_PFN_FREE(available_pfn)) {
            fatal_error("Memory format invalid PTE has an active or free page...");
        }

        // If the page is in its modified state, we will remove it from the modified list,
        // Make its status active, map it to its VA, and update the PTE.
        // Since it is not in the disk slot yet, there is no need to update its disk metadata.
        if (IS_PFN_MODIFIED(available_pfn)) {
            modified_page_count--;

        }
        // Otherwise, we know this page mut be in its standby state.
        else if (IS_PFN_STANDBY(available_pfn)) {
            standby_page_count--;
            page_file_metadata[available_pfn->disk_index] = 0;
        }

        // Regardless of standby or modified, these steps should happen to perform a soft fault!
        RemoveEntryList(&available_pfn->entry);
        active_page_count++;
        *frame_numbers_to_map = pte->memory_format.frame_number;
        map_pages(1, faulting_va, frame_numbers_to_map);
        available_pfn->status = PFN_ACTIVE;
        set_PTE_to_valid(pte, pte->memory_format.frame_number);

        return TRUE;
    }

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
        *frame_numbers_to_map = get_frame_from_PFN(available_pfn);
        set_PTE_to_valid(pte,  *frame_numbers_to_map);

        // Map the VA to its new page.
        map_pages(1, faulting_va, frame_numbers_to_map);

#if DEBUG
        printf("Successfully mapped page from free list to faulting VA in iteration %d.\n", i);
#endif

        return TRUE;
    }

    // If no page is available on the zero or free list, then we will repurpose one from the standby list
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
    *frame_numbers_to_map = get_frame_from_PFN(available_pfn);
    PPTE old_pte = available_pfn->PTE;

    if (!IS_PTE_ON_DISK(pte) && !IS_PTE_ZEROED(pte)) {
        fatal_error("Faulting VA is not in disk state or zeroed state...");
    }

    // Update old pte -- move it into disk format
    map_pte_to_disk(old_pte, available_pfn->disk_index);

    // If PTE is zeroed, do not do the disk read. But if the PTE is on the disk, read its contents back!
    if (IS_PTE_ON_DISK(pte)) {
        // Get location of new pte on disk
        UINT64 disk_slot = pte->disk_format.disk_index;

        // Link contents from page file to kernel VA
        map_pages(1, kernel_read_va, frame_numbers_to_map);

        // Zero the page before transferring new data into it
        memset(kernel_read_va, 0, PAGE_SIZE);

        // Copy data from page file into physical pages
        memcpy(kernel_read_va, page_file + disk_slot * PAGE_SIZE, PAGE_SIZE);

        // Mark disk slot as available
        page_file_metadata[disk_slot] = 0;

        // Unmap page from kernel VA
        unmap_pages(1, kernel_read_va);
    }
    // Map page to user VA
    map_pages(1, faulting_va, frame_numbers_to_map);

    // Update PTE and PFN
    set_PTE_to_valid(pte, *frame_numbers_to_map);
    SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
    available_pfn->PTE = pte;


#if DEBUG
    printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, *frame_numbers_to_map, pte);
#endif

    return TRUE;
}
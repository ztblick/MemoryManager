//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/page_fault_handler.h"
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"
#include "../include/writer.h"

PULONG_PTR get_beginning_of_VA_region(PULONG_PTR va) {
    return get_VA_from_PTE(get_PTE_from_VA(va));
}


char* get_page_file_offset(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX) {
        fatal_error("Attempted to map to page file offset with disk slot exceeding 22 bit limit!");
    }
    return (page_file + disk_slot * PAGE_SIZE);
}

void clear_disk_slot(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX) {
        fatal_error("Attempted to clear disk slot disk slot exceeding 22 bit limit!");
    }
    page_file_metadata[disk_slot] = (char) DISK_SLOT_EMPTY;
    empty_disk_slots++;
}

void set_disk_slot(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX) {
        fatal_error("Attempted to clear disk slot disk slot exceeding 22 bit limit!");
    }
    page_file_metadata[disk_slot] = (char) DISK_SLOT_IN_USE;
    empty_disk_slots--;
}


BOOL validate_page(PULONG_PTR va) {
    PULONG_PTR page_start = (PULONG_PTR) ((ULONG_PTR) va / 0x1000 * 0x1000);
    for (PULONG_PTR spot = page_start; spot < application_va_base + VIRTUAL_ADDRESS_SIZE; spot++) {
        if (*spot != 0 && *spot > (ULONG_PTR) page_start && *spot < (ULONG_PTR) (application_va_base + VIRTUAL_ADDRESS_SIZE))
            return TRUE;
    }
    return FALSE;
 }

BOOL page_fault_handler(PULONG_PTR faulting_va, int i) {
#if DEBUG
    printf("\nResolving page fault...\n\n");
#endif

    // The PFN associated with the page that will be mapped to the faulting VA.
    PPFN available_pfn = NULL;

    // Grab the PTE for the VA
    PPTE pte = get_PTE_from_VA(faulting_va);

    // If we faulted on a valid VA, something is very wrong
    if (pte->memory_format.valid == 1) {
        fatal_error("Faulted on a hardware valid PTE.");
    }

    // SOFT FAULT RESOLUTION
    // If the PTE has been mapped before and is NOT in its disk format,
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
#if DEBUG
            printf("Resolving soft fault with modified page...\n");
#endif
        }
        // Otherwise, we know this page mut be in its standby state.
        else if (IS_PFN_STANDBY(available_pfn)) {
            standby_page_count--;
            clear_disk_slot(available_pfn->disk_index);
#if DEBUG
            printf("Resolving soft fault with standby page, clearing disk slot %llu\n", available_pfn->disk_index);
#endif
        }

        // Regardless of standby or modified, these steps should happen to perform a soft fault!
        // Remove the page from its list (standby or modified) and map the VA to it.
        RemoveEntryList(&available_pfn->entry);
        active_page_count++;
        *frame_numbers_to_map = pte->memory_format.frame_number;
        map_pages(1, faulting_va, frame_numbers_to_map);

        // Update the PTE and PFN to the active state.
        set_PFN_active(available_pfn, pte);
        set_PTE_to_valid(pte, pte->memory_format.frame_number);

        // Update statistics
        soft_faults_resolved++;

        // ENSURE THAT MEMORY READ BACK IS VALID!
#if DEBUG
        printf("VA: %p\n", faulting_va);
        printf("Value stored: %llu\n", *faulting_va);
        if (!validate_page(faulting_va))
            fatal_error("Page read from memory has invalid contents :(");
#endif

        return TRUE;
    }

    // Before resolving hard faults, double-check that the PTE thinks it should be hard faulting...
    if (!IS_PTE_ON_DISK(pte) && !IS_PTE_ZEROED(pte)) {
        fatal_error("Faulting VA is not in disk state or zeroed state...");
    }

    // HARD FAULT RESOLUTION
    // First: get a free or standby page
    if (!IsListEmpty(free_list)) {
        // Get the next free page.
        available_pfn = get_first_frame_from_list(free_list);
        NULL_CHECK(available_pfn, "Free page was null :(");
        free_page_count--;
        active_page_count++;
#if DEBUG
        printf("Iteration %d: Resolving hard fault with free page...\n", i);
#endif
    }

    // If no page is available on the zero or free list, then we will repurpose one from the standby list
    else if (!IsListEmpty(standby_list)) {

        // Remove the standby page from the standby list
        available_pfn = get_first_frame_from_list(standby_list);
        NULL_CHECK(available_pfn, "Standby page was null :(");
        standby_page_count--;
        active_page_count++;

        // Map the previous page's owner to its disk slot!
        PPTE old_pte = available_pfn->PTE;
        map_pte_to_disk(old_pte, available_pfn->disk_index);

#if DEBUG
        printf("Iteration %d: Resolving hard fault with standby page...\n", i);
#endif
    }
    // If no pages are available, return with an error message.
    else {
#if DEBUG
        printf("Fault handler could not resolve fault as no pages were available on zero, free, or standby!");
#endif
        return FALSE;
    }

    // Get the frame number
    *frame_numbers_to_map = get_frame_from_PFN(available_pfn);

    // Now that we have a page, let's check if we need to do a disk read.
    // If PTE is zeroed, do not do the disk read. But if the PTE is on the disk, read its contents back!
    if (IS_PTE_ON_DISK(pte)) {
        // Get location of new pte on disk
        UINT64 disk_slot = pte->disk_format.disk_index;

        // Map the kernel VA to the available frame
        map_pages(1, kernel_read_va, frame_numbers_to_map);

        // Zero the page before transferring new data into it
        // memset(kernel_read_va, 0, PAGE_SIZE);

        // Copy data from page file into physical pages
        memcpy(kernel_read_va, get_page_file_offset(disk_slot), PAGE_SIZE);

        // Mark disk slot as available
        clear_disk_slot(disk_slot);

        // Unmap page from kernel VA
        unmap_pages(1, kernel_read_va);
    }

    // Map page to user VA
    map_pages(1, faulting_va, frame_numbers_to_map);

    // ENSURE THAT MEMORY READ BACK IS VALID!
#if DEBUG
    if (IS_PTE_ON_DISK(pte)) {
        printf("VA: %p\n", faulting_va);
        printf("Value stored: %llu\n", *faulting_va);
        if (!validate_page(faulting_va))
            fatal_error("Page read from memory has invalid contents :(");
    }
#endif

    // Update PTE and PFN
    set_PTE_to_valid(pte, *frame_numbers_to_map);
    set_PFN_active(available_pfn, pte);

#if DEBUG
    printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, *frame_numbers_to_map, pte);
#endif
    hard_faults_resolved++;
    return TRUE;
}

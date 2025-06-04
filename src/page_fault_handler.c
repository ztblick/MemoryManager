//
// Created by zblickensderfer on 5/5/2025.
//

/*
 *  IMPORTANT NOTE ABOUT DISK SLOTS:
 *  Disk slots and disk indices are 1-indexed. The value stored in the PFN will be 1-indexed.
 *  This is done to allow 0 to be the value of a disk index for a PFN that is NOT mapped to disk.
 *  Pages in the page file are 0-indexed.
 *  So, any time we go from disk index to page file index (or in reverse) we will need a -1.
 */

#include "../include/page_fault_handler.h"
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"

// Masks off the last bits to round the VA up to the previous page boundary.
PULONG_PTR get_beginning_of_VA_region(PULONG_PTR va) {
    return (PULONG_PTR) ((ULONG_PTR) va & ~(PAGE_SIZE - 1));
}

// Since disk slot is from 1 to PAGES_IN_PAGE_FILE, we must decrement it when getting the offset.
char* get_page_file_offset(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX || disk_slot < MIN_DISK_INDEX) {
        fatal_error("Attempted to map to page file offset with disk slot exceeding 22 bit limit!");
    }
    return (page_file + (disk_slot - 1) * PAGE_SIZE);
}

void clear_disk_slot(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX || disk_slot < MIN_DISK_INDEX) {
        fatal_error("Attempted to clear disk slot disk slot exceeding 22 bit limit!");
    }
    page_file_metadata[disk_slot] = (char) DISK_SLOT_EMPTY;
    empty_disk_slots++;
}

void set_disk_slot(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX || disk_slot < MIN_DISK_INDEX) {
        fatal_error("Attempted to clear disk slot disk slot exceeding 22 bit limit!");
    }
    page_file_metadata[disk_slot] = (char) DISK_SLOT_IN_USE;
    empty_disk_slots--;
}


BOOL validate_page(PULONG_PTR va) {
    PULONG_PTR page_start = get_beginning_of_VA_region(va);
    for (PULONG_PTR spot = page_start; spot < page_start + PAGE_SIZE / BYTES_PER_VA; spot++) {
        if (*spot == (ULONG_PTR) spot)
            return TRUE;
    }
    return FALSE;
 }

// If we cannot access the VA, return FALSE.
BOOL va_faults_on_access(PULONG_PTR va) {
    __try {
        *va = (ULONG_PTR) va;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return TRUE;
    }
    return FALSE;
}

BOOL page_fault_handler(PULONG_PTR faulting_va, int i) {

    // When should the fault handler be allowed to fail? There are only two situations:
        // A - a hardware failure. This is beyond the scope of this program.
        // B - the user provides an invalid VA (beyond the VA space allocated)
    // This handles situation B:
    if (faulting_va < application_va_base || faulting_va > application_va_base + VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS)
        return FALSE;

    // This wil hold the physical frame number that we are mapping to the faulting VA.
    ULONG_PTR frame_numbers_to_map;

    // This variable exists to capture the state of the pte associated with the VA's PTE.
    // This is necessary to perform page validation (which only happens on disk-format PTEs) after the
    // PTE has been moved from disk to valid. Without this, there would be no way to know if the PTE
    // had previously been on disk, as the PTE itself has changed its state.
    BYTE pte_entry_state;

#if DEBUG
    printf("\nResolving page fault...\n\n");
#endif

    // This while loop is here to provide the mechanism for the fault-handler to try again.
    // This occurs when there are no pages available, and the fault handler has to wait
    // for the pages_available event to be set by the writer.
    // In that circumstance, this thread will wake up, and then wait grab the lock again.
    while (TRUE) {

        EnterCriticalSection(&page_fault_lock);

        // Check to see if your page fault has been resolved while you were waiting.
        if (!va_faults_on_access(faulting_va)) {

            LeaveCriticalSection(&page_fault_lock);
            return TRUE;
        }

        // The PFN associated with the page that will be mapped to the faulting VA.
        PPFN available_pfn = NULL;

        // Grab the PTE for the VA
        PPTE pte = get_PTE_from_VA(faulting_va);

        // If we faulted on a valid VA, something is very wrong
        if (pte->memory_format.valid == 1) {
            fatal_error("Faulted on a hardware valid PTE.");
        }

        // Set entry pte state
        pte_entry_state = pte->memory_format.status;

        // SOFT FAULT RESOLUTION
        // If the PTE is in transition, we should be able to locate its PFN!
        if (IS_PTE_TRANSITION(pte)) {
            available_pfn = get_PFN_from_PTE(pte);

            // Ensure that the page is either modified or standby.
            ASSERT(!IS_PFN_ACTIVE(available_pfn) && !IS_PFN_FREE(available_pfn));

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
            else {
                ASSERT(IS_PFN_STANDBY(available_pfn));
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
            frame_numbers_to_map = pte->memory_format.frame_number;
            map_pages(1, faulting_va, &frame_numbers_to_map);

            // Update the PTE and PFN to the active state.
            set_PFN_active(available_pfn, pte);
            set_PTE_to_valid(pte, pte->memory_format.frame_number);

            // Update statistics
            soft_faults_resolved++;

            // Ensure that memory read back into the page is valid.
            ASSERT(validate_page(faulting_va));
            LeaveCriticalSection(&page_fault_lock);
            return TRUE;
        }

        // Before resolving hard faults, double-check that the PTE thinks it should be hard faulting...
        ASSERT ((IS_PTE_ON_DISK(pte) || IS_PTE_ZEROED(pte)));

        // FIRST FAULT / HARD FAULT RESOLUTION
        // First: get a free or standby page
        if (!IsListEmpty(&free_list)) {
            // Get the next free page.
            available_pfn = get_first_frame_from_list(&free_list);
            NULL_CHECK(available_pfn, "Free page was null :(");
            free_page_count--;
            active_page_count++;
#if DEBUG
            printf("Iteration %d: Resolving hard fault with free page...\n", i);
#endif
        }

        // If no page is available on the zero or free list, then we will repurpose one from the standby list
        else if (!IsListEmpty(&standby_list)) {

            // Remove the standby page from the standby list
            available_pfn = get_first_frame_from_list(&standby_list);
            NULL_CHECK(available_pfn, "Standby page was null :(");
            standby_page_count--;
            active_page_count++;

            // Map the previous page's owner to its disk slot!
            PPTE old_pte = available_pfn->PTE;
            map_pte_to_disk(old_pte, available_pfn->disk_index);

        }
        // If no pages are available, release lock and wait for pages available event.
        // Once the event is triggered, return to the beginning of the loop, then wait for
        // the page fault lock again.
        else {
            LeaveCriticalSection(&page_fault_lock);
            WaitForSingleObject(standby_pages_ready_event, INFINITE);
            continue;
        }


        // Get the frame number
        frame_numbers_to_map = get_frame_from_PFN(available_pfn);

        // Map the kernel VA to the available frame
        map_pages(1, kernel_read_va, &frame_numbers_to_map);

        // Now that we have a page, let's check if we need to do a disk read.
        // If PTE is zeroed, do not do the disk read. But if the PTE is on the disk, read its contents back!
        if (IS_PTE_ON_DISK(pte)) {
            // Get location of new pte on disk
            UINT64 disk_slot = pte->disk_format.disk_index;

            // Copy data from page file into physical pages
            memcpy(kernel_read_va, get_page_file_offset(disk_slot), PAGE_SIZE);

            // Mark disk slot as available
            clear_disk_slot(disk_slot);
        }
        // Otherwise, our PTE is in its zeroed state. In this case, it is possible we are about to give it a page that
        // has memory on it that needs to be zeroed. Let's zero that memory now.
        else {
            ASSERT(IS_PTE_ZEROED(pte));

            // Zero the page
            memset(kernel_read_va, 0, PAGE_SIZE);
        }

        // Unmap page from kernel VA
        unmap_pages(1, kernel_read_va);

        // Map page to user VA
        map_pages(1, faulting_va, &frame_numbers_to_map);

        // Update PTE and PFN
        set_PTE_to_valid(pte, frame_numbers_to_map);
        set_PFN_active(available_pfn, pte);

        // Ensure that the page contents correctly contain the relevant VA!
        if (pte_entry_state == PTE_ON_DISK && !validate_page(faulting_va)) // Replace this reference to pte with reference to entry state variable
            fatal_error("Page read from memory has invalid contents :(");

#if DEBUG
        printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, frame_numbers_to_map, pte);
#endif
        hard_faults_resolved++;
        LeaveCriticalSection(&page_fault_lock);
        return TRUE;
    }
}

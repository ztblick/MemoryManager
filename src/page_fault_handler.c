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
    InterlockedIncrement64(&empty_disk_slots);
}

void set_disk_slot(UINT64 disk_slot) {
    if (disk_slot > MAX_DISK_INDEX || disk_slot < MIN_DISK_INDEX) {
        fatal_error("Attempted to clear disk slot disk slot exceeding 22 bit limit!");
    }

    page_file_metadata[disk_slot] = (char) DISK_SLOT_IN_USE;

    // Decrement the empty disk slot count without risking race conditions.
    InterlockedDecrement64(&empty_disk_slots);
}

void lock_disk_slot(UINT64 disk_slot) {
    EnterCriticalSection(&disk_metadata_locks[disk_slot]);
}

void unlock_disk_slot(UINT64 disk_slot) {
    LeaveCriticalSection(&disk_metadata_locks[disk_slot]);
}

BOOL try_acquire_page_from_list(PPFN* available_page_address, PPAGE_LIST list) {

    // First, we will try to get a page. To do so, we will ask the list for its size (without a lock).
    // If this returns a zero value, we can reasonably conclude that the list is empty
    while (!is_page_list_empty(list)) {

        // Now, we will get more serious. Let's lock the free list
        // then try to acquire the lock on its first element.
        lock_list_exclusive(list);

        // Let's make sure that the list is STILL empty...! If it's not, let's leave and try for standby pages.
        if (is_page_list_empty(list)) {
            unlock_list_exclusive(list);
            break;
        }

        PPFN pfn = peek_from_list_head(list);

        // If we can't acquire the lock on the front page, let's release all our locks,
        // then just try again!
        if (!try_lock_pfn(pfn)) {
            unlock_list_exclusive(list);
            continue;
        }

        // It is possible that the page, since being peeked at, was removed from the free or
        // standby lists. If that is the case, then we may not be able to acquire it.
        // It may now be an active page. So, we will double-check to make sure the page
        // has the anticipated status.
        if ((list == &free_list && !IS_PFN_FREE(pfn)) ||
            (list == &standby_list && !IS_PFN_STANDBY(pfn))) {
            unlock_list_exclusive(list);
            unlock_pfn(pfn);
            continue;
        }

        // Now that we have the page lock and we are sure it is in the right state,
        // we will remove it from its list.
        remove_page_from_list(list, pfn);
        ASSERT(pfn);

        // If we are removing the last standby page, we will reset the "pages available" event.
        if (list == &standby_list && is_page_list_empty(list)) {
            ResetEvent(standby_pages_ready_event);
        }

        // Finally, release the list lock!
        unlock_list_exclusive(list);

        // We have acquired a free page! Set this flag for future flow of control.
        *available_page_address = pfn;
        return TRUE;
    }

    return FALSE;
}

/*
 *  Here, we check to see if a page read back from memory is valid.
 *  There are two cases where it can be valid:
 *  1. The page contains the stamp from a VA that previously faulted on it.
 *  2. The page was about to be stamped, but before it could be, it was taken from
 *  the VA, causing another fault. In this case, a page to be validated could be zeroed,
 *  and that's okay.
 */

// This is currently ALWAYS returning true to avoid issues with Landy's TLB Flush bug.
BOOL validate_page(PULONG_PTR va) {

    // PULONG_PTR page_start = get_beginning_of_VA_region(va);
    // for (PULONG_PTR spot = page_start; spot < page_start + PAGE_SIZE / BYTES_PER_VA; spot++) {
    //     if (*spot != (ULONG_PTR) spot && *spot != 0) return FALSE;
    // }
    return TRUE;
 }

// If we cannot access the VA, return TRUE. If we CAN access the VA, return FALSE.
BOOL va_faults_on_access(PPTE pte) {

    return pte->memory_format.valid == PTE_INVALID;
}

BOOL resolve_soft_fault(PULONG_PTR faulting_va, PPTE pte) {

    // This wil hold the physical frame number that we are mapping to the faulting VA.
    ULONG_PTR frame_numbers_to_map;

    // Since our PTE is in transition format, we can get its PFN.
    PPFN available_pfn = get_PFN_from_PTE(pte);

    // Check for a disk-format PTE leading to an invalid page!
    if (!available_pfn) {
        // In this case, we should release locks and begin fault handling again.
        unlock_pte(pte);
        return FALSE;
    }

    // Lock the pfn
    lock_pfn(available_pfn);

    // Since we were waiting for the page, it is possible that it was associated
    // with a hard fault. It may have been given away from the standby list.
    // We will check its PTE to be sure:
    if (IS_PTE_ON_DISK(pte)) {
        // Same as above -- release locks and begin again.
        unlock_pfn(available_pfn);
        unlock_pte(pte);
        return FALSE;
    }

    // Since we faulted on this transition-format PTE, what could have happened to its frame?
    // It could be mid-write. It also could have been written out to the disk. It could also be hanging out
    // on the modified list. We will need to address all of these situations.

    // Ensure that the page is either modified or standby or mid-write.
    ASSERT(!IS_PFN_ACTIVE(available_pfn) && !IS_PFN_FREE(available_pfn));

    // If the PFN is mid-write, then we will need to set its soft fault mid-write bit, which will
    // tell the writer to clear the disk slot when it is finished writing.
    if (IS_PFN_MID_WRITE(available_pfn)) {
        set_soft_fault_write_bit(available_pfn);
    }

    // Otherwise, we know this page mut be in its standby or modified states.
    else {
        // This page list variable is necessary to know which page list we will remove from.
        // This allows us to decrement its size without calling one of its removal methods.
        PPAGE_LIST list_to_decrement;

        // If the page is in its modified state, we will remove it from the modified list,
        // Make its status active, map it to its VA, and update the PTE.
        // Since it is not in the disk slot yet, there is no need to update its disk metadata.
        if (IS_PFN_MODIFIED(available_pfn)) {
            list_to_decrement = &modified_list;
        }
        else {
            ASSERT(IS_PFN_STANDBY(available_pfn));

            // Acquire lock on disk slot, clear it, then release the lock.
            lock_disk_slot(available_pfn->disk_index);
            clear_disk_slot(available_pfn->disk_index);
            unlock_disk_slot(available_pfn->disk_index);

            list_to_decrement = &standby_list;
        }

        // Remove the page from its list (standby or modified)
        lock_list_and_remove_page(list_to_decrement, available_pfn);
    }

    // Regardless of standby or modified or mid-write, these steps should happen to perform a soft fault!
    frame_numbers_to_map = pte->memory_format.frame_number;
    map_pages(1, faulting_va, &frame_numbers_to_map);

    // Update the PTE and PFN to the active state.
    set_PFN_active(available_pfn, pte);
    set_PTE_to_valid(pte, pte->memory_format.frame_number);

    // Update statistics
    InterlockedIncrement64(&soft_faults_resolved);

    // Ensure that memory read back into the page is valid.
    ASSERT(validate_page(faulting_va));

    // Release locks and return!
    unlock_pfn(available_pfn);
    unlock_pte(pte);

    return TRUE;
}

void check_to_initiate_trimming(void) {
    free_page_count = get_size(&free_list);
    standby_page_count = get_size(&standby_list);

    // If there is sufficient need, age and trim pages for anticipated page faults.
    if (free_page_count + standby_page_count < WORKING_SET_THRESHOLD) {
        // SetEvent(initiate_aging_event);
        SetEvent(initiate_trimming_event);
    }
}

BOOL page_fault_handler(PULONG_PTR faulting_va) {

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

    // The PFN associated with the page that will be mapped to the faulting VA.
    PPFN available_pfn;

    // This while loop is here to provide the mechanism for the fault-handler to try again.
    // This occurs when there are no pages available, and the fault handler has to wait
    // for the pages_available event to be set by the writer.
    // In that circumstance, this thread will wake up, and then wait grab the lock again.
    while (TRUE) {

        // Grab the PTE for the VA
        PPTE pte = get_PTE_from_VA(faulting_va);

        // Acquire the PTE lock
        lock_pte(pte);

        // Check to see if your page fault has been resolved while you were waiting.
        if (!va_faults_on_access(pte)) {

            // Since the PTE is actually valid (the fault must have been resolved by another thread)
            // we can release the PTE lock.
            unlock_pte(pte);

            return TRUE;
        }

        // Set entry pte state
        pte_entry_state = pte->memory_format.status;

        //~~~~~~~~~~~~~~~~~~~~~~~//
        // SOFT FAULT RESOLUTION //
        //~~~~~~~~~~~~~~~~~~~~~~~//
        // If the PTE is in transition, we should be able to locate its PFN!
        if (IS_PTE_TRANSITION(pte)) {
            // If we can resolve the soft fault, we are done!
            if (resolve_soft_fault(faulting_va, pte)) return TRUE;
            // Otherwise, we have our edge case in which we fault on a pte
            // that was written out to disk (on a standby page grabbed by a hard fault).
            // In this case, we simply try again.
            continue;
        }

        // Before resolving hard faults, double-check that the PTE thinks it should be hard faulting...
        ASSERT ((IS_PTE_ON_DISK(pte) || IS_PTE_ZEROED(pte)));

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
        // FIRST FAULT / HARD FAULT RESOLUTION //
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

        BOOL free_page_acquired = FALSE;
        BOOL standby_page_acquired = FALSE;

        // If there is no soft fault, let's take this chance to initiate trimming (if necessary)
        check_to_initiate_trimming();

        // First, attempt to grab a free page.
        free_page_acquired = try_acquire_page_from_list(&available_pfn, &free_list);

        // If no page is available on the free list, then we will repurpose one from the standby list
        if (!free_page_acquired) {

            standby_page_acquired = try_acquire_page_from_list(&available_pfn, &standby_list);

            if (standby_page_acquired) {
                // Map the previous page's owner to its disk slot! We are doing this WITHOUT the page table lock.
                // This is okay, but we will now need to double-check the PTE on a simultaneous soft fault.
                // If that PTE lock is acquired and the thread is waiting for the page lock, then the PTE will have changed
                // while they were waiting.
                PPTE old_pte = available_pfn->PTE;
                map_pte_to_disk(old_pte, available_pfn->disk_index);
            }
        }

        // If no pages are available, release locks and wait for pages available event.
        // Once the event is triggered, return to the beginning of the loop, then wait for
        // the page fault lock again.
        if (!free_page_acquired && !standby_page_acquired) {

            unlock_pte(pte);
            WaitForSingleObject(standby_pages_ready_event, INFINITE);
            continue;
        }

        // At this point, we KNOW we have an available page. It is locked, and its PTE is locked.
        // Let's now resolve the fault.

        // Get the frame number
        frame_numbers_to_map = get_frame_from_PFN(available_pfn);

        // Map the kernel VA to the available frame
        EnterCriticalSection(&kernel_read_lock);
        map_pages(1, kernel_read_va, &frame_numbers_to_map);

        // Now that we have a page, let's check if we need to do a disk read.
        // If PTE is zeroed, do not do the disk read. But if the PTE is on the disk, read its contents back!
        if (IS_PTE_ON_DISK(pte)) {
            // Get location of new pte on disk
            UINT64 disk_slot = pte->disk_format.disk_index;

            // Lock disk slot
            lock_disk_slot(pte->disk_format.disk_index);

            // Copy data from page file into physical pages
            memcpy(kernel_read_va, get_page_file_offset(disk_slot), PAGE_SIZE);

            // Mark disk slot as available
            clear_disk_slot(disk_slot);

            // Unlock disk slot
            unlock_disk_slot(pte->disk_format.disk_index);
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

        // Unlock kernel read space
        LeaveCriticalSection(&kernel_read_lock);

        // Map page to user VA
        map_pages(1, faulting_va, &frame_numbers_to_map);

        // Ensure that the page contents correctly contain the relevant VA!
        ASSERT((pte_entry_state != PTE_ON_DISK || validate_page(faulting_va)));

        // Update PTE and PFN
        set_PTE_to_valid(pte, frame_numbers_to_map);
        set_PFN_active(available_pfn, pte);

        // Update statistics
        InterlockedIncrement64(&hard_faults_resolved);

        // Release locks
        unlock_pfn(available_pfn);
        unlock_pte(pte);
        return TRUE;
    }
}


#include "../include/page_fault_handler.h"

// Masks off the last bits to round the VA up to the previous page boundary.
PULONG_PTR get_beginning_of_VA_region(PULONG_PTR va) {
    return (PULONG_PTR) ((ULONG_PTR) va & ~(PAGE_SIZE - 1));
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

        // It is possible that the page is still on the list, but no longer at the head. Let's double-check:
        if (!is_at_head_of_list(list, pfn)) {
            unlock_list_exclusive(list);
            unlock_pfn(pfn);
            continue;
        }

        // Now that we have the page lock and we are sure it is in the right state,
        // we will remove it from its list.
        remove_page_from_list(list, pfn);
        decrement_available_count();
        ASSERT(pfn);

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
#if DEBUG
    PULONG_PTR page_start = get_beginning_of_VA_region(va);
    for (PULONG_PTR spot = page_start; spot < page_start + PAGE_SIZE / BYTES_PER_VA; spot++) {
        if (*spot != (ULONG_PTR) spot && *spot != 0) return FALSE;
    }
#endif
    return TRUE;
 }

// If we cannot access the VA, return TRUE. If we CAN access the VA, return FALSE.
BOOL va_faults_on_access(PPTE pte) {
    return pte->memory_format.valid == PTE_INVALID;
}

BOOL resolve_soft_fault(PPTE pte) {

    // Now we will catch a snapshot of the PTE, because it CAN be changed without the lock
    // when we are sending it to the disk.
    lock_pte(pte);
    PTE pte_snapshot = *pte;

    // If someone else updated the PTE since we began, we release the lock.
    // We will try again from the beginning of the fault handler.
    if (!IS_PTE_TRANSITION(&pte_snapshot)) {
        unlock_pte(pte);
        return FALSE;
    }
    // Since our PTE is in transition format, we can get its PFN.
    PPFN available_pfn = get_PFN_from_PTE(&pte_snapshot);

    // Check for a disk-format PTE leading to an invalid page!
    // In this case, we should release locks and begin fault handling again.
    if (!available_pfn) {
        ASSERT(IS_PTE_ON_DISK(pte));
        unlock_pte(pte);
        return FALSE;
    }

    // Now we will lock the pfn
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

    // Now we should DEFINITELY have a transition PTE, since we have the page lock
    // and we already checked for disk-format PTEs. It should not be possible
    // for a locked PTE to go from transition to any other state than disk.
    // And a disk-state PTE cannot move to active without the PTE lock.
    ASSERT(IS_PTE_TRANSITION(pte));

    // Since we faulted on this transition-format PTE, what could have happened to its frame?
    // It could be mid-write or mid-trim.
    // It also could have been written out to the disk and is on the standby list.
    // Or it could be hanging out on the modified list.
    // We will need to address all of these situations.

    // Ensure that the page is either modified or standby or mid-write or mid-write.
    ASSERT(!IS_PFN_ACTIVE(available_pfn) && !IS_PFN_FREE(available_pfn));

    // If the PFN is mid-trim or mid-write, then we will not need to remove it from any list.
    //But if it is in its standby or modified states, we will need to remove it from the list!
    if (!IS_PFN_MID_WRITE(available_pfn) && !IS_PFN_MID_TRIM(available_pfn)) {

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

            // Clear the disk slot for the copy of our data on the disk.
            // We can do this lockless because we hold the PTE and PFN locks.
            clear_disk_slot(available_pfn->fields.disk_index);

            list_to_decrement = &standby_list;
        }

        // Remove the page from its list (standby or modified)
        lock_list_and_remove_page(list_to_decrement, available_pfn);
    }

    // Regardless, these steps should happen to perform a soft fault!
    // Update the PTE and PFN to the active state. Map the page!
    map_single_page_from_pte(pte);
    set_PFN_active(available_pfn, pte);
    set_PTE_to_valid(pte, pte->memory_format.frame_number);

    // Release locks and return!
    unlock_pfn(available_pfn);
    unlock_pte(pte);

    // Update statistics
    InterlockedIncrement64(&n_soft);

    return TRUE;
}

BOOL resolve_hard_fault(PPTE pte, PTHREAD_INFO user_thread_info) {

    // This wil hold the physical frame number that we are mapping to the faulting VA.
    ULONG_PTR frame_number_to_map;

    // This variable exists to capture the state of the pte associated with the VA's PTE.
    // This is necessary to perform page validation (which only happens on disk-format PTEs) after the
    // PTE has been moved from disk to valid. Without this, there would be no way to know if the PTE
    // had previously been on disk, as the PTE itself has changed its state.
    BYTE pte_entry_state;

    // The PFN associated with the page that will be mapped to the faulting VA.
    PPFN available_pfn;

    BOOL free_page_acquired = FALSE;
    BOOL standby_page_acquired = FALSE;

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
            map_pte_to_disk(old_pte, available_pfn->fields.disk_index);
        }
    }

    // If no pages are available, release locks and wait for pages available event.
    // We will MANUALLY initiate trimming here, although we were hoping to avoid it.
    // Once the event is triggered, return to the beginning of the loop, then wait for
    // the page fault lock again.
    if (!free_page_acquired && !standby_page_acquired) {
        SetEvent(initiate_trimming_event);
        WaitForSingleObject(standby_pages_ready_event, INFINITE);
        return FALSE;
    }

    // At this point, we KNOW we have an available page. It is locked.
    // Let's now resolve the fault.

    // First, we will set OUR kernel read VA space.
    PULONG_PTR kernel_read_va = user_thread_info->kernel_va_spaces[user_thread_info->kernel_va_index];

    // Get the frame number
    frame_number_to_map = get_frame_from_PFN(available_pfn);

    // Map the kernel VA to the available frame
    map_pages(1, kernel_read_va, &frame_number_to_map);

    // Since we are committing to using this read va, we will need to increase the count.
    user_thread_info->kernel_va_index++;

    // Now we will finally try to acquire the PTE lock
    // If we cannot get it, we will try again.
    BOOL pte_lock_acquired = try_lock_pte(pte);

    // If the pte is no longer hard faulting, add the page to the free list and return
    if (!pte_lock_acquired || IS_PTE_VALID(pte) || IS_PTE_TRANSITION(pte)) {

        // If we were able to get the lock, release it. There is no need for it.
        if (pte_lock_acquired) unlock_pte(pte);

        // Zero the page
        memset(kernel_read_va, 0, PAGE_SIZE);

        // Add the page to the free list and update its status
        lock_list_then_insert_to_tail(&free_list, &available_pfn->entry);
        increment_available_count();
        set_PFN_free(available_pfn);

        // Unlock the page
        unlock_pfn(available_pfn);
        return FALSE;
    }

    // Now that we have a page, let's check if we need to do a disk read.
    // If PTE is zeroed, do not do the disk read. But if the PTE is on the disk, read its contents back!
    if (IS_PTE_ON_DISK(pte)) {

        // Get location of new pte on disk
        UINT64 disk_slot = pte->disk_format.disk_index;

        // Copy data from page file into physical pages
        memcpy(kernel_read_va, get_page_file_offset(disk_slot), PAGE_SIZE);

        // Mark disk slot as available. We can do this lockless
        // because we hold the PTE & PFN locks.
        clear_disk_slot(disk_slot);
    }
    // Otherwise, our PTE is in its zeroed state. In this case, it is possible we are about to give it a page that
    // has memory on it that needs to be zeroed. Let's zero that memory now.
    else {
        ASSERT(IS_PTE_ZEROED(pte));

        // Zero the page
        memset(kernel_read_va, 0, PAGE_SIZE);
    }

    // Map page to user VA
    map_pages(1, get_VA_from_PTE(pte), &frame_number_to_map);

    // Ensure that the page contents correctly contain the relevant VA!
    ASSERT(validate_page(get_VA_from_PTE(pte)));

    // Update PTE and PFN
    set_PTE_to_valid(pte, frame_number_to_map);
    set_PFN_active(available_pfn, pte);

    // Release locks
    unlock_pfn(available_pfn);
    unlock_pte(pte);

    // Update statistics
    InterlockedIncrement64(&n_hard);
    return TRUE;
}


BOOL page_fault_handler(PULONG_PTR faulting_va, PTHREAD_INFO user_thread_info) {

    // When should the fault handler be allowed to fail? There are only two situations:
        // A - a hardware failure. This is beyond the scope of this program.
        // B - the user provides an invalid VA (beyond the VA space allocated)
    // This handles situation B:
    if (faulting_va < application_va_base || faulting_va > application_va_base + VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS)
        return FALSE;

    // This is the PTE of our faulting VA. We will need him. But we will lock him as little
    // as possible, as there is a lot of locking of PTEs.
    PPTE pte = get_PTE_from_VA(faulting_va);

    // This while loop is here to provide the mechanism for the fault-handler to try again.
    // This occurs when there are no pages available, and the fault handler has to wait
    // for the pages_available event to be set by the writer.
    // In that circumstance, this thread will wake up, and then wait grab the lock again.
    while (!IS_PTE_VALID(pte)) {

        // Here are the kernel VA spaces for our reads and our index keeping track of which
        // ones have been mapped so far.
        // First: if we need to unmap ALL the kernel read VAs, we will do so in one big batch
        if (user_thread_info->kernel_va_index == NUM_KERNEL_READ_ADDRESSES) {
            if (!MapUserPhysicalPagesScatter(user_thread_info->kernel_va_spaces,
                NUM_KERNEL_READ_ADDRESSES,
                NULL)) DebugBreak();
            user_thread_info->kernel_va_index = 0;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~//
        // SOFT FAULT RESOLUTION //
        //~~~~~~~~~~~~~~~~~~~~~~~//

        // If the PTE is in transition, we should be able to locate its PFN!
        if (IS_PTE_TRANSITION(pte)) {
            // If we can resolve the soft fault, we are done!
            if (resolve_soft_fault(pte)) return TRUE;
            // Otherwise, we have our edge case in which we fault on a pte
            // that was written out to disk (on a standby page grabbed by a hard fault).
            // In this case, we simply try again.
            continue;
        }

        // Before resolving hard faults, double-check that the PTE thinks it should be hard faulting...
        if (!IS_PTE_ON_DISK(pte) && !IS_PTE_ZEROED(pte)) {
            // The PTE either went to transition or back to active. Either way, we will just try again.
            continue;
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
        // FIRST FAULT / HARD FAULT RESOLUTION //
        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
        if (resolve_hard_fault(pte, user_thread_info)) return TRUE;

        // If we get here, then we were unable to conclusively resolve the fault.
        // No worries! We will go around and try again.
    }
    return TRUE;
}


#include "page_fault_handler.h"

#include <sys/stat.h>

// Masks off the last bits to round the VA up to the previous page boundary.
PULONG_PTR get_beginning_of_VA_region(PULONG_PTR va) {
    return (PULONG_PTR) ((ULONG_PTR) va & ~(PAGE_SIZE - 1));
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

#if DEBUG
    validate_pfn(available_pfn);
#endif

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

            // Check to see if the modified list needs to be refilled
            signal_event_if_list_is_about_to_run_low(   list_to_decrement,
                                                        initiate_trimming_event,
                                                        TRIMMING_THREAD_ID);
        }

        else {
            ASSERT(IS_PFN_STANDBY(available_pfn));

            // Clear the disk slot for the copy of our data on the disk.
            // We can do this lockless because we hold the PTE and PFN locks.
            clear_disk_slot(available_pfn->fields.disk_index);
            list_to_decrement = &standby_list;

            // Check to see if the standby list needs to be refilled
            signal_event_if_list_is_about_to_run_low(   list_to_decrement,
                                                        initiate_writing_event,
                                                        WRITING_THREAD_ID);
        }

        // Remove the page from its list (standby or modified)
        remove_page_on_soft_fault(list_to_decrement, available_pfn);
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
    InterlockedIncrement64(&stats.n_soft);

    return TRUE;
}

/*
    Attempts to LOCK and return a free page. Re-fills the cache if possible.
    If no free pages are to be found, returns false.
 */
BOOL acquire_free_page(PUSER_THREAD_INFO thread_info, PPFN *available_page_address) {

    // First, check if the cache is empty. If it is, we will try to get free pages
    // to re-fill it
    USHORT count = thread_info->free_page_count;
    if (count == 0) {

        // Here, we try to re-fill the cache. If this fails, we will return FALSE.
        if (!try_get_free_pages(thread_info)) return FALSE;
    }

    // Now that we know we have pages in the cache, we will pop one off to resolve our fault.
    count = thread_info->free_page_count;
    count--;
    *available_page_address = thread_info->free_page_cache[count];
    thread_info->free_page_count = count;
    lock_pfn(*available_page_address);
    return TRUE;
}

BOOL move_batch_from_standby_to_cache(PUSER_THREAD_INFO thread_info) {
    PPFN pfn;

    // Grab a batch of pages from the standby list
    USHORT batch_size = remove_batch_from_list_head(&standby_list,
                                                    &pfn,
                                                    FREE_PAGE_CACHE_SIZE / 4);

    // Using recent consumption, decide if we need to signal the writer to bring in more standby pages!
    signal_event_if_list_is_about_to_run_low(   &standby_list,
                                                initiate_writing_event,
                                                WRITING_THREAD_ID);

    if (batch_size == 0) return FALSE;

    // Copy the pages into the free page cache
    // While doing so, we MUST map their old PTEs to the disk!
    PPFN next;
    for (USHORT i = 0; i < batch_size; ++i) {

#if DEBUG
        validate_pfn(pfn);
#endif

        // Map the previous page's owner to its disk slot! We are doing this WITHOUT the page table lock.
        // This is okay, but we will now need to double-check the PTE on a simultaneous soft fault.
        // If that PTE lock is acquired and the thread is waiting for the page lock, then the PTE will have changed
        // while they were waiting.
        PPTE old_pte = pfn->PTE;
        map_pte_to_disk(old_pte, pfn->fields.disk_index);

        // Now we can copy the page into the cache and release its lock
        thread_info->free_page_cache[i] = pfn;
        next = pfn->flink;
        unlock_pfn(pfn);
        pfn = next;
    }

    ASSERT(thread_info->free_page_count == 0);
    thread_info->free_page_count = batch_size;
    return TRUE;
}

VOID signal_event_if_list_is_about_to_run_low(PPAGE_LIST list,
                                              HANDLE event_to_set,
                                              USHORT thread_id) {

    // Find the amount of pages expected to be consumed while the event is executed
    double time_to_perform_event = stats.worker_runtimes[thread_id];
    double consumption_rate = stats.page_consumption_per_second;
    LONG64 pages_consumed_during_event = (LONG64) (consumption_rate * time_to_perform_event);
    LONG64 current_list_size = list->list_size;

    // If the list is expected to run low, we will initiate our event now to counteract it
    if (current_list_size - pages_consumed_during_event < LOW_PAGE_THRESHOLD)
        SetEvent(event_to_set);
}

/*
    Unmaps all the thread's kernal VAs if they have all been used.
    If they haven't all been used -- it does nothing.
 */
void unmap_and_reset_all_kernal_va_for_this_thread(PUSER_THREAD_INFO user_thread_info) {
    if (user_thread_info->kernel_va_index != NUM_KERNEL_READ_ADDRESSES) return;

    // Gather the VAs to unmap
    PULONG_PTR base_kernal_va = user_thread_info->kernel_va_space;
    PULONG_PTR VAs_to_unmap[NUM_KERNEL_READ_ADDRESSES];
    for (int i = 0; i < NUM_KERNEL_READ_ADDRESSES; ++i) {
        VAs_to_unmap[i] = base_kernal_va + i * PAGE_SIZE / 8;
    }

    // Do the batch unmap
    BOOL success = MapUserPhysicalPagesScatter(VAs_to_unmap,
                                    NUM_KERNEL_READ_ADDRESSES,
                                                NULL);

    // Check that the unmap succeeded
    ASSERT(success);

    // Reset the index to zero
    user_thread_info->kernel_va_index = 0;
}

BOOL resolve_hard_fault(PPTE pte, PUSER_THREAD_INFO thread_info) {

    // This wil hold the physical frame number that we are mapping to the faulting VA.
    ULONG_PTR frame_number_to_map;

    // The PFN associated with the page that will be mapped to the faulting VA.
    PPFN available_pfn;

    // First, we will attempt to grab a free page from our cache or from a free list
    BOOL free_page_acquired;
    while (TRUE) {
        free_page_acquired = acquire_free_page(thread_info, &available_pfn);
        if (free_page_acquired) break;

        // We will now try to move a batch of pages from standby to the cache.
        // If we are able to do so, we will loop around again and grab a page from the cache!
        // If we are NOT able to get a page, we will break and set an event for the trimmer.
        BOOL standby_page_acquired = move_batch_from_standby_to_cache(thread_info);
        if (!standby_page_acquired) {
            // If no pages can be grabbed from the standby list, we will reset its event
            // so we will wait (and effectively track latency).
            ResetEvent(standby_pages_ready_event);
            break;
        }
    }

    // If no pages are available, release locks and wait for pages available event.
    // We will MANUALLY initiate trimming here, although we were hoping to avoid it.
    // Once the event is triggered, return to the beginning of the loop, then wait for
    // the page fault lock again.
    if (!free_page_acquired) {
        SetEvent(initiate_trimming_event);

        LONGLONG start = get_timestamp();
        WaitForSingleObject(standby_pages_ready_event, INFINITE);

        LONGLONG end = get_timestamp();
        InterlockedAdd64(&stats.wait_time, (end - start));
        InterlockedIncrement64(&stats.hard_faults_missed);

        return FALSE;
    }
    // At this point, we KNOW we have an available page. It is locked.
    // Let's now resolve the fault.

    // Now we will set OUR kernel read VA space.
    ASSERT(thread_info->kernel_va_index < NUM_KERNEL_READ_ADDRESSES);                       // TODO figure out why this is firing...!
    PULONG_PTR kernel_read_va = thread_info->kernel_va_space + thread_info->kernel_va_index * PAGE_SIZE / 8;

    // Get the frame number
    frame_number_to_map = get_frame_from_PFN(available_pfn);

    // Now we will finally try to acquire the PTE lock
    // If we cannot get it, we will try again.
    BOOL pte_lock_acquired = try_lock_pte(pte);

    // If the pte is no longer hard faulting, add the page to the free list and return
    if (!pte_lock_acquired || IS_PTE_VALID(pte) || IS_PTE_TRANSITION(pte)) {

        // If we were able to get the lock, release it. There is no need for it.
        if (pte_lock_acquired) unlock_pte(pte);

        // Add the page to the free list and update its status
        add_page_to_free_lists(available_pfn, thread_info->thread_id);
        increment_available_count();
        set_PFN_free(available_pfn);

        // Unlock the page
        unlock_pfn(available_pfn);
        return FALSE;
    }

    // Since we have the PTE and page locks, we can now safely map the page.
    // We map the available frame to the kernel VA AND the faulting VA at the same time (for efficiency)
    map_both_va_to_same_page(kernel_read_va, get_VA_from_PTE(pte), frame_number_to_map);

    // Since we are committing to using this read va, we will need to increase the count.
    thread_info->kernel_va_index++;

    // Now that we have the page in memory, let's check if we need to do a disk read.
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

    // Ensure that the page contents correctly contain the relevant VA!
    ASSERT(validate_page(get_VA_from_PTE(pte)));

    // Update PTE and PFN
    set_PTE_to_valid(pte, frame_number_to_map);
    set_PFN_active(available_pfn, pte);

    // Release locks
    unlock_pfn(available_pfn);
    unlock_pte(pte);

    // Update statistics
    InterlockedIncrement64(&stats.n_hard);

    // If we need to unmap ALL the kernel read VAs, we will do so in one big batch
    unmap_and_reset_all_kernal_va_for_this_thread(thread_info);

    return TRUE;
}

BOOL page_fault_handler(PULONG_PTR faulting_va, PUSER_THREAD_INFO user_thread_info) {

    // When should the fault handler be allowed to fail? There are only two situations:
        // A - a hardware failure. This is beyond the scope of this program.
        // B - the user provides an invalid VA (beyond the VA space allocated)
    // This handles situation B:
    if (faulting_va < vm.application_va_base || faulting_va >= vm.application_va_base + vm.va_size_in_pointers)
        return FALSE;

    // This is the PTE of our faulting VA. We will need him. But we will lock him as little
    // as possible, as there is a lot of locking of PTEs.
    PPTE pte = get_PTE_from_VA(faulting_va);

    // This while loop is here to provide the mechanism for the fault-handler to try again.
    // This occurs when there are no pages available, and the fault handler has to wait
    // for the pages_available event to be set by the writer.
    // In that circumstance, this thread will wake up, and then wait grab the lock again.
    while (!IS_PTE_VALID(pte)) {

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

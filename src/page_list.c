//
// Created by ztblick on 6/5/2025.
//

#include "../include/page_list.h"

PAGE_LIST_ARRAY free_lists;
PAGE_LIST zero_list;
PAGE_LIST modified_list;
PAGE_LIST standby_list;

VOID add_page_to_free_lists(PPFN page, ULONG first_index) {

    ULONG count = free_lists.number_of_lists;
    ULONG index = first_index % count;
    PPAGE_LIST list;

    while (TRUE) {

        // Scan through free lists, finding one you can lock
        // once locked, add the page to the tail
        list = &free_lists.list_array[index];

        if (!try_lock_list_exclusive(list)) {
            index = (index + 1) % count;
            continue;
        }

        insert_to_list_tail(list, page);
        unlock_list_exclusive(list);

        // Update metadata and return
#if DEBUG
        increment_list_size(list);
#endif
        increment_free_lists_total_count();

        return;
    }
}

// First index is set by the thread ID -- we will mod it by our number of lists to guarantee it is in bounds.
BOOL try_get_free_page(PPFN *address_to_save, ULONG first_index) {

    // First, just see if there are ANY free pages.
    if (free_lists.page_count == 0) return FALSE;

    ULONG attempts = 0;
    ULONG count = free_lists.number_of_lists;
    ULONG index = first_index % count;
    PPFN page;

    while (attempts < count) {

        page = try_pop_from_free_list(index);
        if (page != NULL) {
            *address_to_save = page;
            lock_pfn(page);
            return TRUE;
        }

        // Increment attempts and wrap index (if necessary)
        index++;
        attempts++;
        index %= count;
    }
    return FALSE;
}

PPFN try_pop_from_free_list(ULONG list_index) {

    ASSERT (list_index < free_lists.number_of_lists);
    PPAGE_LIST list = &free_lists.list_array[list_index];

    // Check for pages, and then try for lock
    if (is_page_list_empty(list)) return NULL;
    if (!try_lock_list_exclusive(list)) return NULL;
    if (is_page_list_empty(list)) {
        unlock_list_exclusive(list);
        return NULL;
    }

    // With the lock acquired, we can remove our entry
    PPFN page = remove_from_head_of_list(list);
    unlock_list_exclusive(list);

#if DEBUG
    validate_list(list);

    // list size is only used in debugger
    decrement_list_size(list);
#endif

    // Update metadata and return
    decrement_free_lists_total_count();
    decrement_available_count();
    return page;
}

VOID initialize_page_list(PPAGE_LIST list) {
    list->head = zero_malloc(sizeof(PFN));
    initialize_list_head(list->head);
    list->list_size = 0;
    InitializeSRWLock(&list->lock);
}

BOOL is_page_list_empty(PPAGE_LIST list) {

    PPFN head = list->head;
    return head->flink == head;
}

ULONG64 get_size(PPAGE_LIST list) {
    return list->list_size;
}

VOID increment_free_lists_total_count(VOID) {
    InterlockedIncrement64(&free_lists.page_count);
#if DEBUG
    validate_free_counts();
#endif
}

VOID decrement_free_lists_total_count(VOID) {
    InterlockedDecrement64(&free_lists.page_count);
#if DEBUG
    validate_free_counts();
#endif
}

VOID increment_list_size(PPAGE_LIST list) {
    InterlockedIncrement64(&list->list_size);
}

VOID decrement_list_size(PPAGE_LIST list) {
    InterlockedDecrement64(&list->list_size);
}

VOID change_list_size(PPAGE_LIST list, LONG64 amt) {
    InterlockedAdd64(&list->list_size, amt);
}

VOID remove_page_on_soft_fault(PPAGE_LIST list, PPFN pfn) {
	// First, we will try to lock pfn, its flink, and its blink
	// We will attempt this a few times. If we are not successful,
	// then we will lock the list exclusively.
	ULONG attempts = 0;
    ULONG wait_time = 1;
	PPFN flink = pfn->flink;
	PPFN blink = pfn->blink;

	// We cannot proceed while someone else has an exclusive lock on this
	// list, so we will wait until we can acquire the shared lock
#if DEBUG
    validate_list(list);
#endif

	// Attempt to grab both flink and blink locks
	while (attempts < MAX_SOFT_ACCESS_ATTEMPTS) {
		attempts++;
	    wait_time = (wait_time << 1);

	    if (!try_lock_list_shared(list)) {
            wait(wait_time);
	        continue;
	    }

		if (!try_lock_pfn(flink)) {
		    unlock_list_shared(list);
		    wait(wait_time);
		    continue;
		}

	    // Special case: if this is the only page on the list, then the flink and the blink are the same.
	    // They must both be the head.
	    if (flink == blink) {
	        ASSERT(flink == list->head);
	    }
	    else if (!try_lock_pfn(blink)) {
	        unlock_list_shared(list);
	        unlock_pfn(flink);
	        wait(wait_time);
	        continue;
	    }

	    // If we can get both locks, we can safely remove our entry!
	    remove_page_from_list(pfn);

	    // Release locks
	    unlock_list_shared(list);
	    unlock_pfn(blink);                      // Since the writer goes forward from the head, let's unlock blinks FIRST
	    if (flink != blink) unlock_pfn(flink);

#if DEBUG
	    validate_list(list);
#endif

	    // Update metadata and reset events, if necessary
        decrement_list_size(list);
	    if (list == &standby_list) {
	        decrement_available_count();

	        if (is_page_list_empty(&standby_list)) ResetEvent(standby_pages_ready_event);
	    }
	    return;
	}

#if DEBUG
    validate_list(list);
#endif
	// If we get here, we will need to lock the list exclusively
    lock_list_exclusive(list);

    // Remove our relevant page, then immediately unlock the list
    remove_page_from_list(pfn);
    unlock_list_exclusive(list);

    // Now we can update metadata appropriately. This happens without the lock, as none
    // of it is mission-critical.
    decrement_list_size(list);

    // If we have pulled off the standby list, we will want to decrement our available count
    if (list == &standby_list) {
        decrement_available_count();

        // And if it happens to be the last standby page, we will want to hold all other faulting
        // threads until there are available standby pages.
        if (is_page_list_empty(&standby_list)) ResetEvent(standby_pages_ready_event);
    }
#if DEBUG
    validate_list(list);
#endif
}

/*
 *  Precondition: PFN is already locked when called by writer. PFN is not locked
 *  when called by trimmer, but PTE is locked, which protects the page.
 */
VOID insert_page_to_tail(PPAGE_LIST list, PPFN pfn) {

    PPFN head = list->head;

    // Continue to try to acquire the necessary page locks
    while (TRUE) {

        lock_list_shared(list);

        // First, get the lock on the head
        if (!try_lock_pfn(head)) {
            unlock_list_shared(list);
            continue;
        }

        // Then, get the lock on the last page
        PPFN blink = head->blink;

        // Special case: if the list is empty, then blink is the same as the head.
        // In this case, we do not need to worry about locking blink.
        if (blink == head) {
            insert_to_list_tail(list, pfn);
            unlock_list_shared(list);
            unlock_pfn(head);
            increment_list_size(list);
            return;
        }

        // Otherwise, we will need to lock the blink, too
        if (!try_lock_pfn(blink)) {
            unlock_list_shared(list);
            unlock_pfn(head);
            continue;
        }

        // With both locks and the shared list lock, we can safely insert
        insert_to_list_tail(list, pfn);

        // Unlock and update metadata
        unlock_list_shared(list);
        unlock_pfn(blink);
        unlock_pfn(head);
        increment_list_size(list);

#if DEBUG
        validate_list(list);
#endif
        return;
    }
}

PPFN try_pop_from_list(PPAGE_LIST list) {

    // If someone has the exclusive lock, return and try again later.
    if (!try_lock_list_shared(list)) return NULL;

    PPFN head = list->head;

    // If we can't lock the head, unlock and return.
    if (!try_lock_pfn(head)) {
        unlock_list_shared(list);
        return NULL;
    }

    // At this point we will need to make sure the list has at least one page!
    PPFN page = head->flink;
    if (head == page) {
        unlock_list_shared(list);
        unlock_pfn(head);
        return NULL;
    }

    // If we can't lock the first entry, unlock and return
    if (!try_lock_pfn(page)) {
        unlock_list_shared(list);
        unlock_pfn(head);
        return NULL;
    }

    // Special case: if the flink's flink is the head, we can remove this page -- the LAST page
    PPFN flink = page->flink;
    if (flink == head) {
        // Remove and unlock head
        remove_page_from_list(page);
        unlock_list_shared(list);
        unlock_pfn(head);

        // Update metadata
        decrement_list_size(list);
        return page;
    }

    // If we can't lock the second entry, unlock and return
    if (!try_lock_pfn(flink)) {
        unlock_list_shared(list);
        unlock_pfn(head);
        unlock_pfn(page);
        return NULL;
    }

    // If we got here -- we were able to acquire all three locks!
    // Remove the page, update and unlock the head/flink, and unlock the list
    remove_page_from_list(page);
    unlock_list_shared(list);
    unlock_pfn(flink);
    unlock_pfn(head);

    // Update metadata and return the locked, removed page
    decrement_list_size(list);
    return page;
}

ULONG64 remove_batch_from_list_head(PPAGE_LIST list,
                                    PPFN* address_of_first_page,
                                    ULONG64 capacity,
                                    ULONG64 count) {

    ULONG wait_time = 1;

    // My current page
    PPFN pfn, list_head, batch_first, batch_last;

    // The number of pages we have added to our batch
    ULONG64 num_pages_batched = 0;

    // Wait until the list can be locked shared and the head can be locked, too
    while (TRUE) {
        lock_list_shared(list);

        list_head = list->head;

        // If we can't lock the head, unlock, wait, and retry.
        if (!try_lock_pfn(list_head)) {
            unlock_list_shared(list);
            wait(wait_time);
            wait_time = max(wait_time << 1, MAX_WAIT_TIME_BEFORE_RETRY);
            continue;
        }
        break;
    }
    // Now we know we have locked the list shared AND we have locked the head.


    // Our current page to lock is, by default, the first page at the head of the list
    batch_first = list_head->flink;
    batch_last = batch_first;

    // Special case: if there are no pages, unlock and return 0
    if (batch_first == list_head) {
        unlock_list_shared(list);
        unlock_pfn(list_head);
        return 0;
    }

    pfn = batch_first;

    BOOL wrapped_around = FALSE;
    BOOL cannot_acquire_lock = FALSE;

    // While we want pages and there are pages available, we will continue to expand our batch
    for ( ; num_pages_batched + count < capacity; num_pages_batched++) {

        wrapped_around = FALSE;
        cannot_acquire_lock = FALSE;

        // If the next page is the head, we have reached the end and locked ALL the pages on the list.
        if (pfn == list_head){
            wrapped_around = TRUE;
            break;
        }
        // OR, if we can't lock the next page, then we will take the batch we have,
        // release locks on the current and previous page as well as the head and list,
        // and return (letting a soft-faulter make progress).
        if (!try_lock_pfn(pfn)) {
            cannot_acquire_lock = TRUE;
            break;
        }

        // We have locked the next page. Hooray! Let's move on to the next page.
        batch_last = pfn;
        pfn = pfn->flink;
    }

    // If no pages could be batched, we must have not been able to lock the first page
    if (num_pages_batched == 0) {
        ASSERT(cannot_acquire_lock && !wrapped_around)
        unlock_list_shared(list);
        unlock_pfn(list_head);
        return 0;
    }

    // If ONE page was locked, we still won't return any
    if (num_pages_batched == 1) {
        unlock_list_shared(list);
        unlock_pfn(list_head);
        if (!cannot_acquire_lock) {
            unlock_pfn(batch_last);
        }
        return 0;
    }

    // Now the fun case! We have pages to flush! We will remove them from the list, then add them to our array
    // Update flinks/blinks to remove the batch from the list and add them to the temp list
    list_head->flink = batch_last;
    batch_last->blink = list_head;
    unlock_list_shared(list);
    unlock_pfn(list_head);
    ASSERT (batch_last != list_head)
    unlock_pfn(batch_last);

    // Since we did not include batch_last, which was counted,
    // we will need to decrement our count
    num_pages_batched--;

    // Decrease the list size by the number of batched pages
    change_list_size(list, 0 - num_pages_batched);

    // We will now save the address of the first page in our batch
    *address_of_first_page = batch_first;

    return num_pages_batched;
}

VOID insert_list_to_tail_list(PPAGE_LIST destination_list, PPAGE_LIST origin_list) {
    PPFN first = origin_list->head->flink;
    PPFN last = origin_list->head->blink;

    PPFN head;
    PPFN tail;

    ULONG64 wait_time = 1;

    // Acquire locks on the destination list, its head, and its final page
    while (TRUE) {
        lock_list_shared(destination_list);
        head = destination_list->head;
        if (!try_lock_pfn(head)) {
            unlock_list_shared(destination_list);
            wait(wait_time);
            wait_time = max(wait_time << 1, MAX_WAIT_TIME_BEFORE_RETRY);
            continue;
        }

        tail = head->blink;

        // If the tail is the head -- i.e. there are NO pages on the list -- then we can
        // continue without another page lock.
        if (tail == head) break;

        // Otherwise, we must lock the tail, too.
        if (!try_lock_pfn(tail)) {
            unlock_list_shared(destination_list);
            unlock_pfn(head);
            wait(wait_time);
            wait_time = max(wait_time << 1, wait_time);
            continue;
        }

        // Yay! We have locked the list, head, and tail
        break;
    }

    // head .... tail <--> first .... last <--> head
    tail->flink = first;
    first->blink = tail;

    last->flink = head;
    head->blink = last;

    // Now that everyone has been inserted, we can unlock everything
    unlock_list_shared(destination_list);
    unlock_pfn(head);
    if (head != tail) unlock_pfn(tail);
}

VOID lock_list_shared(PPAGE_LIST list) {
    AcquireSRWLockShared(&list->lock);
}

BOOL try_lock_list_shared(PPAGE_LIST list) {
    return TryAcquireSRWLockShared(&list->lock);
}

VOID unlock_list_shared(PPAGE_LIST list) {
    ReleaseSRWLockShared(&list->lock);
}

VOID lock_list_exclusive(PPAGE_LIST list) {
    AcquireSRWLockExclusive(&list->lock);
}

BOOL try_lock_list_exclusive(PPAGE_LIST list) {
    return TryAcquireSRWLockExclusive(&list->lock);
}

VOID unlock_list_exclusive(PPAGE_LIST list) {
    ReleaseSRWLockExclusive(&list->lock);
}

VOID initialize_list_head(PPFN head) {
    head->flink = head->blink = head;
}

BOOL remove_page_from_list(PPFN page) {

    PPFN flink = page->flink;
    PPFN blink = page->blink;

    blink->flink = flink;
    flink->blink = blink;

    return (BOOLEAN)(flink == blink);
}

PPFN remove_from_head_of_list(PPAGE_LIST list) {
    PPFN head = list->head;

    PPFN page = head->flink;
    PPFN flink = page->flink;
    head->flink = flink;
    flink->blink = head;

    return page;
}

VOID insert_to_list_tail(PPAGE_LIST list, PPFN page) {
    PPFN blink;

    PPFN head = list->head;
    blink = head->blink;

    page->flink = head;
    page->blink = blink;
    blink->flink = page;
    head->blink = page;
}

#if DEBUG
VOID validate_list(PPAGE_LIST list) {
    PPFN current_page;
    ULONG64 forward_length = 0;
    ULONG64 backward_length = 0;


    const ULONG64 MAX_EXPECTED_LENGTH = vm.allocated_frame_count + 10; // Safety limit

    PPFN head = list->head;

    // Check empty list case -- removed, as the validate list calls come before the size is incremented!
    // if (list->list_size == 0) {
    //     ASSERT (head->blink == head->flink && head->flink == head);
    //     return;
    // }

    lock_list_exclusive(list);

    // Validate forward traversal with cycle detection
    current_page = head->flink;
    while (current_page != head && forward_length < MAX_EXPECTED_LENGTH) {
        // Check for cross-list corruption
        ASSERT(current_page != modified_list.head &&
               current_page != standby_list.head &&
               current_page != zero_list.head);

        // Validate bidirectional linking
        if (current_page->blink->flink != current_page) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (current_page->flink->blink != current_page) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        current_page = current_page->flink;
        forward_length++;
    }

    // Check for infinite loop
    if (forward_length >= MAX_EXPECTED_LENGTH) {
        ASSERT(FALSE); // Possible infinite loop detected
        return ;
    }

    // Validate backward traversal
    current_page = head->blink;
    while (current_page != head && backward_length < MAX_EXPECTED_LENGTH) {

        if (current_page->blink->flink != current_page) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (current_page->flink->blink != current_page) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        current_page = current_page->blink;

        backward_length++;
    }

    // Verify all lengths match
    BOOL valid = (forward_length == backward_length);
    ASSERT(valid);

    unlock_list_exclusive(list);
}
#endif
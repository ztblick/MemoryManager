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
    PAGE_LIST list;

    while (TRUE) {

        // Scan through free lists, finding one you can lock
        // once locked, add the page to the tail
        list = free_lists.list_array[index];

        if (!try_lock_list_exclusive(&list)) {
            index = (index + 1) % count;
            continue;
        }

        insert_to_list_tail(&list, page);
        unlock_list_exclusive(&list);

        // Update metadata and return
        increment_list_size(&list);
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
    if (list->list_size == 0) return NULL;
    if (!try_lock_list_exclusive(list)) return NULL;
    if (list->list_size == 0) {
        unlock_list_exclusive(list);
        return NULL;
    }

    // With the lock acquired, we can remove our entry
    PPFN page = remove_from_head_of_list(list);
    unlock_list_exclusive(list);

    // Update metadata and return
    decrement_list_size(list);
    decrement_free_lists_total_count();
    decrement_available_count();
    return page;
}

VOID initialize_page_list(PPAGE_LIST list) {
    list->head = zero_malloc(sizeof(PFN));
    initialize_list_head(list->head);
    list->list_size = 0;
    InitializeSRWLock(&list->lock);
#if DEBUG
    validate_list(list);
#endif
}

BOOL is_page_list_empty(PPAGE_LIST list) {

    PPFN head = list->head;
    return (BOOLEAN)((PPFN) head->flink == head);
}

ULONG64 get_size(PPAGE_LIST list) {
    return list->list_size;
}

VOID increment_free_lists_total_count(VOID) {
    InterlockedIncrement64(&free_lists.page_count);
}

VOID decrement_free_lists_total_count(VOID) {
    InterlockedDecrement64(&free_lists.page_count);
}

VOID increment_list_size(PPAGE_LIST list) {
    InterlockedIncrement64(&list->list_size);
}

VOID decrement_list_size(PPAGE_LIST list) {
    ASSERT(list->list_size > 0);
    InterlockedDecrement64(&list->list_size);
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

	    if (!try_lock_pfn(blink)) {
	        unlock_list_shared(list);
	        unlock_pfn(flink);
	        wait(wait_time);
	        continue;
	    }

	    // If we can get both locks, we can safely remove our entry!
	    remove_page_from_list(pfn);
	    unlock_list_shared(list);

	    // Release locks
	    unlock_pfn(flink);
	    unlock_pfn(blink);

	    // Update metadata and reset events, if necessary
        decrement_list_size(list);
	    if (list == &standby_list) {
	        decrement_available_count();

	        if (is_page_list_empty(&standby_list)) ResetEvent(standby_pages_ready_event);
	    }
	    return;
	}

	// If we get here, we will need to lock the list exclusively
    lock_list_exclusive(list);
#if DEBUG
    validate_list(list);
#endif
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
 *  Precondition: pfn is already locked when called by writer. PFN is not locked
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
    if (page == head) {
        // Remove and unlock head
        remove_page_from_list(page);
        unlock_list_shared(list);
        unlock_pfn(head);

        // Update metadata
        decrement_list_size(list);
        return page;
    }

    // If we can't lock the second entry, unlock and return
    PPFN flink = page->flink;
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
    PLIST_ENTRY currentEntry;
    ULONG64 forwardLength = 0;
    ULONG64 backwardLength = 0;


    const ULONG64 MAX_EXPECTED_LENGTH = NUMBER_OF_PHYSICAL_PAGES + 10; // Safety limit

    // Check empty list case
    if (list->list_size == 0) {
        ASSERT (list->head.Blink == &list->head && list->head.Flink == &list->head);
        return;
    }


    // Validate forward traversal with cycle detection
    currentEntry = list->head.Flink;
    while (currentEntry != &list->head && forwardLength < MAX_EXPECTED_LENGTH) {
        // Check for cross-list corruption
        ASSERT(currentEntry != &modified_list.head &&
               currentEntry != &standby_list.head &&
               currentEntry != &zero_list.head);

        // Validate bidirectional linking
        if (currentEntry->Blink->Flink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (currentEntry->Flink->Blink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        currentEntry = currentEntry->Flink;
        forwardLength++;
    }

    // Check for infinite loop
    if (forwardLength >= MAX_EXPECTED_LENGTH) {
        ASSERT(FALSE); // Possible infinite loop detected
        return ;
    }

    // Validate backward traversal
    currentEntry = list->head.Blink;
    while (currentEntry != &list->head && backwardLength < MAX_EXPECTED_LENGTH) {

        if (currentEntry->Blink->Flink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (currentEntry->Flink->Blink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        currentEntry = currentEntry->Blink;

        backwardLength++;
    }

    // Verify all lengths match
    BOOL valid = (forwardLength == backwardLength && forwardLength == list->list_size);
    ASSERT(valid);
    return;
}
#endif
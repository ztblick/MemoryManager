//
// Created by zblickensderfer on 6/5/2025.
//

#include "../include/page_list.h"

PAGE_LIST zero_list;
PAGE_LIST free_list;
PAGE_LIST modified_list;
PAGE_LIST standby_list;

VOID initialize_page_list(PPAGE_LIST list) {
    create_zeroed_pfn(&list->head);
    InitializeListHead(&list->head.entry);
    list->list_size = 0;
    InitializeSRWLock(&list->lock);
#if DEBUG
    validate_list(list);
#endif
}

BOOL is_page_list_empty(PPAGE_LIST list) {
    return IsListEmpty(&list->head.entry);
}

ULONG64 get_size(PPAGE_LIST list) {
    return list->list_size;
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
	PPFN flink = (PPFN) pfn->entry.Flink;
	PPFN blink = (PPFN) pfn->entry.Blink;

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
	    RemoveEntryList(&pfn->entry);
	    unlock_list_shared(list);
	    unlock_pfn(flink);
	    unlock_pfn(blink);
        decrement_list_size(list);
	    return;
	}

	// If we get here, we will need to lock the list exclusively
    lock_list_exclusive(list);
#if DEBUG
    validate_list(list);
#endif
    // Remove our relevant page, then immediately unlock the list
    RemoveEntryList(&pfn->entry);
    unlock_list_exclusive(list);

    // Now we can update metadata appropriately. This happens without the lock, as none
    // of it is mission-critical.
    decrement_list_size(list);

    // If we have pulled off the standby list, we will want to decrement our available count
    if (list == &standby_list) {
        decrement_available_count();

        // And if it happens to be the last standby page, we will want to hold all other faulters
        // until there are available standby pages.
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

    PPFN head = &list->head;

    // Continue to try to acquire the necessary page locks
    while (TRUE) {

        lock_list_shared(list);

        // First, get the head lock
        if (!try_lock_pfn(head)) {
            unlock_list_shared(list);
            continue;
        }

        // Then, get the lock on the last page
        PPFN blink = (PPFN) head->entry.Blink;

        // Special case: if the list is empty, then blink is the same as the head.
        // In this case, we do not need to worry about locking blink.
        if (blink == head) {
            InsertTailList(&list->head.entry, &pfn->entry);
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
        InsertTailList(&list->head.entry, &pfn->entry);

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

    PPFN head = &list->head;

    // If we can't lock the head, unlock and return.
    if (!try_lock_pfn(head)) {
        unlock_list_shared(list);
        return NULL;
    }

    // At this point we will need to make sure the list has at least one page!
    PPFN page = (PPFN) head->entry.Flink;
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

    // If we can't lock the second entry, unlock and return
    PPFN flink = (PPFN) page->entry.Flink;
    if (!try_lock_pfn(flink)) {
        unlock_list_shared(list);
        unlock_pfn(head);
        unlock_pfn(page);
        return NULL;
    }

    // If we got here -- we were able to acquire all three locks!
    // Remove the page, update and unlock the head/flink, and unlock the list
    RemoveEntryList(&page->entry);
    unlock_list_shared(list);
    unlock_pfn(flink);
    unlock_pfn(head);
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
               currentEntry != &free_list.head &&
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

//
// Created by zblickensderfer on 6/5/2025.
//

#include "../include/page_list.h"

PAGE_LIST zero_list;
PAGE_LIST free_list;
PAGE_LIST modified_list;
PAGE_LIST standby_list;

VOID initialize_page_list(PPAGE_LIST list) {
    InitializeListHead(&list->head);
    list->list_size = 0;
    InitializeSRWLock(&list->lock);
#if DEBUG
    validate_list(list);
#endif
}

BOOL is_page_list_empty(PPAGE_LIST list) {
    return IsListEmpty(&list->head);
}

ULONG64 get_size(PPAGE_LIST list) {
    return list->list_size;
}

PPFN pop_from_head_list(PPAGE_LIST list) {
#if DEBUG
    validate_list(list);
#endif
    PLIST_ENTRY entry = RemoveHeadList(&list->head);
    entry->Flink = NULL;
    entry->Blink = NULL;
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    decrement_list_size(list);
#if DEBUG
    validate_list(list);
#endif
    return pfn;
}

VOID add_to_tail_list(PPAGE_LIST list, PPFN pfn) {
#if DEBUG
    validate_list(list);
#endif
    InsertTailList(&list->head, &pfn->entry);
    increment_list_size(list);
#if DEBUG
    validate_list(list);
#endif
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
	PPFN flink = (PPFN) pfn->entry.Flink;
	PPFN blink = (PPFN) pfn->entry.Blink;

	// We cannot proceed while someone else has an exclusive lock on this
	// list, so we will wait until we can acquire the shared lock
	lock_list_shared(list);

	// Attempt to grab both flink and blink locks
	while (attempts < MAX_SOFT_FAULT_ATTEMPTS) {
		attempts++;
		if (!try_lock_pfn(flink)) continue;

	    if (!try_lock_pfn(blink)) {
	        unlock_pfn(flink);
	        continue;
	    }

	    // If we can get both locks, we can safely remove our entry!
	    RemoveEntryList(&pfn->entry);
	    unlock_list_shared(list);
	    unlock_pfn(flink);
	    unlock_pfn(blink);
	    return;
	}

	// If we get here, we will need to lock the list exclusively
    unlock_list_shared(list);
    lock_list_exclusive(list);

#if DEBUG
    validate_list(list);
#endif
    RemoveEntryList(&pfn->entry);

    // If we have pulled off the standby list, we will want to decrement our available count
    if (list == &standby_list) {
        decrement_available_count();

        // And if it happens to be the last standby page, we will want to hold all other faulters
        // until there are available standby pages.
        if (is_page_list_empty(&standby_list)) ResetEvent(standby_pages_ready_event);
    }
    decrement_list_size(list);

#if DEBUG
    validate_list(list);
#endif
    unlock_list_exclusive(list);
}

VOID remove_page_from_list(PPAGE_LIST list, PPFN pfn) {
#if DEBUG
    validate_list(list);
#endif
    RemoveEntryList(&pfn->entry);
    pfn->entry.Flink = NULL;
    pfn->entry.Blink = NULL;
    decrement_list_size(list);
#if DEBUG
    validate_list(list);
#endif
    // If we are removing the last standby page, we will reset the "pages available" event.
    if (list == &standby_list && is_page_list_empty(list)) {
        ResetEvent(standby_pages_ready_event);
    }
}

VOID lock_list_then_insert_to_tail(PPAGE_LIST list, PPFN pfn) {

    lock_list_exclusive(list);
#if DEBUG
    validate_list(list);
#endif
    InsertTailList(&list->head, &pfn->entry);
    increment_list_size(list);
#if DEBUG
    validate_list(list);
#endif
    unlock_list_exclusive(list);
}

VOID lock_list_then_insert_to_head(PPAGE_LIST list, PLIST_ENTRY entry) {
    lock_list_exclusive(list);
#if DEBUG
    validate_list(list);
#endif
    InsertHeadList(&list->head, entry);
    increment_list_size(list);
#if DEBUG
    validate_list(list);
#endif
    unlock_list_exclusive(list);
}

BOOL is_at_head_of_list(PPAGE_LIST list, PPFN pfn) {
    return (PPFN) list->head.Flink == pfn;
}


PPFN peek_from_list_head(PPAGE_LIST list) {

    ASSERT(!is_page_list_empty(list));
#if DEBUG
    validate_list(list);
#endif

    PLIST_ENTRY entry = list->head.Flink;
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

VOID lock_list_shared(PPAGE_LIST list) {
    AcquireSRWLockShared(&list->lock);
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

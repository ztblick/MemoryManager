//
// Created by zblickensderfer on 6/5/2025.
//

#include "../include/page_list.h"
#include "../include/initializer.h"
#include "../include/macros.h"

VOID initialize_page_list(PPAGE_LIST list) {
    InitializeListHead(&list->head);
    list->list_size = 0;
    InitializeSRWLock(&list->lock);
}

BOOL is_page_list_empty(PPAGE_LIST list) {
    return IsListEmpty(&list->head);
}

ULONG64 get_size(PPAGE_LIST list) {
    return list->list_size;
}

PPFN pop_from_head_list(PPAGE_LIST list) {

    PLIST_ENTRY entry = RemoveHeadList(&list->head);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    decrement_list_size(list);
    return pfn;
}

VOID increment_list_size(PPAGE_LIST list) {
    InterlockedIncrement64(&list->list_size);
}

VOID decrement_list_size(PPAGE_LIST list) {
    ASSERT(list->list_size > 0);
    InterlockedDecrement64(&list->list_size);
}

VOID lock_list_and_remove_page(PPAGE_LIST list, PPFN pfn) {
    lock_list_exclusive(list);
    RemoveEntryList(&pfn->entry);

    // Check for cleared standby list!
    if (list == &standby_list && is_page_list_empty(&standby_list)) {
        ResetEvent(standby_pages_ready_event);
    }

    unlock_list_exclusive(list);
    decrement_list_size(list);
}

VOID remove_page_from_list(PPAGE_LIST list, PPFN pfn) {
    RemoveEntryList(&pfn->entry);
    decrement_list_size(list);
}

// TODO modify this to accept PPFN as the parameter, not PLIST_ENTRY
VOID lock_list_then_insert_to_tail(PPAGE_LIST list, PLIST_ENTRY entry) {

    lock_list_exclusive(list);
    InsertTailList(&list->head, entry);
    unlock_list_exclusive(list);
    increment_list_size(list);
}

VOID lock_list_then_insert_to_head(PPAGE_LIST list, PLIST_ENTRY entry) {
    lock_list_exclusive(list);
    InsertHeadList(&list->head, entry);
    unlock_list_exclusive(list);
    increment_list_size(list);
}

PPFN peek_from_list_head(PPAGE_LIST list) {

    PLIST_ENTRY entry = list->head.Flink;
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
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
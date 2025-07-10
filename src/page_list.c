//
// Created by zblickensderfer on 6/5/2025.
//

#include "../include/page_list.h"
#include "../include/macros.h"

VOID initialize_page_list(PPAGE_LIST list) {
    InitializeListHead(&list->head);
    list->list_size = 0;
    InitializeCriticalSection(&list->lock);
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
    list->list_size++;
}

VOID decrement_list_size(PPAGE_LIST list) {

    ASSERT(list->list_size > 0);
    list->list_size--;
}

VOID lock_list_and_remove_page(PPAGE_LIST list, PPFN pfn) {
    lock_list(list);
    RemoveEntryList(&pfn->entry);
    decrement_list_size(list);
    unlock_list(list);
}

VOID remove_page_from_list(PPAGE_LIST list, PPFN pfn) {
    RemoveEntryList(&pfn->entry);
    decrement_list_size(list);
}

VOID lock_list_then_insert_to_tail(PPAGE_LIST list, PLIST_ENTRY entry) {

    EnterCriticalSection(&list->lock);
    InsertTailList(&list->head, entry);
    increment_list_size(list);
    LeaveCriticalSection(&list->lock);
}

VOID lock_list_then_insert_to_head(PPAGE_LIST list, PLIST_ENTRY entry) {
    EnterCriticalSection(&list->lock);
    InsertHeadList(&list->head, entry);
    increment_list_size(list);
    LeaveCriticalSection(&list->lock);
}

PPFN peek_from_list_head(PPAGE_LIST list) {

    PLIST_ENTRY entry = list->head.Flink;
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

VOID lock_list(PPAGE_LIST list) {
    EnterCriticalSection(&list->lock);
}

BOOL try_lock_list(PPAGE_LIST list) {
    return TryEnterCriticalSection(&list->lock);
}

VOID unlock_list(PPAGE_LIST list) {
    LeaveCriticalSection(&list->lock);
}
//
// Created by zblickensderfer on 6/5/2025.
//

#include "../include/page_list.h"

VOID initialize_page_list(PPAGE_LIST list) {
    InitializeListHead(&list->head);
    list->list_size = 0;
    InitializeCriticalSection(&list->lock);
}

BOOL is_page_list_empty(PPAGE_LIST list) {
    return IsListEmpty(&list->head);
}

PPFN pop_from_list_head(PPAGE_LIST list) {

    // TODO add PFN locking here?

    EnterCriticalSection(&list->lock);

    if (is_page_list_empty(list)) {
        LeaveCriticalSection(&list->lock);
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(&list->head);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    list->list_size--;
    LeaveCriticalSection(&list->lock);
    return pfn;
}

VOID insert_tail_list(PPAGE_LIST list, PLIST_ENTRY entry) {

    EnterCriticalSection(&list->lock);
    InsertTailList(&list->head, entry);
    list->list_size++;
    LeaveCriticalSection(&list->lock);
}

VOID insert_head_list(PPAGE_LIST list, PLIST_ENTRY entry) {
    EnterCriticalSection(&list->lock);
    InsertHeadList(&list->head, entry);
    list->list_size++;
    LeaveCriticalSection(&list->lock);
}
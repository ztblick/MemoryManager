//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/pfn.h"

PPFN PFN_array;

VOID create_zeroed_pfn(PPFN new_pfn) {

    // Here, we will create a temporary PFN. We will read its data into
    // our new PFN in 64-byte chunks (to prevent tearing).
    PFN temp = {0};
    temp.lock.semaphore = UNLOCKED;
    temp.fields.status = PFN_FREE;
    temp.fields.disk_index = NO_DISK_INDEX;

    WriteULong64NoFence(&new_pfn->raw_pfn_data, temp.raw_pfn_data);
    WriteULong64NoFence((DWORD64 *) &new_pfn->PTE, (DWORD64) NULL);
}

ULONG_PTR get_frame_from_PFN(PPFN pfn) {
    ASSERT (pfn >= PFN_array && pfn <= PFN_array + max_frame_number);
    return pfn - PFN_array;
}

PPFN get_PFN_from_PTE(PPTE pte) {

    // In our rare case of a transition PTE becoming disk format during concurrent
    // soft- and hard-faults on the same standby page, we will return NULL.
    if (pte->memory_format.status == PTE_ON_DISK) return NULL;

    // Otherwise, we return our PFN!
    return get_PFN_from_frame(pte->memory_format.frame_number);
}

PPFN get_PFN_from_frame(ULONG_PTR frame_number) {
    ASSERT (frame_number >= min_frame_number && frame_number <= max_frame_number);
    return PFN_array + frame_number;
}

VOID set_PFN_active(PPFN pfn, PPTE pte) {
    // Here, we will create a temporary PFN. We will read its data into
    // our new PFN in 64-byte chunks (to prevent tearing).
    PFN snapshot = *pfn;
    PFN temp = {0};
    temp.lock.semaphore = snapshot.lock.semaphore;
    temp.fields.status = PFN_ACTIVE;
    temp.fields.disk_index = NO_DISK_INDEX;

    WriteULong64NoFence(&pfn->raw_pfn_data, temp.raw_pfn_data);
    WriteULong64NoFence((DWORD64 *) &pfn->PTE, (DWORD64) pte);
}

VOID set_PFN_free(PPFN pfn) {

    // Here, we will create a temporary PFN. We will read its data into
    // our new PFN in 64-byte chunks (to prevent tearing).
    PFN snapshot = *pfn;
    PFN temp = {0};
    temp.lock.semaphore = snapshot.lock.semaphore;
    temp.fields.status = PFN_FREE;
    temp.fields.disk_index = NO_DISK_INDEX;

    WriteULong64NoFence(&pfn->raw_pfn_data, temp.raw_pfn_data);
    WriteULong64NoFence((DWORD64 *) &pfn->PTE, (DWORD64) NULL);
}

VOID set_pfn_standby(PPFN pfn, ULONG64 disk_index) {

    // Here, we will create a temporary PFN. We will read its data into
    // our new PFN in 64-byte chunks (to prevent tearing).
    PFN snapshot = *pfn;
    PFN temp = {0};
    temp.lock.semaphore = snapshot.lock.semaphore;
    temp.fields.status = PFN_STANDBY;
    temp.fields.disk_index = disk_index;

    WriteULong64NoFence(&pfn->raw_pfn_data, temp.raw_pfn_data);
}

VOID lock_pfn(PPFN pfn) {
    lock(&pfn->lock);
}

BOOL try_lock_pfn(PPFN pfn) {
    return try_lock(&pfn->lock);
}

VOID unlock_pfn(PPFN pfn) {
    unlock(&pfn->lock);
}

VOID set_pfn_mid_trim(PPFN pfn) {
    PFN snapshot = *pfn;
    PFN temp = {0};
    temp.lock.semaphore = snapshot.lock.semaphore;
    temp.fields.status = PFN_MID_TRIM;
    temp.fields.disk_index = snapshot.fields.disk_index;

    WriteULong64NoFence(&pfn->raw_pfn_data, temp.raw_pfn_data);
}

VOID set_pfn_mid_write(PPFN pfn) {
    PFN snapshot = *pfn;
    PFN temp = {0};
    temp.lock.semaphore = snapshot.lock.semaphore;
    temp.fields.status = PFN_MID_WRITE;
    temp.fields.disk_index = snapshot.fields.disk_index;

    WriteULong64NoFence(&pfn->raw_pfn_data, temp.raw_pfn_data);
}
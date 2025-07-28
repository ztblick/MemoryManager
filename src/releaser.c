//
// Created by zachb on 7/24/2025.
//

#include "../include/releaser.h"

void free_lock(PCRITICAL_SECTION lock) {
    DeleteCriticalSection(lock);
}

void free_list_data(void) {

}

void unmap_all_pages(void) {
    for (PPTE pte = PTE_base; pte < PTE_base + NUM_PTEs; pte++) {
        if (pte->disk_format.valid) {
            PULONG_PTR va = get_VA_from_PTE(pte);
            unmap_pages(1, va);
        }
    }
}

void free_all_data_and_shut_down(void) {

    unmap_all_pages();

    free_events();

    free_list_data();

    free_page_file_data();

    free_PFN_data();

    free_PTE_data();

    free_VA_space_data();

    // Free physical pages
    FreeUserPhysicalPages(physical_page_handle, &allocated_frame_count, allocated_frame_numbers);
    free(allocated_frame_numbers);
}


void free_events(void) {
    free(user_threads);
    free(scheduling_threads);
    free(aging_threads);
    free(trimming_threads);
    free(writing_threads);

    free(user_thread_ids);
    free(scheduling_thread_ids);
    free(aging_thread_ids);
    free(trimming_thread_ids);
    free(writing_thread_ids);

    CloseHandle(system_start_event);
    CloseHandle(standby_pages_ready_event);
    CloseHandle(initiate_aging_event);
    CloseHandle(initiate_trimming_event);
    CloseHandle(initiate_writing_event);
}

void free_VA_space_data(void) {

    VirtualFree (application_va_base, 0, MEM_RELEASE);
    VirtualFree(kernel_read_va, 0, MEM_RELEASE);
    VirtualFree(kernel_write_va, 0, MEM_RELEASE);

    free_lock(&kernel_read_lock);
    free_lock(&kernel_write_lock);
}

void free_PFN_data(void) {
    // Free all PFN locks
    for (ULONG64 i = 0; i < allocated_frame_count; i++) {
        PPFN pfn = get_PFN_from_frame(allocated_frame_numbers[i]);
        free_lock(&pfn->lock);
    }

    VirtualFree (PFN_array, 0, MEM_RELEASE);
}

void free_page_file_data(void) {
    for (int i = MIN_DISK_INDEX; i <= PAGES_IN_PAGE_FILE; i++) {
        free_lock(&disk_metadata_locks[i]);
    }
    free(disk_metadata_locks);
    free(page_file);
    free(page_file_metadata);
}

void free_PTE_data(void) {
    // TODO free PTE locks based on REGIONS
    free(PTE_base);
}
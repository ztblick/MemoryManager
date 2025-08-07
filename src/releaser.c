//
// Created by zachb on 7/24/2025.
//

#include "../include/releaser.h"

void free_lock(PCRITICAL_SECTION lock) {
    DeleteCriticalSection(lock);
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

    VirtualFree(application_va_base, 0, MEM_RELEASE);
    VirtualFree(kernel_write_va, 0, MEM_RELEASE);


    // TODO free all user thread kernel read VA spaces
}

void free_PFN_data(void) {
    // Free all PFN locks
    VirtualFree (PFN_array, 0, MEM_RELEASE);
}

void free_page_file_data(void) {
    free(pf.page_file);
    free(pf.page_file_bitmaps);
}

void free_PTE_data(void) {
    free(PTE_base);
}
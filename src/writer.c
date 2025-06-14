//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/macros.h"
#include "../include/page_fault_handler.h"

// Returns a disk index from 1 to PAGES_IN_PAGE_FILE
// It is important to note that the indices are off by one, as the value of 0 is reserved
// for a PFN that is not mapped to a disk slot
UINT64 find_and_lock_free_disk_index(void) {

    UINT64 disk_index = MIN_DISK_INDEX;
    while (TRUE) {
        // Try to acquire the lock on this disk index
        if (TryEnterCriticalSection(&disk_metadata_locks[disk_index])) {

            // If the lock can be acquired, check the value at the slot. If it's empty, return the locked slot.
            if (page_file_metadata[disk_index] == DISK_SLOT_EMPTY) {
                return disk_index;
            }

            // If it is taken, release the lock and continue scanning...
            LeaveCriticalSection(&disk_metadata_locks[disk_index]);
        }

        disk_index++;
        if (disk_index > PAGES_IN_PAGE_FILE) disk_index = MIN_DISK_INDEX;
    }

    // This will spin forever if no open disk slots can be found.
    return 0;
}

VOID write_pages(VOID) {

    // First, check to see if there are any pages to write
    if (is_page_list_empty(&modified_list)) {
#if DEBUG
        printf("No modified pages to write to disk.\n");
#endif
        return;
    }

    PPFN pfn;
    int num_pages_in_write_batch = WRITE_BATCH_SIZE;

    // Initialize frame number array
    PULONG_PTR frame_numbers_to_map = zero_malloc(num_pages_in_write_batch * sizeof(ULONG_PTR));

    // Get all pages to write:
    int i = 0;
    while (i < num_pages_in_write_batch) {

        // Get list lock
        EnterCriticalSection(&modified_list.lock);

        // Get the PFN for the page we want to try to use
        pfn = peek_from_list_head(&modified_list);

        // Try to get the page lock out of order
        if (!try_lock_pfn(pfn)) {
            // If we can't get the lock, release the list lock, then try again.
            LeaveCriticalSection(&modified_list.lock);
            continue;
        }

        // Now that we have acquired the page lock, remove it from the modified list.
        RemoveEntryList(&pfn->entry);
        decrement_list_size(&modified_list);
        modified_page_count--;

        // Release the PFN lock, now that we have its information
        // TODO Ask Landy...do we need the PFN lock here? Why?
        unlock_pfn(pfn);

        // Unlock the modified list
        LeaveCriticalSection(&modified_list.lock);

        // Grab the frame number for mapping/unmapping
        frame_numbers_to_map[i] = get_frame_from_PFN(pfn);
        i++;
    }

    // Grab kernel write lock
    EnterCriticalSection(&kernel_write_lock);

    // Map all pages to the kernel VA space
    map_pages(num_pages_in_write_batch, kernel_write_va, frame_numbers_to_map);

    // Get disk slot for this PTE
    UINT64 disk_index = find_and_lock_free_disk_index();

    // Get the location to write to in page file
    char* page_file_location = get_page_file_offset(disk_index);

    // Perform the write
    memcpy(page_file_location, kernel_write_va, PAGE_SIZE * num_pages_in_write_batch);
    set_disk_slot(disk_index);

    // Un-map kernal VA
    unmap_pages(num_pages_in_write_batch, kernel_write_va);

    // Release kernel lock
    LeaveCriticalSection(&kernel_write_lock);

    // Lock the PFN again
    lock_pfn(pfn);

    // TODO check to see if this page was actually soft faulted! If so, invalidate the disk slot.

    // Update PFN to be in standby state, add disk index
    SET_PFN_STATUS(pfn, PFN_STANDBY);
    pfn->disk_index = disk_index;

    // Add page to the standby list
    lock_list_then_insert_to_tail(&standby_list, &pfn->entry);
    standby_page_count++;

    // Release disk slot lock
    LeaveCriticalSection(&disk_metadata_locks[disk_index]);

    // Unlock the PFN
    unlock_pfn(pfn);

    // Free frame number array
    free(frame_numbers_to_map);

#if DEBUG
    printf("Wrote one page out to disk, moved page to standby list!\n");
#endif
}

VOID write_pages_thread(VOID) {

    ULONG index;
    HANDLE events[2];
    events[ACTIVE_EVENT_INDEX] = initiate_writing_event;
    events[EXIT_EVENT_INDEX] = system_exit_event;

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        // Wait for one of two events: initiate writing (which calls write_pages), or exit, which...exits!
        index = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE);

        if (index == EXIT_EVENT_INDEX) {
            return;
        }
        // TODO when ready, remove these guards!
        EnterCriticalSection(&page_fault_lock);
        write_pages();
        LeaveCriticalSection(&page_fault_lock);
    }
}
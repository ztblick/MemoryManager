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

    // PFN for the current page being selected for disk write.
    PPFN pfn;
    int num_pages_batched = 0;
    int num_pages_in_write_batch = WRITE_BATCH_SIZE;

    // First, check to see if there are any pages to write
    if (is_page_list_empty(&modified_list)) {
        return;
    }

    // Initialize frame number array
    PULONG_PTR frame_numbers_to_map = zero_malloc(num_pages_in_write_batch * sizeof(ULONG_PTR));

    // TODO grab disk slots while grabbing pages. Do not get too many of either.
    // Get all pages to write
    while (num_pages_batched < num_pages_in_write_batch) {

        // Get list lock
        lock_list_exclusive(&modified_list);

        // If there are no longer pages on the modified list, stop looping. Unlock the modified list.
        // Most importantly, we update the size of the write batch to our count.
        if (get_size(&modified_list) == 0) {
            unlock_list_exclusive(&modified_list);
            num_pages_in_write_batch = num_pages_batched;
            break;
        }

        // Get the PFN for the page we want to try to use
        pfn = peek_from_list_head(&modified_list);

        // Try to get the page lock out of order
        if (!try_lock_pfn(pfn)) {
            // If we can't get the lock, release the list lock, then try again.
            unlock_list_exclusive(&modified_list);
            continue;
        }

        // Now that we have acquired the page lock, remove it from the modified list.
        remove_page_from_list(&modified_list, pfn);

        // Update PFN status
        SET_PFN_STATUS(pfn, PFN_MID_WRITE);

        // Release the PFN lock, now that we pulled it off the modified list.
        // unlock_pfn(pfn);

        // Unlock the modified list
        unlock_list_exclusive(&modified_list);

        // Grab the frame number for mapping/unmapping
        frame_numbers_to_map[num_pages_batched] = get_frame_from_PFN(pfn);
        num_pages_batched++;
    }

    // TODO batch the writes!

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
    // lock_pfn(pfn);

    // If we had a soft fault on the page mid-write, we will need to undo this disk write.
    // We will do so by clearing the disk slot and not modifying the PFN's status.
    if (soft_fault_happened_mid_write(pfn)) {
        clear_disk_slot(disk_index);
    }

    // Otherwise, we move along as normal: the page moves to standby, and we update the PFN with its disk slot.
    else {
        // Update PFN to be in standby state, add disk index
        SET_PFN_STATUS(pfn, PFN_STANDBY);
        pfn->disk_index = disk_index;

        // Add page to the standby list
        lock_list_then_insert_to_tail(&standby_list, &pfn->entry);

        // Broadcast to waiting user threads that there are standby pages ready.
        SetEvent(standby_pages_ready_event);
    }

    // Release disk slot lock
    LeaveCriticalSection(&disk_metadata_locks[disk_index]);

    // Unlock the PFN
    unlock_pfn(pfn);

    // Free frame number array
    free(frame_numbers_to_map);
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
        write_pages();
    }
}
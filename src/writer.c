//
// Created by zblickensderfer on 5/5/2025.
//

#include "../include/writer.h"
#include "../include/debug.h"
#include "../include/initializer.h"
#include "../include/macros.h"
#include "../include/page_fault_handler.h"

// Returns a disk index from 1 to PAGES_IN_PAGE_FILE, inclusive
// It is important to note that the indices are off by one, as the value of 0 is reserved
// for a PFN that is not mapped to a disk slot
// TODO pick up from last empty slot...duh...
UINT64 find_and_lock_free_disk_index(void) {

    UINT64 disk_index = MIN_DISK_INDEX;
    for (; disk_index <= MAX_DISK_INDEX; disk_index++) {

        if (page_file_metadata[disk_index] != DISK_SLOT_EMPTY) continue;

        // TODO debug this...?
        ASSERT(disk_index >= MIN_DISK_INDEX && disk_index <= MAX_DISK_INDEX);

        // Try to acquire the lock on this disk index
        if (TryEnterCriticalSection(&disk_metadata_locks[disk_index])) {

            // If the lock can be acquired, check the value at the slot. If it's empty, return the locked slot.
            if (page_file_metadata[disk_index] == DISK_SLOT_EMPTY) {
                set_disk_slot(disk_index);
                return disk_index;
            }

            // If it is taken, release the lock and continue scanning...
            LeaveCriticalSection(&disk_metadata_locks[disk_index]);
        }
    }

    return NO_DISK_INDEX;
}

// Returns the number of allocated disk slots and fills the given array with them.
ULONG64 allocate_disk_slot_batch(ULONG_PTR* disk_slots, ULONG64 target_page_count) {
    // This will keep track of the number of slots we have locked.
    ULONG64 count = 0;
    // This will keep track of the particular slot we are currently locking.
    ULONG64 slot = 0;
    // Update our target count -- no need to get too greedy
    target_page_count = min(MAX_WRITE_BATCH_SIZE, target_page_count);

    // TODO consider using the empty_disk_slots variable here for a speedup
    while (count < target_page_count) {

        // Once we have a slot, check to see if it is valid. If we couldn't get one,
        // we know there are none to get!
        slot = find_and_lock_free_disk_index();
        if (slot == NO_DISK_INDEX) break;

        // If it is, update our slots and continue.
        disk_slots[count] = slot;
        count++;
    }

    return count;
}

void unlock_and_clear_disk_slots(ULONG_PTR* disk_slots, ULONG64 num_slots, ULONG64 first_slot_to_unlock) {
    for (ULONG64 i = first_slot_to_unlock; i < num_slots; i++) {
        clear_disk_slot(disk_slots[i]);
        unlock_disk_slot(disk_slots[i]);
    }
}

VOID write_pages(VOID) {

    // PFN for the current page being selected for disk write.
    PPFN pfn;
    // The number of pages we have added to our batch
    ULONG64 num_pages_batched = 0;
    // This will track the number of disk slots I have locked before grabbing pages.
    ULONG64 num_disk_slots_batched = 0;
    // The upper bound on the size of THIS particular batch. This will be set to the max, then (possibly)
    // brought lower by the disk slot allocation, then (possibly) brought lower by the number of pages
    // that can be removed from the modified list.
    ULONG64 num_pages_in_write_batch = MAX_WRITE_BATCH_SIZE;

    // Let's get a sense of how many modified pages we can reasonably expect. There may be more (from trimming)
    // or fewer (from soft faults), but this gives us an estimate.
    ULONG64 approx_num_mod_pages = get_size(&modified_list);

    // First, check to see if there are sufficient pages to write
    if (approx_num_mod_pages < MIN_WRITE_BATCH_SIZE) return;

    // Initialize disk slot array
    ULONG64 disk_slots[MAX_WRITE_BATCH_SIZE];

    // First, we will grab as many disk slots as we can. If we have too many, we will release them later.
    num_disk_slots_batched = allocate_disk_slot_batch(disk_slots, approx_num_mod_pages);

    // If we couldn't lock any disk slots, we will need to return.
    if (num_disk_slots_batched == 0) return;

    // Update our upper bound on pages in the batch
    num_pages_in_write_batch = num_disk_slots_batched;

    // Initialize frame number array
    PPFN pages_to_write[MAX_WRITE_BATCH_SIZE];

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

        // TODO write this all out to a function

        // Get the PFN for the page we want to try to use
        pfn = peek_from_list_head(&modified_list);

        // Try to get the page lock out of order
        // TODO this will be addressed with our new locking system
        if (!try_lock_pfn(pfn)) {
            // If we can't get the lock, we will unlock the modified list and just try again.
            unlock_list_exclusive(&modified_list);
            continue;
        }

        // Now that we have acquired the page lock, remove it from the modified list.
        remove_page_from_list(&modified_list, pfn);

        // Update PFN status
        SET_PFN_STATUS(pfn, PFN_MID_WRITE);

        // Grab the frame number for mapping/unmapping
        pages_to_write[num_pages_batched] = pfn;
        num_pages_batched++;

        // Release the PFN lock, now that we pulled it off the modified list.
        // Also release the list lock (we will get it again shortly)
        unlock_pfn(pfn);
        unlock_list_exclusive(&modified_list);
    }

    // If we couldn't get any pages, we will need to unlock our disk slots and return.
    if (num_pages_in_write_batch == 0) {
        unlock_and_clear_disk_slots(disk_slots, num_disk_slots_batched, 0);
        return;
    }

    // Create an array of our frame numbers for the calls to map and unmap
    ULONG64 frame_numbers_to_map[MAX_WRITE_BATCH_SIZE];

    for (ULONG64 i = 0; i < num_pages_in_write_batch; i++) {
        frame_numbers_to_map[i] = get_frame_from_PFN(pages_to_write[i]);
    }

    // Grab kernel write lock
    EnterCriticalSection(&kernel_write_lock);

    // Map all pages to the kernel VA space
    map_pages(num_pages_in_write_batch, kernel_write_va, frame_numbers_to_map);

    // Flag to indicate that SOME pages were successfully written
    ULONG64 pages_written = 0;


    for (ULONG64 i = 0; i < num_pages_in_write_batch; i++) {

        // Get the current page
        pfn = pages_to_write[i];

        // Get the disk slot for THIS page
        ULONG64 disk_slot = disk_slots[i];

        // Get the destination in the page file
        char* page_file_location = get_page_file_offset(disk_slot);

        // Write the next page location in the kernal VA space
        memcpy(page_file_location, kernel_write_va + i * PAGE_SIZE / 8, PAGE_SIZE);

        // Lock the PFN again
        lock_pfn(pfn);

        // If we had a soft fault on the page mid-write, we will need to undo this disk write.
        // We will do so by clearing the disk slot and not modifying the PFN's status.
        if (soft_fault_happened_mid_write(pfn)) {
            clear_disk_slot(disk_slot);
        }

        // Otherwise, we move along as normal: the page moves to standby, and we update the PFN with its disk slot.
        else {
            // Update PFN to be in standby state, add disk index
            SET_PFN_STATUS(pfn, PFN_STANDBY);
            pfn->disk_index = disk_slot;

            // Add page to the standby list
            lock_list_then_insert_to_tail(&standby_list, &pfn->entry);

            // Update our count
            pages_written++;
        }

        // Release disk slot lock
        unlock_disk_slot(disk_slots[i]);

        // Unlock the PFN
        unlock_pfn(pfn);
    }
    // Release any remaining disk slot locks
    unlock_and_clear_disk_slots(disk_slots, num_disk_slots_batched, pages_written);

    // Un-map kernal VA
    unmap_pages(num_pages_in_write_batch, kernel_write_va);

    // Release kernel lock
    LeaveCriticalSection(&kernel_write_lock);

    // Broadcast to waiting user threads that there are standby pages ready.
    if (pages_written > 0) SetEvent(standby_pages_ready_event);
}

VOID write_pages_thread(VOID) {

    // Wait for system start event before entering waiting state!
    WaitForSingleObject(system_start_event, INFINITE);

    // If the exit flag has been set, then it's time to go!
    // No need for an atomic operation -- if we do one extra trim, we will live.
    while (writer_exit_flag == SYSTEM_RUN) {

        update_statistics();

        // TODO learn how to use this!
        // QueryPerformanceCounter(&modified_list);

        // Otherwise: if there is sufficient need, wake the writer
        if (modified_page_count > BEGIN_WRITING_THRESHOLD) write_pages();

        // If there isn't need, let's sleep for a moment to avoid spinning and burning up this core.
        else YieldProcessor();
    }
}
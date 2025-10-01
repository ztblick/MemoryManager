//
// Created by zachb on 7/30/2025.
//

#include "disk.h"

PAGE_FILE_STRUCT pf = {0};

VOID initialize_page_file_and_metadata(VOID) {

    // Initialize page file.
    pf.page_file = (char*) zero_malloc(vm.pages_in_page_file * PAGE_SIZE);

    // Initialize page file bitmaps.
    // Note that we ALWAYS set the first slot to full, as there cannot be a disk slot of ZERO.
    pf.page_file_bitmaps = zero_malloc(vm.pages_in_page_file / BITS_PER_BYTE);
    pf.page_file_bitmaps[0] |= DISK_SLOT_IN_USE;
    pf.max_disk_index = vm.pages_in_page_file - 1;
    pf.page_file_bitmap_rows = vm.pages_in_page_file / BITS_PER_BITMAP_ROW;

    // Initialize the writer's array of disk slots
    // This supports extra slots to be stashed for later.
    pf.slot_stack = zero_malloc((MAX_WRITE_BATCH_SIZE * 2) * BYTES_PER_VA);
    pf.last_checked_bitmap_row = 0;
    pf.num_stashed_slots = 0;

    // Initialize disk slot tracker -- minus one because we have an initially full slot at index zero.
    pf.empty_disk_slots = vm.pages_in_page_file - 1;
}

VOID validate_disk_slot(ULONG64 disk_slot) {
    ASSERT (disk_slot <= pf.max_disk_index);
}

VOID push_slots_from_bitmap_row(ULONG64 bitmap_row) {

    ULONG64 current_bit_mask = 0;
    ULONG64 starting_disk_slot = bitmap_row * BITS_PER_BITMAP_ROW;
    ULONG64 current_disk_slot = starting_disk_slot;

    // Get a snapshot of the current row (slots MAY be cleared, but they will not be set).
    ULONG64 row_snapshot = pf.page_file_bitmaps[bitmap_row];

    // If the ENTIRE row is empty, let's grab ALL the slots!
    if (row_snapshot == BITMAP_ROW_EMPTY) {

        // Set the entire row full, if we can!
        PULONG64 bitmap = &pf.page_file_bitmaps[bitmap_row];
        ULONG64 original_value = InterlockedCompareExchange64((volatile LONG64 *) bitmap,
                        BITMAP_ROW_FULL,
                        BITMAP_ROW_EMPTY);

        // If we were able to grab ALL the slots, then we will push them all to the stack
        if(original_value == BITMAP_ROW_EMPTY) {
            // Grab ALL the slots!
            for (int i = 0; i < BITS_PER_BITMAP_ROW; i++) {
                push_slot(current_disk_slot);
                current_disk_slot++;
            }
            LONG64 result = InterlockedAdd64(&pf.empty_disk_slots, -64);
            ASSERT(result < vm.pages_in_page_file && result >= 0);
            return;
        }
    }

    // Otherwise, we will scan through the row, setting slots one at a time
    for (int i = 0; i < BITS_PER_BITMAP_ROW; i++) {
        // Advance our mask and row
        current_disk_slot = starting_disk_slot + i;
        current_bit_mask = 1ULL << i;

        // If the bit is set in the snapshot, skip it!
        if ((row_snapshot & current_bit_mask) == current_bit_mask) continue;

        // Otherwise, set it and add it to our disk slots.
        set_disk_slot(current_disk_slot);

        // Add this slot to the stack!
        push_slot(current_disk_slot);
    }
}

VOID push_slot(ULONG64 slot) {
    pf.slot_stack[pf.num_stashed_slots] = slot;
    InterlockedIncrement64(&pf.num_stashed_slots);
}

ULONG64 pop_slot(VOID) {
    ASSERT(pf.num_stashed_slots > 0);
    InterlockedDecrement64(&pf.num_stashed_slots);
    return pf.slot_stack[pf.num_stashed_slots];
}

// Returns the number of allocated disk slots and fills the given array with them.
VOID set_and_add_slots_to_stack(ULONG64 target_slot_count) {

    // Pick up where we left off searching last time
    ULONG64 current_bitmap_row = pf.last_checked_bitmap_row;

    // We will check until we are done batching, or we can no longer get any slots
    for (ULONG64 i = 0; i < pf.page_file_bitmap_rows; i++) {

        // Advance to the next row, wrapping around if necessary
        current_bitmap_row = (current_bitmap_row + 1) % pf.page_file_bitmap_rows;

        // Get a snapshot of the bitmap -- if it is full, move along
        ULONG64 bitmap_snapshot = pf.page_file_bitmaps[current_bitmap_row];

        if (bitmap_snapshot == BITMAP_ROW_FULL) continue;

        // Otherwise, get as many slots as you can from the row
        push_slots_from_bitmap_row(current_bitmap_row);

        // If we have reached our target, let's get out of here!
        if (pf.num_stashed_slots >= target_slot_count) break;
    }

    // If we get here without breaking, then we have fewer than our target count. Oh well!
    // Let's save our spot for next time. Then return our count of allocated slots!
    pf.last_checked_bitmap_row = current_bitmap_row;
}

VOID pop_and_clear_all_slots(VOID) {
    while (pf.num_stashed_slots > 0) {
        clear_disk_slot(pop_slot());
    }
}

// Since disk slot is from 1 to DEFAULT_PAGES_IN_PAGE_FILE, we must decrement it when getting the offset.
char* get_page_file_offset(ULONG64 disk_slot) {
    validate_disk_slot(disk_slot);

    return pf.page_file + disk_slot * PAGE_SIZE;
}

VOID clear_disk_slot(ULONG64 disk_slot) {
    validate_disk_slot(disk_slot);

    // First, get the row and offset of this slot in our bitmap,
    // and use that to generate our mask. Our mask will be all ones except our slot to clear.
    // E.g.  slot 66 --> row 1, bit 2
    // Mask: 111111...111101

    ULONG64 row = BITMAP_ROW(disk_slot);
    ULONG64 mask = ~(1ULL << BITMAP_OFFSET(disk_slot));
    PULONG64 bitmap = &pf.page_file_bitmaps[row];

    // Now we will clear the slot, saving the ORIGINAL value for debugging.
    ULONG64 original_value = InterlockedAnd64((volatile LONG64 *) bitmap, mask);

    // Double-check -- the original bit SHOULD have been set!
    ASSERT((original_value & ~mask) == ~mask);

    // Increment our count of clear slots!
    LONG64 result = InterlockedIncrement64(&pf.empty_disk_slots);
    ASSERT(result < vm.pages_in_page_file && result >= 0);
}

VOID set_disk_slot(UINT64 disk_slot) {
    validate_disk_slot(disk_slot);

    ULONG64 row = BITMAP_ROW(disk_slot);
    ULONG64 mask = 1ULL << BITMAP_OFFSET(disk_slot);
    PULONG64 bitmap = &pf.page_file_bitmaps[row];

    // Now we will set the slot, saving the ORIGINAL value for debugging.
    ULONG64 original_value = InterlockedOr64((volatile LONG64 *) bitmap, mask);

    // Double-check -- the original bit SHOULD have been clear!
    ASSERT((original_value & mask) == 0ULL);

    // Decrement the empty disk slot count without risking race conditions.
    LONG64 result = InterlockedDecrement64(&pf.empty_disk_slots);
    ASSERT(result < vm.pages_in_page_file && result >= 0);
}
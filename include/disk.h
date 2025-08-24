//
// Created by zachb on 7/30/2025.
//

#pragma once
#include <Windows.h>
#include "utils.h"

#define DISK_SLOT_IN_USE                1
#define DISK_SLOT_EMPTY                 0

// Details for the page file and the bitmaps that describe it:
#define BITS_PER_BITMAP_ROW             64
#define BITMAP_ROW_FULL                 MAXULONG64
#define BITMAP_ROW_EMPTY                0ULL

#define BITMAP_ROW(disk_slot)           (disk_slot / BITS_PER_BITMAP_ROW)
#define BITMAP_OFFSET(disk_slot)        (disk_slot % BITS_PER_BITMAP_ROW)

typedef struct __page_file_struct {
    char* page_file;
    PULONG64 page_file_bitmaps;
    ULONG64 page_file_bitmap_rows;
    ULONG64 max_disk_index;
    volatile LONG64 empty_disk_slots;
    PULONG64 slot_stack;
    ULONG64 last_checked_bitmap_row;
    volatile LONG64 num_stashed_slots;

} PAGE_FILE_STRUCT, *PPAGE_FILE_STRUCT;

extern PAGE_FILE_STRUCT pf;

VOID initialize_page_file_and_metadata(VOID);

VOID validate_disk_slot(ULONG64 disk_slot);

char* get_page_file_offset(ULONG64 disk_slot);

VOID clear_disk_slot(ULONG64 disk_slot);

VOID set_disk_slot(UINT64 disk_slot);

VOID pop_and_clear_all_slots(VOID);

VOID set_and_add_slots_to_stack(ULONG64 target_slot_count);

VOID push_slots_from_bitmap_row(ULONG64 bitmap_row);

VOID push_slot(ULONG64 slot);

ULONG64 pop_slot(VOID);
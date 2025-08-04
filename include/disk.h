//
// Created by zachb on 7/30/2025.
//

#pragma once

#include "initializer.h"

#define BITMAP_ROW(disk_slot)       (disk_slot / BITS_PER_BITMAP_ROW)
#define BITMAP_OFFSET(disk_slot)    (disk_slot % BITS_PER_BITMAP_ROW)

VOID validate_disk_slot(ULONG64 disk_slot);

char* get_page_file_offset(ULONG64 disk_slot);

VOID clear_disk_slot(ULONG64 disk_slot);

VOID set_disk_slot(UINT64 disk_slot);

VOID pop_and_clear_all_slots(VOID);

VOID set_and_add_slots_to_stack(ULONG64 target_slot_count);

VOID push_slots_from_bitmap_row(ULONG64 bitmap_row);

VOID push_slot(ULONG64 slot);

ULONG64 pop_slot(VOID);
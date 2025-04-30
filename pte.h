//
// Created by zblickensderfer on 4/26/2025.
//

#pragma once

typedef struct {
    UINT64 frame_number : 40;   // 40 bits to hold the frame number
    UINT64 unused : 22;         // Remaining bits reserved for later
    UINT64 status : 1;          // 1 bit to encode transition (00) or on disk (10)
    UINT64 valid : 1;           // Valid bit -- 1 indicating PTE is valid
} VALID_PTE;

typedef struct {
    UINT64 disk_index : 22;   // 40 bits to hold the frame number
    UINT64 unused : 41;         // Remaining bits reserved for later
    UINT64 valid : 1;           // Valid bit -- 1 indicating PTE is valid
} INVALID_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disk_format;
    };
} PTE, *PPTE;

#define IS_PTE_ZEROED(pte) ((pte)->memory_format.valid == 0 && (pte)->memory_format.frame_number == 0)

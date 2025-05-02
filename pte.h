//
// Created by zblickensderfer on 4/26/2025.
//

#pragma once

#define PTE_INVALID 0
#define PTE_VALID 1
#define PTE_IN_TRANSITION 0
#define PTE_ON_DISK 1

typedef struct {
    UINT64 frame_number : 40;   // 40 bits to hold the frame number
    UINT64 unused : 23;         // Remaining bits reserved for later
    UINT64 valid : 1;           // Valid bit -- 1 indicating PTE is valid
} VALID_PTE;

typedef struct {
    UINT64 disk_index : 22;   // 40 bits to hold the frame number
    UINT64 unused : 40;         // Remaining bits reserved for later
    UINT64 status : 1;          // 1 bit to encode transition (0) or on disk (1)
    UINT64 valid : 1;           // Valid bit -- 0 indicating PTE is invalid
} INVALID_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disk_format;
    };
} PTE, *PPTE;

#define IS_PTE_ZEROED(pte) ((pte)->memory_format.valid == 0 && (pte)->memory_format.frame_number == 0)

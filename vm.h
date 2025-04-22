//
// Created by zblickensderfer on 4/22/2025.
//

#ifndef VM_H
#define VM_H
#include <Windows.h>

// This is my central controller for editing with a debug mode. All debug settings are enabled
// and disabled by DEBUG.
#define DEBUG                       0

#if DEBUG
#define assert(x)                   if (!x) { printf("error");}
#else
#define  assert(x)
#endif


#define PAGE_SIZE                   4096

#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

#define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

#endif //VM_H

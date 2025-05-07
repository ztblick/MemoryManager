//
// Created by zblickensderfer on 5/5/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../include/initializer.h"
#include "../include/macros.h"
#include "../include/pfn.h"
#include "../include/pte.h"
#include "../include/simulator.h"
#include "../include/debug.h"
#include "../include/page_fault_handler.h"
#include "../include/scheduler.h"

VOID full_virtual_memory_test (VOID) {
    PULONG_PTR arbitrary_va;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    HANDLE physical_page_handle;

    // Acquire privilege to manage pages from the operating system.
    privilege = GetPrivilege ();
    if (privilege == FALSE) {
        fatal_error ("full_virtual_memory_test : could not get privilege.");
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
    physical_page_handle = CreateSharedMemorySection ();
    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }
#else
    physical_page_handle = GetCurrentProcess ();
#endif

    // Grab physical pages from the OS
    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;
    physical_page_numbers = malloc (NUMBER_OF_PHYSICAL_PAGES * sizeof (ULONG_PTR));
    NULL_CHECK (physical_page_numbers, "full_virtual_memory_test : could not allocate array to hold physical page numbers.");

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);
    if (allocated == FALSE) {
        fatal_error ("full_virtual_memory_test : could not allocate physical pages.");
    }
    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }

    // Find largest frame number for PFN array
    set_max_frame_number();

    // Initialize major data structures
    initialize_data_structures();

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
    MEM_EXTENDED_PARAMETER parameter = { 0 };
    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //
    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;
    p = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);
#else
    // Reserve user virtual address space.
    application_va_base = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);
#endif

    NULL_CHECK (application_va_base, "Could not reserve user VA space.");

    kernal_write_va = VirtualAlloc (NULL,
                      PAGE_SIZE * MAX_WRITE_BATCH_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    NULL_CHECK (kernal_write_va, "Could not reserve kernal write VA space.");

    kernal_read_va = VirtualAlloc (NULL,
                      PAGE_SIZE * MAX_READ_BATCH_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    NULL_CHECK (kernal_read_va, "Could not reserve kernal read VA space.");

    // Now perform random accesses
    for (int i = 0; i < MB (1); i += 1) {

        // Ask the scheduler to age, trim, and write as is necessary.
        schedule_tasks();

        // Randomly access different portions of the virtual address space.
        arbitrary_va = get_arbitrary_va(application_va_base);

        // Attempt to write the virtual address into memory page.
        page_faulted = FALSE;
        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        // Begin handling page faults
        if (page_faulted) {

            // Fault handler maps the VA to its new page
            page_fault_handler(arbitrary_va, i);

            // No exception handler needed now since we have connected
            // the virtual address above to one of our physical pages.
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        }
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", MB (1));

    // Now that we're done with our memory we can be a good
    // citizen and free it.
    VirtualFree (application_va_base, 0, MEM_RELEASE);
    unmap_all_pages();
    free_all_data();
}


PULONG_PTR get_arbitrary_va(PULONG_PTR p) {
    // Randomly access different portions of the virtual address space.
    unsigned random_number = rand () * rand () * rand ();
    random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

    // Ensure the write to the arbitrary virtual address doesn't
    // straddle a PAGE_SIZE boundary.
    random_number &= ~0x7;
    return p + random_number;
}

VOID
main (int argc, char** argv) {

    // Test our very complicated usermode virtual implementation.
    full_virtual_memory_test ();
}
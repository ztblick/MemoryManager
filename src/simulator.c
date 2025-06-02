//
// Created by zblickensderfer on 5/5/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../include/initializer.h"
#include "../include/simulator.h"
#include "../include/debug.h"
#include "../include/page_fault_handler.h"
#include "../include/scheduler.h"

VOID setup_memory_test (VOID) {

    BOOL allocated;
    BOOL privilege;

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
    allocated_frame_count = NUMBER_OF_PHYSICAL_PAGES;
    allocated_frame_numbers = malloc (NUMBER_OF_PHYSICAL_PAGES * sizeof (ULONG_PTR));
    NULL_CHECK (allocated_frame_numbers, "full_virtual_memory_test : could not allocate array to hold physical page numbers.");

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &allocated_frame_count,
                                           allocated_frame_numbers);
    if (allocated == FALSE) {
        fatal_error ("full_virtual_memory_test : could not allocate physical pages.");
    }
    if (allocated_frame_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                allocated_frame_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }
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

void run_user_app_simulation(void) {

    // Adding variables only necessary to kick off fault handler!
    PULONG_PTR arbitrary_va;
    BOOL page_faulted = FALSE;
    BOOL fault_resolved = TRUE;

    // Now perform random accesses
    for (int i = 0; i < ITERATIONS; i += 1) {

#if DEBUG
        printf("\n\n~~~~~~~~~~~~~~~\nBegin iteration %d...\n", i);
#endif

        // Ask the scheduler to age, trim, and write as is necessary.   // TODO remove this once you have these running on their own threads
        schedule_tasks();

        // Randomly access different portions of the virtual address space.
        // If we faulted before, use the previous address!
        if (fault_resolved) {
            arbitrary_va = get_arbitrary_va(application_va_base);
        }

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
            fault_resolved = page_fault_handler(arbitrary_va, i);

            // If we were successful, we will do allow our usermode program to continue with its goal.
            if (fault_resolved){
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
            }
            else {
                fatal_error("User app attempted to access invalid VA.");
            }
        }
    }
}

void terminate_memory_test(void) {

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", ITERATIONS);

    // Now that we're done with our memory we can be a good citizen and free it.
    unmap_all_pages();
    VirtualFree (application_va_base, 0, MEM_RELEASE);
    free_all_data();
    FreeUserPhysicalPages(physical_page_handle, &allocated_frame_count, allocated_frame_numbers);
}

VOID main (int argc, char** argv) {

    // Get privileges and execute boilerplate code.
    setup_memory_test();

    // Initialize major data structures
    initialize_data_structures();

    // Start helper threads -- scheduler, writer, trimmer!
    // TODO initiate helper threads here

    // Run test with a simulated user app.
    run_user_app_simulation();

    // Free all memory and end the simulation.
    terminate_memory_test();
}
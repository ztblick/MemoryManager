#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "vm.h"

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

BOOL
GetPrivilege (VOID)
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege.
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %u\n", GetLastError ());
        return FALSE;
    }

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    return TRUE;
}

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
                                  NULL,
                                  SECTION_MAP_READ | SECTION_MAP_WRITE,
                                  PAGE_READWRITE,
                                  SEC_RESERVE,
                                  0,
                                  NULL,
                                  &parameter,
                                  1);

    return section;
}

#endif

// PFN Data Structures
PPFN PFN_array;

// PTE Data Structures
PULONG_PTR virtual_addresses_mapped[64];
ULONG_PTR page_bitmap = 0;

short get_next_free_page() {
    return 0;
}

BOOL page_in_use(short i) {
    return page_bitmap & (1 << i);
}

void unmap_all_pages() {
    for (int i = 0; i < 64; i++) {
        if (page_in_use(i)) {
            PULONG_PTR VA = virtual_addresses_mapped[i];
            // unmap user phys page
            if (MapUserPhysicalPages (VA, 1, NULL) == FALSE) {

                printf ("full_virtual_memory_test : could not unmap VA %p.\n", VA);
                return;
            }
            page_bitmap = page_bitmap & ~(1 << i);
        }

    }
}

VOID
full_virtual_memory_test (VOID) {
    unsigned i;
    PULONG_PTR p;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
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

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = malloc (physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }

    // Find largest frame number for PFN array
    ULONG max_page_number = 0;
    PULONG_PTR page = physical_page_numbers;
    for (int i = 0; i < physical_page_count; i++) {
        max_page_number = max(*page, max_page_number);
        page++;
    }

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~PAGE_SIZE;

    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);

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

    p = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

#endif

    if (p == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());

        return;
    }

    // Initialize PFN Array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * max_page_number,
                                MEM_RESERVE,
                                PAGE_NOACCESS);

    //
    // Now perform random accesses.
    //

    for (i = 0; i < MB (1); i += 1) {

        // Randomly access different portions of the virtual address space.
        random_number = rand () * rand () * rand ();
        random_number %= virtual_address_size_in_unsigned_chunks;

        // Write the virtual address into each page.
        page_faulted = FALSE;

        // Ensure the write to the arbitrary virtual address doesn't
        // straddle a PAGE_SIZE boundary just to keep things simple for
        // now.
        random_number &= ~0x7;
        arbitrary_va = p + random_number;

        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        // Begin handling page faults
        if (page_faulted) {

            // Get the location of the next available page.
            short offset = get_next_free_page(arbitrary_va);

            if (MapUserPhysicalPages (arbitrary_va, 1, physical_page_numbers + offset * sizeof(PULONG_PTR)) == FALSE) {

                printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, *physical_page_numbers);
                return;
            }

            // No exception handler needed now since we have connected
            // the virtual address above to one of our physical pages
            // so no subsequent fault can occur.
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        }

        // TODO Unmap any remaining virtual address translations.
        unmap_all_pages();
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (p, 0, MEM_RELEASE);

    return;
}

VOID
main (int argc, char** argv) {
    // Test our very complicated usermode virtual implementation.
    full_virtual_memory_test ();
    return;
}


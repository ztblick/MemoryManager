#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "vm.h"
#include "macros.h"
#include "pfn.h"
#include "pte.h"

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
ULONG max_page_number = 0;
PULONG_PTR physical_page_numbers;
PLIST_ENTRY free_list;
PLIST_ENTRY active_list;
PLIST_ENTRY modified_list;

// PTE Data Structures
PPTE PTE_base;
PULONG_PTR page_file;

// Initialize the above structures
void initialize_data_structures(void) {

    // Initialize PTE array
    PTE_base = malloc(sizeof(PTE) * NUM_PTEs);

    // TODO Initialize all PTEs?

    // Initialize PFN sparse array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * max_page_number,
                                MEM_RESERVE,
                                PAGE_NOACCESS);

    if (!PFN_array) {
        printf("Failed to reserve VA space for PFN_array\n");
        ExitProcess(1);
    }

    // Initialize lists for PFN state machine
    free_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(free_list);
    active_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(active_list);
    modified_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(modified_list);

    // Create all PFNs, adding them to the free list
    PULONG_PTR page = physical_page_numbers;
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        ULONG_PTR frame_number = *page;
        PPFN new_pfn = VirtualAlloc((LPVOID)(PFN_array + frame_number),
                                    sizeof(PFN),
                                    MEM_COMMIT,
                                    PAGE_READWRITE);
        if (new_pfn == NULL) {
            DWORD error = GetLastError();
            fprintf(stderr, "Error: Failed to commit memory for PFN at frame number %llu (error code %lu)\n",
                    (unsigned long long)frame_number, (unsigned long)error);
            ExitProcess(1);
        }

        printf("New PFN created at VA: %p.\n", new_pfn);


        new_pfn->PTE = NULL;
        new_pfn->status = PFN_FREE;
        InsertHeadList(free_list, &new_pfn->entry);
        page++;
    }

    page_file = malloc(VIRTUAL_ADDRESS_SIZE);
}

PPFN get_next_free_page(void) {
    if (IsListEmpty(free_list)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(free_list);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);

    pfn->status = PFN_ACTIVE; // Mark it as active now
    return pfn;
}

void unmap_all_pages(void) {
    return;
}

void free_all_data(void) {
    VirtualFree (PFN_array, 0, MEM_RELEASE);
    free(PTE_base);
    free(page_file);
}

void set_max_page_number(void) {
    PULONG_PTR page = physical_page_numbers;
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        max_page_number = max(*page, max_page_number);
        page++;
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

VOID
full_virtual_memory_test (VOID) {
    PULONG_PTR p;
    PULONG_PTR arbitrary_va;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    ULONG_PTR physical_page_count;
    HANDLE physical_page_handle;

    // Acquire privilege to manage pages from the operating system.
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

    // Grab physical pages from the OS
    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;
    physical_page_numbers = malloc (NUMBER_OF_PHYSICAL_PAGES * sizeof (ULONG_PTR));
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
    set_max_page_number();

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
    p = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);
#endif

    if (p == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());
        return;
    }

    // Now perform random accesses
    for (int i = 0; i < MB (1); i += 1) {

        // Randomly access different portions of the virtual address space.
        arbitrary_va = get_arbitrary_va(p);

        // Attempt to write the virtual address into memory page.
        page_faulted = FALSE;
        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        // Begin handling page faults
        if (page_faulted) {

            // Get the next free page.
            PPFN available_pfn = get_next_free_page();

            // If no page is available on the free list, then we will evict one from the active list
            if (available_pfn == NULL) {
                // TODO remove page from active list
                printf("NO FREE PAGE!");
            }

            // Set the PFN state
            available_pfn->status = PFN_ACTIVE;

            // Calculate index in PFN sparse array, giving the page number
            ULONG_PTR frame_number  = (ULONG_PTR) (available_pfn - PFN_array);
            PULONG_PTR page = &frame_number;

            /** TODO --
             * we need to access the page from the PFN. Landy's sparse array strategy is cool,
             * but I can't seem to get the address of the page from the offset in the sparse array.
             */

            if (MapUserPhysicalPages (arbitrary_va, 1, page) == FALSE) {

                printf ("full_virtual_memory_test : could not map VA %p to page %llX after %u iterations.\n",
                            arbitrary_va, *page, i);
                return;
            }

            // No exception handler needed now since we have connected
            // the virtual address above to one of our physical pages.
            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            printf("Successfully mapped and trimmed page #%d...\n", i + 1);
        }

        // TODO Unmap any remaining virtual address translations.
        unmap_all_pages();
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", MB (1));

    // Now that we're done with our memory we can be a good
    // citizen and free it.
    VirtualFree (p, 0, MEM_RELEASE);
    free_all_data();
    return;
}

VOID
main (int argc, char** argv) {
    // Test our very complicated usermode virtual implementation.
    full_virtual_memory_test ();
    return;
}


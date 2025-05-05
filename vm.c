#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "vm.h"
#include "macros.h"
#include "pfn.h"
#include "pte.h"

#define COLOR_RED "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

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

VOID fatal_error(char *msg)
{
    if (msg == NULL) {
        msg = "system unexpectedly terminated";
    }
    DWORD error_code = GetLastError();
    LPVOID error_msg;
    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &error_msg,
            0, NULL );

    printf(COLOR_RED "fatal error" COLOR_RESET " : %s\n" COLOR_RED "%s" COLOR_RESET "\n", msg, (char*)error_msg);
    fflush(stdout);
    DebugBreak();
    TerminateProcess(GetCurrentProcess(), 1);
}

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
ULONG_PTR max_page_number;
ULONG_PTR min_page_number;
PULONG_PTR physical_page_numbers;
ULONG_PTR physical_page_count;
PLIST_ENTRY free_list;
PLIST_ENTRY active_list;
PLIST_ENTRY modified_list;

// PTE Data Structures
PULONG_PTR VA_base;
PPTE PTE_base;
PULONG_PTR page_file;

// Initialize the above structures
void initialize_data_structures(void) {

    // Initialize PTE array
    PTE_base = malloc(sizeof(PTE) * NUM_PTEs);
    if (!PTE_base) {
        fatal_error("Failed to allocate PTE array.");
    }
    // Zero the entire region so all PTEs are in the zeroed (never used) state
    memset(PTE_base, 0, NUM_PTEs * sizeof(PTE));

    // Initialize PFN sparse array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * max_page_number,
                                MEM_RESERVE,
                                PAGE_READWRITE);

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
    // Note -- it is critical to not save the returned value of VirtualAlloc as the VA of the PFN.
    // VirtualAlloc returns the beginning of the page that has been committed, which can round down.
    // Once the memory is successfully committed, the PFN should map to the region inside that page
    // That corresponds with the value of the frame number.
    for (ULONG64 i = 0; i < physical_page_count; i++) {
        if (physical_page_numbers[i] == 0) {
            continue; // skip frame number 0, as it is an invalid page of memory per our PTE encoding.
        }
       LPVOID result = VirtualAlloc((LPVOID)(PFN_array + physical_page_numbers[i]),
                                    sizeof(PFN),
                                    MEM_COMMIT,
                                    PAGE_READWRITE);
        if (result == NULL) {
            fatal_error("Error: Failed to commit memory for PFN.");
        }

        PPFN new_pfn = PFN_array + physical_page_numbers[i];
#if DEBUG
        printf("%llu || New PFN created at VA %p for physical page %llu.\n", i, new_pfn, physical_page_numbers[i]);
#endif
        new_pfn->PTE = NULL;
        new_pfn->status = PFN_FREE;
        InsertHeadList(free_list, &new_pfn->entry);
    }

    // Initialize page file.
    page_file = malloc(VIRTUAL_ADDRESS_SIZE);
}

PPFN get_next_free_page(void) {
    if (IsListEmpty(free_list)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(free_list);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

PPFN get_first_active_page(void) {
    if (IsListEmpty(active_list)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(active_list);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

void unmap_all_pages(void) {
    // TODO complete this
    return;
}

void free_all_data(void) {
    VirtualFree (PFN_array, 0, MEM_RELEASE);
    free(PTE_base);
    free(page_file);
}

void set_max_page_number(void) {
    max_page_number = 0;
    min_page_number = ULONG_MAX;
    for (int i = 0; i < physical_page_count; i++) {
        max_page_number = max(max_page_number, physical_page_numbers[i]);
        min_page_number = min(min_page_number, physical_page_numbers[i]);
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

// Returns the pte associated with the faulting VA. Divides the offset of the VA within the VA space
// by the page_size, resulting in the index of the VA within the PTE array.
PPTE get_PTE_from_VA(PULONG_PTR faulting_VA) {
    ULONG_PTR va_offset = (ULONG_PTR)faulting_VA - (ULONG_PTR)VA_base;
    size_t pte_index = va_offset / PAGE_SIZE;
    return PTE_base + pte_index;
}

PVOID get_va_from_pte(PPTE pte) {
    size_t index = (size_t)(pte - PTE_base);  // Already scaled correctly
    return (PVOID)((char*)VA_base + index * PAGE_SIZE);
}

UINT64 get_frame_from_pfn(PPFN pfn) {
    if (pfn < PFN_array) {
        fatal_error("PFN out of bounds while attempting to map PFN to frame number.");
    }
    return pfn - PFN_array;
}

void map_pte_to_disk(PPTE pte, size_t disk_index) {

    pte->disk_format.disk_index = disk_index;
    pte->disk_format.status = PTE_ON_DISK;
    pte->disk_format.valid = PTE_INVALID;
}

size_t get_disk_index_from_pte(PPTE pte) {
    return (size_t)(pte - PTE_base);
}

void write_page_to_disk(PPTE pte) {

    // Temporarily map an un-used VA to this frame
    PULONG_PTR kernal_VA = malloc(sizeof(ULONG_PTR));
    ULONG_PTR frame_number = pte->memory_format.frame_number;
    if (MapUserPhysicalPages (kernal_VA, 1, &frame_number) == FALSE) {
        fatal_error("Could not map Kernal VA to physical page.");
    }

    // Get disk slot for this PTE
    // TODO update this with a reasonable disk slot strategy
    PULONG_PTR disk_slot = page_file + pte->disk_format.disk_index * PAGE_SIZE;
    memcpy(disk_slot, kernal_VA, PAGE_SIZE);

    // Un-map kernal VA
    if (MapUserPhysicalPages (kernal_VA, 1, NULL) == FALSE) {
        fatal_error("Could not unmap Kernal VA to physical page.");
    }

    // Free the kernal VA pointer.
    free(kernal_VA);
}

void load_page_from_disk(PPTE pte, PVOID destination_va) {

    PULONG_PTR disk_slot = page_file + pte->disk_format.disk_index * PAGE_SIZE;

    memcpy(destination_va, disk_slot, PAGE_SIZE);
}

VOID page_fault_handler(PULONG_PTR faulting_va, int i) {

    // Grab the PTE for the VA
    PPTE pte = get_PTE_from_VA(faulting_va);

    // If we faulted on a valid VA, something is very wrong
    if (pte->memory_format.valid == 1) {
        fatal_error("Faulted on a hardware valid PTE.");
    }

    // Get the next free page.
    PPFN available_pfn = get_next_free_page();

    // If a free page is available, map the PFN to the PTE.
    if (available_pfn != NULL) {
        // Set the PFN to its active state and add it to the end of the active list
        available_pfn->PTE = pte;
        SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
        InsertTailList(active_list, &available_pfn->entry);

        // Set the PTE into its memory format
        ULONG_PTR frame_number = get_frame_from_pfn(available_pfn);
        pte->memory_format.frame_number = frame_number;
        pte->memory_format.valid = 1;

        // Map the VA to its new page.
        if (MapUserPhysicalPages (faulting_va, 1, &frame_number) == FALSE) {
            fatal_error("Could not map VA to page in MapUserPhysicalPages.");
        }
        return;
    }


    // If no page is available on the free list, then we will evict one from the head of the active list (FIFO)
    if (available_pfn == NULL) {
        available_pfn = get_first_active_page();
    }

    // If no pages are available from the active list, fail immediately
    if (available_pfn == NULL) {
        fatal_error("No PFNs available to trim from active list");
    }

    // Get the previous PTE mapping to this frame
    ULONG_PTR frame_number = get_frame_from_pfn(available_pfn);
    PPTE old_pte = available_pfn->PTE;

    // Unmap the old VA from this physical page
    if (MapUserPhysicalPages (get_va_from_pte(old_pte), 1, NULL) == FALSE) {
        fatal_error("Could not un-map old VA.");
    }

    // Unmap the old PTE, put it into disk format, and map it to its disk index;
    size_t disk_index = get_disk_index_from_pte(old_pte);
    map_pte_to_disk(old_pte, disk_index);

    // Write the page out to the disk, saving its disk index
    write_page_to_disk(old_pte);

    // Map the new VA to the free page
    if (MapUserPhysicalPages (faulting_va, 1, &frame_number) == FALSE) {
        fatal_error("Could not map VA to page in MapUserPhysicalPages.");
    }

    // Update the PFN's PTE to the new PTE
    available_pfn->PTE = pte;

    // If the PTE is in disk format, then we know we need to load its contents from the disk
    // This writes them into the page that our old PTE's VA was previously mapped to,
    // which we will presently be mapping to the faulting VA.
    if (pte->disk_format.status == PTE_ON_DISK) {
        load_page_from_disk(pte, faulting_va);
    }

    if (IS_PTE_ZEROED(pte)) {
        // Nothing to be done!
    }

    // Set the PFN to its active state and add it to the end of the active list
    SET_PFN_STATUS(available_pfn, PFN_ACTIVE);
    InsertTailList(active_list, &available_pfn->entry);

    // Set the PTE into its memory format
    pte->memory_format.frame_number = frame_number;
    pte->memory_format.valid = 1;

#if DEBUG
    printf("Iteration %d: Successfully mapped and trimmed physical page %llu with PTE %p.\n", i, frame_number, pte);
#endif
}

VOID
full_virtual_memory_test (VOID) {
    PULONG_PTR arbitrary_va;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
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
    VA_base = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);
#endif

    if (VA_base == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());
        return;
    }

    // Now perform random accesses
    for (int i = 0; i < MB (1); i += 1) {

        // Randomly access different portions of the virtual address space.
        arbitrary_va = get_arbitrary_va(VA_base);

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
    VirtualFree (VA_base, 0, MEM_RELEASE);
    // TODO Unmap any remaining virtual address translations.
    unmap_all_pages();
    free_all_data();
    return;
}

VOID
main (int argc, char** argv) {
#if DEBUG
    LPSYSTEM_INFO lpSystemInfo = malloc(sizeof(SYSTEM_INFO));
    GetSystemInfo(lpSystemInfo);
    printf("%lu\n", lpSystemInfo->dwAllocationGranularity);
#endif

    // Test our very complicated usermode virtual implementation.
    full_virtual_memory_test ();
    return;
}


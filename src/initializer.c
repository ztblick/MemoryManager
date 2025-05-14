#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"
#include "../include/pfn.h"
#include "../include/pte.h"
#include "../include/scheduler.h"

BOOL GetPrivilege (VOID) {
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

// Initialize the above structures
void initialize_data_structures(void) {

    free_page_count = 0;
    active_page_count = 0;
    modified_page_count = 0;
    standby_page_count = 0;

    // Initialize PTE array
    PTE_base = malloc(sizeof(PTE) * NUM_PTEs);
    if (!PTE_base) {
        fatal_error("Failed to allocate PTE array.");
    }
    // Zero the entire region so all PTEs are in the zeroed (never used) state
    memset(PTE_base, 0, NUM_PTEs * sizeof(PTE));

    // Initialize PFN sparse array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * max_frame_number,
                                MEM_RESERVE,
                                PAGE_READWRITE);

    if (!PFN_array) {
        printf("Failed to reserve VA space for PFN_array\n");
        ExitProcess(1);
    }

    // Initialize lists for PFN state machine
    zero_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(zero_list);

    free_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(free_list);

    modified_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(modified_list);

    standby_list = malloc(sizeof(LIST_ENTRY));
    InitializeListHead(standby_list);

    // Create all PFNs, adding them to the free list
    // Note -- it is critical to not save the returned value of VirtualAlloc as the VA of the PFN.
    // VirtualAlloc returns the beginning of the page that has been committed, which can round down.
    // Once the memory is successfully committed, the PFN should map to the region inside that page
    // That corresponds with the value of the frame number.
    for (ULONG64 i = 0; i < allocated_frame_count; i++) {
        if (allocated_frame_numbers[i] == 0) {
            continue; // skip frame number 0, as it is an invalid page of memory per our PTE encoding.
        }
       LPVOID result = VirtualAlloc((LPVOID)(PFN_array + allocated_frame_numbers[i]),
                                    sizeof(PFN),
                                    MEM_COMMIT,
                                    PAGE_READWRITE);
        if (result == NULL) {
            fatal_error("Error: Failed to commit memory for PFN.");
        }

        PPFN new_pfn = PFN_array + allocated_frame_numbers[i];

        new_pfn->PTE = NULL;
        new_pfn->status = PFN_FREE;
        InsertHeadList(free_list, &new_pfn->entry);
        free_page_count++;
    }

    // Initialize page file.
    page_file = malloc(PAGES_IN_PAGE_FILE * PAGE_SIZE);

#if DEBUG
    printf("All data structures initialized!\n");
#endif
}

void map_pages(int num_pages, PULONG_PTR va, PULONG_PTR frame_numbers) {
    if (MapUserPhysicalPages (va, num_pages, frame_numbers) == FALSE) {
        fatal_error("Could not map VA to page in MapUserPhysicalPages.");
    }
}

void unmap_pages(int num_pages, PULONG_PTR va) {
    if (MapUserPhysicalPages (va, num_pages, NULL) == FALSE) {
        fatal_error("Could not un-map old VA.");
    }
}

void unmap_all_pages(void) {
    // TODO complete this
    return;
}

PPFN get_first_frame_from_list(PLIST_ENTRY head) {
    if (IsListEmpty(head)) {
        return NULL;
    }

    PLIST_ENTRY entry = RemoveHeadList(head);
    PPFN pfn = CONTAINING_RECORD(entry, PFN, entry);
    return pfn;
}

void free_all_data(void) {
    VirtualFree (PFN_array, 0, MEM_RELEASE);
    free(PTE_base);
    free(page_file);
}

void set_max_frame_number(void) {
    max_frame_number = 0;
    min_frame_number = ULONG_MAX;
    for (int i = 0; i < allocated_frame_count; i++) {
        max_frame_number = max(max_frame_number, allocated_frame_numbers[i]);
        min_frame_number = min(min_frame_number, allocated_frame_numbers[i]);
    }
}
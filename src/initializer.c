#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../include/initializer.h"
#include "../include/debug.h"
#include "../include/macros.h"
#include "../include/pfn.h"
#include "../include/pte.h"
#include "../include/simulator.h"
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

PULONG_PTR zero_malloc(size_t bytes_to_allocate) {
    PULONG_PTR destination = malloc(bytes_to_allocate);
    NULL_CHECK(destination, "Zero malloc failed!");
    memset(destination, 0, bytes_to_allocate);
    return destination;
}

void initialize_statistics(void) {
    free_page_count = 0;
    active_page_count = 0;
    modified_page_count = 0;
    standby_page_count = 0;
    hard_faults_resolved = 0;
    soft_faults_resolved = 0;
    faults_unresolved = 0;
}

// Initialize all global critical sections
void initialize_locks(void) {
    InitializeCriticalSection (&page_fault_lock);
    InitializeCriticalSection(&kernal_read_lock);
    InitializeCriticalSection(&kernal_write_lock);
}

void initialize_physical_pages(void) {

    BOOL allocated;
    BOOL privilege;
    // Acquire privilege to manage pages from the operating system.
    privilege = GetPrivilege ();
    if (privilege == FALSE) {
        fatal_error ("full_virtual_memory_test : could not get privilege.");
    }
    physical_page_handle = GetCurrentProcess ();

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

void initialize_page_table(void) {
    // Allocate and zero all PTE data.
    PTE_base = (PPTE) zero_malloc(sizeof(PTE) * NUM_PTEs);

    // Initialize all PTE locks
    for (PPTE pte = PTE_base; pte < PTE_base + NUM_PTEs; pte++) {
        InitializeCriticalSection(&pte->lock);
    }
}

void initialize_page_lists(void) {
    // Initialize lists for PFN state machine
    InitializeListHead(&zero_list);
    InitializeListHead(&free_list);
    InitializeListHead(&modified_list);
    InitializeListHead(&standby_list);
}

void initialize_PFN_data(void) {

    // Initialize PFN sparse array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * max_frame_number,
                                MEM_RESERVE,
                                PAGE_READWRITE);

    if (!PFN_array) {
        fatal_error("Failed to reserve VA space for PFN_array\n");
    }

    // Create all PFNs, adding them to the free list
    // Note -- it is critical to not save the returned value of VirtualAlloc as the VA of the PFN.
    // VirtualAlloc returns the beginning of the page that has been committed, which can round down.
    // Once the memory is successfully committed, the PFN should map to the region inside that page
    // That corresponds with the value of the frame number.
    for (ULONG64 i = 0; i < allocated_frame_count; i++) {

        if (allocated_frame_numbers[i] == NO_FRAME_ASSIGNED) {
            continue; // skip frame number 0, as it is an invalid page of memory per our PTE encoding.
        }

        LPVOID result = VirtualAlloc((LPVOID)(PFN_array + allocated_frame_numbers[i]),
                                     sizeof(PFN),
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
        if (result == NULL) {
            fatal_error("Error: Failed to commit memory for PFN.");
        }

        // Initialize the new PFN, then insert it to the free list.
        PPFN new_pfn = PFN_array + allocated_frame_numbers[i];
        create_zeroed_pfn(new_pfn);
        InsertHeadList(&free_list, &new_pfn->entry);
        free_page_count++;
    }
}

void initialize_page_file_and_metadata(void) {

    // Initialize page file.
    page_file = (char*) zero_malloc(PAGES_IN_PAGE_FILE * PAGE_SIZE);

    // Initialize page file metadata -- initially, this will be a bytemap to represent free or used pages in the page file.
    // The +1 is here because 0 is not a valid disk slot (reserved to represent a disk slot that is not connected)
    page_file_metadata = (char*) zero_malloc(PAGES_IN_PAGE_FILE + 1);

    // Initialize disk slot tracker
    empty_disk_slots = PAGES_IN_PAGE_FILE;
}

void initialize_kernel_VA_spaces(void) {
    // Initialize kernel VA space
    kernel_write_va = VirtualAlloc (NULL,
                  PAGE_SIZE * MAX_WRITE_BATCH_SIZE,
                  MEM_RESERVE | MEM_PHYSICAL,
                  PAGE_READWRITE);

    NULL_CHECK (kernel_write_va, "Could not reserve kernal write VA space.");

    kernel_read_va = VirtualAlloc (NULL,
                      PAGE_SIZE * MAX_READ_BATCH_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    NULL_CHECK (kernel_read_va, "Could not reserve kernal read VA space.");
}

void initialize_user_VA_space(void) {
    // Reserve user virtual address space.
    application_va_base = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    NULL_CHECK (application_va_base, "Could not reserve user VA space.");
}

void initialize_threads(void) {

    // Initialize handles to for each thread.
    user_threads = (PHANDLE) zero_malloc(NUM_USER_THREADS * sizeof(HANDLE));
    system_threads = (PHANDLE) zero_malloc(NUM_SYSTEM_THREADS * sizeof(HANDLE));
    user_thread_ids = (PULONG) zero_malloc(NUM_USER_THREADS * sizeof(ULONG));
    system_thread_ids = (PULONG) zero_malloc(NUM_SYSTEM_THREADS * sizeof(ULONG));

    // Create user threads, each of which are running the user app simulation.
    for (ULONG64 i = 0; i < NUM_USER_THREADS; i++) {
        user_threads[i] = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) run_user_app_simulation,
                               (LPVOID) i,
                               DEFAULT_CREATION_FLAGS,
                               &user_thread_ids[i]);

        NULL_CHECK(user_threads[i], "Could not create user threads.");
    }

    // TODO initialize system threads for writing, trimming, etc.
}

void initialize_events(void) {
    system_start_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    NULL_CHECK(system_start_event, "Could not intialize system start event.");

    standby_pages_ready_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(system_start_event, "Could not intialize standby pages ready event.");

    initiate_trimming_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(initiate_trimming_event, "Could not initialize trimming event.");

    initiate_writing_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(initiate_writing_event, "Could not initialize writing event.");
}

// Initialize the above structures
void initialize_system(void) {

    // Initialize statistics to track page consumption (for scheduler).
    initialize_statistics();

    // TODO remove this once trimmer runs on its own thread.
    trimmer_offset = 0;

    // Get the privilege and physical pages from the OS.
    initialize_physical_pages();

    // Find largest frame number for PFN array
    set_max_frame_number();

    // Initialize PTEs
    initialize_page_table();

    // Initialize page lists
    initialize_page_lists();

    // Initialize all PFN data
    initialize_PFN_data();

    // Initialize all page file and metadata
    initialize_page_file_and_metadata();

    // Initialize VA spaces
    initialize_kernel_VA_spaces();
    initialize_user_VA_space();

    // Initialize events
    initialize_threads();

    // Initialize threads
    initialize_events();

    // Initialize all locks on global data structures.
    initialize_locks();
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
    for (PPTE pte = PTE_base; pte < PTE_base + NUM_PTEs; pte++) {
        if (pte->disk_format.valid) {
            PULONG_PTR va = get_VA_from_PTE(pte);
            unmap_pages(1, va);
        }
    }
}

// TODO move to page list file, wait for lock on list head before removing.
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
    free(page_file_metadata);
}

void set_max_frame_number(void) {
    max_frame_number = 0;
    min_frame_number = ULONG_MAX;
    for (int i = 0; i < allocated_frame_count; i++) {
        max_frame_number = max(max_frame_number, allocated_frame_numbers[i]);
        min_frame_number = min(min_frame_number, allocated_frame_numbers[i]);
    }
}
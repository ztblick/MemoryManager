
#include "initializer.h"

// Initializing global structs
STATS stats = {0};
VM vm = {0};
PAGE_LIST_ARRAY free_lists = {0};

PUSER_THREAD_INFO user_thread_info;

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

VOID set_max_frame_number(VOID) {
    ULONG64 max_frame_number = 0;
    ULONG64 min_frame_number = ULLONG_MAX;
    for (int i = 0; i < vm.allocated_frame_count; i++) {
        max_frame_number = max(max_frame_number, vm.allocated_frame_numbers[i]);
        min_frame_number = min(min_frame_number, vm.allocated_frame_numbers[i]);
    }

    // Update globals
    vm.max_frame_number = max_frame_number;
    vm.min_frame_number = min_frame_number;
}

void initialize_statistics(void) {
    stats.n_free = &free_lists.page_count;
    stats.n_modified = &modified_list.list_size;
    stats.n_standby = &standby_list.list_size;
    stats.n_hard = 0;
    stats.n_soft = 0;

    stats.n_available = *stats.n_free;

    // Get the frequency of the performance counter
    LARGE_INTEGER lpFrequency;
    QueryPerformanceFrequency(&lpFrequency);
    stats.timer_frequency = lpFrequency.QuadPart;

    // Initialize consumption rate
    stats.page_consumption_per_second = DEFAULT_PAGE_CONSUMPTION_RATE;
    stats.worker_runtimes[TRIMMING_THREAD_ID] = DEFAULT_TRIM_DURATION;
    stats.worker_runtimes[WRITING_THREAD_ID] = DEFAULT_WRITE_DURATION;
    stats.worker_runtimes[PRUNING_THREAD_ID] = DEFAULT_PRUNE_DURATION;
}

HANDLE CreateSharedMemorySection (VOID) {
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    HANDLE section = CreateFileMapping2(INVALID_HANDLE_VALUE,
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

void initialize_physical_pages(void) {

    BOOL allocated;
    BOOL privilege;
    // Acquire privilege to manage pages from the operating system.
    privilege = GetPrivilege ();
    if (privilege == FALSE) {
        fatal_error ("full_virtual_memory_test : could not get privilege.");
    }

    vm.physical_page_handle = CreateSharedMemorySection();

    if (vm.physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }

    // Grab physical pages from the OS
    vm.allocated_frame_numbers = zero_malloc (vm.allocated_frame_count * sizeof (ULONG_PTR));
    NULL_CHECK (vm.allocated_frame_numbers, "full_virtual_memory_test : could not allocate array to hold physical page numbers.");

    allocated = AllocateUserPhysicalPages (vm.physical_page_handle,
                                           &vm.allocated_frame_count,
                                           vm.allocated_frame_numbers);
    if (allocated == FALSE) {
        fatal_error ("full_virtual_memory_test : could not allocate physical pages.");
    }
#if DEBUG
        printf ("full_virtual_memory_test : allocated %llu pages\n", vm.allocated_frame_count);
#endif
}

void initialize_page_lists(void) {
    // Initialize lists for PFN state machine
    initialize_page_list(&zero_list);
    initialize_page_list(&modified_list);
    initialize_page_list(&standby_list);

    // Initialize group of free lists
    free_lists.number_of_lists = FREE_LIST_COUNT;
    free_lists.page_count = 0;
    free_lists.free_list_locks = 0;
    free_lists.list_array = zero_malloc(sizeof(PAGE_LIST) * FREE_LIST_COUNT);
    for (int i = 0; i < FREE_LIST_COUNT; i++) {
        initialize_page_list(&free_lists.list_array[i]);
    }
}

void initialize_PFN_data(void) {

    // Initialize PFN sparse array
    PFN_array = VirtualAlloc    (NULL,
                                sizeof(PFN) * (vm.max_frame_number + 1),
                                MEM_RESERVE,
                                PAGE_READWRITE);

    ASSERT(PFN_array);

    // Create all PFNs, adding them to the free list
    // Note -- it is critical to not save the returned value of VirtualAlloc as the VA of the PFN.
    // VirtualAlloc returns the beginning of the page that has been committed, which can round down.
    // Once the memory is successfully committed, the PFN should map to the region inside that page
    // That corresponds with the value of the frame number.
    ULONG list_index = 0;
    ULONG num_lists = free_lists.number_of_lists;

    for (ULONG64 i = 0; i < vm.allocated_frame_count; i++) {

        LPVOID result = VirtualAlloc((LPVOID)(PFN_array + vm.allocated_frame_numbers[i]),
                                     sizeof(PFN),
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
        if (result == NULL) {
            fatal_error("Error: Failed to commit memory for PFN.");
        }

        // Initialize the new PFN, then insert it to the free list.
        PPFN new_pfn = PFN_array + vm.allocated_frame_numbers[i];
        create_zeroed_pfn(new_pfn);

        // Add the page to one of the free lists
        PPAGE_LIST list = &free_lists.list_array[list_index];
        insert_to_list_tail(list, new_pfn);

        // Adjust metadata
        increment_list_size(list);
        increment_free_lists_total_count();
        list_index = (list_index + 1) % num_lists;
    }
}

void initialize_kernel_VA_spaces(void) {

    // Initialize kernel VA space
    vm.kernel_write_va = VirtualAlloc2 (NULL,
                    NULL,
                  PAGE_SIZE * MAX_WRITE_BATCH_SIZE,
                  MEM_RESERVE | MEM_PHYSICAL,
                  PAGE_READWRITE,
                  &vm.virtual_alloc_shared_parameter,
                  1);

    NULL_CHECK (vm.kernel_write_va, "Could not reserve kernal write VA space.");
}

void initialize_user_VA_space(void) {

    ULONG64 va_span = VA_SPAN(vm.allocated_frame_count, vm.pages_in_page_file);
    // In case fewer pages were allocated, we reduce the size.
    vm.va_size_in_bytes = va_span * PAGE_SIZE;
    vm.va_size_in_pointers = vm.va_size_in_bytes / sizeof(PULONG_PTR);

    // Reserve user virtual address space.
    vm.application_va_base = VirtualAlloc2 (NULL,
                       NULL,
                       vm.va_size_in_bytes,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &vm.virtual_alloc_shared_parameter,
                       1);

    NULL_CHECK (vm.application_va_base, "Could not reserve user VA space.");
}

void initialize_threads(void) {

    ULONG num_user_threads = vm.num_user_threads;

    // Initialize array of user thread handles and IDs
    user_threads = (PHANDLE) zero_malloc(num_user_threads * sizeof(HANDLE));
    user_thread_ids = (PULONG) zero_malloc(num_user_threads * sizeof(ULONG));
    worker_thread_ids = (PULONG) zero_malloc(NUM_WORKER_THREADS * sizeof(ULONG));
    worker_thread_ids[TRIMMING_THREAD_ID] = TRIMMING_THREAD_ID;
    worker_thread_ids[WRITING_THREAD_ID] = WRITING_THREAD_ID;
    worker_thread_ids[PRUNING_THREAD_ID] = PRUNING_THREAD_ID;
    worker_thread_ids[AGING_THREAD_ID] = AGING_THREAD_ID;
    worker_thread_ids[SCHEDULING_THREAD_ID] = SCHEDULING_THREAD_ID;

    // Create structs to pass to threads, including each thread's kernel
    // read spaces, which are divided into 16 individual read spaces.
    user_thread_info = (PUSER_THREAD_INFO) zero_malloc(num_user_threads * sizeof(USER_THREAD_INFO));

    // Initialize the kernel VA spaces for each thread
    for (ULONG i = 0; i < num_user_threads; i++) {
        user_thread_info[i].kernel_va_space = VirtualAlloc2 (NULL,
                                                    NULL,
                                                            PAGE_SIZE * NUM_KERNEL_READ_ADDRESSES,
                                                    MEM_RESERVE | MEM_PHYSICAL,
                                                    PAGE_READWRITE,
                                                                &vm.virtual_alloc_shared_parameter,
                                                    1);

        // While we're here, update the thread IDs and seed the random number
        user_thread_info[i].thread_id = i;
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        ULONG64 seed = counter.QuadPart ^ ((ULONG64) i << 32) ^ (counter.QuadPart >> 16);
        user_thread_info[i].random_seed = seed;

        // And we will fill the initial free page caches of the threads, too
        // We will find the correct free list by wrapping around (since the number of
        // user threads will likely be greater than the number of free lists).
        PPFN first_page;
        ULONG index = i % free_lists.number_of_lists;
        PPAGE_LIST free_list = &free_lists.list_array[index];
        ULONG64 num_pages = remove_batch_from_list_head_exclusive(free_list,
                                                        &first_page,
                                                        FREE_PAGE_CACHE_SIZE);

        // Decrement the total free count
        free_lists.page_count -= (LONG64) num_pages;

        // Add all removed pages to the free cache and unlock them
        PPFN pfn = first_page;
        PPFN next;
        for (ULONG64 j = 0; j < num_pages; j++) {
            user_thread_info[i].free_page_cache[j] = pfn;
            next = pfn->flink;

            // Unlock the pfn (was locked by the list removal) and move on to the next pfn
            unlock_pfn(pfn);
            pfn = next;
        }
        user_thread_info[i].free_page_count = num_pages;
    }

    // Create user threads, each of which are running the user app simulation.
    for (ULONG64 i = 0; i < num_user_threads; i++) {
        user_threads[i] = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) run_user_app_simulation,
                               &user_thread_info[i],
                               DEFAULT_CREATION_FLAGS,
                               &user_thread_ids[i]);

        ASSERT(user_threads[i]);
    }
#if SCHEDULING
    // Create system scheduling thread
    scheduling_thread = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) schedule_tasks,
                               NULL,
                               DEFAULT_CREATION_FLAGS,
                               &worker_thread_ids[SCHEDULING_THREAD_ID]);

    ASSERT(scheduling_thread);
#endif
    // Create system trimming thread
    trimming_thread = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) trim_pages_thread,
                               NULL,
                               DEFAULT_CREATION_FLAGS,
                               &worker_thread_ids[TRIMMING_THREAD_ID]);

    ASSERT(trimming_thread);

    // Create system writing thread
    writing_thread = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) write_pages_thread,
                               NULL,
                               DEFAULT_CREATION_FLAGS,
                               &worker_thread_ids[WRITING_THREAD_ID]);

    ASSERT(writing_thread);

#if PRUNING
    // Create worker thread to prune pages from standby to free lists
    pruning_thread = CreateThread (DEFAULT_SECURITY,
                    DEFAULT_STACK_SIZE,
        (LPTHREAD_START_ROUTINE) prune_pages_thread,
                                NULL,
                    DEFAULT_CREATION_FLAGS,
                                &worker_thread_ids[PRUNING_THREAD_ID]);
    ASSERT(pruning_thread);
#endif
    // Initialize trimmer and writer sampling
#if STATS_MODE
    trim_samples.head = 0;
    write_samples.head = 0;
    memset(trim_samples.data, 0, NUMBER_OF_SAMPLES * sizeof(batch_sample));
    memset(write_samples.data, 0, NUMBER_OF_SAMPLES * sizeof(batch_sample));
#endif

#if DEBUG
    debug_thread = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               (LPTHREAD_START_ROUTINE) debug_thread_function,
                               NULL,
                               DEFAULT_CREATION_FLAGS,
                               NULL);
#endif
}

void initialize_events(void) {
    system_start_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    NULL_CHECK(system_start_event, "Could not intialize system start event.");

    initiate_aging_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(initiate_aging_event, "Could not intialize aging event.");

    initiate_trimming_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    NULL_CHECK(initiate_trimming_event, "Could not initialize trimming event.");

    initiate_writing_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(initiate_writing_event, "Could not initialize writing event.");

    initiate_pruning_event = CreateEvent(NULL, AUTO_RESET, FALSE, NULL);
    NULL_CHECK(initiate_pruning_event, "Could not initialize writing event.");

    standby_pages_ready_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    NULL_CHECK(standby_pages_ready_event, "Could not intialize standby pages ready event.");

    system_exit_event = CreateEvent(NULL, MANUAL_RESET, FALSE, NULL);
    NULL_CHECK(system_exit_event, "Could not intialize standby pages ready event.");
}

void initialize_shared_page_parameter(void) {
    memset(&vm.virtual_alloc_shared_parameter, 0, sizeof(vm.virtual_alloc_shared_parameter));
    vm.virtual_alloc_shared_parameter.Type = MemExtendedParameterUserPhysicalHandle;
    vm.virtual_alloc_shared_parameter.Handle = vm.physical_page_handle;
}

void set_defaults(void) {
    // Declare and initialize various global variables
    vm.num_user_threads = DEFAULT_NUM_USER_THREADS;
    vm.iterations = DEFAULT_ITERATIONS;
    vm.allocated_frame_count = DEFAULT_NUMBER_OF_PHYSICAL_PAGES;
    vm.pages_in_page_file = DEFAULT_PAGES_IN_PAGE_FILE;
}

void initialize_system(void) {

    // Get the privilege and physical pages from the OS.
    initialize_physical_pages();

    // Find largest frame number for PFN array
    set_max_frame_number();

    // Initialize VA spaces
    initialize_shared_page_parameter();
    initialize_user_VA_space();
    initialize_kernel_VA_spaces();

    // Initialize PTEs
    initialize_page_table();

    // Initialize page lists
    initialize_page_lists();

    // Initialize all PFN data
    initialize_PFN_data();

    // Initialize all page file and metadata
    initialize_page_file_and_metadata();

    // Initialize events
    initialize_events();

    // Initialize threads
    initialize_threads();

    // Initialize statistics to track page consumption (for scheduler).
    initialize_statistics();

#if DEBUG
    g_traceIndex = -1;
#endif
}

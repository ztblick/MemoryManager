#pragma once

#include "pfn.h"
#include "debug.h"

/*
 * A page list is the head of a doubly-linked list of pages.
 * When the list is empty, the head's entry's flink and blink point to the entry itself.
 * The head also keeps track of the size of the list. It also has the lock on the page list.
*/

/*
 *  Our guiding rule on locks and page lists: to add/remove an entry from a list, you MUST have the lock
 *  on that page AS WELL AS the lock on the list head. ONLY if you have BOTH locks may you add or remove a page
 *  from a list.
 */

// This will determine the number of times we will attempt to grab page locks
// before grabbing the exclusive lock.
#define MAX_HARD_ACCESS_ATTEMPTS		30
#define MAX_SOFT_ACCESS_ATTEMPTS		30

// Total size: 24 bytes + 40 bytes of padding
__declspec(align(64))
typedef struct __page_list {
    PPFN head;                   // 8 bytes
    volatile LONG64 list_size;   // 8 bytes
    SRWLOCK lock;                // 8 bytes
} PAGE_LIST, *PPAGE_LIST;

typedef struct __page_list_array {
    ULONG number_of_lists;
    volatile LONG64 page_count;
    PAGE_LIST *list_array;
} PAGE_LIST_ARRAY, *PPAGE_LIST_ARRAY;

extern PAGE_LIST_ARRAY free_lists;

// Page lists
extern PAGE_LIST zero_list;
extern PAGE_LIST modified_list;
extern PAGE_LIST standby_list;

/*
 *  Pushes a page onto a free list.
 */
VOID add_page_to_free_lists(PPFN page, ULONG first_index);

/*
 *  This attempts to grab a free page from the array of free lists.
 */
BOOL try_get_free_page(PPFN *address_to_save, ULONG first_index);

/*
 *  Attempts to lock a particular free list. If it can be locked, and there is at least one
 *  page on the list, it pops that page and returns it. Otherwise, it releases locks and
 *  returns null. Note: since the page is free, it does NOT need to be locked.
 */
PPFN try_pop_from_free_list(ULONG list_index);

/*
 *  Initialize a page list. This creates the critical section, initializes the list head,
 *  and sets the size to zero.
 */
VOID initialize_page_list(PPAGE_LIST list);

/*
 *  Returns the length of the list when asked, with no guarantees about future length.
 */
ULONG64 get_size(PPAGE_LIST list);

/*
 *  Check if the list is empty. Returns true or false.
 */
BOOL is_page_list_empty(PPAGE_LIST list);

/*
 *  Adds a page to the head of the list. This assumes the page being added is already locked by the caller!
 */
VOID lock_list_then_insert_to_head(PPAGE_LIST list, PLIST_ENTRY entry);

/*
 *  Returns true if the given PFN is at the head of the given list. Does not lock anything.
 */
BOOL is_at_head_of_list(PPAGE_LIST list, PPFN pfn);

/*
 *  Adds a page to the tail of the list. This assumes the page being added is already locked by the caller!
 */
VOID insert_page_to_tail(PPAGE_LIST list, PPFN pfn);

/*
 *  Increments the list size.
 */
VOID increment_list_size(PPAGE_LIST list);

/*
 *  Decrements a list size, which is done when removing elements during a soft fault.
 */
VOID decrement_list_size(PPAGE_LIST list);

/*
    Changes the list size by the given amount.
 */
VOID change_list_size(PPAGE_LIST list, LONG64 amt);

/*
    Bumps up the free list total count. Done so with interlocked operations, as access can be
    concurrent.
 */
VOID increment_free_lists_total_count(VOID);
VOID decrement_free_lists_total_count(VOID);

/*
 *  This pops an entry from the head of the list. It assumes nothing -- the programmer
 *  must first be sure that the list is non-empty before calling this function.
 */
PPFN pop_from_head_list(PPAGE_LIST list);

/*
 *  @brief Locks the list, then removes an entry from that list.
 *  @precondition pfn is locked and on the given list
 */
VOID remove_page_on_soft_fault(PPAGE_LIST list, PPFN pfn);

/*
 *  Pops and returns from the head of the list.
 *  Returns NULL is the list is empty.
 */
PPFN lock_list_then_pop_from_head(PPAGE_LIST list);

/*
 *  Returns the PFN at the head of the list, but does not remove it from the list.
 *  Returns NULL is the list is empty.
 */
PPFN peek_from_list_head(PPAGE_LIST list);

/*
 *  Attempts to pop a page from the head of the list.
 *  @postcondition If successful, returns a locked page.
 *  Otherwise, returns NULL
 */
PPFN try_pop_from_list(PPAGE_LIST list);

/*
 *  Locks the list shared.
 */
VOID lock_list_shared(PPAGE_LIST list);
VOID unlock_list_shared(PPAGE_LIST list);
BOOL try_lock_list_shared(PPAGE_LIST list);

/*
 *  Waits until it can acquire the lock on a given page list!
 */
VOID lock_list_exclusive(PPAGE_LIST list);

/*
 *  Attempts to lock a given page list, but does not wait. Returns TRUE if the page list lock is acquired.
 */
BOOL try_lock_list_exclusive(PPAGE_LIST list);


/*
 *  Unlocks a previously locked page list.
 */
VOID unlock_list_exclusive(PPAGE_LIST list);

VOID initialize_list_head(PPFN head);
BOOL remove_page_from_list(PPFN page);
PPFN remove_from_head_of_list(PPAGE_LIST list);
VOID insert_to_list_tail(PPAGE_LIST list, PPFN page);

#if DEBUG
VOID validate_list(PPAGE_LIST list);
#endif

/*
    Removes a batch of pages from the head of the given list. Adds them into the
    given list pages. Pages are UNLOCKED.
    Returns the total number of pages batched.
 */
ULONG64 remove_batch_from_list_head(PPAGE_LIST list,
                                    PPFN *address_of_first_page,
                                    ULONG64 capacity);

/*
    Inserts all pages from a given list to the tail of another.
 */
VOID insert_list_to_tail_list(PPAGE_LIST destination_list, PPAGE_LIST origin_list);